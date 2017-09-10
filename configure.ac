## Author: Mo McRoberts <mo.mcroberts@bbc.co.uk>
##
## Copyright 2014-2015 BBC
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

AC_INIT([libcluster],m4_esyscmd([/bin/sh m4/get-version.sh]),[RESFeedback@bbc.co.uk],,[https://github.com/bbcarchdev/libcluster/])
PACKAGE_DESC='Clustering support library'
AC_SUBST([PACKAGE_DESC])

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([scripts])

AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_MAINTAINER_MODE
AC_CONFIG_HEADER([config.h])

BT_PROG_XCODE
AC_PROG_CC([clang c99 gcc cc])
AC_PROG_CC_C99
BT_PROG_CC_WARN
BT_PROG_CC_DEBUG
BT_ENABLE_POSIX
BT_ENABLE_XSI
LT_INIT
BT_DEFINE_PREFIX

need_syslog=no
need_pthread=no
need_libuuid=yes
need_libcurl=no
need_libjansson=no
need_liburi=no
convenience_libs=''

dnl Feature tests

dnl --disable-logging (enabled by default)
AC_ARG_ENABLE([logging],[AS_HELP_STRING([disable-logging],[disable logging callbacks])],[enable_logging=$enableval],[enable_logging=yes])
if test x"$enable_logging" = x"yes" ; then
	need_syslog=yes
fi

dnl --disable-pthreads (enabled by default, and overidden by etcd)
AC_ARG_ENABLE([pthreads],[AS_HELP_STRING([disable-pthreads],[disable POSIX threads (note: POSIX threads are required by etcd support)])],[enable_pthreads=$enableval],[enable_pthreads=yes])

dnl --disable-etcd (etcd support is enabled by default)
AC_ARG_ENABLE([etcd],[AS_HELP_STRING([disable-etcd],[disable etcd support])],[enable_etcd=$enableval],[enable_etcd=yes])
AM_CONDITIONAL([ENABLE_ETCD],[test x"$enable_etcd" = x"yes"])
if test x"$enable_etcd" = x"yes" ; then
	if test x"$enable_pthreads" = x"no" ; then
		AC_WARN([ignoring --disable-pthreads because etcd support is enabled; use --disable-etcd to build without it])
	fi
	enable_pthreads=yes
	need_pthreads=yes
	need_libcurl=yes
	need_liburi=yes
	need_libjansson=yes
	convenience_libs="$convenience_libs \${top_builddir}/libetcd/libetcd.la"
fi

AC_ARG_ENABLE([sql],[AS_HELP_STRING([disable-sql],[disable SQL database support])],[enable_sql=$enableval],[enable_sql=yes])
AM_CONDITIONAL([ENABLE_SQL],[test x"$enable_sql" = x"yes"])
if test x"$enable_sql" = x"yes" ; then
	need_libsql=yes
	need_liburi=yes
fi

dnl Feature dependencies
test x"$enable_pthreads" = x"yes" && need_pthreads=yes

if test x"$enable_logging" = x"yes" ; then
	AC_DEFINE_UNQUOTED([ENABLE_LOGGING],[1],[define to 1 to build with logging callback support])
fi

if test x"$enable_pthreads" = x"yes" ; then
	AC_DEFINE_UNQUOTED([ENABLE_ETCD],[1],[define to 1 to build with etcd support])
fi

if test x"$enable_etcd" = x"yes" ; then
	AC_DEFINE_UNQUOTED([ENABLE_ETCD],[1],[define to 1 to build with etcd support])
fi

if test x"$enable_sql" = x"yes" ; then
	AC_DEFINE_UNQUOTED([ENABLE_SQL],[1],[define to 1 to build with SQL database support])
fi

dnl Feature summaries
AC_MSG_CHECKING([whether to build with etcd support])
AC_MSG_RESULT([$enable_etcd])
AC_MSG_CHECKING([whether to build with SQL database support])
AC_MSG_RESULT([$enable_sql])
AC_MSG_CHECKING([whether to build with POSIX threads support])
AC_MSG_RESULT([$enable_pthreads])
AC_MSG_CHECKING([whether to build with logging callbacks])
AC_MSG_RESULT([$enable_logging])

dnl Dependency tests

AC_CHECK_HEADERS([unistd.h])

if test x"$need_syslog" = x"yes" ; then
	AC_CHECK_HEADERS([syslog.h])
fi

if test x"$need_pthreads" = x"yes" ; then
	BT_REQUIRE_PTHREAD
fi

if test x"$need_libcurl" = x"yes" ; then
	BT_REQUIRE_LIBCURL
fi

if test x"$need_libjansson" = x"yes" ; then
	BT_REQUIRE_LIBJANSSON
fi

if test x"$need_libuuid" = x"yes" ; then
	BT_REQUIRE_LIBUUID
fi

dnl Always check for possibly-uninstalled libraries last
if test x"$need_liburi" = x"yes" ; then
	BT_REQUIRE_LIBURI_INCLUDED
fi

if test x"$need_libsql" = x"yes" ; then
   BT_REQUIRE_LIBSQL
fi

AC_SUBST([convenience_libs])

AC_CONFIG_FILES([
Makefile
libetcd/Makefile
engines/Makefile
util/Makefile
m4/Makefile
libcluster.pc
libcluster-uninstalled.pc
])

AC_OUTPUT
