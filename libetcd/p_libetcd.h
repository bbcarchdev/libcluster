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

#ifndef P_LIBETCD_H_
# define P_LIBETCD_H_                   1

# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <ctype.h>

# include <curl/curl.h>

# include "libetcd.h"

# define PAYLOAD_ALLOC_BLOCK            1024
# define MAX_PAYLOAD_SIZE               16777216

struct etcd_struct
{
	URI *uri;
	int verbose;
};

ETCD *etcd_dir_create_(ETCD *parent, const char *name);

CURL *etcd_curl_create_(ETCD *etcd, URI *uri, const char *query);
CURL *etcd_curl_put_(ETCD *etcd, URI *uri, const char *data, const char *query);
CURL *etcd_curl_delete_(ETCD *etcd, URI *uri, const char *query);
void etcd_curl_done_(CURL *ch);
int etcd_curl_perform_(CURL *ch);
int etcd_curl_perform_json_(CURL *ch, jd_var *dict);

#endif /*!P_LIBETCD_H_*/
