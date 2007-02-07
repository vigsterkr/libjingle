libcricketbase_la_SOURCES = socketaddress.cc \
                            time.cc \
                            asyncudpsocket.cc \
                            messagequeue.cc \
                            thread.cc \
                            physicalsocketserver.cc \
                            bytebuffer.cc \
                            asyncpacketsocket.cc \
                            network.cc \
                            asynctcpsocket.cc \
                            socketadapters.cc \
                            md5c.c \
                            base64.cc \
                            task.cc \
                            taskrunner.cc \
                            host.cc \
                            proxydetect.cc \
                            socketaddresspair.cc \
                            stringencode.cc \
                            stringdigest.cc \
                            stringutils.cc \
                            proxyinfo.cc \
                            common.cc \
                            logging.cc \
                            stream.cc \
                            ssladapter.cc \
                            openssladapter.cc \
                            helpers.cc \
                            asynchttprequest.cc \
                            firewallsocketserver.cc \
                            httpcommon.cc \
                            httpbase.cc \
                            httpclient.cc \
			    httpserver.cc \
                            socketpool.cc \
                            signalthread.cc \
                            autodetectproxy.cc \
                            urlencode.cc \
                            pathutils.cc \
			    fileutils.cc \
			    unixfilesystem.cc \
			    tarstream.cc \
			    streamutils.cc \
			    diskcache.cc \
			    diskcachestd.cc

libcrickettest_la_SOURCES = testclient.cc \
                            natserver.cc \
                            natsocketfactory.cc \
                            nattypes.cc \
                            virtualsocketserver.cc

noinst_HEADERS =            asyncfile.h \
                            common.h \
			    convert.h \
                            asyncpacketsocket.h \
                            socketfactory.h \
                            asyncsocket.h \
                            socket.h \
                            asynctcpsocket.h \
                            linked_ptr.h \
                            asyncudpsocket.h \
                            logging.h \
                            socketserver.h \
                            base64.h \
                            md5.h \
                            stl_decl.h \
                            basicdefs.h \
                            messagequeue.h \
                            basictypes.h \
                            stringdigest.h \
                            stringencode.h \
                            stringutils.h \
                            bytebuffer.h \
                            task.h \
                            byteorder.h \
                            taskrunner.h \
                            criticalsection.h \
                            network.h \
                            thread.h \
                            time.h \
                            physicalsocketserver.h \
                            proxyinfo.h \
                            host.h \
                            scoped_ptr.h \
                            sigslot.h \
                            winping.h \
                            socketadapters.h \
                            socketaddress.h \
                            host.h \
                            socketaddresspair.h \
                            Equifax_Secure_Global_eBusiness_CA-1.h \
                            stream.h \
                            ssladapter.h \
                            openssladapter.h \
			    cryptstring.h \
			    httpbase.h \
			    httpclient.h \
                            httpcommon.h \
			    httpserver.h \
			    httpcommon-inl.h \
			    proxydetect.h \
			    helpers.h \
			    socketpool.h \
			    asynchttprequest.h \
			    signalthread.h \
			    firewallsocketserver.h \
			    diskcache.h \
     			    pathutils.h \
                            socketstream.h \
			    autodetectproxy.h \
			    urlencode.h \
			    fileutils.h \
			    unixfilesystem.h \
			    win32filesystem.h \
			    tarstream.h \
			    streamutils.h \
			    diskcachestd.h \
			    testclient.h \
			    natserver.h \
			    nattypes.h \
			    natsocketfactory.h \
			    virtualsocketserver.h \
			    event.h

AM_CXXFLAGS = -DPOSIX
DEFAULT_INCLUDES = -I$(top_srcdir) 

noinst_LTLIBRARIES = libcricketbase.la libcrickettest.la
noinst_PROGRAMS = natserver nat_unittest virtualsocket_unittest

natserver_SOURCES = natserver_main.cc
natserver_LDADD = libcrickettest.la libcricketbase.la -lpthread
nat_unittest_SOURCES = nat_unittest.cc
nat_unittest_LDADD = libcrickettest.la libcricketbase.la -lpthread
virtualsocket_unittest_SOURCES = virtualsocket_unittest.cc
virtualsocket_unittest_LDADD = libcrickettest.la libcricketbase.la -lpthread

EXTRA_DIST =  	diskcache_win32.h \
		diskcache_win32.cc \
		win32.h \
                winping.h \
                winping.cc \
                win32filesystem.cc \
		win32socketserver.cc \
                win32socketserver.h \
		win32window.h \
                winfirewall.h \
                winfirewall.cc \
                schanneladapter.h \
                schanneladapter.cc \
                sec_buffer.h
