/* Author: Mo McRoberts <mo.mcroberts@bbc.co.uk>
 *
 * Copyright (c) 2015-2017 BBC
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
typedef struct cluster_job_struct CLUSTERJOB;
typedef int (*CLUSTERBALANCE)(CLUSTER *cluster, CLUSTERSTATE *state);

/* Enumeration for how libcluster should behave when the process invokes
 * fork()
 */
typedef enum
{
	/* The cluster membership shall be transferred to the child process */
	CLUSTER_FORK_CHILD = (1<<0),
	/* The cluster membership shall continue in the parent process */
	CLUSTER_FORK_PARENT = (1<<1),
	/* The cluster membership shall continue in both the parent and the child,
	 * with the child being assigned a new node UUID
	 */
	CLUSTER_FORK_BOTH = (1<<2)
} CLUSTERFORK;

/* A cluster member state structure, passed to the balancing callback when
 * this member's position within the cluster (or the overall size of the
 * cluster) changes.
 */
struct cluster_state_struct
{
	/* The index of the first worker in this cluster member */
	int index;
	/* The number of workers this member has */
	int workers;
	/* The total number of workers across the whole cluster */
	int total;
	/* Have we joined passively? */
	int passive;
};

/* Create a new cluster connection */
CLUSTER *cluster_create(const char *key);

/* Destroy a cluster connection */
int cluster_destroy(CLUSTER *cluster);

/* Join a cluster using the currently configured parameters */
int cluster_join(CLUSTER *cluster);

/* Join a cluster passively - i.e., without participating in it */
int cluster_join_passive(CLUSTER *cluster);

/* Leave a cluster previously joined with cluster_join() */
int cluster_leave(CLUSTER *cluster);

/* Set the cluster's verbose flag, which enables more debugging information */
int cluster_set_verbose(CLUSTER *cluster, int verbose);

/* Retrieve the key used by this cluster */
/* MT-safety: safe provided barriered against cluster_destroy() */
const char *cluster_key(CLUSTER *cluster);

/* Retrieve the name of the environment used by this cluster */
/* MT-safety: safe provided barriered against cluster_set_env() or
 *            cluster_destroy()
 */
const char *cluster_env(CLUSTER *cluster);

/* Set the name of the environment used by this cluster */
int cluster_set_env(CLUSTER *cluster, const char *envname);

/* Retrieve the identifier of this member */
/* MT-safety: safe provided barriered against cluster_set_instance() or
 *            cluster_destroy()
 */
const char *cluster_instance(CLUSTER *cluster);

/* Set the unique member instance identifer of this cluster member */
int cluster_set_instance(CLUSTER *cluster, const char *instid);

/* Reset the unique member instance identifer of this cluster member */
int cluster_reset_instance(CLUSTER *cluster);

/* Retrieve the partition this member is part of (if any) */
/* MT-safety: safe provided barriered against cluster_set_partition() or
 *            cluster_destroy()
 */
const char *cluster_partition(CLUSTER *cluster);

/* Set the partition that this member is part of (cannot be invoked after the
 * cluster has been joined)
 */
int cluster_set_partition(CLUSTER *cluster, const char *partition);

/* Get the index of a worker in this cluster member (not valid when not joined)
 * The first worker is 0, the second is 1, ...
 */
int cluster_index(CLUSTER *cluster, int worker);

/* Get the total worker count for this cluster (not valid when not joined) */
int cluster_total(CLUSTER *cluster);

/* Get the number of workers this cluster member has */
int cluster_workers(CLUSTER *cluster);

/* Set the number of worker this cluster member has */
int cluster_set_workers(CLUSTER *cluster, int nworkers);

/* Atomically obtain the current cluster state */
int cluster_state(CLUSTER *cluster, CLUSTERSTATE *statebuf);

/* Set the registry endpoint URI; NULL indicates this is a static cluster */
int cluster_set_registry(CLUSTER *cluster, const char *uri);

/* Set the logging callback */
int cluster_set_logger(CLUSTER *cluster, void (*logger)(int priority, const char *format, va_list ap));

/* Set the callback invoked when this member's status within the cluster
 * has changed
 */
int cluster_set_balancer(CLUSTER *cluster, CLUSTERBALANCE callback);

/* Set the fork behaviour (default is CLUSTER_FORK_CHILD) */
int cluster_set_fork(CLUSTER *cluster, CLUSTERFORK mode);

/** Static clustering support **/

/* Set the numeric index of this member (0..n) */
int cluster_static_set_index(CLUSTER *cluster, int instindex);

/* Set the total number of workers in the cluster */
int cluster_static_set_total(CLUSTER *cluster, int total);

/** Job tracking **/

/* Create a job object */
CLUSTERJOB *cluster_job_create(CLUSTER *cluster);

/* Create a job object with a specific ID */
CLUSTERJOB *cluster_job_create_id(CLUSTER *cluster, const char *str);

/* Create a job object with a name and a parent ID */
CLUSTERJOB *cluster_job_create_id_name(CLUSTER *cluster, const char *parentid, const char *name);

/* Create a job object with a name and a parent job */
CLUSTERJOB *cluster_job_create_job_name(CLUSTER *cluster, CLUSTERJOB *parent, const char *name);

/* Destroy a job object */
int cluster_job_destroy(CLUSTERJOB *job);

/* Set the parent of a job */
int cluster_job_set_parent_job(CLUSTERJOB *job, CLUSTERJOB *parent);
int cluster_job_set_parent_id(CLUSTERJOB *job, const char *parentstr);

/* Change the ID of a job, if possible */
int cluster_job_set_id(CLUSTERJOB *job, const char *newid);

/* Change the name of a job, if possible */
int cluster_job_set_name(CLUSTERJOB *job, const char *newname);

/* Set the total and progress values for a job (not including child job processing) */
int cluster_job_set_total(CLUSTERJOB *job, int total);
int cluster_job_set_progress(CLUSTERJOB *job, int total);

/* Set the tag used in log messages */
int cluster_job_set_tag(CLUSTERJOB *restrict job, const char *restrict tag);

/* Set a key-value property pair on the job */
int cluster_job_set(CLUSTERJOB *restrict job, const char *key, const char *value);

/* Log an event related to a job */
int cluster_job_log(CLUSTERJOB *job, int prio, const char *message);
int cluster_job_vlogf(CLUSTERJOB *job, int prio, const char *message, va_list ap);
int cluster_job_logf(CLUSTERJOB *job, int prio, const char *message, ...);

/* Job status tracking */
int cluster_job_wait(CLUSTERJOB *job);
int cluster_job_begin(CLUSTERJOB *job);
int cluster_job_complete(CLUSTERJOB *job);
int cluster_job_fail(CLUSTERJOB *job);

#endif /*!LIBCLUSTER_H_*/
