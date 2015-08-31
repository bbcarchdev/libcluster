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

/* Set the numeric index of this member (0..n) */
int
cluster_static_set_index(CLUSTER *cluster, int instindex)
{
   	cluster_wrlock_(cluster);
	if(cluster->flags & CF_JOINED)
	{
		cluster_logf_locked_(cluster, LOG_NOTICE, "libcluster: cannot alter cluster parameters while joined\n");
		errno = EPERM;
		cluster_unlock_(cluster);
		return -1;
	}
	if(instindex < 0)
	{
		cluster_logf_locked_(cluster, LOG_ERR, "libcluster: static: instance index cannot be a negative number\n");
		cluster_unlock_(cluster);
		errno = EINVAL;
		return -1;
	}
	cluster->inst_index = instindex;
	if(cluster->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: static: this instance's index set to %d\n", cluster->inst_index);
	}
	cluster_unlock_(cluster);
	return 0;
}

/* Set the total number of threads in the cluster */
int
cluster_static_set_total(CLUSTER *cluster, int total)
{
   	cluster_wrlock_(cluster);
	if(cluster->flags & CF_JOINED)
	{
		cluster_logf_locked_(cluster, LOG_NOTICE, "libcluster: cannot alter cluster parameters while joined\n");
		errno = EPERM;
		cluster_unlock_(cluster);
		return -1;
	}
	if(total < 1)
	{
		cluster_logf_locked_(cluster, LOG_ERR, "libcluster: static: thread count must be a positive integer\n");
		cluster_unlock_(cluster);
		errno = EINVAL;
		return -1;
	}
	cluster->total_threads = total;
	if(cluster->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: static: total thread count set to %d\n", cluster->inst_index);
	}
	cluster_unlock_(cluster);
	return 0;
}

/* Joining a static cluster is effectively a no-op, because it requires no
 * coordination with other nodes: we can only assume the parameters provided
 * are correct, so we set the CF_JOINED flag and invoke the balancer callback
 * to inform the application of the state.
 */
int
cluster_static_join_(CLUSTER *cluster)
{
	cluster_wrlock_(cluster);
	if(!cluster->total_threads)
	{
		cluster->total_threads = 1;
	}
	if(cluster->inst_index >= cluster->total_threads)
	{
		cluster_logf_locked_(cluster, LOG_ERR, "libcluster: static: cannot join static cluster because the instance index (%d) is not less than the total number of threads in the cluster (%d)\n", cluster->inst_index, cluster->total_threads);
		cluster_unlock_(cluster);
		errno = EINVAL;
		return -1;
	}
	if(cluster->inst_index + cluster->inst_threads >= cluster->total_threads)
	{
		cluster_logf_locked_(cluster, LOG_ERR, "libcluster: static: cannot join static cluster because the highest thread index (%d) is not less than the total number of threads in the cluster (%d)\n", cluster->inst_index + cluster->inst_threads, cluster->total_threads);
		cluster_unlock_(cluster);
		errno = EINVAL;
		return -1;
	}
	cluster->flags |= CF_JOINED;
	cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: static: successfully joined the cluster\n");
	cluster_unlock_(cluster);
	return cluster_rebalanced_(cluster);
}

/* Similarly, leaving a static cluster simply involves unsetting CF_JOINED */
int
cluster_static_leave_(CLUSTER *cluster)
{
	cluster_wrlock_(cluster);
	cluster->flags &= ~(CF_JOINED | CF_LEAVING);
	cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: static: successfully left the cluster\n");
	cluster_unlock_(cluster);
	return 0;
}
