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

/* Create a new cluster connection */
CLUSTER *
cluster_create(const char *key)
{
	CLUSTER *p;
	uuid_t uuid;
	char uuidbuf[40], *s;
	const char *t;

	p = (CLUSTER *) calloc(1, sizeof(CLUSTER));
	if(!p)
	{
		return NULL;
	}
	pthread_rwlock_init(&(p->lock), NULL);
	p->inst_threads = 1;
	p->instid = (char *) malloc(33);
	if(!p->instid)
	{
		cluster_logf_(p, LOG_CRIT, "libcluster: failed to allocate buffer for instance identifier\n");
		cluster_destroy(p);
		return NULL;
	}
	uuid_generate(uuid);
	uuid_unparse_lower(uuid, uuidbuf);
	s = p->instid;
	for(t = uuidbuf; *t; t++)
	{
		if(isalnum(*t))
		{
			*s = *t;
			s++;
		}
	}
	*s = 0;
	p->key = strdup(key);
	if(!p->key)
	{
		cluster_logf_(p, LOG_CRIT, "libcluster: failed to duplicate cluster key\n");
		cluster_destroy(p);
		return NULL;
	}
	p->env = strdup(CLUSTER_DEFAULT_ENV);
	if(!p->env)
	{
		cluster_logf_(p, LOG_CRIT, "libcluster: failed to duplicate default environment name\n");
		cluster_destroy(p);
		return NULL;
	}
	p->etcd_ttl = CLUSTER_DEFAULT_TTL;
	p->etcd_refresh = CLUSTER_DEFAULT_REFRESH;
	return p;
}

/* Destroy a cluster connection (may block until the cluster has been left) */
int
cluster_destroy(CLUSTER *cluster)
{
	cluster_leave(cluster);
	cluster_wrlock_(cluster);
	free(cluster->instid);
	free(cluster->key);
	free(cluster->env);
	free(cluster->registry);
	cluster_unlock_(cluster);
	pthread_rwlock_destroy(&(cluster->lock));
	free(cluster);
	return 0;
}

/* Join a cluster. If successful, the balancing call-back will always be
 * invoked at least once.
 */
int
cluster_join(CLUSTER *cluster)
{
	CLUSTERTYPE type;

	cluster_rdlock_(cluster);
	if((cluster->flags & CF_JOINED))
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: ignoring attempt to join a cluster which has already been joined\n");
		cluster_unlock_(cluster);
		return 0;
	}
	type = cluster->type;
	cluster_unlock_(cluster);
	switch(type)
	{
	case CT_STATIC:
		return cluster_static_join_(cluster);
	case CT_ETCD:
		return cluster_etcd_join_(cluster);
	}
	return 0;
}

/* Leave a cluster (will block until any threads have been terminated) */
int
cluster_leave(CLUSTER *cluster)
{
	CLUSTERTYPE type;

	cluster_rdlock_(cluster);
	if(!(cluster->flags & CF_JOINED))
	{
		cluster_unlock_(cluster);
		return 0;
	}
	type = cluster->type;
	cluster_unlock_(cluster);
	switch(type)
	{
	case CT_STATIC:
		return cluster_static_leave_(cluster);
	case CT_ETCD:
		return cluster_etcd_leave_(cluster);
	}
	return 0;
}

/* Set the cluster's verbose flag, which enables more debugging information */
int
cluster_set_verbose(CLUSTER *cluster, int verbose)
{
	cluster_wrlock_(cluster);
	if(verbose)
	{
		cluster->flags |= CF_VERBOSE;
	}
	else
	{
		cluster->flags &= ~CF_VERBOSE;
	}
	cluster_unlock_(cluster);
	return 0;
}

/* Set the environment name for this cluster */
int
cluster_set_env(CLUSTER *cluster, const char *env)
{
	char *p;
	
	cluster_wrlock_(cluster);
	if(cluster->flags & CF_JOINED)
	{
		cluster_logf_locked_(cluster, LOG_NOTICE, "libcluster: cannot alter cluster parameters while joined\n");
		cluster_unlock_(cluster);
		errno = EPERM;
		return -1;
	}
	if(!env)
	{
		env = CLUSTER_DEFAULT_ENV;
	}
	p = strdup(env);
	if(!p)
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: failed to duplicate environment name\n");
		cluster_unlock_(cluster);
		return -1;
	}
	free(cluster->env);
	cluster->env = p;
	if(cluster->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: environment name now set to '%s'\n", cluster->env);
	}
	cluster_unlock_(cluster);
	return 0;
}

/* Set the instance name for this cluster */
int
cluster_set_instance(CLUSTER *cluster, const char *name)
{
	char *p;

	cluster_wrlock_(cluster);
	if(cluster->flags & CF_JOINED)
	{
		cluster_logf_locked_(cluster, LOG_NOTICE, "libcluster: cannot alter cluster parameters while joined\n");
		cluster_unlock_(cluster);
		errno = EPERM;
		return -1;
	}
	if(!name)
	{
		cluster_logf_locked_(cluster, LOG_ERR, "libcluster: attempt to set a NULL instance name\n");
		cluster_unlock_(cluster);
		errno = EINVAL;
		return -1;
	}
	p = strdup(name);
	if(!p)
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: failed to duplicate instance name\n");
		cluster_unlock_(cluster);
		return -1;
	}
	free(cluster->instid);
	cluster->instid = p;
	if(cluster->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: instance name now set to '%s'\n", cluster->instid);
	}
	cluster_unlock_(cluster);
	return 0;
}

