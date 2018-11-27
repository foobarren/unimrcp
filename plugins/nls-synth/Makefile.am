AM_CPPFLAGS                = -I./include \
				-I$(DEV_HOME)/include \
				-I$(DEV_HOME)/include/nlsmsc \
				$(UNIMRCP_PLUGIN_INCLUDES)

plugin_LTLIBRARIES         = nlssynth.la

nlssynth_la_SOURCES       = src/nls_synth_engine.cpp \
				src/nls_tts.cpp
nlssynth_la_LDFLAGS       = $(UNIMRCP_PLUGIN_OPTS) \
				-L$(DEV_HOME)/lib -ltinyxml2 -lrealTimeUnity -lssl -lcrypto -lopus -lm -ldl -lz

install-data-local:
	test -d $(DESTDIR)$(sysconfdir) || $(MAKE) def-conf
	test -f $(DESTDIR)$(sysconfdir)/nlssynth.xml || $(INSTALL) -m 644 conf/nlssynth.xml $(DESTDIR)$(sysconfdir)

include $(top_srcdir)/build/rules/uniplugin.am
