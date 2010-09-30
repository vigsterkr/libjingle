# Copyright 2010 Google Inc.
# All Rights Reserved.
#
# Author: Tim Haloun (thaloun@google.com)
#         Daniel Petersson (dape@google.com)
#
import os

# Keep a global list of what libraries have a 64 bit version.  We use it for
# replacements when linking a 64 bit dylib or executable.
libs64 = []

def Library(env, **kwargs):
  """Extends ComponentLibrary to support multiplatform builds.

  Args:
    env: The current environment.
    kwargs: The keyword arguments.
  """
  params = CombineDicts(kwargs, {'COMPONENT_STATIC': True})
  if params.has_key('also64bit'):
    libs64.append(params['name'])

  return ExtendComponent(env, 'ComponentLibrary', **params)


def DynamicLibrary(env, **kwargs):
  """Extends ComponentLibrary to support multiplatform builds
     of dynmic libraries. This use COMPONENT_STATIC = false.

  Args:
    env: The environment object.
    kwargs: The keyword arguments.

  Returns:
    See swtoolkit ComponentLibrary
  """
  params = CombineDicts(kwargs, {'COMPONENT_STATIC': False})
  return ExtendComponent(env, 'ComponentLibrary', **params)


def Object(env, **kwargs):
  return ExtendComponent(env, 'ComponentObject', **kwargs)


def Unittest(env, **kwargs):
  """Extends ComponentTestProgram to support unittest built
     for multiple platforms.

  Args:
    env: The current environment.
    kwargs: The keyword arguments.

  Returns:
    See swtoolkit ComponentProgram.
  """
  kwargs['name'] = kwargs['name'] + '_unittest'

  common_test_params = {
    'posix_cppdefines': ['GUNIT_NO_GOOGLE3', 'GTEST_HAS_RTTI=0'],
    'libs': ['unittest_main', 'gunit']
  }
  if not kwargs.has_key('explicit_libs'):
    common_test_params['win_libs'] = [
      'advapi32',
      'crypt32',
      'iphlpapi',
      'secur32',
      'shell32',
      'shlwapi',
      'user32',
      'wininet',
      'ws2_32'
    ]
    common_test_params['lin_libs'] = [
      'pthread',
      ':libssl.so.0.9.8',
      ':libcrypto.so.0.9.8',
    ]

  params = CombineDicts(kwargs, common_test_params)
  return ExtendComponent(env, 'ComponentTestProgram', **params)


def App(env, **kwargs):
  """Extends ComponentProgram to support executables with platform specific
     options.

  Args:
    env: The current environment.
    kwargs: The keyword arguments.

  Returns:
    See swtoolkit ComponentProgram.
  """
  if not kwargs.has_key('explicit_libs'):
    common_app_params = {
      'win_libs': [
        'advapi32',
        'crypt32',
        'iphlpapi',
        'secur32',
        'shell32',
        'shlwapi',
        'user32',
        'wininet',
        'ws2_32'
      ]}
    params = CombineDicts(kwargs, common_app_params)
  else:
    params = kwargs
  return ExtendComponent(env, 'ComponentProgram', **params)

def WiX(env, **kwargs):
  """ Extends the WiX builder
  Args:
    env: The current environment.
    kwargs: The keyword arguments.

  Returns:
    The node produced by the environment's wix builder
  """
  return ExtendComponent(env, 'WiX', **kwargs)

def Repository(env, at, path):
  """Maps a directory external to $MAIN_DIR to the given path so that sources
     compiled from it end up in the correct place under $OBJ_DIR.  NOT required
     when only referring to header files.

  Args:
    env: The current environment object.
    at: The 'mount point' within the current directory.
    path: Path to the actual directory.
  """
  env.Dir(at).addRepository(env.Dir(path))


def Components(*paths):
  """Completes the directory paths with the correct file
     names such that the directory/directory.scons name
     convention can be used.

  Args:
    paths: The paths to complete. If it refers to an existing
           file then it is ignored.

  Returns:
    The completed lif scons files that are needed to build talk.
  """
  files = []
  for path in paths:
    if os.path.isfile(path):
      files.append(path)
    else:
      files.append(ExpandSconsPath(path))
  return files


