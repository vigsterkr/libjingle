# This file will be picked up by gyp to initialize some global settings.
# Those settings will override the webrtc setttings, which by default
# disable some of the components for chrome build, for example the audio/video
# capture and render.
{
  'variables': {
    'build_with_chromium': 1,
    'clang_use_chrome_plugins': 0,
    'enable_protobuf': 1,
    'include_internal_audio_device': 1,
    'include_internal_video_capture': 1,
    'include_internal_video_render': 1,
    'include_pulse_audio': 1,
  },
  'target_defaults': {
    'conditions': [
      ['OS=="mac" and clang==1', {
        'xcode_settings': {
          'WARNING_CFLAGS': [
            # TODO(ronghuawu): Remove once crbug/156530 is fixed.
            '-Wno-unknown-warning-option',
          ],
        },
      }],
    ],
  }, # target_defaults
}
