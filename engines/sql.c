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

#ifdef ENABLE_SQL

# define CLUSTER_SQL_SCHEMA_VERSION     8
# define CLUSTER_SQL_BALANCE_SLEEP      5
# define CLUSTER_SQL_MAX_BALANCEWAIT    30

static int cluster_sql_rejoin_(CLUSTER *cluster);
static int cluster_sql_ping_(CLUSTER *cluster);
static int cluster_sql_perform_ping_(SQL *restrict sql, void *restrict userdata);
static int cluster_sql_unping_(CLUSTER *cluster);
static void *cluster_sql_ping_thread_(void *arg);
static void *cluster_sql_balancer_thread_(void *arg);
static int cluster_sql_balance_(CLUSTER *cluster);

static int cluster_sql_migrate_(SQL *restrict sql, const char *restrict identifier, int newversion, void *restrict userdata);

static int cluster_sql_querylog_(SQL *restrict sql, const char *restrict query);
static int cluster_sql_errorlog_(SQL *restrict sql, const char *restrict sqlstate, const char *restrict message);
static int cluster_sql_noticelog_(SQL *restrict sql, const char *restrict message);

/* Join a SQL database cluster. To do this, we first update the relevant
 * directory with information about ourselves, then spawn a 're-balancing
 * thread' which watches for changes on that directory.
 *
 * The cluster lock should not be held when invoking this function.
 */
int
cluster_sql_join_(CLUSTER *cluster)
{	
	cluster_wrlock_(cluster);
	cluster->inst_index = -1;
	cluster->pingdb = sql_connect(cluster->registry);
	if(!cluster->pingdb)
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: SQL: cannot establish ping connection to SQL database <%s>\n", cluster->registry);
		cluster_unlock_(cluster);
		cluster_sql_leave_(cluster);
		return -1;
	}
	sql_set_userdata(cluster->pingdb, (void *) cluster);
	sql_set_noticelog(cluster->pingdb, cluster_sql_noticelog_);
	sql_set_errorlog(cluster->pingdb, cluster_sql_errorlog_);
	sql_set_querylog(cluster->pingdb, cluster_sql_querylog_);
	if(sql_migrate(cluster->pingdb, "com.github.bbcarchdev.libcluster", cluster_sql_migrate_, (void *) cluster))
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: SQL: schema migration failed\n");
		cluster_unlock_(cluster);
		cluster_sql_leave_(cluster);
		return -1;
	}
	cluster->balancedb = sql_connect(cluster->registry);
	if(!cluster->balancedb)
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: SQL: cannot establish balancer connection to SQL database <%s>\n", cluster->registry);
		cluster_unlock_(cluster);
		cluster_sql_leave_(cluster);
		return -1;
	}
	sql_set_userdata(cluster->balancedb, (void *) cluster);
	sql_set_noticelog(cluster->balancedb, cluster_sql_noticelog_);
	sql_set_errorlog(cluster->balancedb, cluster_sql_errorlog_);
	sql_set_querylog(cluster->balancedb, cluster_sql_querylog_);
	if(cluster_sql_ping_(cluster))
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: SQL: failed to perform initial ping\n");
		cluster_unlock_(cluster);
		cluster_sql_leave_(cluster);
		return -1;
	}
	if(cluster_sql_balance_(cluster))
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: SQL: failed to perform initial balancing\n");
		cluster_unlock_(cluster);
		cluster_sql_leave_(cluster);
		return -1;
	}
	if(!(cluster->flags & CF_PASSIVE))
	{
		pthread_create(&(cluster->ping_thread), NULL, cluster_sql_ping_thread_, (void *) cluster);
	}
	pthread_create(&(cluster->balancer_thread), NULL, cluster_sql_balancer_thread_, (void *) cluster);
	cluster->flags |= CF_JOINED;
	cluster_unlock_(cluster);
	return 0;
}

