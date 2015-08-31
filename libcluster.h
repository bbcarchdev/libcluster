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

#ifndef LIBCLUSTER_H_
# define LIBCLUSTER_H_                 1

# include <stdarg.h>

typedef struct cluster_struct CLUSTER;
typedef struct cluster_state_struct CLUSTERSTATE;
typedef int (*CLUSTERBALANCE)(CLUSTER *cluster, CLUSTERSTATE *state);

/* A cluster member state structure, passed to the balancing callback when
 * this member's position within the cluster (or the overall size of the
 * cluster) changes.
 */
struct cluster_state_struct
{
	/* The index of this member */
	int index;
	/* The number of threads (or sub-instances) this member has */
	int threads;
	/* The total number of instances, including threads, across the
	 * whole cluster.
	 */
	int total;
};

/* Create a new cluster connection */
CLUSTER *cluster_create(const char *key);

/* Destroy a cluster connection */
int cluster_destroy(CLUSTER *cluster);

/* Join a cluster using the currently configured parameters */
int cluster_join(CLUSTER *cluster);

/* Leave a cluster previously joined with cluster_join() */
int cluster_leave(CLUSTER *cluster);

/* Set the cluster's verbose flag, which enables more debugging information */
int cluster_set_verbose(CLUSTER *cluster, int verbose);

/* Set the name of the environment used by this cluster */
int cluster_set_env(CLUSTER *cluster, const char *envname);

/* Set the unique instance ID of this cluster member */
int cluster_set_instance(CLUSTER *cluster, const char *instid);

/* Set the number of threads (or 'sub-instances') this cluster member has */
int cluster_set_threads(CLUSTER *cluster, int nthreads);

/* Set the registry endpoint URI; NULL indicates this is a static cluster */
int cluster_set_registry(CLUSTER *cluster, const char *uri);

/* Set the logging callback */
int cluster_set_logger(CLUSTER *cluster, void (*logger)(int priority, const char *format, va_list ap));

/* Set the callback invoked when this member's status within the cluster
 * has changed
 */
int cluster_set_balancer(CLUSTER *cluster, CLUSTERBALANCE callback);

/** Static clustering support **/

/* Set the numeric index of this member (0..n) */
int cluster_static_set_index(CLUSTER *cluster, int instindex);

/* Set the total number of threads in the cluster */
int cluster_static_set_total(CLUSTER *cluster, int total);

#endif /*!LIBCLUSTER_H_*/