def ExpandSconsPath(path):
  """Expands a directory path into the path to the
     scons file that our build uses.
     Ex: magiflute/plugin/common => magicflute/plugin/common/common.scons

  Args:
    path: The directory path to expand.

  Returns:
    The expanded path.
  """
  return '%s/%s.scons' % (path, os.path.basename(path))


def AddMediaLibs(env, **kwargs):
  lmi_libdir = '$GOOGLE3/third_party/lmi/files/merged/lib/'
  if env.Bit('windows'):
    if env.get('COVERAGE_ENABLED'):
      lmi_libdir += 'win32/c_only'
    else:
      lmi_libdir += 'win32/Release'
  elif env.Bit('mac'):
    lmi_libdir += 'macos'
  elif env.Bit('linux'):
    lmi_libdir += 'linux/x86'

  ipp_libdir = '$GOOGLE3/third_party/Intel_ipp/%s/ia32/lib'
  if env.Bit('windows'):
    ipp_libdir %= 'v_5_2_windows'
  elif env.Bit('mac'):
    ipp_libdir %= 'v_5_3_mac_os_x'
  elif env.Bit('linux'):
    ipp_libdir %= 'v_5_2_linux'

  AddToDict(kwargs, 'libdirs', [
    '$MAIN_DIR/third_party/gips/Libraries/',
    ipp_libdir,
    lmi_libdir,
  ])

  gips_lib = ''
  if env.Bit('windows'):
    if env.Bit('debug'):
      gips_lib = 'gipsvoiceenginelib_mtd'
    else:
      gips_lib = 'gipsvoiceenginelib_mt'
  elif env.Bit('mac'):
    gips_lib = 'VoiceEngine_mac_universal_gcc'
  elif env.Bit('linux'):
    gips_lib = 'VoiceEngine_Linux_external_gcc'

  AddToDict(kwargs, 'libs', [
    gips_lib,
    'LmiAudioCommon',
    'LmiClient',
    'LmiCmcp',
    'LmiDeviceManager',
    'LmiH263ClientPlugIn',
    'LmiH263CodecCommon',
    'LmiH263Decoder',
    'LmiH263Encoder',
    'LmiH264ClientPlugIn',
    'LmiH264CodecCommon',
    'LmiH264Common',
    'LmiH264Decoder',
    'LmiH264Encoder',
    'LmiIce',
    'LmiMediaPayload',
    'LmiOs',
    'LmiPacketCache',
    'LmiProtocolStack',
    'LmiRateShaper',
    'LmiRtp',
    'LmiSecurity',
    'LmiSignaling',
    'LmiStun',
    'LmiTransport',
    'LmiUi',
    'LmiUtils',
    'LmiVideoCommon',
    'LmiXml',
    'ippsmerged',
    'ippsemerged',
    'ippvcmerged',
    'ippvcemerged',
    'ippimerged',
    'ippiemerged',
    'ippsrmerged',
    'ippsremerged',
  ])

  if env.Bit('windows'):
    AddToDict(kwargs, 'libs', [
      'dsound',
      'd3d9',
      'gdi32',
      'ippcorel',
      'ippscmerged',
      'ippscemerged',
      'strmiids',
    ])
  else:
    AddToDict(kwargs, 'libs', [
      'ippcore',
      'ippacmerged',
      'ippacemerged',
      'ippccmerged',
      'ippccemerged',
      'ippchmerged',
      'ippchemerged',
      'ippcvmerged',
      'ippcvemerged',
      'ippdcmerged',
      'ippdcemerged',
      'ippjmerged',
      'ippjemerged',
      'ippmmerged',
      'ippmemerged',
      'ipprmerged',
      'ippremerged',
    ])

  return kwargs


def ReadVersion(filename):
  """Executes the supplied file and pulls out a version definition from it. """
  defs = {}
  execfile(str(filename), defs)
  if not defs.has_key('version'):
    return '0.0.0.0'
  version = defs['version']
  parts = version.split(',')
  build = os.environ.get('GOOGLE_VERSION_BUILDNUMBER')
  if build:
    parts[-1] = str(build)
  return '.'.join(parts)


