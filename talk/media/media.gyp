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
      'target_name': 'jingle_media',
      'type': 'static_library',
      'sources': [
        'base/codec.cc',
        'base/constants.cc',
        'base/cpuid.cc',
        'base/filemediaengine.cc',
        'base/hybridvideoengine.cc',
        'base/mediaengine.cc',
        'base/rtpdataengine.cc',
        'base/rtpdump.cc',
        'base/rtputils.cc',
        'base/streamparams.cc',
        'base/videoadapter.cc',
        'base/videocapturer.cc',
        'base/videocommon.cc',
        'base/videoframe.cc',
        'devices/devicemanager.cc',
        'devices/dummydevicemanager.cc',
        'devices/filevideocapturer.cc',
      ],
      'conditions': [
        ['OS=="linux"', {
          'include_dirs': [
            '../third_party/libudev',
          ],
          'sources': [
            'devices/gtkvideorenderer.cc',
            'devices/libudevsymboltable.cc',
            'devices/linuxdevicemanager.cc',
            'devices/v4llookup.cc',
          ],
          'cflags': [
            '<!@(pkg-config --cflags gtk+-2.0)',
          ],
        }],
        ['OS=="win"', {
          'sources': [
            'devices/gdivideorenderer.cc',
            'devices/win32devicemanager.cc',
          ],
        }],
        ['OS=="mac"', {
          'sources': [
            'devices/carbonvideorenderer.cc',
            'devices/macdevicemanager.cc',
            'devices/macdevicemanagermm.mm',
          ],
        }],
      ],
    },  # target jingle_media
  ],
}
