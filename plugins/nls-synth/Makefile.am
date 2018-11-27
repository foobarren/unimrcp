AM_CPPFLAGS                = -I./include \
				-I$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/include \
				$(UNIMRCP_PLUGIN_INCLUDES)

plugin_LTLIBRARIES         = nlssynth.la

nlssynth_la_SOURCES       = src/nls_synth_engine.cpp \
				src/nls_tts.cpp
nlssynth_la_LDFLAGS       = $(UNIMRCP_PLUGIN_OPTS) \
				-L$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux -ltinyxml2 -lrealTimeUnity -lssl -lcrypto -lopus -lm -ldl -lz

install-data-local:
	test -d $(DESTDIR)$(sysconfdir) || $(MAKE) def-conf
	test -f $(DESTDIR)$(sysconfdir)/nlssynth.xml || $(INSTALL) -m 644 conf/nlssynth.xml $(DESTDIR)$(sysconfdir)

include $(top_srcdir)/build/rules/uniplugin.am
