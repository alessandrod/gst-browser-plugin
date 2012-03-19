AC_PREREQ(2.52)
AC_INIT(GStreamer-Browser-Plugin, 0.10.1.1)
AM_CONFIG_HEADER([config.h])
AC_DEFINE([PLAYBACK_THREAD_POOL], [],
    [Use a thread pool to execute playback commands])
AC_DEFINE([PLAYBACK_THREAD_POOL_MAX_SIZE], [10],
    [Max number of threads to create in the pool])
AC_DEFINE([PLAYBACK_THREAD_POOL_MAX_IDLE_TIME], [5000],
    [Max idle time for a thread in the pool])
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE
dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([src/gbp-plugin.h])
AC_LIBTOOL_WIN32_DLL
AM_PROG_LIBTOOL
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AS
PKG_PROG_PKG_CONFIG
GST_REQ=0.10.18
PKG_CHECK_MODULES(GST, gstreamer-0.10)
AM_CONDITIONAL([OSX_BUILD], [test x$target_vendor = xapple])
AM_CONDITIONAL([MINGW_BUILD], [test x${target_os:0:5} = xmingw])
if test "x$target_vendor" = "xapple"; then
  MANIFEST_PLATFORM=Darwin
else
  MANIFEST_PLATFORM=Linux
fi
AC_SUBST(MANIFEST_PLATFORM)
AC_CONFIG_FILES(
Makefile
src/Makefile
bundle/Makefile
bundle/install.rdf
)
AC_OUTPUT
