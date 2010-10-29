// Copyright 2008 Google Inc. All Rights Reserved.


#ifndef TALK_BASE_LINUX_H_
#define TALK_BASE_LINUX_H_

#ifdef LINUX
#include <string>
#include <map>
#include <vector>
#include "talk/base/stream.h"

namespace talk_base {

//////////////////////////////////////////////////////////////////////////////
// ConfigParser parses a FileStream of an ".ini."-type format into a map.
//////////////////////////////////////////////////////////////////////////////

// Sample Usage:
//   ConfigParser parser;
//   ConfigParser::MapVector key_val_pairs;
//   if (parser.Open(inifile) && parser.Parse(&key_val_pairs)) {
//     for (int section_num=0; i < key_val_pairs.size(); ++section_num) {
//       std::string val1 = key_val_pairs[section_num][key1];
//       std::string val2 = key_val_pairs[section_num][key2];
//       // Do something with valn;
//     }
//   }

class ConfigParser {
 public:
  typedef std::map<std::string, std::string> SimpleMap;
  typedef std::vector<SimpleMap> MapVector;

  ConfigParser();
  virtual ~ConfigParser();

  virtual bool Open(const std::string& filename);
  virtual void Attach(StreamInterface* stream);
  virtual bool Parse(MapVector *key_val_pairs);
  virtual bool ParseSection(SimpleMap *key_val_pair);
  virtual bool ParseLine(std::string *key, std::string *value);

 private:
  scoped_ptr<StreamInterface> instream_;
};

//////////////////////////////////////////////////////////////////////////////
// ProcCpuInfo reads CPU info from the /proc subsystem on any *NIX platform.
//////////////////////////////////////////////////////////////////////////////

// Sample Usage:
//   ProcCpuInfo proc_info;
//   int no_of_cpu;
//   if (proc_info.LoadFromSystem()) {
//      std::string out_str;
//      proc_info.GetNumCpus(&no_of_cpu);
//      proc_info.GetCpuStringValue(0, "vendor_id", &out_str);
//      }
//   }

class ProcCpuInfo {
 public:
  ProcCpuInfo();
  virtual ~ProcCpuInfo();

  // Reads the proc subsystem's cpu info into memory. If this fails, this
  // returns false; if it succeeds, it returns true.
  virtual bool LoadFromSystem();

  // Obtains the number of CPUs and places the value num.
  virtual bool GetNumCpus(int *num);

  // Looks for the CPU proc item with the given name for the given CPU number
  // and places the string value in result.
  virtual bool GetCpuStringValue(int cpu_id, const std::string& key,
                                 std::string *result);

  // Looks for the CPU proc item with the given name for the given CPU number
  // and places the int value in result.
  virtual bool GetCpuIntValue(int cpu_id, const std::string& key,
                              int *result);

 private:
  ConfigParser::MapVector cpu_info_;
};

// Builds a string containing the info from lsb_release on a single line.
std::string ReadLinuxLsbRelease();

// Returns the output of "uname".
std::string ReadLinuxUname();

// Returns the content (int) of
// /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq
// Returns -1 on error.
int ReadCpuMaxFreq();

}  // namespace talk_base

#endif  // LINUX
#endif  // TALK_BASE_LINUX_H_
