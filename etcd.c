/* Author: Mo McRoberts <mo.mcroberts@bbc.co.uk>
 *
 * Copyright (c) 2015 BBC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "p_libcluster.h"

static int cluster_etcd_ping_(CLUSTER *cluster, ETCDFLAGS flags);
static int cluster_etcd_unping_(CLUSTER *cluster, ETCDFLAGS flags);
static void *cluster_etcd_ping_thread_(void *arg);
static void *cluster_etcd_balancer_thread_(void *arg);
static int cluster_etcd_balance_(CLUSTER *cluster);

/* Join an etcd-based cluster. To do this, we first update the relevant
 * directory with information about ourselves, then spawn a 're-balancing
 * thread' which watches for changes on that directory.
 *
 * The cluster lock should not be held when invoking this function.
 */
int
cluster_etcd_join_(CLUSTER *cluster)
{	
	cluster_wrlock_(cluster);
	cluster->inst_index = -1;
	cluster->etcd_root = etcd_connect(cluster->registry);
	if(!cluster->etcd_root)
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: etcd: cannot connect to registry <%s>\n", cluster->registry);
		cluster_unlock_(cluster);
		cluster_etcd_leave_(cluster);
		return -1;
	}
	etcd_set_verbose(cluster->etcd_root, (cluster->flags & CF_VERBOSE));
	cluster->etcd_clusterdir = etcd_dir_create(cluster->etcd_root, cluster->key, ETCD_NONE);
	if(!cluster->etcd_clusterdir)
	{
		cluster->etcd_clusterdir = etcd_dir_open(cluster->etcd_root, cluster->key);
		if(!cluster->etcd_clusterdir)
		{
			cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: etcd; failed to create or open registry directory for cluster '%s'\n", cluster->key);
			cluster_unlock_(cluster);
			cluster_etcd_leave_(cluster);
			return -1;
		}
	}
	cluster->etcd_envdir = etcd_dir_create(cluster->etcd_clusterdir, cluster->env, ETCD_NONE);
	if(!cluster->etcd_envdir)
	{
		cluster->etcd_envdir = etcd_dir_open(cluster->etcd_clusterdir, cluster->env);
		if(!cluster->etcd_envdir)
		{
			cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: etcd: failed to create or open registry directory for environment '%s/%s'\n", cluster->key, cluster->env);
			cluster_unlock_(cluster);
			cluster_etcd_leave_(cluster);
			return -1;
		}
	}
	if(cluster_etcd_ping_(cluster, ETCD_NONE))
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: etcd: failed to perform initial ping\n");
		cluster_unlock_(cluster);
		cluster_etcd_leave_(cluster);
		return -1;
	}
	if(cluster_etcd_balance_(cluster))
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: etcd: failed to perform initial balancing\n");
		cluster_unlock_(cluster);
		cluster_etcd_leave_(cluster);
		return -1;
	}
	pthread_create(&(cluster->ping_thread), NULL, cluster_etcd_ping_thread_, (void *) cluster);
	pthread_create(&(cluster->balancer_thread), NULL, cluster_etcd_balancer_thread_, (void *) cluster);
	cluster->flags |= CF_JOINED;
	cluster_unlock_(cluster);
	return 0;
}

/* Leave an etcd-based cluster. We first set a flag to indicate that we want
 * to leave the cluster (which the re-balancing thread will check), then
 * pthread_join() that thread to wait for it to shut down. Once it has, we
 * can remove our own entry from the directory.
 *
 * The cluster lock should not be held when invoking this function.
 */
int
cluster_etcd_leave_(CLUSTER *cluster)
{
	/* Use a write-lock to prevent a read-lock - write-lock race */
	cluster_wrlock_(cluster);
	if(cluster->flags & CF_JOINED)
	{
		cluster->flags |= CF_LEAVING;
		/* Unlock to allow the threads to read the flag */
		cluster_unlock_(cluster);
		pthread_join(cluster->ping_thread, NULL);
		pthread_join(cluster->balancer_thread, NULL);
		/* Re-acquire the lock so that the unwinding can safely complete */
		cluster_wrlock_(cluster);
	}
	cluster->flags &= ~(CF_JOINED|CF_LEAVING);
	cluster->ping_thread = 0;
	cluster->balancer_thread = 0;
	if(cluster->etcd_envdir)
	{
		etcd_dir_close(cluster->etcd_envdir);
		cluster->etcd_envdir = NULL;
	}
	if(cluster->etcd_clusterdir)
	{
		etcd_dir_close(cluster->etcd_clusterdir);
		cluster->etcd_clusterdir = NULL;
	}
	if(cluster->etcd_root)
	{
		etcd_disconnect(cluster->etcd_root);
		cluster->etcd_root = NULL;
	}
	cluster_unlock_(cluster);
	return 0;
}

