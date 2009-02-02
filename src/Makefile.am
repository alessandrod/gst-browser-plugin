
lib_LTLIBRARIES = libgst-browser-plugin.la

libgst_browser_plugin_la_SOURCES = \
	gbp-npapi.c \
  gbp-np-class.c \
  npn-gate.c \
  gbp-plugin.c \
  gbp-player.c

nodist_libgst_browser_plugin_la_SOURCES = \
  gbp-marshal.c

libgst_browser_plugin_la_CFLAGS = $(GST_CFLAGS) -Wall -Werror
libgst_browser_plugin_la_LIBADD = $(GST_LIBS) -lgstinterfaces-0.10
libgst_browser_plugin_la_LDFLAGS = -avoid-version -no-undefined

noinst_HEADERS = \
  gbp-npapi.h \
  gbp-np-class.h \
  gbp-player.h \
  gbp-plugin.h \
  npapi.h \
  npruntime.h \
  npupp.h
 
gbp-marshal.h: gbp-marshal.list
	glib-genmarshal --header --prefix=gbp_marshal $(srcdir)/gbp-marshal.list > gbp-marshal.h.tmp
	mv gbp-marshal.h.tmp gbp-marshal.h

gbp-marshal.c: gbp-marshal.list
	echo "#include \"gbp-marshal.h\"" >> gbp-marshal.c.tmp
	glib-genmarshal --body --prefix=gbp_marshal $(srcdir)/gbp-marshal.list >> gbp-marshal.c.tmp
	mv gbp-marshal.c.tmp gbp-marshal.c

BUILT_SOURCES = gbp-marshal.h gbp-marshal.c
EXTRA_DIST = gbp-marshal.list

clean-local:
	rm -f $(BUILT_SOURCES)

