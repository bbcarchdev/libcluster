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
#ifdef WITH_PTHREAD
	pthread_rwlock_init(&(p->lock), NULL);
#endif
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
# if defined(ENABLE_ETCD) || defined(ENABLE_SQL)
	p->ttl = CLUSTER_DEFAULT_TTL;
	p->refresh = CLUSTER_DEFAULT_REFRESH;
# endif
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
#ifdef WITH_PTHREAD
	pthread_rwlock_destroy(&(cluster->lock));
#endif
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
#ifdef ENABLE_ETCD
	case CT_ETCD:
		return cluster_etcd_join_(cluster);
#endif
#ifdef ENABLE_SQL
	case CT_SQL:
		return cluster_sql_join_(cluster);
#endif
	default:
		break;
	}
	cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: cannot join cluster type <%d> which is not implemented\n", type);
	cluster_unlock_(cluster);
	errno = EPERM;
	return -1;
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
#ifdef ENABLE_ETCD
	case CT_ETCD:
		return cluster_etcd_leave_(cluster);
#endif
#ifdef ENABLE_SQL
	case CT_SQL:
		return cluster_sql_leave_(cluster);
#endif
	default:
		break;
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

/* Retrieve the key used by this cluster */
/* MT-safety: safe provided barriered against cluster_destroy() */
const char *
cluster_key(CLUSTER *cluster)
{
	/* Note that we don't lock here: this call's effects, and its return
	 * value, are undefined if not sufficiently protected against
	 * cluster_destroy()
	 */
	return cluster->key;
}

/* Retrieve the name of the environment used by this cluster */
/* MT-safety: safe provided barriered against cluster_set_env() or
 *            cluster_destroy()
 */
const char *
cluster_env(CLUSTER *cluster)
{
	/* Note that we don't lock here: this call's effects, and its return
	 * value, are undefined if not sufficiently protected against
	 * cluster_set_env() or cluster_destroy()
	 */
	return cluster->env;
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

/* Retrieve the identifier of this instance */
/* MT-safety: safe provided barriered against cluster_set_instance() or
 *            cluster_destroy()
 */
const char *
cluster_instance(CLUSTER *cluster)
{
	/* Note that we don't lock here: this call's effects, and its return
	 * value, are undefined if not sufficiently protected against
	 * cluster_set_instance() or cluster_destroy()
	 */
	return cluster->instid;
}

/* Set the instance identifier for this cluster */
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
		cluster_logf_locked_(cluster, LOG_ERR, "libcluster: attempt to set a NULL instance identifier\n");
		cluster_unlock_(cluster);
		errno = EINVAL;
		return -1;
	}
	p = strdup(name);
	if(!p)
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: failed to duplicate instance identifier\n");
		cluster_unlock_(cluster);
		return -1;
	}
	free(cluster->instid);
	cluster->instid = p;
	if(cluster->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: instance identifier set to '%s'\n", cluster->instid);
	}
	cluster_unlock_(cluster);
	return 0;
}

/* Get the index of a worker in this cluster member (not valid when
 * not joined)
 */
int
cluster_index(CLUSTER *cluster, int worker)
{
	int r;

	cluster_rdlock_(cluster);
	if(cluster->flags & CF_JOINED)
	{
		r = cluster->inst_index + worker;
	}
	else
	{
		cluster_logf_locked_(cluster, LOG_WARNING, "libcluster: attempt to retrieve worker index when not joined\n");
		errno = EPERM;
		r = -1;
	}
	cluster_unlock_(cluster);
	return r;
}

/* Get the total worker count for this cluster (not valid when not joined) */
int
cluster_total(CLUSTER *cluster)
{
	int r;

	cluster_rdlock_(cluster);
	if(cluster->flags & CF_JOINED)
	{
		r = cluster->total_threads;
	}
	else
	{
		cluster_logf_locked_(cluster, LOG_WARNING, "libcluster: attempt to retrieve cluster thread count when not joined\n");
		errno = EPERM;
		r = 0;
	}
	cluster_unlock_(cluster);
	return r;
}

/* Get the number of threads (or 'sub-instances') this cluster member has */
int
cluster_workers(CLUSTER *cluster)
{
	int r;

	cluster_rdlock_(cluster);
	if(cluster->flags & CF_JOINED)
	{
		r = cluster->inst_threads;
	}
	else
	{
		cluster_logf_locked_(cluster, LOG_WARNING, "libcluster: attempt to retrieve member worker count when not joined\n");
		errno = EPERM;
		r = 0;
	}
	cluster_unlock_(cluster);
	return r;
}

