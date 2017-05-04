## Author: Mo McRoberts <mo.mcroberts@bbc.co.uk>
##
## Copyright 2015-2017 BBC.
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

ACLOCAL_AMFLAGS = -I m4

if ENABLE_ETCD

SUBDIRS = libetcd .

endif

EXTRA_DIST = README.md LICENSE-2.0 libcluster.pc.in libcluster-uninstalled.pc.in

DISTCLEANFILES = libcluster.pc libcluster-uninstalled.pc

pkgconfigdir = $(libdir)/pkgconfig

noinst_DATA = libcluster-uninstalled.pc

pkgconfig_DATA = libcluster.pc

lib_LTLIBRARIES = libcluster.la

include_HEADERS = libcluster.h

bin_PROGRAMS = cluster-test

AM_CPPFLAGS = @AM_CPPFLAGS@ @CPPFLAGS@ @LIBUUID_CPPFLAGS@ \
	-Ilibetcd -I$(srcdir)/libetcd

libcluster_la_SOURCES = libcluster.h \
	p_libcluster.h \
	cluster.c static.c etcd.c sql.c

libcluster_la_LDFLAGS = @AM_LDFLAGS@ \
	@LIBURI_LDFLAGS@ \
	-noinst -avoid-version

libcluster_la_LIBADD = \
	@convenience_libs@ @LOCAL_LIBS@ @LIBS@

cluster_test_LDADD = libcluster.la

BRANCH ?= develop

DEVELOP_SUBMODULES = m4

checkout:
	@for submod in $(DEVELOP_SUBMODULES) ; do \
		echo "+++ Checking out the latest version of $$submod on $(BRANCH)" >&2 ; \
		cd $$submod && \
		git checkout $(BRANCH) && \
		git pull && \
		$(MAKE) checkout BRANCH=$(BRANCH) || exit $? && \
		cd .. ; \
	done
