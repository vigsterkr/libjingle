#!/usr/bin/env python
#
# libjingle
# Copyright 2004--2010, Google Inc.
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

"""
Copied from Chrome's src/tools/valgrind/memcheck/PRESUBMIT.py

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts
for more details on the presubmit API built into gcl.
"""

import os
import re
import sys

def CheckChange(input_api, output_api):
  """Checks the memcheck suppressions files for bad data."""

  # Add the path to the Chrome valgrind dir to the import path:
  tools_vg_path = os.path.join(input_api.PresubmitLocalPath(), '..', '..',
                               'valgrind')
  sys.path.append(tools_vg_path)
  import suppressions

  sup_regex = re.compile('suppressions.*\.txt$')
  suppressions = {}
  errors = []
  check_for_memcheck = False
  # skip_next_line has 3 possible values:
  # - False: don't skip the next line.
  # - 'skip_suppression_name': the next line is a suppression name, skip.
  # - 'skip_param': the next line is a system call parameter error, skip.
  skip_next_line = False
  for f in filter(lambda x: sup_regex.search(x.LocalPath()),
                  input_api.AffectedFiles()):
    for line, line_num in zip(f.NewContents(),
                              xrange(1, len(f.NewContents()) + 1)):
      line = line.lstrip()
      if line.startswith('#') or not line:
        continue

      if skip_next_line:
        if skip_next_line == 'skip_suppression_name':
          if 'insert_a_suppression_name_here' in line:
            errors.append('"insert_a_suppression_name_here" is not a valid '
                          'suppression name')
          if suppressions.has_key(line):
            if f.LocalPath() == suppressions[line][1]:
              errors.append('suppression with name "%s" at %s line %s '
                            'has already been defined at line %s' %
                            (line, f.LocalPath(), line_num,
                             suppressions[line][1]))
            else:
              errors.append('suppression with name "%s" at %s line %s '
                            'has already been defined at %s line %s' %
                            (line, f.LocalPath(), line_num,
                             suppressions[line][0], suppressions[line][1]))
          else:
            suppressions[line] = (f, line_num)
            check_for_memcheck = True;
        skip_next_line = False
        continue
      if check_for_memcheck:
        if not line.startswith('Memcheck:'):
          errors.append('"%s" should be "Memcheck:..." in %s line %s' %
                        (line, f.LocalPath(), line_num))
        check_for_memcheck = False;
      if line == '{':
        skip_next_line = 'skip_suppression_name'
        continue
      if line == "Memcheck:Param":
        skip_next_line = 'skip_param'
        continue

      if (line.startswith('fun:') or line.startswith('obj:') or
          line.startswith('Memcheck:') or line == '}' or
          line == '...'):
        continue
      errors.append('"%s" is probably wrong: %s line %s' % (line, f.LocalPath(),
                                                            line_num))
  if errors:
    return [output_api.PresubmitError('\n'.join(errors))]
  return []

def CheckChangeOnUpload(input_api, output_api):
  return CheckChange(input_api, output_api)

def CheckChangeOnCommit(input_api, output_api):
  return CheckChange(input_api, output_api)

def GetPreferredTrySlaves():
  # We don't have any memcheck slaves yet, so there's no use for this method.
  # When we have, the slave name(s) should be put into this list.
  return []