/* Set the logging callback */
int
cluster_set_logger(CLUSTER *cluster, void (*logger)(int priority, const char *format, va_list ap))
{
	(void) cluster;
	(void) logger;
#ifdef ENABLE_LOGGING
	cluster_wrlock_(cluster);
	cluster->logger = logger;
	cluster_unlock_(cluster);
#endif
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
#ifdef ENABLE_SQL
	{
		const char *t;
		char scheme[64], *p;

		t = strchr(uri, ':');
		if(t && (t - uri) < 63)
		{
			strncpy(scheme, uri, t - uri);
			scheme[t - uri] = 0;
			if(sql_scheme_exists(scheme))
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
				cluster->type = CT_SQL;
				if(cluster->flags & CF_VERBOSE)
				{
					cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: cluster type set to 'SQL' with database <%s>\n", cluster->registry);
				}
				cluster_unlock_(cluster);
				return 0;
			}
		}
	}
#endif /*ENABLE_SQL*/
#ifdef ENABLE_ETCD
	if(!strncmp(uri, "http:", 5))
	{
		char *p;

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
#endif
	cluster_logf_locked_(cluster, LOG_ERR, "libcluster: unsupported scheme in registry URI <%s>\n", uri);
	cluster_unlock_(cluster);
	errno = EINVAL;
	return -1;
}

/* Set the number of workers (or 'sub-instances') this cluster member has */
int
cluster_set_workers(CLUSTER *cluster, int nworkers)
{
	/* TODO: we should notify the ping thread that this has changed so that
	 * that it doesn't take until cluster->etcd_refresh to inform other
	 * nodes.
	 */
	cluster_wrlock_(cluster);
	cluster->inst_threads = nworkers;
	if(cluster->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: number of workers in this cluster member set to %d\n", cluster->inst_threads);
	}
	cluster_unlock_(cluster);
	return 0;
}

/* DEPRECATED: provided only for binary compatibility */
int
cluster_set_threads(CLUSTER *cluster, int nthreads)
{
	return cluster_set_workers(cluster, nthreads);
}

/* Log a message when the cluster is already locked */
/* Because this function is varadic, we provide a no-op implementation
 * rather than using macros to eliminate calls if the compiler doesn't
 * provide a sensible way to do that.
 */
#if ENABLE_LOGGING || NEED_LOGGING_NOOPS
void
cluster_logf_locked_(CLUSTER *cluster, int priority, const char *format, ...)
{
#ifdef ENABLE_LOGGING
	va_list ap;

	va_start(ap, format);
	cluster_vlogf_locked_(cluster, priority, format, ap);
	va_end(ap);
#else
	(void) cluster;
	(void) priority;
#endif
}
#endif /*ENABLE_LOGGING || NEED_LOGGING_NOOPS*/

#ifdef ENABLE_LOGGING
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
#endif /*ENABLE_LOGGING*/

/* Log a message */
#if ENABLE_LOGGING || NEED_LOGGING_NOOPS
void
cluster_logf_(CLUSTER *cluster, int priority, const char *format, ...)
{
#ifdef ENABLE_LOGGING
	va_list ap;

	va_start(ap, format);
	cluster_rdlock_(cluster);
	cluster_vlogf_locked_(cluster, priority, format, ap);
	cluster_unlock_(cluster);
	va_end(ap);
#else
	(void) cluster;
	(void) priority;
#endif
}
#endif /*ENABLE_LOGGING || NEED_LOGGING_NOOPS*/

/* Inform the calling application that the cluster has been re-balanced.
 * The calling thread should not hold the lock when this function is
 * invoked.
 */
int
cluster_rebalanced_(CLUSTER *cluster)
{
	CLUSTERSTATE state;

	cluster_rdlock_(cluster);
	cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: re-balanced; this instance has base index %d (%d workers) from a total of %d\n", cluster->inst_index, cluster->inst_threads, cluster->total_threads);
	if(!cluster->balancer)
	{
		cluster_unlock_(cluster);
		return 0;
	}
	memset(&state, 0, sizeof(state));
	state.index = cluster->inst_index;
	state.workers = cluster->inst_threads;
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
	(void) cluster;

#ifdef WITH_PTHREAD
	pthread_rwlock_rdlock(&(cluster->lock));
#endif
}

/* Write-lock the cluster so that the object's contents can be inspected
 * and modified. A write-lock is exclusive: no other threads can acquire
 * either a read or write lock while a write lock is held.
 */
void
cluster_wrlock_(CLUSTER *cluster)
{
	(void) cluster;

#ifdef WITH_PTHREAD
	pthread_rwlock_wrlock(&(cluster->lock));
#endif
}

/* Unlock a locked cluster */
void
cluster_unlock_(CLUSTER *cluster)
{
	(void) cluster;

#ifdef WITH_PTHREAD
	pthread_rwlock_unlock(&(cluster->lock));
#endif
}

