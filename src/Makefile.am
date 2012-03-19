
lib_LTLIBRARIES = libgst-browser-plugin.la

libgst_browser_plugin_la_SOURCES = \
	gbp-npapi.c \
  gbp-np-class.c \
  gbp-plugin.c \
  gbp-player.c \
	npn-gate.c

nodist_libgst_browser_plugin_la_SOURCES = \
  gbp-marshal.c

.rc.o:
	$(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --tag=RC --mode=compile \
		i586-mingw32msvc-windres $(RCFLAGS) $< -o $@

libgst_browser_plugin_la_LIBADD = $(GST_LIBS) -lgstinterfaces-0.10
libgst_browser_plugin_la_LDFLAGS = -avoid-version -no-undefined -dynamic
libgst_browser_plugin_la_CFLAGS = $(GST_CFLAGS) -Wall -Werror
if OSX_BUILD
libgst_browser_plugin_la_CFLAGS += -DXP_MACOSX -ObjC -framework AppKit -framework Carbon 
else
libgst_browser_plugin_la_LDFLAGS += -Wl, -znodelete
endif

if MINGW_BUILD
libgst_browser_plugin_la_CFLAGS += -DXP_WIN -DWIN32 -D_WINDOWS \
  -DMOZILLA_STRICT_API -DNPBASIC_EXPORTS -D_WIN32_WINNT=0x0502 
libgst_browser_plugin_la_LDFLAGS += -export-symbols $(srcdir)/nprt.def \
  -Xlinker resource.o --enable-stdcall-fixup
libgst_browser_plugin_la_SOURCES += resource.rc
libgst_browser_plugin_la_DEPENDENCIES = resource.o
endif

noinst_HEADERS = \
	gbp-np-class.h \
	gbp-npapi.h \
	gbp-player.h \
	gbp-plugin.h \
	npapi.h \
	npfunctions.h \
	npruntime.h \
	nptypes.h

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

