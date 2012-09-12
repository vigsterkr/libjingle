#
# libjingle
# Copyright 2012, Google Inc.
#
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions are met:
#
#  1. Redistributions of source code must retain the above copyright notice, 
#     this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright notice,
#     this list of conditions and the following disclaimer in the documentation
#     and/or other materials provided with the distribution.
#  3. The name of the author may not be used to endorse or promote products 
#     derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
# EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

{
  'includes': ['build/common.gypi'],
  'targets': [
    {
      'target_name': 'libjingle',
      'type': 'static_library',
      'dependencies': [
        '<(DEPTH)/third_party/expat/expat.gyp:expat',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(DEPTH)/third_party/expat/files/lib',
        ],
      },
      'sources': [
        'base/asyncfile.cc',
        'base/asynchttprequest.cc',
        'base/asyncsocket.cc',
        'base/asynctcpsocket.cc',
        'base/asyncudpsocket.cc',
        'base/autodetectproxy.cc',
        'base/bandwidthsmoother.cc',
        'base/base64.cc',
        'base/basicpacketsocketfactory.cc',
        'base/bytebuffer.cc',
        'base/checks.cc',
        'base/common.cc',
        'base/cpumonitor.cc',
        'base/crc32.cc',
        'base/diskcache.cc',
        'base/event.cc',
        'base/filelock.cc',
        'base/fileutils.cc',
        'base/firewallsocketserver.cc',
        'base/flags.cc',
        'base/helpers.cc',
        'base/host.cc',
        'base/httpbase.cc',
        'base/httpclient.cc',
        'base/httpcommon.cc',
        'base/httprequest.cc',
        'base/httpserver.cc',
        'base/ipaddress.cc',
        'base/logging.cc',
        'base/md5.cc',
        'base/messagedigest.cc',
        'base/messagehandler.cc',
        'base/messagequeue.cc',
        'base/multipart.cc',
        'base/natserver.cc',
        'base/natsocketfactory.cc',
        'base/nattypes.cc',
        'base/nethelpers.cc',
        'base/network.cc',
        'base/optionsfile.cc',
        'base/pathutils.cc',
        'base/physicalsocketserver.cc',
        'base/proxydetect.cc',
        'base/proxyinfo.cc',
        'base/proxyserver.cc',
        'base/ratelimiter.cc',
        'base/ratetracker.cc',
        'base/sha1.cc',
        'base/sharedexclusivelock.cc',
        'base/signalthread.cc',
        'base/socketadapters.cc',
        'base/socketaddress.cc',
        'base/socketaddresspair.cc',
        'base/socketpool.cc',
        'base/socketstream.cc',
        'base/ssladapter.cc',
        'base/sslsocketfactory.cc',
        'base/sslidentity.cc',
        'base/sslstreamadapter.cc',
        'base/stream.cc',
        'base/stringencode.cc',
        'base/stringutils.cc',
        'base/systeminfo.cc',
        'base/task.cc',
        'base/taskparent.cc',
        'base/taskrunner.cc',
        'base/testclient.cc',
        'base/thread.cc',
        'base/timeutils.cc',
        'base/timing.cc',
        'base/transformadapter.cc',
        'base/urlencode.cc',
        'base/versionparsing.cc',
        'base/virtualsocketserver.cc',
        'base/worker.cc',
        'xmllite/qname.cc',
        'xmllite/xmlbuilder.cc',
        'xmllite/xmlconstants.cc',
        'xmllite/xmlelement.cc',
        'xmllite/xmlnsstack.cc',
        'xmllite/xmlparser.cc',
        'xmllite/xmlprinter.cc',
        'xmpp/chatroommoduleimpl.cc',
        'xmpp/constants.cc',
        'xmpp/discoitemsquerytask.cc',
        'xmpp/hangoutpubsubclient.cc',
        'xmpp/iqtask.cc',
        'xmpp/jid.cc',
        'xmpp/moduleimpl.cc',
        'xmpp/mucroomconfigtask.cc',
        'xmpp/mucroomdiscoverytask.cc',
        'xmpp/mucroomlookuptask.cc',
        'xmpp/mucroomuniquehangoutidtask.cc',
        'xmpp/pingtask.cc',
        'xmpp/pubsubclient.cc',
        'xmpp/pubsub_task.cc',
        'xmpp/pubsubtasks.cc',
        'xmpp/receivetask.cc',
        'xmpp/rostermoduleimpl.cc',
        'xmpp/saslmechanism.cc',
        'xmpp/xmppclient.cc',
        'xmpp/xmppengineimpl.cc',
        'xmpp/xmppengineimpl_iq.cc',
        'xmpp/xmpplogintask.cc',
        'xmpp/xmppstanzaparser.cc',
        'xmpp/xmpptask.cc',
      ],
      'conditions': [
        ['OS=="linux"', {
          'sources': [
            'base/dbus.cc',
            'base/libdbusglibsymboltable.cc',
            'base/linux.cc',
            'base/linuxfdwalk.c',
            'base/linuxwindowpicker.cc',
          ],
          'link_settings': {
            'libraries': [
              '-lcrypto',
              '-ldl',
              '-lrt',
              '-lssl',
              '-lX11',
              '-lXcomposite',
              '-lXrender',
            ],
          },
        }],
        ['OS=="mac"', {
          'sources': [
            'base/macasyncsocket.cc',
            'base/maccocoasocketserver.mm',
            'base/maccocoathreadhelper.mm',
            'base/macconversion.cc',
            'base/macsocketserver.cc',
            'base/macutils.cc',
            'base/macwindowpicker.cc',
            'base/scoped_autorelease_pool.mm',
          ],
          'link_settings': {
            'libraries': [
              '-lcrypto',
              '-lssl',
            ],
            'xcode_settings': {
              'OTHER_LDFLAGS': [
                '-framework Carbon',
                '-framework Cocoa',
                '-framework IOKit',
                '-framework Security',
                '-framework SystemConfiguration',
              ],
            },
          },
        }],
        ['OS=="win"', {
          'sources': [
            'base/diskcache_win32.cc',
            'base/schanneladapter.cc',
            'base/win32.cc',
            'base/win32filesystem.cc',
            'base/win32regkey.cc',
            'base/win32securityerrors.cc',
            'base/win32socketinit.cc',
            'base/win32socketserver.cc',
            'base/win32window.cc',
            'base/win32windowpicker.cc',
            'base/winfirewall.cc',
            'base/winping.cc',
          ],
          'msvs_settings': {
            'VCLibrarianTool': {
              'AdditionalDependencies': [
                'crypt32.lib',
                'iphlpapi.lib',
                'secur32.lib',
              ],
            },
          },
        }],
        ['os_posix==1', {
          'sources': [
            'base/latebindingsymboltable.cc',
            'base/openssladapter.cc',
            'base/openssldigest.cc',
            'base/opensslidentity.cc',
            'base/opensslstreamadapter.cc',
            'base/posix.cc',
            'base/unixfilesystem.cc',
          ],
        }],
      ],  # conditions
    },  # target libjingle
    {
      'target_name': 'libjingle_sound',
      'type': 'static_library',
      'dependencies': [
        'libjingle',
      ],
      'sources': [
        'sound/nullsoundsystem.cc',
        'sound/nullsoundsystemfactory.cc',
        'sound/platformsoundsystem.cc',
        'sound/platformsoundsystemfactory.cc',
        'sound/soundsysteminterface.cc',
        'sound/soundsystemproxy.cc',
      ],
      'conditions': [
        ['OS=="linux"', {
          'sources': [
            'sound/alsasoundsystem.cc',
            'sound/alsasymboltable.cc',
            'sound/linuxsoundsystem.cc',
            'sound/pulseaudiosoundsystem.cc',
            'sound/pulseaudiosymboltable.cc',
          ],
        }],
      ],
    },  # target libjingle_sound    
    {
      'target_name': 'libjingle_media',
      'type': 'static_library',
      'dependencies': [
        'libjingle',
        'libjingle_sound',
      ],
      'sources': [
        'media/base/codec.cc',
        'media/base/constants.cc',
        'media/base/cpuid.cc',
        'media/base/filemediaengine.cc',
        'media/base/hybridvideoengine.cc',
        'media/base/mediaengine.cc',
        'media/base/rtpdataengine.cc',
        'media/base/rtpdump.cc',
        'media/base/rtputils.cc',
        'media/base/streamparams.cc',
        'media/base/videoadapter.cc',
        'media/base/videocapturer.cc',
        'media/base/videocommon.cc',
        'media/base/videoframe.cc',
        'media/devices/devicemanager.cc',
        'media/devices/filevideocapturer.cc',
      ],
      'conditions': [
        ['OS=="linux"', {
          'sources': [
            'media/devices/gtkvideorenderer.cc',
            'media/devices/libudevsymboltable.cc',
            'media/devices/linuxdevicemanager.cc',
            'media/devices/v4llookup.cc',
          ],
          'include_dirs': [
            'third_party/libudev'
          ],
          'cflags': [
            '<!@(pkg-config --cflags gtk+-2.0)',
          ],
        }],
        ['OS=="win"', {
          'sources': [
            'media/devices/gdivideorenderer.cc',
            'media/devices/win32devicemanager.cc',
          ],
          'msvs_settings': {
            'VCLibrarianTool': {
              'AdditionalDependencies': [
                'd3d9.lib',
                'gdi32.lib',
                'strmiids.lib',
                'winmm.lib',
              ],
            },
          },
        }],
        ['OS=="mac"', {
          'sources': [
            'media/devices/carbonvideorenderer.cc',
            'media/devices/macdevicemanager.cc',
            'media/devices/macdevicemanagermm.mm',
          ],
          'xcode_settings': {
            'WARNING_CFLAGS': [
              # TODO(ronghuawu): Update macdevicemanager.cc to stop using
              # deprecated functions and remove this flag.
              '-Wno-deprecated-declarations',
            ],
          },
          'link_settings': {
            'xcode_settings': {
              'OTHER_LDFLAGS': [
                '-framework CoreAudio',
                '-framework QTKit',
              ],
            },
          },
        }],
      ],
    },  # target libjingle_media     
    {
      'target_name': 'libjingle_p2p',
      'type': 'static_library',
      'dependencies': [
        '<(DEPTH)/third_party/libsrtp/libsrtp.gyp:libsrtp',
        'libjingle',
        'libjingle_media',
      ],
      'include_dirs': [
        '<(DEPTH)/third_party/gtest/include',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(DEPTH)/third_party/gtest/include',
        ],
      },
      'sources': [
        'p2p/base/constants.cc',
        'p2p/base/dtlstransportchannel.cc',
        'p2p/base/p2ptransport.cc',
        'p2p/base/p2ptransportchannel.cc',
        'p2p/base/parsing.cc',
        'p2p/base/port.cc',
        'p2p/base/portallocator.cc',
        'p2p/base/portallocatorsessionproxy.cc',
        'p2p/base/portproxy.cc',
        'p2p/base/pseudotcp.cc',
        'p2p/base/relayport.cc',
        'p2p/base/relayserver.cc',
        'p2p/base/rawtransport.cc',
        'p2p/base/rawtransportchannel.cc',
        'p2p/base/session.cc',
        'p2p/base/sessiondescription.cc',
        'p2p/base/sessionmanager.cc',
        'p2p/base/sessionmessages.cc',
        'p2p/base/stun.cc',
        'p2p/base/stunport.cc',
        'p2p/base/stunrequest.cc',
        'p2p/base/stunserver.cc',
        'p2p/base/tcpport.cc',
        'p2p/base/transport.cc',
        'p2p/base/transportchannel.cc',
        'p2p/base/transportchannelproxy.cc',
        'p2p/base/udpport.cc',
        'p2p/client/basicportallocator.cc',
        'p2p/client/connectivitychecker.cc',
        'p2p/client/httpportallocator.cc',
        'p2p/client/socketmonitor.cc',
        'session/tunnel/pseudotcpchannel.cc',
        'session/tunnel/tunnelsessionclient.cc',
        'session/tunnel/securetunnelsessionclient.cc',
        'session/media/audiomonitor.cc',
        'session/media/call.cc',
        'session/media/channel.cc',
        'session/media/channelmanager.cc',
        'session/media/currentspeakermonitor.cc',
        'session/media/mediamessages.cc',
        'session/media/mediamonitor.cc',
        'session/media/mediarecorder.cc',
        'session/media/mediasession.cc',
        'session/media/mediasessionclient.cc',
        'session/media/rtcpmuxfilter.cc',
        'session/media/rtcpmuxfilter.cc',
        'session/media/soundclip.cc',
        'session/media/srtpfilter.cc',
        'session/media/ssrcmuxfilter.cc',
        'session/media/typingmonitor.cc',
      ],
    },  # target libjingle_p2p
  ],
}
