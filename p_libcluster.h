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
# include <unistd.h>
# include <syslog.h>
# include <pthread.h>
# include <ctype.h>
# include <errno.h>
# include <uuid/uuid.h>

# include "libetcd.h"
# include "libcluster.h"

/* Default environment name, overridden with cluster_set_env() */
# define CLUSTER_DEFAULT_ENV            "production"
/* Default etcd key time-to-live */
# define CLUSTER_DEFAULT_TTL            120
/* Default etcd refresh time */
# define CLUSTER_DEFAULT_REFRESH        30

typedef enum
{
	CT_STATIC,
	CT_ETCD
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
	pthread_rwlock_t lock;
	char *instid;
	char *key;
	char *env;
	char *registry;
	/* Current state */
	int inst_index;
	int inst_threads;
	int total_threads;
	/* Callbacks */
	void (*logger)(int priority, const char *format, va_list ap);
	CLUSTERBALANCE balancer;
	/* etcd-based clustering */
	ETCD *etcd_root;
	ETCD *etcd_clusterdir;
	ETCD *etcd_envdir;
	pthread_t ping_thread;
	pthread_t balancer_thread;
	int etcd_ttl;
	int etcd_refresh;
};

void cluster_logf_(CLUSTER *cluster, int priority, const char *format, ...);
void cluster_logf_locked_(CLUSTER *cluster, int priority, const char *format, ...);
void cluster_vlogf_locked_(CLUSTER *cluster, int priority, const char *format, va_list ap);

void cluster_rdlock_(CLUSTER *cluster);
void cluster_wrlock_(CLUSTER *cluster);
void cluster_unlock_(CLUSTER *cluster);

int cluster_rebalanced_(CLUSTER *cluster);

int cluster_static_join_(CLUSTER *cluster);
int cluster_static_leave_(CLUSTER *cluster);

int cluster_etcd_join_(CLUSTER *cluster);
int cluster_etcd_leave_(CLUSTER *cluster);

#endif /*!LIBCLUSTER_H_*/