/* Set the logging callback */
int
cluster_set_logger(CLUSTER *cluster, void (*logger)(int priority, const char *format, va_list ap))
{
	cluster_wrlock_(cluster);
	cluster->logger = logger;
	cluster_unlock_(cluster);
	return 0;
}

/* Set the callback invoked when this member's status within the cluster
 * has changed
 */
int
cluster_set_balancer(CLUSTER *cluster, CLUSTERBALANCE callback)
{
	cluster_wrlock_(cluster);
	cluster->balancer = callback;
	cluster_unlock_(cluster);
	return 0;
}

/* Set the registry URI for this cluster */
int
cluster_set_registry(CLUSTER *cluster, const char *uri)
{
	char *p;

   	cluster_wrlock_(cluster);
	if(cluster->flags & CF_JOINED)
	{
		cluster_logf_locked_(cluster, LOG_NOTICE, "libcluster: cannot alter cluster parameters while joined\n");
		cluster_unlock_(cluster);
		errno = EPERM;
		return -1;
	}
	if(!uri)
	{		
		free(cluster->registry);
		cluster->type = CT_STATIC;
		if(cluster->flags & CF_VERBOSE)
		{
			cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: cluster type set to 'static' (no registry)\n");
		}
		cluster_unlock_(cluster);
		return 0;
	}
	if(!strncmp(uri, "http:", 5))
	{
		p = strdup(uri);
		if(!p)
		{
			cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: failed to duplicate registry URI\n");
			cluster_unlock_(cluster);
			return -1;
		}
		free(cluster->registry);
		cluster->registry = p;
		cluster->type = CT_ETCD;
		if(cluster->flags & CF_VERBOSE)
		{
			cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: cluster type set to 'etcd' with registry <%s>\n", cluster->registry);
		}
		cluster_unlock_(cluster);
		return 0;
	}
	cluster_logf_locked_(cluster, LOG_ERR, "libcluster: unsupported scheme in registry URI <%s>\n", uri);
	cluster_unlock_(cluster);
	errno = EINVAL;
	return -1;
}

/* Set the number of threads (or 'sub-instances') this cluster member has */
int
cluster_set_threads(CLUSTER *cluster, int nthreads)
{
	/* TODO: we should notify the ping thread that this has changed so that
	 * that it doesn't take until cluster->etcd_refresh to inform other
	 * nodes.
	 */
	cluster_wrlock_(cluster);
	cluster->inst_threads = nthreads;
	if(cluster->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: number of threads in this instance set to %d\n", cluster->inst_threads);
	}
	cluster_unlock_(cluster);
	return 0;
}

/* Log a message when the cluster is already locked */
void
cluster_logf_locked_(CLUSTER *cluster, int priority, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	cluster_vlogf_locked_(cluster, priority, format, ap);
	va_end(ap);
}

void
cluster_vlogf_locked_(CLUSTER *cluster, int priority, const char *format, va_list ap)
{
	if(cluster->logger)
	{
		cluster->logger(priority, format, ap);
	}
	else
	{
		if(priority <= LOG_CRIT)
		{
			fprintf(stderr, "libcluster<%d>: ", priority);
			vfprintf(stderr, format, ap);
		}
	}
}

/* Log a message */
void
cluster_logf_(CLUSTER *cluster, int priority, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	cluster_rdlock_(cluster);
	cluster_vlogf_locked_(cluster, priority, format, ap);
	cluster_unlock_(cluster);
	va_end(ap);
}

/* Inform the calling application that the cluster has been re-balanced.
 * The calling thread should not hold the lock when this function is
 * invoked.
 */
int
cluster_rebalanced_(CLUSTER *cluster)
{
	CLUSTERSTATE state;

	cluster_rdlock_(cluster);
	cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: re-balanced; this instance has index %d (%d threads) from a total of %d\n", cluster->inst_index, cluster->inst_threads, cluster->total_threads);
	if(!cluster->balancer)
	{
		cluster_unlock_(cluster);
		return 0;
	}
	memset(&state, 0, sizeof(state));
	state.index = cluster->inst_index;
	state.threads = cluster->inst_threads;
	state.total = cluster->total_threads;
	cluster_unlock_(cluster);
	cluster->balancer(cluster, &state);
	return 0;
}

/* Read-lock the cluster so that the object's contents can be inspected.
 *
 * Mulitple threads can read-lock a cluster simultaneously, but only if
 * none have write-locked it.
 */
void
cluster_rdlock_(CLUSTER *cluster)
{
	pthread_rwlock_rdlock(&(cluster->lock));
}

/* Write-lock the cluster so that the object's contents can be inspected
 * and modified. A write-lock is exclusive: no other threads can acquire
 * either a read or write lock while a write lock is held.
 */
void
cluster_wrlock_(CLUSTER *cluster)
{
	pthread_rwlock_wrlock(&(cluster->lock));
}

/* Unlock a locked cluster */
void
cluster_unlock_(CLUSTER *cluster)
{
	pthread_rwlock_unlock(&(cluster->lock));
}

