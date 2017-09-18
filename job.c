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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "p_libcluster.h"

/* Cluster job management */

static int cluster_job_id_valid_(const char *str);

/* Create a job object */
CLUSTERJOB *
cluster_job_create(CLUSTER *cluster)
{
	return cluster_job_create_id(cluster, NULL);
}

/* Create a job object with a specific ID */
CLUSTERJOB *
cluster_job_create_id(CLUSTER *cluster, const char *str)
{
	CLUSTERJOB *job;
	uuid_t uuid;
	char uuidbuf[40], *s, *t;
	   
	if(str && !cluster_job_id_valid_(str))
	{
		errno = EINVAL;
		return NULL;
	}	
	if(!str)
	{
#ifdef WITH_LIBUUID
		uuid_generate(uuid);
		uuid_unparse_lower(uuid, uuidbuf);
		s = uuidbuf;
		for(t = uuidbuf; *t; t++)
		{
			if(isalnum(*t))
			{
				*s = *t;
				s++;
			}
		}
		*s = 0;
		str = uuidbuf;
#else
		return NULL;   
#endif
	}
	job = (CLUSTERJOB *) calloc(1, sizeof(CLUSTERJOB));
	if(!job)
	{
		return NULL;
	}
	job->cluster = cluster;
	strncpy(job->id, str, CLUSTER_JOB_ID_LEN);
	job->id[CLUSTER_JOB_ID_LEN] = 0;
	strncpy(job->tag, str, CLUSTER_JOB_TAG_LEN);
	job->tag[CLUSTER_JOB_TAG_LEN] = 0;
	cluster_job_logf(job, LOG_INFO, "created job %s\n", job->id);
	return job;
}

/* Destroy a job object */
int
cluster_job_destroy(CLUSTERJOB *job)
{
	free(job);
	return 0;
}

/* Set the parent of a job */
int
cluster_job_set_parent_job(CLUSTERJOB *job, CLUSTERJOB *parent)
{
	if(!parent)
	{
		cluster_job_set_parent_id(job, NULL);
	}
	return cluster_job_set_parent_id(job, parent->id);
}

int
cluster_job_set_parent_id(CLUSTERJOB *job, const char *parentstr)
{
	if(!parentstr)
	{
		cluster_job_logf(job, LOG_INFO, "job no longer has a parent\n");
		job->parent[0] = 0;
		return 0;
	}
	else if(parentstr && !cluster_job_id_valid_(parentstr))
	{
		errno = EINVAL;
		return -1;
	}	
	strncpy(job->parent, parentstr, CLUSTER_JOB_ID_LEN);
	job->parent[CLUSTER_JOB_ID_LEN] = 0;
	cluster_job_logf(job, LOG_INFO, "job is now a child of %s\n", parentstr);
	return 0;
}

/* Change the ID of a job, if possible */
int
cluster_job_set_id(CLUSTERJOB *job, const char *newid)
{
	if(!newid || cluster_job_id_valid_(newid))
	{
		return -1;
	}
	cluster_job_logf(job, LOG_INFO, "job %s has been given a new ID of %s\n", job->id, newid);
	strncpy(job->id, newid, CLUSTER_JOB_ID_LEN);
	job->id[CLUSTER_JOB_ID_LEN] = 0;
	strncpy(job->tag, newid, CLUSTER_JOB_TAG_LEN);
	job->tag[CLUSTER_JOB_TAG_LEN] = 0;
	return 0;
}

/* Set the total and progress values for a job (not including child job processing) */
int
cluster_job_set_total(CLUSTERJOB *job, int total)
{
	job->total = total;
	cluster_job_logf(job, LOG_INFO, "job progress %d/%d\n", job->progress, job->total);
	return 0;
}

int
cluster_job_set_progress(CLUSTERJOB *job, int progress)
{
	job->progress = progress;
	cluster_job_logf(job, LOG_INFO, "job progress %d/%d\n", job->progress, job->total);
	return 0;
}

/* Set the tag used by a job in log messages */
int
cluster_job_set_tag(CLUSTERJOB *restrict job, const char *restrict tag)
{
	strncpy(job->tag, tag, CLUSTER_JOB_TAG_LEN);
	job->tag[CLUSTER_JOB_TAG_LEN] = 0;
	return 0;
}

/* Set a string property on a job */
int
cluster_job_set(CLUSTERJOB *restrict job, const char *key, const char *value)
{
	cluster_job_logf(job, LOG_DEBUG, "job property %s => %s\n", key, value);
	return 0;
}

/* Log an event related to a job */
int
cluster_job_log(CLUSTERJOB *job, int prio, const char *message)
{
	cluster_logf_(job->cluster, prio, "[%s] %s", job->tag, message);
	return 0;
}

int
cluster_job_vlogf(CLUSTERJOB *job, int prio, const char *format, va_list ap)
{
	if(!job->logbuf)
	{
		job->logbuf = (char *) calloc(1, CLUSTER_JOB_LOG_LEN + 1);
		if(!job->logbuf)
		{
			cluster_logf_(job->cluster, LOG_CRIT, "failed to allocate buffer for job log messages\n");
			return -1;
		}
	}
	vsnprintf(job->logbuf, CLUSTER_JOB_LOG_LEN, format, ap);
	cluster_logf_(job->cluster, prio, "[%s] %s", job->tag, job->logbuf);
	return 0;
}

int
cluster_job_logf(CLUSTERJOB *job, int prio, const char *format, ...)
{
	va_list ap;
	int r;

	va_start(ap, format);
	r = cluster_job_vlogf(job, prio, format, ap);
	va_end(ap);

	return r;
}

/* Job status tracking */
int
cluster_job_wait(CLUSTERJOB *job)
{
	cluster_job_logf(job, LOG_INFO, "--- job is now in state WAIT ---\n");
	return 0;
}

int
cluster_job_begin(CLUSTERJOB *job)
{
	cluster_job_logf(job, LOG_INFO, "+++ job is now in state ACTIVE +++\n");
	return 0;
}

int
cluster_job_complete(CLUSTERJOB *job)
{
	cluster_job_logf(job, LOG_INFO, "--- job is now in state COMPLETE ---\n");
	return 0;
}

int
cluster_job_fail(CLUSTERJOB *job)
{
	cluster_job_logf(job, LOG_INFO, "*** job is now in state FAIL ***\n");
	return 0;
}

/* Determine if a job ID is valid */
static int
cluster_job_id_valid_(const char *str)
{
	if(strlen(str) < 2 ||
	   strlen(str) > 32)
	{
		return 0;
	}
	return 1;
}
