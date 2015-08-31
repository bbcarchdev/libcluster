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

struct etcd_data_struct
{
	CURL *ch;
	char *buf;
	size_t size, len;
};

static size_t etcd_payload_(char *ptr, size_t size, size_t nmemb, void *userdata);
static size_t etcd_sink_(char *ptr, size_t size, size_t nmemb, void *userdata);

ETCD *
etcd_connect(const char *url)
{
	URI *uri;
	ETCD *p;

	uri = uri_create_str(url, NULL);
	if(!uri)
	{
		return NULL;
	}
	p = etcd_connect_uri(uri);
	uri_destroy(uri);
	return p;
}

ETCD *
etcd_connect_uri(const URI *uri)
{
	ETCD *p;

	p = (ETCD *) calloc(1, sizeof(ETCD));
	if(!p)
	{
		return NULL;
	}
	p->uri = uri_create_str("/v2/keys/", uri);
	if(!p->uri)
	{
		free(p);
		return NULL;
	}
	return p;
}

ETCD *
etcd_clone(ETCD *etcd)
{
	ETCD *p;

	p = (ETCD *) calloc(1, sizeof(ETCD));
	if(!p)
	{
		return NULL;
	}
	p->uri = uri_create_uri(etcd->uri, NULL);
	if(!p->uri)
	{
		free(p);
		return NULL;
	}
	p->verbose = etcd->verbose;
	return p;
}


void
etcd_disconnect(ETCD *etcd)
{
	if(etcd && etcd->uri)
	{
		uri_destroy(etcd->uri);
	}
	free(etcd);
}

int
etcd_set_verbose(ETCD *etcd, int verbose)
{
	etcd->verbose = verbose;
	return 0;
}

CURL *
etcd_curl_create_(ETCD *etcd, URI *uri, const char *query)
{
	char *uristr, *buf;
	CURL *ch;

	(void) etcd;

	uristr = uri_stralloc(uri);
	if(!uristr)
	{
		return NULL;
	}
	if(query)
	{
		buf = (char *) calloc(1, strlen(uristr) + strlen(query) + 2);
		if(!buf)
		{
			free(uristr);
			return NULL;
		}
		strcpy(buf, uristr);
		strcat(buf, "?");
		strcat(buf, query);
		free(uristr);
		uristr = buf;
	}
	ch = curl_easy_init();
	if(!ch)
	{
		free(uristr);
		return NULL;
	}
	curl_easy_setopt(ch, CURLOPT_VERBOSE, (int) (etcd->verbose));
	curl_easy_setopt(ch, CURLOPT_URL, uristr);
	curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, etcd_sink_);
	free(uristr);
	return ch;
}

void
etcd_curl_done_(CURL *ch)
{
	curl_easy_cleanup(ch);
}

CURL *
etcd_curl_put_(ETCD *etcd, URI *uri, const char *data, const char *query)
{
	CURL *ch;

	if(!(ch = etcd_curl_create_(etcd, uri, query)))
	{
		return NULL;
	}
	curl_easy_setopt(ch, CURLOPT_POSTFIELDS, (char *) data);
	curl_easy_setopt(ch, CURLOPT_CUSTOMREQUEST, "PUT");
	return ch;
}

CURL *
etcd_curl_delete_(ETCD *etcd, URI *uri, const char *query)
{
	CURL *ch;

	if(!(ch = etcd_curl_create_(etcd, uri, query)))
	{
		return NULL;
	}
	curl_easy_setopt(ch, CURLOPT_CUSTOMREQUEST, "DELETE");
	return ch;
}

int
etcd_curl_perform_(CURL *ch)
{
	CURLcode c;
	long status;

	c = curl_easy_perform(ch);
	if(c != CURLE_OK)
	{
		return c;
	}
	status = 0;
	curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &status);
	if(status == 0)
	{
		return -1;
	}
	if(status >= 200 && status <= 299)
	{
		return 0;
	}
	return status;
}

int
etcd_curl_perform_json_(CURL *ch, jd_var *dict)
{
	CURLcode c;
	long status;
	struct etcd_data_struct data;

	memset(&data, 0, sizeof(data));
	data.ch = ch;
	curl_easy_setopt(data.ch, CURLOPT_WRITEFUNCTION, etcd_payload_);
	curl_easy_setopt(data.ch, CURLOPT_WRITEDATA, (void *) &data);
	c = curl_easy_perform(ch);
	if(c != CURLE_OK)
	{
		return c;
	}
	status = 0;
	curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &status);
	if(status == 0)
	{
		return -1;
	}
	if(status >= 200 && status <= 299)
	{
		if(data.len)
		{
			jd_from_jsons(dict, data.buf);
		}
		free(data.buf);
		return 0;
	}
	free(data.buf);
	return -1;
}

static size_t
etcd_sink_(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	(void) ptr;
	(void) userdata;

	return size * nmemb;
}

static size_t
etcd_payload_(char *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct etcd_data_struct *data;
	size_t n;
	char *p;
	
	data = (struct etcd_data_struct *) userdata;
	size *= nmemb;
	n = data->size;
	while(n < data->len + size + 1)
	{
		n += PAYLOAD_ALLOC_BLOCK;
	}
	if(n != data->size)
	{
		if(n > MAX_PAYLOAD_SIZE)
		{
			return 0;
		}
		p = (char *) realloc(data->buf, n);
		if(!p)
		{
			return 0;
		}
		data->size = n;
		data->buf = p;
	}
	memcpy(&(data->buf[data->len]), ptr, size);
	data->len += size;
	data->buf[data->len] = 0;
	return size;
}
