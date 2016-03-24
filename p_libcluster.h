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

#ifndef P_LIBCLUSTER_H_
# define P_LIBCLUSTER_H_               1

# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <ctype.h>
# include <errno.h>

# ifdef HAVE_UNISTD_H
#  include <unistd.h>
# endif

# ifdef HAVE_SYSLOG_H
#  include <syslog.h>
# endif

# ifdef WITH_PTHREAD
#  include <pthread.h>
# endif

# ifdef WITH_LIBUUID
#  include <uuid/uuid.h>
# endif

# ifdef ENABLE_SQL
#  include <libsql.h>
# endif

# ifdef ENABLE_ETCD
#  include "libetcd.h"
# endif

# include "libcluster.h"

/* Default environment name, overridden with cluster_set_env() */
# define CLUSTER_DEFAULT_ENV            "production"
/* Default etcd key time-to-live */
# define CLUSTER_DEFAULT_TTL            120
/* Default etcd refresh time */
# define CLUSTER_DEFAULT_REFRESH        30

/* We only use syslog for the LOG_xxx constants; if they aren't available
 * we can provide generic values instead.
 */
# ifndef LOG_EMERG
#  define LOG_EMERG                    0
# endif

# ifndef LOG_ALERT
#  define LOG_ALERT                    1
# endif

# ifndef LOG_CRIT
#  define LOG_CRIT                     2
# endif

# ifndef LOG_ERR
#  define LOG_ERR                      3
# endif

# ifndef LOG_WARNING
#  define LOG_WARNING                  4
# endif

# ifndef LOG_NOTICE
#  define LOG_NOTICE                   5
# endif

# ifndef LOG_INFO
#  define LOG_INFO                     6
# endif

# ifndef LOG_DEBUG
#  define LOG_DEBUG                    7
# endif

typedef enum
{
	CT_STATIC,
	CT_ETCD,
	CT_SQL
} CLUSTERTYPE;

typedef enum
{
	CF_NONE = 0,
	CF_JOINED = (1<<0),
	CF_LEAVING = (1<<1),
	CF_VERBOSE = (1<<2)
} CLUSTERFLAGS;

struct cluster_struct
{
	CLUSTERTYPE type;
	CLUSTERFLAGS flags;
# ifdef WITH_PTHREAD
	pthread_rwlock_t lock;
# endif
	char *instid;
	char *key;
	char *env;
	char *registry;
	char *partition;
	/* Current state */
	int inst_index;
	int inst_threads;
	int total_threads;
	/* Callbacks */
# ifdef ENABLE_LOGGING
	void (*logger)(int priority, const char *format, va_list ap);
# endif
	CLUSTERBALANCE balancer;
# if defined(ENABLE_ETCD) || defined(ENABLE_SQL)
	int ttl;
	int refresh;
# endif
# ifdef ENABLE_ETCD
	/* etcd-based clustering */
	ETCD *etcd_root;
	ETCD *etcd_clusterdir;
	ETCD *etcd_partitiondir;
	ETCD *etcd_envdir;
# endif /*ENABLE_ETCD*/
# ifdef ENABLE_SQL
	SQL *pingdb;
	SQL *balancedb;
# endif /*ENABLE_SQL*/
# ifdef WITH_PTHREAD
	pthread_t ping_thread;
	pthread_t balancer_thread;
# endif /*WITH_PTHREAD*/
};

void cluster_logf_(CLUSTER *cluster, int priority, const char *format, ...);
void cluster_logf_locked_(CLUSTER *cluster, int priority, const char *format, ...);
void cluster_vlogf_locked_(CLUSTER *cluster, int priority, const char *format, va_list ap);

# ifndef ENABLE_LOGGING
#  if __STDC_VERSION__ >= 199901L
#   define cluster_logf_(...)          /* */
#   define cluster_logf_locked_(...)   /* */
#  elif __GNUC__
#   define cluster_logf_(cluster...)   /* */
#   define cluster_logf_locked_(cluster...) /* */
#  else
#   define NEED_LOGGING_NOOPS          1
#  endif
#  define cluster_vlogf_locked_(c, p, f, a) /* */
# endif

void cluster_rdlock_(CLUSTER *cluster);
void cluster_wrlock_(CLUSTER *cluster);
void cluster_unlock_(CLUSTER *cluster);

int cluster_rebalanced_(CLUSTER *cluster);

int cluster_static_join_(CLUSTER *cluster);
int cluster_static_leave_(CLUSTER *cluster);

# ifdef ENABLE_ETCD
int cluster_etcd_join_(CLUSTER *cluster);
int cluster_etcd_leave_(CLUSTER *cluster);
# endif

# ifdef ENABLE_SQL
int cluster_sql_join_(CLUSTER *cluster);
int cluster_sql_leave_(CLUSTER *cluster);
# endif

/* Deprecated public methods retained for binary compatibility */
int cluster_set_threads(CLUSTER *cluster, int nthreads);

#endif /*!LIBCLUSTER_H_*/
