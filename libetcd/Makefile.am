## Author: Mo McRoberts <mo.mcroberts@bbc.co.uk>
##
## Copyright 2015 BBC.
##
##  Licensed under the Apache License, Version 2.0 (the "License");
##  you may not use this file except in compliance with the License.
##  You may obtain a copy of the License at
##
##      http://www.apache.org/licenses/LICENSE-2.0
##
##  Unless required by applicable law or agreed to in writing, software
##  distributed under the License is distributed on an "AS IS" BASIS,
##  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
##  See the License for the specific language governing permissions and
##  limitations under the License.
##

noinst_LTLIBRARIES = libetcd.la

libetcd_la_SOURCES = libetcd.h \
	p_libetcd.h \
	connect.c dir.c key.c

libetcd_la_LDFLAGS = @AM_LDFLAGS@ \
	@LIBJSONDATA_LDFLAGS@ @LIBURI_LDFLAGS@ @LIBCURL_LDFLAGS@ \
	-noinst -avoid-version
libetcd_la_LIBADD = \
	@LIBJSONDATA_LOCAL_LIBS@ @LIBJSONDATA_LIBS@ \
	@LIBURI_LOCAL_LIBS@ @LIBURI_LIBS@ \
	@LIBCURL_LOCAL_LIBS@ @LIBCURL_LIBS@ \
	@LIBS@