/* Leave a SQL-based cluster. We first set a flag to indicate that we want
 * to leave the cluster (which the re-balancing thread will check), then
 * pthread_join() that thread to wait for it to shut down. Once it has, we
 * can remove our own entry from the directory.
 *
 * The cluster lock should not be held when invoking this function.
 */
int
cluster_sql_leave_(CLUSTER *cluster)
{
	pthread_t pt, bt;

	/* Use a write-lock to prevent a read-lock - write-lock race */
	cluster_wrlock_(cluster);
	if(cluster->flags & CF_JOINED)
	{
		cluster->flags |= CF_LEAVING;
		pt = cluster->ping_thread;
		bt = cluster->balancer_thread;
		/* Unlock to allow the threads to read the flag */		
		cluster_unlock_(cluster);
		if(pt)
		{
			pthread_join(pt, NULL);
		}
		if(bt)
		{
			pthread_join(bt, NULL);
		}
		/* Re-acquire the lock so that the unwinding can safely complete */
		cluster_wrlock_(cluster);
	}
	cluster->flags &= ~(CF_JOINED|CF_LEAVING);
	cluster->ping_thread = 0;
	cluster->balancer_thread = 0;
	if(cluster->pingdb)
	{
		sql_disconnect(cluster->pingdb);
		cluster->pingdb = NULL;
	}
	if(cluster->balancedb)
	{
		sql_disconnect(cluster->balancedb);
		cluster->balancedb = NULL;
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
cluster_sql_ping_(CLUSTER *cluster)
{
	if(cluster->flags & CF_PASSIVE)
	{
		return 0;
	}
	return sql_perform(cluster->pingdb, cluster_sql_perform_ping_, (void *) cluster, 5, SQL_TXN_CONSISTENT);
}

static int
cluster_sql_perform_ping_(SQL *restrict sql, void *restrict userdata)
{
	CLUSTER *cluster;
	time_t t;
	struct tm tm;
	char nowbuf[64], expbuf[64];

	t = time(NULL);
	cluster = (CLUSTER *) userdata;
	if(sql_executef(sql, "DELETE FROM \"cluster_node\" WHERE \"id\" = %Q AND \"key\" = %Q AND \"env\" = %Q", cluster->instid, cluster->key, cluster->env))
	{
		return -1;
	}
	gmtime_r(&t, &tm);
	strftime(nowbuf, sizeof(nowbuf) -1, "%Y-%m-%d %H:%M:%S", &tm);
	
	t += cluster->ttl;
	gmtime_r(&t, &tm);
	strftime(expbuf, sizeof(expbuf) -1, "%Y-%m-%d %H:%M:%S", &tm);

	if(sql_executef(sql, "INSERT INTO \"cluster_node\" (\"id\", \"key\", \"partition\", \"env\", \"threads\", \"updated\", \"expires\") VALUES (%Q, %Q, %Q, %Q, %d, %Q, %Q)",
					cluster->instid, cluster->key, cluster->partition,
					cluster->env, cluster->inst_threads, nowbuf, expbuf))
	{
		return -1;
	}
	return 1;
}

/* 'Un-ping' - that is, remove our entry from the directory.
 *
 * The cluster should be at least read-locked when invoking this function.
 */
static int
cluster_sql_unping_(CLUSTER *cluster)
{
	if(cluster->flags & CF_PASSIVE)
	{
		return 0;
	}
	if(sql_executef(cluster->pingdb, "DELETE FROM \"cluster_node\" WHERE \"id\" = %Q AND \"key\" = %Q AND \"env\" = %Q", cluster->instid, cluster->key, cluster->env))
	{
		return -1;
	}
	return 0;
}

/* Read the directory from the registry service and determine what our index
 * in the cluster is.
 *
 * The cluster should be write-locked when invoking this function. The lock
 * may be released and re-acquired during the course of its execution.
 */
static int
cluster_sql_balance_(CLUSTER *cluster)
{
	SQL_STATEMENT *rs;
	const char *id;
	int total, base, val;
	time_t now;
	struct tm tm;
	char nowbuf[64];
	
	if(cluster->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: re-balancing cluster %s/%s:\n", cluster->key, cluster->env);
	}
	now = time(NULL);
	gmtime_r(&now, &tm);
	strftime(nowbuf, sizeof(nowbuf) - 1, "%Y-%m-%d %H:%M:%S", &tm);
	if(cluster->partition)
	{
		rs = sql_queryf(cluster->balancedb, "SELECT \"id\", \"threads\" FROM \"cluster_node\" WHERE \"key\" = %Q AND \"env\" = %Q AND \"partition\" = %Q AND \"expires\" >= %Q ORDER BY \"id\" ASC",
						cluster->key, cluster->env, cluster->partition, nowbuf);
	}
	else
	{
		rs = sql_queryf(cluster->balancedb, "SELECT \"id\", \"threads\" FROM \"cluster_node\" WHERE \"key\" = %Q AND \"env\" = %Q AND \"partition\" IS NULL AND \"expires\" >= %Q ORDER BY \"id\" ASC",
						cluster->key, cluster->env, nowbuf);
	}
	total = 0;
	base = -1;
	for(; !sql_stmt_eof(rs); sql_stmt_next(rs))
	{
		id = sql_stmt_str(rs, 0);
		val = sql_stmt_long(rs, 1);
		if(!strcmp(id, cluster->instid) && !(cluster->flags & CF_PASSIVE))
		{
			base = total;
			if(cluster->flags & CF_VERBOSE)
			{
				cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: * %s [%d]\n", cluster->instid, total);
			}
		}
		else
		{
			if(cluster->flags & CF_VERBOSE)
			{
				cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster:   %s [%d]\n", id, total);
			}
		}
		total += val;
	}
	sql_stmt_destroy(rs);
	if(total != cluster->total_threads || base != cluster->inst_index)
	{
		if(base == -1)
		{
			if(cluster->partition)
			{
				cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: this instance is no longer a member of %s[%s]/%s\n", cluster->key, cluster->partition, cluster->env);
			}
			else
			{
				cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: this instance is no longer a member of %s/%s\n", cluster->key, cluster->env);
			}
		}
		else
		{
			if(cluster->partition)
			{
				cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: cluster %s[%s]/%s has re-balanced: new base is %d (was %d), new total is %d (was %d)\n", cluster->key, cluster->partition, cluster->env, base, cluster->inst_index, total, cluster->total_threads);
			}
			else
			{
				cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: cluster %s/%s has re-balanced: new base is %d (was %d), new total is %d (was %d)\n", cluster->key, cluster->env, base, cluster->inst_index, total, cluster->total_threads);
			}		
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

/* Invoked before a parent process forks */
void
cluster_sql_prepare_(CLUSTER *p)
{
	CLUSTERFLAGS flags;
	pthread_t pt, bt;

	cluster_wrlock_(p);
	if(p->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(p, LOG_INFO, "libcluster: SQL: preparing for fork()\n");
	}
	flags = p->flags;
	p->flags |= CF_LEAVING;
	pt = p->ping_thread;
	bt = p->balancer_thread;
	cluster_unlock_(p);
	if(pt)
	{
		pthread_join(pt, NULL);
	}
	if(bt)
	{
		pthread_join(bt, NULL);
	}
	cluster_wrlock_(p);
	p->ping_thread = 0;
	p->balancer_thread = 0;
	p->inst_index = -1;
	p->total_threads = 0;
	cluster_rebalanced_(p);
	if(p->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(p, LOG_INFO, "libcluster: SQL: threads terminated\n");
	}
	p->flags = flags;
	/* Leave the cluster locked */
}

void
cluster_sql_parent_(CLUSTER *p)
{
	int r;

	/* The cluster is locked on entry */
	r = 0;
	if((p->forkmode & CLUSTER_FORK_PARENT) && (p->flags & CF_JOINED))
	{
		if(p->flags & CF_VERBOSE)
		{
			cluster_logf_locked_(p, LOG_NOTICE, "libcluster: SQL: resuming cluster membership in parent process\n");
		}
		cluster_sql_rejoin_(p);
	}
	cluster_unlock_(p);
	if(r)
	{
		cluster_sql_leave_(p);
	}
}

void
cluster_sql_child_(CLUSTER *p)
{
	int r;

	/* The cluster was locked by the parent on entry, reset the lock */
	pthread_rwlock_init(&(p->lock), NULL);
	cluster_wrlock_(p);
	r = 0;
	if(p->forkmode & CLUSTER_FORK_CHILD)
	{
		if(p->forkmode & CLUSTER_FORK_PARENT)
		{
			/* We're re-joining the cluster in both the parent and the child, therefore
			 * the child will be assigned a new instance UUID
			 */
			cluster_reset_instance_locked_(p);
		}
		if(p->flags & CF_JOINED)
		{
			if(p->flags & CF_VERBOSE)
			{
				cluster_logf_locked_(p, LOG_NOTICE, "libcluster: SQL: resuming cluster membership in child process\n");
			}
			r = cluster_sql_rejoin_(p);
		}
	}
	cluster_unlock_(p);
	if(r)
	{
		cluster_sql_leave_(p);
	}
}

/* Re-join a cluster after a fork() - the write lock must be held */
static int
cluster_sql_rejoin_(CLUSTER *cluster)
{
	if(cluster_sql_ping_(cluster))
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: SQL: failed to perform initial ping\n");
		return -1;
	}
	if(cluster_sql_balance_(cluster))
	{
		cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: SQL: failed to perform initial balancing\n");
		return -1;
	}
	if(!(cluster->flags & CF_PASSIVE))
	{
		pthread_create(&(cluster->ping_thread), NULL, cluster_sql_ping_thread_, (void *) cluster);
	}
	pthread_create(&(cluster->balancer_thread), NULL, cluster_sql_balancer_thread_, (void *) cluster);
	return 0;
}


/* Periodic ping thread: periodically (every cluster->etcd_refresh seconds)
 * ping the registry service until cluster->flags & CF_LEAVING is set.
 */
static void *
cluster_sql_ping_thread_(void *arg)
{
	CLUSTER *cluster;
	int refresh, count, verbose;
	
	cluster = (CLUSTER *) arg;

	cluster_rdlock_(cluster);
	verbose = (cluster->flags & CF_VERBOSE);
	refresh = cluster->refresh;
	count = refresh;
	cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: ping thread starting with ttl=%d, refresh=%d\n", cluster->ttl, cluster->refresh);
	cluster_unlock_(cluster);

	/* The cluster lock is not held at the start of each pass */
	for(;;)
	{
		/* Check the flags within a read-lock */
		cluster_rdlock_(cluster);
		verbose = (cluster->flags & CF_VERBOSE);
		if(cluster->flags & CF_LEAVING)
		{
			cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: 'leaving' flag has been set, will terminate ping thread\n");
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
		if(cluster_sql_ping_(cluster))
		{
			/* TODO: if pinging fails, we should try to re-connect to the
			 *       database, and if that fails we should leave the
			 *       cluster.
			 */
			cluster_logf_locked_(cluster, LOG_ERR, "libcluster: SQL: failed to update registry\n");
			cluster_unlock_(cluster);
			/* Short retry in case of transient problems */
			sleep(5);
			continue;
		}
		count = 0;
		if(verbose)
		{
			cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: updated registry with %s=%d\n", cluster->instid, cluster->inst_threads);
		}
		cluster_unlock_(cluster);
	}
	cluster_rdlock_(cluster);
	cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: ping thread is terminating\n");
	cluster_sql_unping_(cluster);
	cluster_unlock_(cluster);
	return NULL;
}

/* Re-balancing thread: wait for changes to the cluster_node table and
 * invoke cluster_sql_balance_() (which may invoke the re-balancing callback)
 * when they occur.
 *
 */
static void *
cluster_sql_balancer_thread_(void *arg)
{
	CLUSTER *cluster;
	int verbose;
	time_t now, last;
	struct tm tm;
	char nowbuf[64], lastbuf[64];
	SQL_STATEMENT *rs;

	last = 0;
	lastbuf[0] = 0;	
	cluster = (CLUSTER *) arg;
	cluster_rdlock_(cluster);
	verbose = (cluster->flags & CF_VERBOSE);
	if(cluster->partition)
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: re-balancing thread started for %s[%s]/%s\n", cluster->key, cluster->partition, cluster->env);
	}
	else
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: re-balancing thread started for %s/%s\n", cluster->key, cluster->env);
	}
	cluster_unlock_(cluster);

	/* The cluster lock is not held at the start of each pass */	
	for(;;)
	{
		/* Check the flags within a read-lock */
		cluster_rdlock_(cluster);
		verbose = (cluster->flags & CF_VERBOSE);
		if(cluster->flags & CF_LEAVING)
		{
			cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: 'leaving' flag has been set, will terminate balancing thread\n");
			cluster_unlock_(cluster);
			break;
		}
		if(verbose)
		{
			if(cluster->partition)
			{
				cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: waiting for changes to %s[%s]/%s\n", cluster->key, cluster->partition, cluster->env);
			}
			else
			{
				cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL: waiting for changes to %s/%s\n", cluster->key, cluster->env);
			}
		}
		/* Check for changes to the table; we must release the acquired
		 * read lock while we do this (or the ping thread will be
		 * prevented from working until this loop completes).
		 */
		cluster_unlock_(cluster);
		sleep(CLUSTER_SQL_BALANCE_SLEEP);
		now = time(NULL);
		gmtime_r(&now, &tm);
		strftime(nowbuf, sizeof(nowbuf) - 1, "%Y-%m-%d %H:%M:%S", &tm);
		if(lastbuf[0])
		{
			if(cluster->partition)
			{
				rs = sql_queryf(cluster->balancedb, "SELECT \"id\", \"threads\" FROM \"cluster_node\" WHERE \"key\" = %Q AND \"env\" = %Q AND \"partition\" = %Q AND \"expires\" >= %Q AND \"updated\" >= %Q",
								cluster->key, cluster->env, cluster->partition, nowbuf, lastbuf);
			}
			else
			{
				rs = sql_queryf(cluster->balancedb, "SELECT \"id\", \"threads\" FROM \"cluster_node\" WHERE \"key\" = %Q AND \"env\" = %Q AND \"partition\" IS NULL AND \"expires\" >= %Q AND \"updated\" >= %Q",
							cluster->key, cluster->env, nowbuf, lastbuf);
			}
		}
		else
		{
			if(cluster->partition)
			{
				rs = sql_queryf(cluster->balancedb, "SELECT \"id\", \"threads\" FROM \"cluster_node\" WHERE \"key\" = %Q AND \"env\" = %Q AND \"partition\" = %Q AND \"expires\" >= %Q",
								cluster->key, cluster->env, cluster->partition, nowbuf);

			}
			else
			{
				rs = sql_queryf(cluster->balancedb, "SELECT \"id\", \"threads\" FROM \"cluster_node\" WHERE \"key\" = %Q AND \"env\" = %Q AND \"partition\" IS NULL AND \"expires\" >= %Q",
								cluster->key, cluster->env, nowbuf);
			}
		}
		strcpy(lastbuf, nowbuf);
		if(sql_stmt_eof(rs) && now - last < CLUSTER_SQL_MAX_BALANCEWAIT)
		{
			sql_stmt_destroy(rs);
			continue;
		}
		sql_stmt_destroy(rs);
		/* Acquire the write-lock before re-balancing */
		cluster_wrlock_(cluster);
		last = now;
		if(cluster_sql_balance_(cluster))
		{
			cluster_logf_locked_(cluster, LOG_ERR, "libcluster: SQL: failed to balance cluster in response to changes\n");
			cluster_unlock_(cluster);
			continue;
		}
		cluster_unlock_(cluster);
	}
	cluster_logf_(cluster, LOG_DEBUG, "libcluster: SQL: balancing thread is terminating\n");
	return NULL;
}

static int
cluster_sql_migrate_(SQL *restrict sql, const char *restrict identifier, int newversion, void *restrict userdata)
{
	CLUSTER *cluster;
	SQL_VARIANT variant;
	const char *ddl, *timetype;

	(void) identifier;

	ddl = NULL;
	cluster = (CLUSTER *) userdata;
	variant = sql_variant(sql);
	if(!newversion)
	{
		return CLUSTER_SQL_SCHEMA_VERSION;
	}
	cluster_logf_locked_(cluster, LOG_NOTICE, "libcluster: SQL: updating database schema to version %d\n", newversion);
	if(newversion == 1)
	{
		if(sql_execute(sql, "DROP TABLE IF EXISTS \"cluster_node\""))
		{
			return -1;
		}
		if(variant == SQL_VARIANT_MYSQL)
		{
			ddl = "CREATE TABLE \"cluster_node\" ("
				"\"id\" VARCHAR(32) NOT NULL, "
				"\"key\" VARCHAR(32) NOT NULL, "
				"\"env\" VARCHAR(32) NOT NULL, "
				"\"threads\" INT NOT NULL DEFAULT 0, "
				"\"updated\" DATETIME NOT NULL, "
				"\"expires\" DATETIME NOT NULL, "
				"PRIMARY KEY (\"id\", \"key\", \"env\")"
				") ENGINE=InnoDB DEFAULT CHARSET=utf8 DEFAULT COLLATE=utf8_unicode_ci";
		}
		else
		{
			ddl = "CREATE TABLE \"cluster_node\" ("
				"\"id\" VARCHAR(32) NOT NULL, "
				"\"key\" VARCHAR(32) NOT NULL, "
				"\"env\" VARCHAR(32) NOT NULL, "
				"\"threads\" INT NOT NULL DEFAULT 0, "
				"\"updated\" TIMESTAMP NOT NULL, "
				"\"expires\" TIMESTAMP NOT NULL, "
				"PRIMARY KEY (\"id\", \"key\", \"env\")"
				")";
		}
		if(sql_execute(sql, ddl))
		{
			return -1;
		}
		return 0;
	}
	if(newversion == 2)
	{
		if(sql_execute(sql, "CREATE INDEX \"cluster_node_key_env\" ON \"cluster_node\" (\"key\", \"env\")"))
		{
			return -1;
		}
		return 0;
	}
	if(newversion == 3)
	{
		if(sql_execute(sql, "CREATE INDEX \"cluster_node_expires\" ON \"cluster_node\" (\"expires\")"))
		{
			return -1;
		}
		return 0;
	}
	if(newversion == 4)
	{
		if(sql_execute(sql, "CREATE INDEX \"cluster_node_updated\" ON \"cluster_node\" (\"updated\")"))
		{
			return -1;
		}
		return 0;
	}
	if(newversion == 5)
	{
		if(sql_execute(sql, "ALTER TABLE \"cluster_node\" ADD \"partition\" VARCHAR(32) default NULL"))
		{
			return -1;
		}
		if(sql_execute(sql, "CREATE INDEX \"cluster_node_partition\" ON \"cluster_node\" (\"partition\")"))
		{
			return -1;
		}
		return 0;
	}
	if(newversion == 6)
	{
		/* cluster_data allows applications to store associate key-value pairs
		 * with a cluster
		 */
		if(sql_execute(sql, "CREATE TABLE \"cluster_data\" ( "
					   " \"key\" VARCHAR(32) NOT NULL, "
					   " \"env\" VARCHAR(32) NOT NULL, "
					   " \"name\" VARCHAR(32) NOT NULL, "
					   " \"value\" TEXT DEFAULT NULL, "
					   " PRIMARY KEY (\"key\", \"env\", \"name\") "
					   ")"))
		{
			return -1;
		}
		if(sql_execute(sql, "CREATE INDEX \"cluster_data_key_env\" ON \"cluster_data\" (\"key\", \"env\")"))
		{
			return -1;
		}
		return 0;
	}
	if(newversion == 7)
	{
		/* cluster_node_data allows applications to store associate key-value pairs
		 * with an individual node
		 */
		if(sql_execute(sql, "CREATE TABLE \"cluster_node_data\" ( "
					   " \"id\" VARCHAR(32) NOT NULL, "
					   " \"key\" VARCHAR(32) NOT NULL, "
					   " \"env\" VARCHAR(32) NOT NULL, "
					   " \"name\" VARCHAR(32) NOT NULL, "
					   " \"value\" TEXT DEFAULT NULL, "
					   " PRIMARY KEY (\"id\", \"key\", \"env\", \"name\") "
					   ")"))
		{
			return -1;
		}
		if(sql_execute(sql, "CREATE INDEX \"cluster_node_data_id_key_env\" ON \"cluster_node_data\" (\"id\", \"key\", \"env\")"))
		{
			return -1;
		}
		if(sql_execute(sql, "CREATE INDEX \"cluster_node_data_key_env\" ON \"cluster_node_data\" (\"key\", \"env\")"))
		{
			return -1;
		}
		return 0;
	}
	if(newversion == 8)
	{
		if(variant == SQL_VARIANT_MYSQL)
		{
			timetype = "DATETIME";
		}
		else
		{
			timetype = "TIMESTAMP";
		}
		if(sql_executef(sql, "CREATE TABLE \"cluster_job\" ( "
					   " \"id\" VARCHAR(32) NOT NULL, "
					   " \"key\" VARCHAR(32) NOT NULL, "
					   " \"env\" VARCHAR(32) NOT NULL, "
					   " \"parent\" VARCHAR(32) default NULL, "
					   " \"status\" VARCHAR(16) NOT NULL default 'WAIT', "
					   " \"created\" %s NOT NULL, "
					   " \"updated\" %s NOT NULL, "
					   " \"node\" VARCHAR(32) default NULL, "
					   " \"progress\" INT NOT NULL default 0, "
					   " \"total\" INT NOT NULL default 1, "
					   " PRIMARY KEY (\"id\", \"key\", \"env\") "
						")", timetype, timetype))			
		{
			return -1;
		}
		return 0;
	}
	cluster_logf_locked_(cluster, LOG_CRIT, "libcluster: SQL: attempt to update schema to unsupported version %d\n", newversion);
	return -1;
}

/* Logging callbacks invoked by libsql
 * Note that the cluster object will always be locked at the point where these
 * are invoked.
 */
static int
cluster_sql_querylog_(SQL *restrict sql, const char *restrict query)
{
	CLUSTER *cluster;
	
	cluster = (CLUSTER *) sql_userdata(sql);
	if(cluster->flags & CF_VERBOSE)
	{
		cluster_logf_locked_(cluster, LOG_DEBUG, "libcluster: SQL query: %s\n", query);
	}
	return 0;
}

static int
cluster_sql_errorlog_(SQL *restrict sql, const char *restrict sqlstate, const char *restrict message)
{
	CLUSTER *cluster;
	
	cluster = (CLUSTER *) sql_userdata(sql);
	cluster_logf_locked_(cluster, LOG_ERR, "libcluster: SQL: [%s] %s\n", sqlstate, message);
	return 0;
}

static int
cluster_sql_noticelog_(SQL *restrict sql, const char *restrict message)
{
	CLUSTER *cluster;
	
	cluster = (CLUSTER *) sql_userdata(sql);
	cluster_logf_locked_(cluster, LOG_NOTICE, "libcluster: SQL: %s", message);
	return 0;
}

#endif /*ENABLE_SQL*/
