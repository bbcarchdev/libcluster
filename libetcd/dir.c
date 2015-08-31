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
	json_t *dict, *jnode, *jdir;

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
	status = etcd_curl_perform_json_(ch, &dict);
	if(!status)
	{
		status = 1;
		jnode = json_object_get(dict, "node");
		if(jnode && json_typeof(jnode) == JSON_OBJECT)
		{
			jdir = json_object_get(jnode, "dir");
			if(jdir && json_is_true(jdir))
			{
				/* Entry is a directory */
				status = 0;
			}
		}
		json_decref(dict);
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
etcd_dir_get(ETCD *dir, json_t **out)
{
	CURL *ch;
	int status;
   	json_t *dict, *node, *nodes, *entry, *key;
	const char *str, *t;
	size_t c, n;

	*out = NULL;
	ch = etcd_curl_create_(dir, dir->uri, NULL);
	if(!ch)
	{
		return -1;
	}	
	status = etcd_curl_perform_json_(ch, &dict);
	etcd_curl_done_(ch);
	if(status)
	{
		json_decref(dict);
		return status;
	}
	if(json_typeof(dict) != JSON_OBJECT)
	{
		json_decref(dict);
		return -1;
	}
	if(!(node = json_object_get(dict, "node")) || json_typeof(node) != JSON_OBJECT)
	{
		json_decref(dict);
		return -1;
	}
	if(!(nodes = json_object_get(node, "nodes")) || json_typeof(nodes) != JSON_ARRAY)
	{
		/* Return an empty array */
		json_decref(dict);
		*out = json_object();
		return 0;
	}
	/* Transform the returned array into an object, where the 'key' member of
	 * each array member (itself an object) is used as the key in the returned
	 * hash.
	 */
	c = json_array_size(nodes);
	*out = json_object();
	for(n = 0; n < c; n++)
	{
		entry = json_array_get(nodes, n);
		key = json_object_get(entry, "key");
		if(key && json_typeof(key) == JSON_STRING)
		{		   
			str = json_string_value(key);
			t = strrchr(str, '/');
			if(t)
			{
				t++;
			}
			else
			{
				t = str;
			}
			json_object_set(*out, t, entry);
		}
	}
	json_decref(dict);
	return 0;
}

int
etcd_dir_wait(ETCD *dir, ETCDFLAGS flags, json_t **out)
{
	CURL *ch;
	const char *data = "wait=true", *rdata = "wait=true&recursive=true";
	int status, recursive;

	*out = NULL;
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

