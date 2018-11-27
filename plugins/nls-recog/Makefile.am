AM_CPPFLAGS                = -I./include \
				-I$(DEV_HOME)/include \
				-I$(DEV_HOME)/include/nlsmsc \
				$(UNIMRCP_PLUGIN_INCLUDES)

plugin_LTLIBRARIES         = nlsrecog.la

nlsrecog_la_SOURCES       = src/nls_recog_engine.cpp \
				src/nls_asr.cpp
nlsrecog_la_LDFLAGS       = $(UNIMRCP_PLUGIN_OPTS) \
				-L$(DEV_HOME)/lib -ltinyxml2 -lrealTimeUnity -lssl -lcrypto -lopus -lm -ldl -lz

install-data-local:
	test -d $(DESTDIR)$(sysconfdir) || $(MAKE) def-conf
	test -f $(DESTDIR)$(sysconfdir)/nlsrecog.xml || $(INSTALL) -m 644 conf/nlsrecog.xml $(DESTDIR)$(sysconfdir)

include $(top_srcdir)/build/rules/uniplugin.am