#-------------------------------------------------------------------------------
# Helper methods for translating talk.Foo() declarations in to manipulations of
# environmuent construction variables, including parameter parsing and merging,
#
def GetEntry(dict, key):
  """Get the value from a dictionary by key. If the key
     isn't in the dictionary then None is returned. If it is in
     the dictionaruy the value is fetched and then is it removed
     from the dictionary.

  Args:
    key: The key to get the value for.
    kwargs: The keyword argument dictionary.
  Returns:
    The value or None if the key is missing.
  """
  value = None
  if dict.has_key(key):
    value = dict[key]
    dict.pop(key)

  return value


def MergeAndFilterByPlatform(env, params):
  """Take a dictionary of arguments to lists of values, and, depending on
     which platform we are targetting, merge the lists of associated keys.
     Merge by combining value lists like so:
       {win_foo = [a,b], lin_foo = [c,d], foo = [e], mac_bar = [f], bar = [g] }
       becomes {foo = [a,b,e], bar = [g]} on windows, and
       {foo = [e], bar = [f,g]} on mac

  Args:
    env: The hammer environment which knows which platforms are active
    params: The keyword argument dictionary.
  Returns:
    A new dictionary with the filtered and combined entries of params
  """
  platforms = {
    'linux': 'lin_',
    'mac': 'mac_',
    'posix': 'posix_',
    'windows': 'win_',
  }
  active_prefixes = [
    platforms[x] for x in iter(platforms) if env.Bit(x)
  ]
  inactive_prefixes = [
    platforms[x] for x in iter(platforms) if not env.Bit(x)
  ]

  merged = {}
  for arg, values in params.iteritems():
    inactive_platform = False

    key = arg

    for prefix in active_prefixes:
      if arg.startswith(prefix):
        key = arg[len(prefix):]

    for prefix in inactive_prefixes:
      if arg.startswith(prefix):
        inactive_platform = True

    if inactive_platform:
      continue

    AddToDict(merged, key, values)

  return merged


