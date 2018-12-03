AM_CPPFLAGS                = -I./include \
				-I$(DEPS_HOME)/NlsSdkCpp2.0/include \
				-D_GLIBCXX_USE_CXX11_ABI=0 \
				$(UNIMRCP_PLUGIN_INCLUDES)

plugin_LTLIBRARIES         = nls2recog.la

nls2recog_la_SOURCES       = src/nls2_recog_engine.cpp \
				src/nls2_asr.cpp \
				src/tinyxml2.cpp
nls2recog_la_LDFLAGS       = $(UNIMRCP_PLUGIN_OPTS) \				
				-L$(DEPS_HOME)/NlsSdkCpp2.0/lib/linux -lnlsCppSdk -lnlsCommonSdk  -lcurl -lssl -lcrypto -lopus -luuid

install-data-local:
	test -d $(DESTDIR)$(sysconfdir) || $(MAKE) def-conf
	test -f $(DESTDIR)$(sysconfdir)/nls2recog.xml || $(INSTALL) -m 644 conf/nls2recog.xml $(DESTDIR)$(sysconfdir)

include $(top_srcdir)/build/rules/uniplugin.am
