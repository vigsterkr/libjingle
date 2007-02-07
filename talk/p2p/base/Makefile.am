libcricketp2pbase_la_SOURCES = stun.cc \
			       port.cc \
			       udpport.cc \
			       tcpport.cc \
			       stunport.cc \
			       relayport.cc \
			       stunrequest.cc \
			       sessionmanager.cc \
			       session.cc \
			       transport.cc \
			       transportchannel.cc \
			       transportchannelproxy.cc \
			       p2ptransport.cc \
			       p2ptransportchannel.cc \
			       rawtransport.cc \
			       rawtransportchannel.cc \
			       constants.cc \
			       pseudotcp.cc

noinst_HEADERS =               candidate.h \
                               portallocator.h \
			       relayport.h \
		     	       session.h \
			       stunport.h \
			       tcpport.h \
			       port.h \
		       	       sessionid.h \
			       stunrequest.h \
			       udpport.h \
                               pseudotcp.h \
			       sessiondescription.h \
			       sessionmanager.h \
			       stun.h  \
			       relayserver.h \
			       stunserver.h \
			       sessionclient.h \
			       transport.h \
			       transportchannel.h \
			       transportchannelproxy.h \
			       transportchannelimpl.h \
			       p2ptransport.h \
			       p2ptransportchannel.h \
			       rawtransport.h \
			       rawtransportchannel.h \
			       constants.h \
			       common.h

AM_CPPFLAGS = -DPOSIX -DENABLE_DEBUG -D_DEBUG -g

P2PLIBS = libcricketp2pbase.la ../../base/libcricketbase.la -lpthread
XMLLIBS = ../../xmllite/libcricketxmllite.la ../../xmpp/libcricketxmpp.la $(EXPAT_LIBS)
TESTLIBS = ../../base/libcrickettest.la

bin_PROGRAMS = relayserver stunserver
noinst_PROGRAMS = stunserver_unittest session_unittest port_unittest

relayserver_SOURCES = relayserver.cc relayserver_main.cc
relayserver_LDADD = $(P2PLIBS)
stunserver_SOURCES = stunserver.cc stunserver_main.cc
stunserver_LDADD = $(P2PLIBS)
stunserver_unittest_SOURCES = stunserver_unittest.cc stunserver.cc
stunserver_unittest_LDADD = $(P2PLIBS) $(TESTLIBS)
session_unittest_SOURCES = session_unittest.cc stunserver.cc relayserver.cc
session_unittest_LDADD = $(TESTLIBS) $(P2PLIBS) $(XMLLIBS)
port_unittest_SOURCES = port_unittest.cc stunserver.cc relayserver.cc
port_unittest_LDADD = $(P2PLIBS) $(TESTLIBS)

noinst_LTLIBRARIES = libcricketp2pbase.la
