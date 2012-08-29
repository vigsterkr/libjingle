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

# This file contains common settings for building libjingle components.

{
  'target_defaults': {
    'include_dirs': [
      '../..',
    ],
    'defines': [
      'EXPAT_RELATIVE_PATH',
      'FEATURE_ENABLE_VOICEMAIL',
      'GTEST_RELATIVE_PATH',
      'LOGGING=1',
      'SRTP_RELATIVE_PATH',

      # Feature selection
      'FEATURE_ENABLE_SSL',
      'FEATURE_ENABLE_VOICEMAIL',
      'FEATURE_ENABLE_PSTN',
      'HAVE_SRTP',
    ],
    'conditions': [
      ['OS=="linux"', {
        'defines': [
          'LINUX',
        ],
      }],
      ['OS=="mac"', {
        'defines': [
          'OSX',
        ],
      }],
      ['os_posix==1', {
        'defines': [
          'POSIX',
        ],
      }],
    ],
  }, # target_defaults
}
