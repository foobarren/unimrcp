AM_CPPFLAGS                = -I./include \
				-I$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/include \
				$(UNIMRCP_PLUGIN_INCLUDES)

plugin_LTLIBRARIES         = nlsrecog.la

nlsrecog_la_SOURCES       = src/nls_recog_engine.cpp \
				src/nls_asr.cpp \
				src/tinyxml2.cpp
nlsrecog_la_LDFLAGS       = $(UNIMRCP_PLUGIN_OPTS) \
				-L$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux -lrealTimeUnity -lssl -lcrypto -lopus -lm -ldl -lz

install-data-local:
	test -d $(DESTDIR)$(sysconfdir) || $(MAKE) def-conf
	test -f $(DESTDIR)$(sysconfdir)/nlsrecog.xml || $(INSTALL) -m 644 conf/nlsrecog.xml $(DESTDIR)$(sysconfdir)

include $(top_srcdir)/build/rules/uniplugin.am