/* "Ping" the registry - this happens once initially, then periodically
 * thereafter while the cluster connection is active. With etcd, this happens
 * by setting a directory entry (with a pre-defined TTL) whose name is the
 * instance identifier and the value is the number of threads in this
 * instance.
 *
 * The cluster should be at least read-locked when invoking this function.
 */
static int
cluster_etcd_ping_(CLUSTER *cluster, ETCDFLAGS flags)
{
	char buf[64];
	
	snprintf(buf, sizeof(buf) - 1, "%d", cluster->inst_threads);
	return etcd_key_set_ttl(cluster->etcd_envdir, cluster->instid, buf, cluster->etcd_ttl, flags);
}

/* 'Un-ping' - that is, remove our entry from the directory.
 *
 * The cluster should be at least read-locked when invoking this function.
 */
static int
cluster_etcd_unping_(CLUSTER *cluster, ETCDFLAGS flags)
{
	return etcd_key_delete(cluster->etcd_envdir, cluster->instid, flags);
}

/* Read the directory from the registry service and determine what our index
 * in the cluster is.
 *
 * The cluster should be write-locked when invoking this function. The lock
 * may be released and re-acquired during the course of its execution.
 */
static int
cluster_etcd_balance_(CLUSTER *cluster)
{
	int total, base, val;
	size_t n, c;
	const char *name;

	if(cluster->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: etcd: reading state from registry directory\n");
	}
	base = -1;
	JD_SCOPE
	{
		jd_var dict = JD_INIT, keys = JD_INIT;
		jd_var *key, *entry, *value;

		if(etcd_dir_get(cluster->etcd_envdir, &dict))
		{
			cluster_logf_locked_(cluster, LOG_ERR, "libcluster: etcd: failed to retrieve cluster directory\n");
			return -1;
		}
		jd_keys(&keys, &dict);
		jd_sort(&keys);
		c = jd_count(&keys);
		total = 0;
		if(cluster->flags & CF_VERBOSE)
		{
			cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: etcd: re-balancing cluster %s/%s:\n", cluster->key, cluster->env);
		}
		for(n = 0; n < c; n++)
		{
			key = jd_get_idx(&keys, n);
			name = jd_bytes(key, NULL);
			entry = jd_get_key(&dict, key, 0);
			if(!entry || entry->type != HASH)
			{
				continue;
			}
			value = jd_get_ks(entry, "value", 0);
			if(!value)
			{
				continue;
			}
			JD_TRY
			{
				val = jd_get_int(value);
			}
			JD_CATCH(e)
			{
				val = 0;
			}
			if(!strcmp(name, cluster->instid))
			{
				if(cluster->flags & CF_VERBOSE)
				{
					cluster_logf_locked_(cluster, LOG_DEBUG, "* %s [%d]\n", cluster->instid, total);
				}
				base = total;
			}
			else
			{
				if(cluster->flags & CF_VERBOSE)
				{
					cluster_logf_locked_(cluster, LOG_DEBUG, "  %s [%d]\n", name, total);
				}
			}
			total += val;
		}
	}
	if(total != cluster->total_threads || base != cluster->inst_index)
	{
		if(base == -1)
		{
			cluster_logf_locked_(cluster, LOG_NOTICE, "libcluster: etcd: this instance is no longer a member of %s/%s\n", cluster->key, cluster->env);			
		}
		else
		{
			cluster_logf_locked_(cluster, LOG_NOTICE, "libcluster: etcd: cluster %s/%s has re-balanced: new base is %d (was %d), new total is %d (was %d)\n", cluster->key, cluster->env, base, cluster->inst_index, total, cluster->total_threads);
		}
		cluster->inst_index = base;
		cluster->total_threads = total;
		cluster_unlock_(cluster);
		cluster_rebalanced_(cluster);
		/* Re-acquire the lock to restore state */
		cluster_wrlock_(cluster);
	}
	return 0;
}

/* Periodic ping thread: periodically (every cluster->etcd_refresh seconds)
 * ping the registry service until cluster->flags & CF_LEAVING is set.
 */
