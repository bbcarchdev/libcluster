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

#include "p_libetcd.h"

ETCD *
etcd_dir_create_(ETCD *parent, const char *name)
{
	ETCD *dir;
	char *sb, *p, *lp;

	dir = (ETCD *) calloc(1, sizeof(ETCD));
	if(!dir)
	{
		return NULL;
	}
	while(*name == '/')
	{
		name++;
	}
	sb = (char *) calloc(1, strlen(name) + 2);
	if(!sb)
	{
		free(dir);
		return NULL;
	}
	lp = NULL;
	for(p = sb; *name; name++, p++)
	{
		*p = *name;
		if(!lp && *p == '/')
		{
			lp = p;
		}
		else
		{
			lp = NULL;
		}
	}
	if(lp)
	{
		lp++;
		*lp = 0;		
	}
	else
	{
		*p = '/';
		p++;
		*p = 0;
	}
	dir->uri = uri_create_str(sb, parent->uri);
	free(sb);
	if(!dir->uri)
	{
		free(dir);
		return NULL;
	}
	dir->verbose = parent->verbose;
	return dir;
}	

ETCD *
etcd_dir_open(ETCD *parent, const char *name)
{
	ETCD *dir;
	CURL *ch;
	int status;
	jd_var dict = JD_INIT;
	jd_var *node, *k;

	dir = etcd_dir_create_(parent, name);
	if(!dir)
	{
		return NULL;
	}
	ch = etcd_curl_create_(parent, dir->uri, NULL);
	if(!ch)
	{
		etcd_dir_close(dir);
		return NULL;
	}
	JD_SCOPE
	{		
		status = etcd_curl_perform_json_(ch, &dict);
		if(!status && dict.type == HASH)
		{
			status = 1;
			node = jd_get_ks(&dict, "node", 0);
			if(node &&
			   node->type == HASH &&
			   (k = jd_get_ks(node, "dir", 0)) &&
			   jd_cast_int(k))
			{
				/* Entry is a directory */
				status = 0;
			}
		}
		jd_release(&dict);
	}
	etcd_curl_done_(ch);
	if(status)
	{
		etcd_dir_close(dir);
		return NULL;
	}
	return dir;
}

ETCD *
etcd_dir_create(ETCD *parent, const char *name, ETCDFLAGS flags)
{
	ETCD *dir;
	CURL *ch;
	const char *data = "dir=1";
	const char *query;
	int status, mustexist;

	mustexist = (flags & ETCD_EXISTS);
	dir = etcd_dir_create_(parent, name);
	if(!dir)
	{
		return NULL;
	}
	if(mustexist)
	{
		query = "prevExist=true";
	}
	else
	{
		query = NULL;
	}
	ch = etcd_curl_put_(parent, dir->uri, data, query);
	status = etcd_curl_perform_(ch);
	etcd_curl_done_(ch);
	if(status)
	{
		etcd_dir_close(dir);
		return NULL;
	}
	return dir;
}

void
etcd_dir_close(ETCD *dir)
{
	if(dir && dir->uri)
	{
		uri_destroy(dir->uri);
	}
	free(dir);
}

int
etcd_dir_get(ETCD *dir, jd_var *out)
{
	CURL *ch;
	int status;
	jd_var dict = JD_INIT;
	jd_var *node, *nodes, *entry, *key, *outkey;
	const char *str, *t;
	size_t c, n;

	ch = etcd_curl_create_(dir, dir->uri, NULL);
	if(!ch)
	{
		return -1;
	}	
	status = etcd_curl_perform_json_(ch, &dict);
	etcd_curl_done_(ch);
	if(status)
	{
		jd_release(&dict);
		return status;
	}
	if(dict.type != HASH)
	{
		jd_release(&dict);
		return -1;
	}
	if(!(node = jd_get_ks(&dict, "node", 0)) || node->type != HASH)
	{
		jd_release(&dict);
		return -1;
	}
	if(!(nodes = jd_get_ks(node, "nodes", 0)) || nodes->type != ARRAY)
	{
		jd_release(&dict);
		jd_set_hash(out, 1);
		return 0;
	}
	c = jd_count(nodes);
	jd_set_hash(out, c);
	for(n = 0; n < c; n++)
	{
		entry = jd_get_idx(nodes, n);
		key = jd_get_ks(entry, "key", 0);
		if(key && key->type == STRING)
		{
			str = jd_bytes(key, NULL);
			t = strrchr(str, '/');
			if(t)
			{
				t++;
				outkey = jd_get_ks(out, t, 1);
			}
			else
			{
				outkey = jd_get_ks(out, str, 1);
			}
			jd_clone(outkey, entry, 1);
		}
	}
	jd_release(&dict);
	return 0;
}

int
etcd_dir_wait(ETCD *dir, ETCDFLAGS flags, jd_var *out)
{
	CURL *ch;
	const char *data = "wait=true", *rdata = "wait=true&recursive=true";
	int status, recursive;

	recursive = (flags & ETCD_RECURSE);
	ch = etcd_curl_create_(dir, dir->uri, (recursive ? rdata : data));
	if(!ch)
	{
		return -1;
	}	
	status = etcd_curl_perform_json_(ch, out);
	etcd_curl_done_(ch);
	return status;
}

