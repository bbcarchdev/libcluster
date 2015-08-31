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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "libcluster.h"

static volatile int should_exit;

static const char *short_program_name;

static void
inthandler(int signo)
{
	(void) signo;

	fprintf(stderr, "%s: signal received, will terminate\n", short_program_name);
	should_exit = 1;
}

static void
logger(int priority, const char *format, va_list ap)
{
	fprintf(stderr, "libcluster<%d>: ", priority);
	vfprintf(stderr, format, ap);
}

static int
balancer(CLUSTER *cluster, CLUSTERSTATE *state)
{
	(void) cluster;

	fprintf(stderr, "%s: cluster has re-balanced:\n", short_program_name);
	fprintf(stderr, "   instance index:        %d\n", state->index);
	fprintf(stderr, "   instance thread count: %d\n", state->threads);
	fprintf(stderr, "   total thread count:    %d\n", state->total);
	return 0;
}

static void
usage(void)
{
	printf("Usage: %s [OPTIONS]\n"
		   "\n"
		   "OPTIONS are one or more of:\n"
		   "  -h                        Print this message and exit\n"
		   "  -v                        Be more verbose\n"
		   "  -k KEY                    Set the cluster key to KEY\n"
		   "  -e ENV                    Set the cluster environment to ENV\n"
		   "  -i ID                     Set the instance identifier to ID\n"
		   "  -n COUNT                  Set the number of threads to COUNT\n"
		   " etcd-based clustering:\n"
		   "  -r URI                    Set the cluster registry URI\n"
		   " Static clustering:\n"
		   "  -I INDEX                  Set this instance index to INDEX\n"
		   "  -T COUNT                  Set the cluster total to COUNT\n",
		   short_program_name);
}

int
main(int argc, char **argv)
{
	int c;
	const char *t;
	const char *key = "cluster-test";
	const char *env = NULL;
	const char *registry = NULL;
	const char *instid = NULL;
	int nthreads = 0, instindex = 0, total = 0, verbose = 0;
	CLUSTER *cluster;

	t = strrchr(argv[0], '/');
	short_program_name = (t ? t + 1 : argv[0]);
	
	while((c = getopt(argc, argv, "hvk:e:i:n:r:I:T:")) != -1)
	{
		switch(c)
		{
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
		case 'v':
			verbose = 1;
			break;
		case 'k':
			key = optarg;
			break;
		case 'e':
			env = optarg;
			break;
		case 'i':
			instid = optarg;
			break;
		case 'n':
			nthreads = atoi(optarg);
			break;
		case 'r':
			registry = optarg;
			break;
		case 'I':
			instindex = atoi(optarg);
			break;
		case 'T':
			total = atoi(optarg);
			break;
		default:
			usage();
			exit(EXIT_FAILURE);
		}
	}
	cluster = cluster_create(key);
	if(!cluster)
	{
		fprintf(stderr, "%s: failed to create cluster connection: %s\n", short_program_name, strerror(errno));
		exit(EXIT_FAILURE);
	}
	cluster_set_logger(cluster, logger);
	cluster_set_balancer(cluster, balancer);
	cluster_set_verbose(cluster, verbose);
	if(env)
	{
		cluster_set_env(cluster, env);
	}
	if(instid)
	{
		cluster_set_instance(cluster, instid);
	}
	if(registry)
	{
		cluster_set_registry(cluster, registry);
	}
	if(nthreads)
	{
		cluster_set_threads(cluster, nthreads);
	}
	if(instindex)
	{
		cluster_static_set_index(cluster, instindex);
	}
	if(total)
	{
		cluster_static_set_total(cluster, total);
	}   
	signal(SIGINT, inthandler);
	if(cluster_join(cluster))
	{
		fprintf(stderr, "%s: failed to join cluster: %s\n", short_program_name, strerror(errno));
		exit(EXIT_FAILURE);
	}
	/* In a real cluster member, the main processing loop (or equivalent)
	 * would be here. Because this is simply a test of the clustering
	 * mechanism itself, we just sleep until terminated.
	 */
	fprintf(stderr, "%s: cluster joined; sleeping until terminated\n", short_program_name);
	while(!should_exit)
	{
		sleep(60);
	}
	fprintf(stderr, "%s: will now leave the cluster\n", short_program_name);
	/* Destroying the cluster connection object will automatically leave
	 * the cluster.
	 */
	cluster_destroy(cluster);
	fprintf(stderr, "%s: successfully left the cluster\n", short_program_name);
	return 0;
}
