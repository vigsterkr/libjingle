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
      # TODO(ronghuawu): Use gtest.gyp from chromium.
      'target_name': 'gunit',
      'type': 'static_library',
      'sources': [
        '<(DEPTH)/third_party/gtest/src/gtest-all.cc',
      ],
      'include_dirs': [
        '<(DEPTH)/third_party/gtest/include',
        '<(DEPTH)/third_party/gtest',
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(DEPTH)/third_party/gtest/include',
        ],
      },
      'conditions': [
        ['OS=="android"', {
          'include_dirs': [
            '<(android_ndk_include)',
          ]
        }],
      ],
    },  # target gunit
    {
      'target_name': 'libjingle_unittest_main',
      'type': 'static_library',
      'dependencies': [
        'gunit',
      ],
      'sources': [
        'base/unittest_main.cc',
      ],
    },  # target libjingle_unittest_main
    {
      'target_name': 'libjingle_unittest',
      'type': 'executable',
      'dependencies': [
        'gunit',
        'libjingle.gyp:libjingle',
        'libjingle_unittest_main',
      ],
      'sources': [
        'base/asynchttprequest_unittest.cc',
        'base/atomicops_unittest.cc',
        'base/autodetectproxy_unittest.cc',
        'base/bandwidthsmoother_unittest.cc',
        'base/base64_unittest.cc',
        'base/basictypes_unittest.cc',
        'base/buffer_unittest.cc',
        'base/bytebuffer_unittest.cc',
        'base/byteorder_unittest.cc',
        'base/cpumonitor_unittest.cc',
        'base/crc32_unittest.cc',
        'base/event_unittest.cc',
        'base/filelock_unittest.cc',
        'base/fileutils_unittest.cc',
        'base/helpers_unittest.cc',
        'base/host_unittest.cc',
        'base/httpbase_unittest.cc',
        'base/httpcommon_unittest.cc',
        'base/httpserver_unittest.cc',
        'base/ipaddress_unittest.cc',
        'base/logging_unittest.cc',
        'base/md5digest_unittest.cc',
        'base/messagedigest_unittest.cc',
        'base/messagequeue_unittest.cc',
        'base/multipart_unittest.cc',
        'base/nat_unittest.cc',
        'base/network_unittest.cc',
        'base/nullsocketserver_unittest.cc',
        'base/optionsfile_unittest.cc',
        'base/pathutils_unittest.cc',
        'base/physicalsocketserver_unittest.cc',
        'base/proxy_unittest.cc',
        'base/proxydetect_unittest.cc',
        'base/ratelimiter_unittest.cc',
        'base/ratetracker_unittest.cc',
        'base/referencecountedsingletonfactory_unittest.cc',
        'base/rollingaccumulator_unittest.cc',
        'base/sha1digest_unittest.cc',
        'base/sharedexclusivelock_unittest.cc',
        'base/signalthread_unittest.cc',
        'base/sigslot_unittest.cc',
        'base/socket_unittest.cc',
        'base/socketaddress_unittest.cc',
        'base/stream_unittest.cc',
        'base/stringencode_unittest.cc',
        'base/stringutils_unittest.cc',
        # TODO(ronghuawu): Reenable this test.
        # 'base/systeminfo_unittest.cc',
        'base/task_unittest.cc',
        'base/testclient_unittest.cc',
        'base/thread_unittest.cc',
        'base/timeutils_unittest.cc',
        'base/urlencode_unittest.cc',
        'base/versionparsing_unittest.cc',
        'base/virtualsocket_unittest.cc',
        # TODO(ronghuawu): Reenable this test.
        # 'base/windowpicker_unittest.cc',
        'xmllite/qname_unittest.cc',
        'xmllite/xmlbuilder_unittest.cc',
        'xmllite/xmlelement_unittest.cc',
        'xmllite/xmlnsstack_unittest.cc',
        'xmllite/xmlparser_unittest.cc',
        'xmllite/xmlprinter_unittest.cc',
        'xmpp/hangoutpubsubclient_unittest.cc',
        'xmpp/jid_unittest.cc',
        'xmpp/mucroomconfigtask_unittest.cc',
        'xmpp/mucroomdiscoverytask_unittest.cc',
        'xmpp/mucroomlookuptask_unittest.cc',
        'xmpp/mucroomuniquehangoutidtask_unittest.cc',
        'xmpp/pingtask_unittest.cc',
        'xmpp/pubsubclient_unittest.cc',
        'xmpp/pubsubtasks_unittest.cc',
        'xmpp/util_unittest.cc',
        'xmpp/xmppengine_unittest.cc',
        'xmpp/xmpplogintask_unittest.cc',
        'xmpp/xmppstanzaparser_unittest.cc',
      ],  # sources
      'conditions': [
        ['OS=="linux"', {
          'sources': [
            'base/latebindingsymboltable_unittest.cc',
            # TODO(ronghuawu): Reenable this test.
            # 'base/linux_unittest.cc',
            'base/linuxfdwalk_unittest.cc',
          ],
        }],
        ['OS=="win"', {
          'sources': [
            'base/win32_unittest.cc',
            'base/win32regkey_unittest.cc',
            'base/win32socketserver_unittest.cc',
            'base/win32toolhelp_unittest.cc',
            'base/win32window_unittest.cc',
            # TODO(ronghuawu): Reenable this test.
            # 'base/win32windowpicker_unittest.cc',
            'base/winfirewall_unittest.cc',
          ],
        }],
        ['OS=="mac"', {
          'sources': [
            'base/macsocketserver_unittest.cc',
            'base/macutils_unittest.cc',
            'base/macwindowpicker_unittest.cc',
          ],
        }],
        ['os_posix==1', {
          'sources': [
            'base/sslidentity_unittest.cc',
            'base/sslstreamadapter_unittest.cc',
          ],
        }],
      ],  # conditions
    },  # target libjingle_unittest
    {
      'target_name': 'libjingle_sound_unittest',
      'type': 'executable',
      'dependencies': [
        'gunit',
        'libjingle.gyp:libjingle_sound',
        'libjingle_unittest_main',
      ],
      'sources': [
        'sound/automaticallychosensoundsystem_unittest.cc',
      ],
    },  # target libjingle_sound_unittest
    {
      'target_name': 'libjingle_media_unittest',
      'type': 'executable',
      'dependencies': [
        'gunit',
        'libjingle.gyp:libjingle_media',
        'libjingle_unittest_main',
      ],
      # TODO(ronghuawu): Avoid the copies.
      # https://code.google.com/p/libjingle/issues/detail?id=398
      'copies': [
        {
          'destination': '<(DEPTH)/../talk/media/testdata',
          'files': [
            'media/testdata/1.frame_plus_1.byte',
            'media/testdata/captured-320x240-2s-48.frames',
            'media/testdata/h264-svc-99-640x360.rtpdump',
            'media/testdata/video.rtpdump',
            'media/testdata/voice.rtpdump',
          ],
        },
      ],
      'sources': [
        # TODO(ronghuawu): Reenable this test.
        # 'media/base/capturemanager_unittest.cc',
        'media/base/codec_unittest.cc',
        'media/base/filemediaengine_unittest.cc',
        'media/base/rtpdataengine_unittest.cc',
        'media/base/rtpdump_unittest.cc',
        'media/base/rtputils_unittest.cc',
        'media/base/testutils.cc',
        'media/base/videocapturer_unittest.cc',
        'media/base/videocommon_unittest.cc',
        # TODO(ronghuawu): Reenable this test.
        # 'media/devices/devicemanager_unittest.cc',
        'media/devices/dummydevicemanager_unittest.cc',
        'media/devices/filevideocapturer_unittest.cc',
      ],
      'conditions': [
        ['OS=="win"', {
          'msvs_settings': {
            'VCLinkerTool': {
              'AdditionalDependencies': [
                # TODO(ronghuawu): Since we've included strmiids in
                # libjingle_media target, we shouldn't need this here.
                # Find out why it doesn't work without this.
                'strmiids.lib',
              ],
            },
          },
        }],
      ],
    },  # target libjingle_media_unittest
    {
      'target_name': 'libjingle_p2p_unittest',
      'type': 'executable',
      'dependencies': [
        '<(DEPTH)/third_party/libsrtp/libsrtp.gyp:libsrtp',
        'gunit',
        'libjingle.gyp:libjingle',
        'libjingle.gyp:libjingle_p2p',
        'libjingle_unittest_main',
      ],
      'include_dirs': [
        '<(DEPTH)/third_party/libsrtp/srtp',
      ],
      'sources': [
        # TODO(ronghuawu): testutils.cc should be moved to some common place.
        'media/base/testutils.cc',
        'p2p/base/dtlstransportchannel_unittest.cc',
        'p2p/base/p2ptransportchannel_unittest.cc',
        'p2p/base/port_unittest.cc',
        'p2p/base/portallocatorsessionproxy_unittest.cc',
        'p2p/base/pseudotcp_unittest.cc',
        'p2p/base/relayport_unittest.cc',
        'p2p/base/relayserver_unittest.cc',
        'p2p/base/session_unittest.cc',
        'p2p/base/stun_unittest.cc',
        'p2p/base/stunport_unittest.cc',
        'p2p/base/stunrequest_unittest.cc',
        'p2p/base/stunserver_unittest.cc',
        'p2p/base/transport_unittest.cc',
        'p2p/base/transportdescriptionfactory_unittest.cc',
        'p2p/client/connectivitychecker_unittest.cc',
        'p2p/client/portallocator_unittest.cc',
        'session/media/channel_unittest.cc',
        'session/media/channelmanager_unittest.cc',
        'session/media/currentspeakermonitor_unittest.cc',
        'session/media/mediarecorder_unittest.cc',
        'session/media/mediamessages_unittest.cc',
        'session/media/mediasession_unittest.cc',
        'session/media/mediasessionclient_unittest.cc',
        'session/media/rtcpmuxfilter_unittest.cc',
        'session/media/srtpfilter_unittest.cc',
        'session/media/ssrcmuxfilter_unittest.cc',
      ],
      'conditions': [
        ['OS=="win"', {
          'msvs_settings': {
            'VCLinkerTool': {
              'AdditionalDependencies': [
                'strmiids.lib',
              ],
            },
          },
        }],
      ],
    },  # target libjingle_p2p_unittest
    {
      'target_name': 'libjingle_peerconnection_unittest',
      'type': 'executable',
      'dependencies': [
        'gunit',
        'libjingle.gyp:libjingle',
        'libjingle.gyp:libjingle_p2p',
        'libjingle.gyp:libjingle_peerconnection',
        'libjingle_unittest_main',
      ],
      # TODO(ronghuawu): Reenable below unit tests that require gmock.
      'sources': [
        'app/webrtc/dtmfsender_unittest.cc',
        'app/webrtc/jsepsessiondescription_unittest.cc',
        'app/webrtc/localaudiosource_unittest.cc',
        'app/webrtc/localvideosource_unittest.cc',
        # 'app/webrtc/mediastream_unittest.cc',
        # 'app/webrtc/mediastreamhandler_unittest.cc',
        'app/webrtc/mediastreamsignaling_unittest.cc',
        'app/webrtc/peerconnection_unittest.cc',
        'app/webrtc/peerconnectionfactory_unittest.cc',
        'app/webrtc/peerconnectioninterface_unittest.cc',
        # 'app/webrtc/peerconnectionproxy_unittest.cc',
        # 'app/webrtc/statscollector_unittest.cc',
        'app/webrtc/test/fakeaudiocapturemodule.cc',
        'app/webrtc/test/fakeaudiocapturemodule_unittest.cc',
        'app/webrtc/videotrack_unittest.cc',
        'app/webrtc/webrtcsdp_unittest.cc',
        'app/webrtc/webrtcsession_unittest.cc',
      ],
    },  # target libjingle_peerconnection_unittest
  ],
  'conditions': [
    ['libjingle_java == 1', {
      'targets': [
        {
          'target_name': 'libjingle_peerconnection_test_jar',
          'type': 'none',
          'actions': [
            {
              'variables': {
                'java_src_dir': 'app/webrtc/javatests/src',
                'java_files': [
                  'app/webrtc/javatests/src/org/webrtc/PeerConnectionTest.java',
                ],
              },
              'action_name': 'create_jar',
              'inputs': [
                'build/build_jar.sh',
                '<@(java_files)',
                '<(PRODUCT_DIR)/libjingle_peerconnection.jar',
                '<(DEPTH)/third_party/junit/junit-4.11.jar',
              ],
              'outputs': [
                '<(PRODUCT_DIR)/libjingle_peerconnection_test.jar',
              ],
              'action': [
                'build/build_jar.sh', '<@(_outputs)', '<(INTERMEDIATE_DIR)',
                '<(java_src_dir)', '',
                '<(PRODUCT_DIR)/libjingle_peerconnection.jar:<(DEPTH)/third_party/junit/junit-4.11.jar',
                '<@(java_files)'
              ],
            },
          ],
        },
      ],
    }],
  ],
}
