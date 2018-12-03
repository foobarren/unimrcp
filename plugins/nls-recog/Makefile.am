AM_CPPFLAGS                = -I./include \
				-I$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/include \
				$(UNIMRCP_PLUGIN_INCLUDES)

plugin_LTLIBRARIES         = nlsrecog.la

nlsrecog_la_SOURCES       = src/nls_recog_engine.cpp \
				src/nls_asr.cpp \
				src/tinyxml2.cpp
nlsrecog_la_LDFLAGS       = $(UNIMRCP_PLUGIN_OPTS) \
				-L$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux -lrealTimeUnity -lssl -lcrypto -lopus

#nlsrecog_ladir            = $(libdir)
#nlsrecog_la_DATA          = $(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux/libssl.so.1.0.0 \
							$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux/libopus.so.0 \
							$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux/libcrypto.so.1.0.0 \
							$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux/librealTimeUnity.so \
							$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux/librealTimeUnity.a \
							$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux/librealTimeUnity-32.so \
							$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux/librealTimeUnity-32.a \
							$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux/libssl.so \
							$(DEPS_HOME)/NlsSpeechSdk-Release-1.3.4/lib/linux/libcrypto.so 

install-data-local:
	test -d $(DESTDIR)$(sysconfdir) || $(MAKE) def-conf
	test -f $(DESTDIR)$(sysconfdir)/nlsrecog.xml || $(INSTALL) -m 644 conf/nlsrecog.xml $(DESTDIR)$(sysconfdir)

include $(top_srcdir)/build/rules/uniplugin.am
