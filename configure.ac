#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([cached],[0.4],[pr1@pr1.ru])
AM_INIT_AUTOMAKE([cached], [0.4])
WITH_MYSQL()
AC_CONFIG_SRCDIR([src/cached.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
LT_INIT
AM_PROG_CC_C_O

# Checks for libraries.
PKG_CHECK_MODULES([libcurl],[libcurl])
PKG_CHECK_MODULES([gamin],[gamin])
PKG_CHECK_MODULES([libpcre],[libpcre])
libpcre_LIBS="-lpcreposix -lpcre"
PKG_CHECK_MODULES([libconfig],[libconfig])
AC_SEARCH_LIBS([clock_gettime], [rt])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_MODE_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset mkdir regcomp strchr strdup strerror])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