def ExtendComponent(env, component, **kwargs):
  """A wrapper around a scons builder function that preprocesses and post-
     processes its inputs and outputs.  For example, it merges and filters
     certain keyword arguments before appending them to the environments
     construction variables.  It can build signed targets and 64bit copies
     of targets as well.

  Args:
    env: The hammer environment with which to build the target
    component: The environment's builder function, e.g. ComponentProgram
    kwargs: keyword arguments that are either merged, translated, and passed on
            to the call to component, or which control execution.
            TODO(): Document the fields, such as cppdefines->CPPDEFINES,
            prepend_includedirs, include_talk_media_libs, etc.
  Returns:
    The output node returned by the call to component, or a subsequent signed
    dependant node.
  """
  env = env.Clone()

  # get the 'target' field
  name = GetEntry(kwargs, 'name')

  # if this is a signed binary we need to make an unsigned version first
  signed = env.Bit('windows') and GetEntry(kwargs, 'signed')
  if signed:
    name = 'unsigned_' + name

  also64bit = env.Bit('linux') and GetEntry(kwargs, 'also64bit')

  # add default values
  if GetEntry(kwargs, 'include_talk_media_libs'):
    kwargs = AddMediaLibs(env, **kwargs)

  # prune parameters intended for other platforms, then merge
  params = MergeAndFilterByPlatform(env, kwargs)

  # potentially exit now
  srcs = GetEntry(params, 'srcs')
  if not srcs or not hasattr(env, component):
    return None

  # apply any explicit dependencies
  dependencies = GetEntry(kwargs, 'depends')
  if dependencies is not None:
    env.Depends(name, dependencies)

  # put the contents of params into the environment
  # some entries are renamed then appended, others renamed then prepended
  appends = {
    'cppdefines' : 'CPPDEFINES',
    'libdirs' : 'LIBPATH',
    'link_flags' : 'LINKFLAGS',
    'libs' : 'LIBS',
    'FRAMEWORKS' : 'FRAMEWORKS',
  }
  prepends = {
    'ccflags' : 'CCFLAGS',
  }
  if GetEntry(params, 'prepend_includedirs'):
    prepends['includedirs'] = 'CPPPATH'
  else:
    appends['includedirs'] = 'CPPPATH'

  for field, var in appends.items():
    values = GetEntry(params, field)
    if values is not None:
      env.Append(**{var : values})
  for field, var in prepends.items():
    values = GetEntry(params, field)
    if values is not None:
      env.Prepend(**{var : values})

  # workaround for pulse stripping link flag for unknown reason
  if env.Bit('linux'):
    env['SHLINKCOM'] = ('$SHLINK -o $TARGET -m32 $SHLINKFLAGS $SOURCES '
                        '$_LIBDIRFLAGS $_LIBFLAGS')
    env['LINKCOM'] = ('$LINK -o $TARGET -m32 $LINKFLAGS $SOURCES '
                      '$_LIBDIRFLAGS $_LIBFLAGS')

  # any other parameters are replaced without renaming
  for field, value in params.items():
    env.Replace(**{field : value})

  # invoke the builder function
  builder = getattr(env, component)

  node = builder(name, srcs)

  # make a parallel 64bit version if requested
  if also64bit:
    env_64bit = env.Clone()
    env_64bit.FilterOut(CCFLAGS = ['-m32'], LINKFLAGS = ['-m32'])
    env_64bit.Prepend(CCFLAGS = ['-m64', '-fPIC'], LINKFLAGS = ['-m64'])
    name_64bit = name + '64'
    env_64bit.Replace(OBJSUFFIX = '64' + env_64bit['OBJSUFFIX'])
    env_64bit.Replace(SHOBJSUFFIX = '64' + env_64bit['SHOBJSUFFIX'])
    if ('ComponentProgram' == component or
        ('ComponentLibrary' == component and
         env_64bit['COMPONENT_STATIC'] == False)):
      # link 64 bit versions of libraries
      libs = []
      for lib in env_64bit['LIBS']:
        if lib in set(libs64):
          libs.append(lib + '64')
        else:
          libs.append(lib)
      env_64bit.Replace(LIBS = libs)

    env_64bit['SHLINKCOM'] = ('$SHLINK -o $TARGET -m64 $SHLINKFLAGS $SOURCES '
                              '$_LIBDIRFLAGS $_LIBFLAGS')
    env_64bit['LINKCOM'] = ('$LINK -o $TARGET -m64 $LINKFLAGS $SOURCES '
                            '$_LIBDIRFLAGS $_LIBFLAGS')
    builder = getattr(env_64bit, component)
    nodes = [node, builder(name_64bit, srcs)]
    return nodes

  if signed:  # Note currently incompatible with 64Bit flag
    # Get the name of the built binary, then get the name of the final signed
    # version from it.  We need the output path since we don't know the file
    # extension beforehand.
    target = node[0].path.split('_', 1)[1]
    signed_node = env.SignedBinary(
      source = node,
      target = '$STAGING_DIR/' + target,
    )
    env.Alias('signed_binaries', signed_node)
    return signed

  return node


def AddToDict(dictionary, key, values, append=True):
  """Merge the given key value(s) pair into a dictionary.  If it contains an
     entry with that key already, then combine by appending or prepending the
     values as directed.  Otherwise, assign a new keyvalue pair.
  """
  if values is None:
    return

  if not dictionary.has_key(key):
    dictionary[key] = values
    return

  cur = dictionary[key]
  # TODO: Make sure that there are no duplicates
  # in the list. I can't use python set for this since
  # the nodes that are returned by the SCONS builders
  # are not hashable.
  # dictionary[key] = list(set(cur).union(set(values)))
  if append:
    dictionary[key] = cur + values
  else:
    dictionary[key] = values + cur


def CombineDicts(a, b):
  """Unions two dictionaries by combining values of keys shared between them.
  """
  c = {}
  for key in a:
    if b.has_key(key):
      c[key] = a[key] + b.pop(key)
    else:
      c[key] = a[key]

  for key in b:
    c[key] = b[key]

  return c


def RenameKey(d, old, new, append=True):
  AddToDict(d, new, GetEntry(d, old), append)
