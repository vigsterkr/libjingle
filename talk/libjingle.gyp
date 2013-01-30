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

  'conditions': [
    [ 'os_posix == 1 and OS != "mac" and OS != "ios"', {
      'conditions': [
        ['sysroot!=""', {
          'variables': {
            'pkg-config': '../../../build/linux/pkg-config-wrapper "<(sysroot)" "<(target_arch)"',
          },
        }, {
          'variables': {
            'pkg-config': 'pkg-config'
          },
        }],
      ],
    }],

    ['libjingle_java == 1', {
      'targets': [
        {
          'target_name': 'libjingle_peerconnection_so',
          'type': 'loadable_module',
          'dependencies': [
            'libjingle_peerconnection',
            '<(DEPTH)/third_party/icu/icu.gyp:icuuc',
          ],
          'sources': [
            'app/webrtc/java/jni/peerconnection_jni.cc'
          ],
          'conditions': [
            ['OS=="linux"', {
              'include_dirs': [
                '/usr/local/buildtools/java/jdk7-64/include',
                '/usr/local/buildtools/java/jdk7-64/include/linux',
              ],
              'link_settings': {
                'libraries': [
                  '<!@(pkg-config --libs-only-l gtk+-2.0 gthread-2.0)',
                ],
              },
            }],
          ],
        },
        {
          'target_name': 'libjingle_peerconnection_jar',
          'type': 'none',
          'actions': [
            {
              'variables': {
                'java_src_dir': 'app/webrtc/java/src',
                'java_files': [
                  'app/webrtc/java/src/org/webrtc/AudioSource.java',
                  'app/webrtc/java/src/org/webrtc/AudioTrack.java',
                  'app/webrtc/java/src/org/webrtc/IceCandidate.java',
                  'app/webrtc/java/src/org/webrtc/LocalMediaStream.java',
                  'app/webrtc/java/src/org/webrtc/MediaConstraints.java',
                  'app/webrtc/java/src/org/webrtc/MediaSource.java',
                  'app/webrtc/java/src/org/webrtc/MediaStream.java',
                  'app/webrtc/java/src/org/webrtc/MediaStreamTrack.java',
                  'app/webrtc/java/src/org/webrtc/PeerConnectionFactory.java',
                  'app/webrtc/java/src/org/webrtc/PeerConnection.java',
                  'app/webrtc/java/src/org/webrtc/SdpObserver.java',
                  'app/webrtc/java/src/org/webrtc/SessionDescription.java',
                  'app/webrtc/java/src/org/webrtc/VideoCapturer.java',
                  'app/webrtc/java/src/org/webrtc/VideoRenderer.java',
                  'app/webrtc/java/src/org/webrtc/VideoSource.java',
                  'app/webrtc/java/src/org/webrtc/VideoTrack.java',
                ],
              },
              'action_name': 'create_jar',
              'inputs': [
                'build/build_jar.sh',
                '<@(java_files)',
              ],
              'outputs': [
                '<(PRODUCT_DIR)/libjingle_peerconnection.jar',
              ],
              'action': [
                'build/build_jar.sh', '<@(_outputs)', '<(INTERMEDIATE_DIR)',
                '<(java_src_dir)',
                '<(PRODUCT_DIR)/libjingle_peerconnection_so.so',
                '', '<@(java_files)'
              ],
            },
          ],
          'dependencies': [
            'libjingle_peerconnection_so',
          ],
        },
      ],
    }],
  ],

  'targets': [
    {
      'target_name': 'libjingle',
      'type': 'static_library',
      'dependencies': [
        '<(DEPTH)/third_party/expat/expat.gyp:expat',
        '<(DEPTH)/third_party/jsoncpp/jsoncpp.gyp:jsoncpp',
      ],
      'export_dependent_settings': [
        '<(DEPTH)/third_party/expat/expat.gyp:expat',
        '<(DEPTH)/third_party/jsoncpp/jsoncpp.gyp:jsoncpp',
      ],
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
        'base/json.cc',
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
        'base/nssidentity.cc',
        'base/nssstreamadapter.cc',
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
        'base/sslstreamadapterhelper.cc',
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
        'xmpp/presenceouttask.cc',
        'xmpp/presencereceivetask.cc',
        'xmpp/presencestatus.cc',
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
        'xmpp/xmppauth.cc',
        'xmpp/xmpppump.cc',
        'xmpp/xmppsocket.cc',
        'xmpp/xmppthread.cc',
      ],
      'conditions': [
        ['OS=="mac" or OS=="win"', {
          'dependencies': [
            # The chromium copy of nss should NOT be used on platforms that
            # have NSS as system libraries, such as linux.
            '<(DEPTH)/third_party/nss/nss.gyp:nss',
          ],
        }],
        ['OS=="android"', {
          'sources': [
            'base/ifaddrs-android.cc',
          ],
          'link_settings': {
            'libraries': [
              '-llog',
              '-lGLESv2',
            ],
          },
        }],
        ['OS=="linux" or OS=="android"', {
          'sources': [
            'base/linux.cc',
          ],
        }],
        ['OS=="linux"', {
          'sources': [
            'base/dbus.cc',
            'base/libdbusglibsymboltable.cc',
            'base/linuxfdwalk.c',
            'base/linuxwindowpicker.cc',
          ],
          'link_settings': {
            'libraries': [
              '-lcrypto',
              '-ldl',
              '-lrt',
              '-lssl',
              '-lXext',
              '-lX11',
              '-lXcomposite',
              '-lXrender',
              '<!@(<(pkg-config) --libs-only-l nss | sed -e "s/-lssl3//")',
            ],
          },
          'cflags': [
            '<!@(<(pkg-config) --cflags nss)',
          ],
          'ldflags': [
            '<!@(<(pkg-config) --libs-only-L --libs-only-other nss)',
          ],
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
             '$(SDKROOT)/usr/lib/libcrypto.dylib',
             '$(SDKROOT)/usr/lib/libssl.dylib',
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
          # Suppress warnings about WIN32_LEAN_AND_MEAN.
          'msvs_disabled_warnings': [4005],
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
          'conditions': [
            ['OS=="android"', {
              'dependencies': [
                '<(DEPTH)/third_party/openssl/openssl.gyp:openssl',
              ],
            }],
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
        '<(DEPTH)/third_party/libyuv/libyuv.gyp:libyuv',
        '<(DEPTH)/third_party/webrtc/modules/modules.gyp:video_capture_module',
        '<(DEPTH)/third_party/webrtc/modules/modules.gyp:video_render_module',
        '<(DEPTH)/third_party/webrtc/video_engine/video_engine.gyp:video_engine_core',
        '<(DEPTH)/third_party/webrtc/voice_engine/voice_engine.gyp:voice_engine_core',
        '<(DEPTH)/third_party/webrtc/system_wrappers/source/system_wrappers.gyp:system_wrappers',
        'libjingle',
        'libjingle_sound',
      ],
      'sources': [
        'media/base/capturemanager.cc',
        'media/base/capturerenderadapter.cc',
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
        'media/webrtc/webrtcpassthroughrender.cc',
        'media/webrtc/webrtcpassthroughrender.h',
        'media/webrtc/webrtcvideocapturer.cc',
        'media/webrtc/webrtcvideocapturer.h',
        'media/webrtc/webrtcvideoengine.cc',
        'media/webrtc/webrtcvideoengine.h',
        'media/webrtc/webrtcvideoframe.cc',
        'media/webrtc/webrtcvideoframe.h',
        'media/webrtc/webrtcvie.h',
        'media/webrtc/webrtcvoe.h',
        'media/webrtc/webrtcvoiceengine.cc',
        'media/webrtc/webrtcvoiceengine.h',
        'media/webrtc/webrtccommon.h',
      ],
      'conditions': [
        ['OS=="linux"', {
          'sources': [
            'media/devices/gtkvideorenderer.cc',
            'media/devices/libudevsymboltable.cc',
            'media/devices/linuxdeviceinfo.cc',
            'media/devices/linuxdevicemanager.cc',
            'media/devices/v4llookup.cc',
          ],
          'include_dirs': [
            'third_party/libudev'
          ],
          'cflags': [
            '<!@(pkg-config --cflags gtk+-2.0)',
          ],
          'libraries': [
            '-lrt',
            '-lXext',
            '-lX11',
          ],
        }],
        ['OS=="win"', {
          'sources': [
            'media/devices/gdivideorenderer.cc',
            'media/devices/win32deviceinfo.cc',
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
            'media/devices/macdeviceinfo.cc',
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
                '-framework Cocoa',
                '-framework CoreAudio',
                '-framework CoreVideo',
                '-framework OpenGL',
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
        'p2p/base/transportdescriptionfactory.cc',
        'p2p/base/transportdescriptionfactory.h',
        'p2p/base/turnport.cc',
        'p2p/base/turnserver.cc',
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
    {
      'target_name': 'libjingle_peerconnection',
      'type': 'static_library',
      'dependencies': [
        'libjingle',
        'libjingle_media',
        'libjingle_p2p',
      ],
      'sources': [
        'app/webrtc/audiotrack.cc',
        'app/webrtc/datachannel.cc',
        'app/webrtc/dtmfsender.cc',
        'app/webrtc/jsepicecandidate.cc',
        'app/webrtc/jsepsessiondescription.cc',
        'app/webrtc/localaudiosource.cc',
        'app/webrtc/localvideosource.cc',
        'app/webrtc/mediastream.cc',
        'app/webrtc/mediastreamhandler.cc',
        'app/webrtc/mediastreamproxy.cc',
        'app/webrtc/mediastreamsignaling.cc',
        'app/webrtc/mediastreamtrackproxy.cc',
        'app/webrtc/peerconnection.cc',
        'app/webrtc/peerconnectionfactory.cc',
        'app/webrtc/peerconnectionproxy.cc',
        'app/webrtc/portallocatorfactory.cc',
        'app/webrtc/statscollector.cc',
        'app/webrtc/videosourceproxy.cc',
        'app/webrtc/videotrack.cc',
        'app/webrtc/videotrackrenderers.cc',
        'app/webrtc/webrtcsdp.cc',
        'app/webrtc/webrtcsession.cc',
      ],
    },  # target libjingle_peerconnection
  ],
}
