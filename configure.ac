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

BT_REQUIRE_PTHREAD
BT_REQUIRE_OPENSSL
BT_REQUIRE_LIBCURL
BT_REQUIRE_LIBURI
BT_REQUIRE_LIBJSONDATA
BT_REQUIRE_LIBUUID

AC_CONFIG_FILES([
Makefile
libetcd/Makefile
m4/Makefile
libcluster.pc
libcluster-uninstalled.pc
])

AC_OUTPUT
