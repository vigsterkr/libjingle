use_relative_paths = True

vars = {
  # Override root_dir in your .gclient's custom_vars to specify a custom root
  # folder name.
  "root_dir": "trunk",
  "extra_gyp_flag": "-Dextra_gyp_flag=0",

  "googlecode_url": "http://%s.googlecode.com/svn",
  "chromium_trunk" : "http://src.chromium.org/svn/trunk",
  "chromium_git": "https://chromium.googlesource.com",

  "chromium_revision": "172329",
  "webrtc_revision": "3255",
}

# NOTE: Prefer revision numbers to tags for svn deps. Use http rather than
# https; the latter can cause problems for users behind proxies.
deps = {
  "../chromium_deps":
    File(Var("chromium_trunk") + "/src/DEPS@" + Var("chromium_revision")),

  "build":
    Var("chromium_trunk") + "/src/build@" + Var("chromium_revision"),

  # Needed by common.gypi.
  "google_apis/build":
    Var("chromium_trunk") + "/src/google_apis/build@" + Var("chromium_revision"),

  "net/third_party/nss":
    Var("chromium_trunk") + "/src/net/third_party/nss@" + Var("chromium_revision"),

  "third_party/expat":
    Var("chromium_trunk") + "/src/third_party/expat@" + Var("chromium_revision"),

  "third_party/gtest":
    From("chromium_deps", "src/testing/gtest"),

  "third_party/icu/":
    From("chromium_deps", "src/third_party/icu"),

  "third_party/libjpeg":
    Var("chromium_trunk") + "/src/third_party/libjpeg@" + Var("chromium_revision"),

  "third_party/libjpeg_turbo/":
    From("chromium_deps", "src/third_party/libjpeg_turbo"),

  "third_party/libsrtp/":
    From("chromium_deps", "src/third_party/libsrtp"),

  "third_party/libvpx":
    From("chromium_deps", "src/third_party/libvpx"),

  "third_party/libyuv/":
    From("chromium_deps", "src/third_party/libyuv"),

  "third_party/opus":
    Var("chromium_trunk") + "/src/third_party/opus@163910",

  "third_party/opus/src":
    Var("chromium_trunk") + "/deps/third_party/opus@162558",

  "third_party/protobuf":
    Var("chromium_trunk") + "/src/third_party/protobuf@" + Var("chromium_revision"),

  "third_party/sqlite/":
    Var("chromium_trunk") + "/src/third_party/sqlite@" + Var("chromium_revision"),
 
  "third_party/yasm":
    Var("chromium_trunk") + "/src/third_party/yasm@" + Var("chromium_revision"),

  "third_party/yasm/source/patched-yasm":
    From("chromium_deps", "src/third_party/yasm/source/patched-yasm"),
 
  "third_party/webrtc":
    (Var("googlecode_url") % "webrtc") + "/stable/webrtc@" + Var("webrtc_revision"),

  "third_party/zlib":
    Var("chromium_trunk") + "/src/third_party/zlib@" + Var("chromium_revision"),

  "tools/clang":
    Var("chromium_trunk") + "/src/tools/clang@" + Var("chromium_revision"),

  "tools/gyp":
    From("chromium_deps", "src/tools/gyp"),

  "tools/python":
    Var("chromium_trunk") + "/src/tools/python@" + Var("chromium_revision"),

  "tools/valgrind":
    Var("chromium_trunk") + "/src/tools/valgrind@" + Var("chromium_revision"),

  # Needed by build/common.gypi.
  "tools/win/supalink":
    Var("chromium_trunk") + "/src/tools/win/supalink@" + Var("chromium_revision"),
}

deps_os = {
  "win": {
    # Use our own, stripped down, version of Cygwin (required by GYP).
    "third_party/cygwin":
      (Var("googlecode_url") % "webrtc") + "/deps/third_party/cygwin@2672",

    "third_party/winsdk_samples":
      (Var("googlecode_url") % "webrtc") + "/stable/third_party/winsdk_samples@" + Var("webrtc_revision"),

    "third_party/winsdk_samples/src":
      (Var("googlecode_url") % "webrtc") + "/deps/third_party/winsdk_samples_v71@3145",

    # Used by libjpeg-turbo.
    "third_party/yasm/binaries":
      From("chromium_deps", "src/third_party/yasm/binaries"),

    # NSS, for SSLClientSocketNSS.
    "third_party/nss":
      From("chromium_deps", "src/third_party/nss"),
  },

  "mac": {
    # NSS, for SSLClientSocketNSS.
    "third_party/nss":
      From("chromium_deps", "src/third_party/nss"),
  },

  "unix": {
    "third_party/gold":
      From("chromium_deps", "src/third_party/gold"),
  },

  "android": {
    "third_party/android_tools":
      From("chromium_deps", "src/third_party/android_tools"),

    "third_party/openssl":
      From("chromium_deps", "src/third_party/openssl"),
  }
}

hooks = [
  {
    # Pull clang on mac. If nothing changed, or on non-mac platforms, this takes
    # zero seconds to run. If something changed, it downloads a prebuilt clang.
    "pattern": ".",
    "action": ["python", Var("root_dir") + "/tools/clang/scripts/update.py",
               "--mac-only"],
  },
  {
    # Update the cygwin mount on Windows.
    # This is necessary to get the correct mapping between e.g. /bin and the
    # cygwin path on Windows. Without it we can't run bash scripts in actions.
    # Ideally this should be solved in "pylib/gyp/msvs_emulation.py".
    "pattern": ".",
    "action": ["python", Var("root_dir") + "/build/win/setup_cygwin_mount.py",
               "--win-only"],
  },
  {
    # A change to a .gyp, .gypi, or to GYP itself should run the generator.
    "pattern": ".",
    "action": ["python", Var("root_dir") + "/build/gyp_chromium",
               "--depth=" + Var("root_dir"), Var("root_dir") +
               "/talk/libjingle_all.gyp", Var("extra_gyp_flag")],
  },
]

