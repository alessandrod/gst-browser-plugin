bundle: $(top_builddir)/src/libgst-browser-plugin.la
	cp $(top_builddir)/src/.libs/libgst-browser-plugin.so \
			$(top_builddir)/bundle/plugins/
	cp $$(ldd $(top_builddir)/bundle/plugins/libgst-browser-plugin.so| \
				sed -e "s/.*=> //" | cut -d " " -f1 | grep usr) \
			$(top_builddir)/bundle/platform/Linux_x86-gcc3/
	rm -f $(top_builddir)/gstreamer-browser-plugin-$(VERSION).xpi
	zip -r $(top_builddir)/gstreamer-browser-plugin-$(VERSION).xpi *

clean-bundle:
	rm -f $(top_builddir)/bundle/plugins/libgst-browser-plugin.so
	rm -f $(top_builddir)/bundle/platform/Linux_x86-gcc3/*
	rm -f $(top_builddir)/gstreamer-browser-plugin-$(VERSION).xpi

clean-local: clean-bundle
