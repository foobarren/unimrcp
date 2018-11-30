AM_CPPFLAGS                = -I./include \
				-I$(DEPS_HOME)/NlsSdkCpp2.0/include \
				$(UNIMRCP_PLUGIN_INCLUDES)

plugin_LTLIBRARIES         = nls2synth.la

nls2synth_la_SOURCES       = src/nls2_synth_engine.cpp \
				src/nls_tts.cpp \
				src/tinyxml2.cpp
nls2synth_la_LDFLAGS       = $(UNIMRCP_PLUGIN_OPTS) \
				-L$(DEPS_HOME)/NlsSdkCpp2.0/lib/linux -lrealTimeUnity -lssl -lcrypto -lopus

install-data-local:
	test -d $(DESTDIR)$(sysconfdir) || $(MAKE) def-conf
	test -f $(DESTDIR)$(sysconfdir)/nls2synth.xml || $(INSTALL) -m 644 conf/nls2synth.xml $(DESTDIR)$(sysconfdir)

include $(top_srcdir)/build/rules/uniplugin.am
