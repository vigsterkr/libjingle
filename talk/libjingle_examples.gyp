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
  'includes': [
    'build/common.gypi',
  ],
  'targets': [
    {
      'target_name': 'libjingle_xmpphelp',
      'type': 'static_library',
      'dependencies': [
        '<(DEPTH)/third_party/expat/expat.gyp:expat',
        'libjingle.gyp:libjingle',
        'libjingle.gyp:libjingle_p2p',
      ],
      'sources': [
        'xmpp/jingleinfotask.cc',
      ],
    },  # target libjingle_xmpphelp
    {
      'target_name': 'relayserver',
      'type': 'executable',
      'dependencies': [
        'libjingle.gyp:libjingle',
        'libjingle.gyp:libjingle_p2p',
      ],
      'sources': [
        'p2p/base/relayserver_main.cc',
      ],
    },  # target relayserver
    {
      'target_name': 'stunserver',
      'type': 'executable',
      'dependencies': [
        'libjingle.gyp:libjingle',
        'libjingle.gyp:libjingle_p2p',
      ],
      'sources': [
        'p2p/base/stunserver_main.cc',
      ],
    },  # target stunserver
    {
      'target_name': 'login',
      'type': 'executable',
      'dependencies': [
        'libjingle_xmpphelp',
      ],
      'sources': [
        'examples/login/login_main.cc',
      ],
    },  # target login
    {
      'target_name': 'peerconnection_server',
      'type': 'executable',
      'sources': [
        'examples/peerconnection/server/data_socket.cc',
        'examples/peerconnection/server/data_socket.h',
        'examples/peerconnection/server/main.cc',
        'examples/peerconnection/server/peer_channel.cc',
        'examples/peerconnection/server/peer_channel.h',
        'examples/peerconnection/server/utils.cc',
        'examples/peerconnection/server/utils.h',
      ],
      'dependencies': [
        'libjingle.gyp:libjingle',
      ],
      # TODO(ronghuawu): crbug.com/167187 fix size_t to int truncations.
      'msvs_disabled_warnings': [ 4309, ],
    }, # target peerconnection_server
  ],
  'conditions': [
    ['OS!="android"', {
      'targets': [
        {
          'target_name': 'call',
          'type': 'executable',
          'dependencies': [
            'libjingle.gyp:libjingle_p2p',
            'libjingle_xmpphelp',
          ],
          'sources': [
            'examples/call/call_main.cc',
            'examples/call/callclient.cc',
            'examples/call/console.cc',
            'examples/call/friendinvitesendtask.cc',
            'examples/call/mediaenginefactory.cc',
            'examples/call/mucinviterecvtask.cc',
            'examples/call/mucinvitesendtask.cc',
            'examples/call/presencepushtask.cc',
          ],
          'conditions': [
            ['OS=="linux"', {
              'link_settings': {
                'libraries': [
                  '<!@(pkg-config --libs-only-l gtk+-2.0 gthread-2.0)',
                ],
              },
            }],
            ['OS=="win"', {
              'msvs_settings': {
                'VCLinkerTool': {
                  'AdditionalDependencies': [
                    'strmiids.lib',
                  ],
                },
              },
            }],
          ],  # conditions
        },  # target call
      ], # targets
    }],  # OS!="android"
    ['OS=="linux" or OS=="win"', {
      'targets': [
        {
          'target_name': 'peerconnection_client',
          'type': 'executable',
          'sources': [
            'examples/peerconnection/client/conductor.cc',
            'examples/peerconnection/client/conductor.h',
            'examples/peerconnection/client/defaults.cc',
            'examples/peerconnection/client/defaults.h',
            'examples/peerconnection/client/peer_connection_client.cc',
            'examples/peerconnection/client/peer_connection_client.h',
          ],
          'dependencies': [
            '<(DEPTH)/third_party/jsoncpp/jsoncpp.gyp:jsoncpp',
            'libjingle.gyp:libjingle_peerconnection',
          ],
          'conditions': [
            # TODO(ronghuawu): Move these files to a win/ directory then they
            # can be excluded automatically.
            ['OS=="win"', {
              'sources': [
                'examples/peerconnection/client/main.cc',
                'examples/peerconnection/client/main_wnd.cc',
                'examples/peerconnection/client/main_wnd.h',
              ],
              'msvs_settings': {
                'VCLinkerTool': {
                 'SubSystem': '2',  # Windows
                },
              },
            }],  # OS=="win"
            ['OS=="linux"', {
              'sources': [
                'examples/peerconnection/client/linux/main.cc',
                'examples/peerconnection/client/linux/main_wnd.cc',
                'examples/peerconnection/client/linux/main_wnd.h',
              ],
              'cflags': [
                '<!@(pkg-config --cflags gtk+-2.0)',
              ],
              'link_settings': {
                'ldflags': [
                  '<!@(pkg-config --libs-only-L --libs-only-other gtk+-2.0 gthread-2.0)',
                ],
                'libraries': [
                  '<!@(pkg-config --libs-only-l gtk+-2.0 gthread-2.0)',
                  '-lX11',
                  '-lXcomposite',
                  '-lXext',
                  '-lXrender',
                ],
              },
            }],  # OS=="linux"
          ],  # conditions
        },  # target peerconnection_client
      ], # targets
    }],  # OS=="linux" or OS=="win"
  ],
}
