AC_PREREQ(2.52)
AC_INIT(GStreamer-Browser-Plugin, 0.10.0.1)
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
AC_CONFIG_FILES(
Makefile
src/Makefile
bundle/Makefile
bundle/install.rdf
)
AC_OUTPUT
