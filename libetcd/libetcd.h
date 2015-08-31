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

#ifndef LIBETCD_H_
# define LIBETCD_H_                     1

# include "liburi.h"

# include "jsondata.h"

typedef struct etcd_struct ETCD;

typedef enum
{
	ETCD_NONE = 0,
	ETCD_EXISTS = (1<<0),
	ETCD_RECURSE = (1<<1)
} ETCDFLAGS;

ETCD *etcd_connect(const char *url);
ETCD *etcd_connect_uri(const URI *uri);
void etcd_disconnect(ETCD *etcd);
ETCD *etcd_clone(ETCD *etcd);

int etcd_set_verbose(ETCD *etcd, int verbose);

ETCD *etcd_dir_open(ETCD *parent, const char *name);
ETCD *etcd_dir_create(ETCD *parent, const char *name, ETCDFLAGS flags);
int etcd_dir_get(ETCD *dir, jd_var *out);
int etcd_dir_delete(ETCD *parent, const char *name, ETCDFLAGS flags);
void etcd_dir_close(ETCD *dir);
int etcd_dir_wait(ETCD *dir, ETCDFLAGS flags, jd_var *change);

int etcd_key_set(ETCD *dir, const char *name, const char *value, ETCDFLAGS flags);
int etcd_key_delete(ETCD *dir, const char *name, ETCDFLAGS flags);
int etcd_key_set_ttl(ETCD *dir, const char *name, const char *value, int ttl, ETCDFLAGS flags);
int etcd_key_set_data_ttl(ETCD *dir, const char *name, const unsigned char *data, size_t len, int ttl, ETCDFLAGS flags);

#endif /*!LIBETCD_H_*/
