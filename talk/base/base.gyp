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
  'includes': ['../build/common.gypi'],
  'targets': [
    {
      'target_name': 'jingle_base',
      'type': 'static_library',
      'sources': [
        'asyncfile.cc',
        'asynchttprequest.cc',
        'asyncsocket.cc',
        'asynctcpsocket.cc',
        'asyncudpsocket.cc',
        'autodetectproxy.cc',
        'bandwidthsmoother.cc',
        'base64.cc',
        'basicpacketsocketfactory.cc',
        'bytebuffer.cc',
        'checks.cc',
        'common.cc',
        'cpumonitor.cc',
        'crc32.cc',
        'diskcache.cc',
        'event.cc',
        'filelock.cc',
        'fileutils.cc',
        'firewallsocketserver.cc',
        'flags.cc',
        'helpers.cc',
        'host.cc',
        'httpbase.cc',
        'httpclient.cc',
        'httpcommon.cc',
        'httprequest.cc',
        'httpserver.cc',
        'ipaddress.cc',
        'logging.cc',
        'md5.cc',
        'messagedigest.cc',
        'messagehandler.cc',
        'messagequeue.cc',
        'multipart.cc',
        'natserver.cc',
        'natsocketfactory.cc',
        'nattypes.cc',
        'nethelpers.cc',
        'network.cc',
        'optionsfile.cc',
        'pathutils.cc',
        'physicalsocketserver.cc',
        'proxydetect.cc',
        'proxyinfo.cc',
        'proxyserver.cc',
        'ratelimiter.cc',
        'ratetracker.cc',
        'sha1.cc',
        'sharedexclusivelock.cc',
        'signalthread.cc',
        'socketadapters.cc',
        'socketaddress.cc',
        'socketaddresspair.cc',
        'socketpool.cc',
        'socketstream.cc',
        'ssladapter.cc',
        'sslsocketfactory.cc',
        'sslidentity.cc',
        'sslstreamadapter.cc',
        'stream.cc',
        'stringencode.cc',
        'stringutils.cc',
        'systeminfo.cc',
        'task.cc',
        'taskparent.cc',
        'taskrunner.cc',
        'testclient.cc',
        'thread.cc',
        'timeutils.cc',
        'timing.cc',
        'transformadapter.cc',
        'urlencode.cc',
        'versionparsing.cc',
        'virtualsocketserver.cc',
        'worker.cc',
      ],
      'conditions': [
        ['OS=="linux"', {
          'sources': [
            "dbus.cc",
            "libdbusglibsymboltable.cc",
            'linux.cc',
            'linuxfdwalk.c',
            'linuxwindowpicker.cc',
          ],
          'link_settings': {
            'libraries': [
              '-lX11',
              '-lXcomposite',
              '-lXrender',
            ],
          },
        }],
        ['OS=="win"', {
          'sources': [
            'diskcache_win32.cc',
            'schanneladapter.cc',
            'win32.cc',
            'win32filesystem.cc',
            'win32regkey.cc',
            'win32securityerrors.cc',
            'win32socketinit.cc',
            'win32socketserver.cc',
            'win32window.cc',
            'win32windowpicker.cc',
            'winfirewall.cc',
            'winping.cc',
          ],
        }],
        ['os_posix==1', {
          'sources': [
            'latebindingsymboltable.cc',
            'openssladapter.cc',
            'openssldigest.cc',
            'opensslidentity.cc',
            'opensslstreamadapter.cc',
            'posix.cc',
            'unixfilesystem.cc',
          ],
        }],
      ],
    },  # target jingle_base
  ],
}