static void *
cluster_etcd_ping_thread_(void *arg)
{
	CLUSTER *cluster;
	int refresh, count, verbose;
	
	cluster = (CLUSTER *) arg;

	cluster_rdlock_(cluster);
	verbose = (cluster->flags & CF_VERBOSE);
	refresh = cluster->etcd_refresh;
	count = refresh;
	cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: etcd: ping thread starting with ttl=%d, refresh=%d\n", cluster->etcd_ttl, cluster->etcd_refresh);
	cluster_unlock_(cluster);

	/* The cluster lock is not held at the start of each pass */
	for(;;)
	{
		/* Check the flags within a read-lock */
		cluster_rdlock_(cluster);
		verbose = (cluster->flags & CF_VERBOSE);
		if(cluster->flags & CF_LEAVING)
		{
			cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: etcd: 'leaving' flag has been set, will terminate ping thread\n");
			cluster_unlock_(cluster);
			break;
		}
		if(count <= refresh)
		{
			/* We haven't yet hit the refresh time, sleep-and-loop until it
			 * arrives.
			 */
			cluster_unlock_(cluster);
			sleep(1);
			count++;
			continue;
		}
		if(cluster_etcd_ping_(cluster, ETCD_EXISTS))
		{
			/* TODO: if pinging fails, we should try to re-open the
			 *       directories, and if that fails we should leave the
			 *       cluster.
			 */
			cluster_logf_locked_(cluster, LOG_ERR, "libcluster: etcd: failed to update registry\n");
			cluster_unlock_(cluster);
			/* Short retry in case of transient problems */
			sleep(5);
			continue;
		}
		count = 0;
		if(verbose)
		{
			cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: etcd: updated registry with %s=%d\n", cluster->instid, cluster->inst_threads);
		}
		cluster_unlock_(cluster);
	}
	cluster_rdlock_(cluster);
	cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: etcd: ping thread is terminating\n");
	cluster_etcd_unping_(cluster, ETCD_NONE);
	cluster_unlock_(cluster);
	return NULL;
}

/* Re-balancing thread: listen for changes to the etcd directory and
 * invoke cluster_etcd_balance_() (which may invoke the re-balancing callback)
 * when they occur.
 */
static void *
cluster_etcd_balancer_thread_(void *arg)
{
	CLUSTER *cluster;
	ETCD *dir;
	int r, verbose;

	cluster = (CLUSTER *) arg;
	cluster_rdlock_(cluster);
	verbose = (cluster->flags & CF_VERBOSE);
	dir = etcd_clone(cluster->etcd_envdir);
	cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: etcd: re-balancing thread started for %s/%s at <%s>\n", cluster->key, cluster->env, cluster->registry);
	cluster_unlock_(cluster);

	/* The cluster lock is not held at the start of each pass */	
	for(;;)
	{
		r = 0;
		/* Check the flags within a read-lock */
		cluster_rdlock_(cluster);
		verbose = (cluster->flags & CF_VERBOSE);
		if(cluster->flags & CF_LEAVING)
		{
			cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: etcd: 'leaving' flag has been set, will terminate balancing thread\n");
			cluster_unlock_(cluster);
			break;
		}
		JD_SCOPE
		{
			jd_var change = JD_INIT;

			if(verbose)
			{
				cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: etcd: waiting for changes to %s/%s\n", cluster->key, cluster->env);
			}
			/* Wait for changes to the directory; we must release the acquired
			 * read lock while we do this (or the ping thread will be
			 * prevented from working until this loop completes).
			 */
			cluster_unlock_(cluster);
			r = etcd_dir_wait(cluster->etcd_envdir, ETCD_RECURSE, &change);
			if(verbose)
			{
				cluster_logf_(cluster, LOG_DEBUG, "libcluster: etcd: wait result was %d\n", r);
			}
			jd_release(&change);
		}
		if(r)
		{
			cluster_logf_(cluster, LOG_WARNING, "libcluster: etcd: failed to receive changes from registry\n");
			sleep(30);
			continue;
		}
		/* Acquire the write-lock before re-balancing */
		cluster_wrlock_(cluster);  
		if(cluster_etcd_balance_(cluster))
		{			
			cluster_logf_locked_(cluster, LOG_ERR, "libcluster: etcd: failed to balance cluster in response to changes\n");
			cluster_unlock_(cluster);
			continue;
		}
		cluster_unlock_(cluster);
	}
	cluster_logf_(cluster, LOG_DEBUG, "libcluster: etcd: balancing thread is terminating\n");
	etcd_dir_close(dir);
	return NULL;
}
			
