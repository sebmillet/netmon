#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# Sébastien Millet, December 2012, 2019

AC_PREREQ([2.68])

AC_INIT([netmon], [1.1.2], [milletseb@laposte.net])

AC_CONFIG_SRCDIR([src/main.h])
AC_CONFIG_AUX_DIR([admin])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

AM_INIT_AUTOMAKE([subdir-objects])

AM_MAINTAINER_MODE

AC_CONFIG_HEADERS([config.h])

dnl ==================== Options ====================================

AC_ARG_ENABLE(debug,
              [  --enable-debug          enable debugging information],
              enable_debug="$enableval", enable_debug="no")

if test "x$USE_MAINTAINER_MODE" = "xyes"; then
	build_docs=yes
else
	build_docs=no
fi

AM_CONDITIONAL(BUILD_DOCS, test $build_docs = yes)

if test "${enable_debug}" = yes ; then
  CFLAGS="$CFLAGS -DDEBUG -g"
fi

# Hack to successfully link with gcc version 4.6, cf. URL
# http://ubuntuforums.org/showthread.php?p=11347972&posted=1#post11347972
# It is gruiiik, but don't know how to change the options order
# in gcc call.
CFLAGS="-Wl,--no-as-needed $CFLAGS"

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([atexit gethostbyname gettimeofday memset select socket strcasecmp strerror strncasecmp strstr])

dnl AC_CHECK_LIB(pthread, pthread_create)
AC_CHECK_LIB(ssl, SSL_library_init)
AC_CHECK_LIB(crypto, ERR_error_string_n)

AC_OUTPUT(Makefile src/Makefile doc/Makefile)

