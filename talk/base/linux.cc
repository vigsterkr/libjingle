// Copyright 2008 Google Inc. All Rights Reserved.

//

#ifdef LINUX
#include "talk/base/linux.h"

#include <errno.h>
#include <sys/utsname.h>

#include <cstdio>

#include "talk/base/stringencode.h"

namespace talk_base {

static const char kCpuInfoFile[] = "/proc/cpuinfo";
static const char kCpuMaxFreqFile[] =
    "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq";

ProcCpuInfo::ProcCpuInfo() {
}

ProcCpuInfo::~ProcCpuInfo() {
}

bool ProcCpuInfo::LoadFromSystem() {
  ConfigParser procfs;
  if (!procfs.Open(kCpuInfoFile))
    return false;
  return procfs.Parse(&cpu_info_);
};

bool ProcCpuInfo::GetNumCpus(int *num) {
  if (cpu_info_.size() == 0)
    return false;
  *num = cpu_info_.size();
  return true;
}

bool ProcCpuInfo::GetCpuStringValue(int cpu_id, const std::string& key,
                                    std::string *result) {
  if (cpu_id >= static_cast<int>(cpu_info_.size()))
    return false;
  ConfigParser::SimpleMap::iterator iter = cpu_info_[cpu_id].find(key);
  if (iter == cpu_info_[cpu_id].end())
    return false;
  *result = iter->second;
  return true;
}

bool ProcCpuInfo::GetCpuIntValue(int cpu_id, const std::string& key,
                                 int *result) {
  if (cpu_id >= static_cast<int>(cpu_info_.size())) {
    return false;
  }
  ConfigParser::SimpleMap::iterator iter = cpu_info_[cpu_id].find(key);
  if (iter == cpu_info_[cpu_id].end()) {
    return false;
  }
  *result = atoi((iter->second).c_str());
  return true;
}

ConfigParser::ConfigParser() {}

ConfigParser::~ConfigParser() {}

bool ConfigParser::Open(const std::string& filename) {
  FileStream *fs = new FileStream();
  if (!fs->Open(filename, "r"))
    return false;
  instream_.reset(fs);
  return true;
}

void ConfigParser::Attach(StreamInterface* stream) {
  instream_.reset(stream);
}

bool ConfigParser::Parse(MapVector *key_val_pairs) {
  // Parses the file and places the found key-value pairs into key_val_pairs.
  SimpleMap section;
  while (ParseSection(&section)) {
    key_val_pairs->push_back(section);
    section.clear();
  }
  return (!key_val_pairs->empty());
}

bool ConfigParser::ParseSection(SimpleMap *key_val_pair) {
  // Parses the next section in the filestream and places the found key-value
  // pairs into key_val_pair.
  std::string key, value;
  while (ParseLine(&key, &value)) {
    (*key_val_pair)[key] = value;
  }
  return (!key_val_pair->empty());
}

bool ConfigParser::ParseLine(std::string *key, std::string *value) {
  // Parses the next line in the filestream and places the found key-value
  // pair into key and val.
  std::string line;
  if ((instream_->ReadLine(&line)) == EOF)
    return false;
  std::vector<std::string> tokens;
  if (2 != split(line, ':', &tokens))
    return false;
  // Removes whitespace at the end of Key name
  size_t pos = tokens[0].length() - 1;
  while ((pos > 0) && isspace(tokens[0][pos]))
    pos--;
  tokens[0].erase(pos + 1);
  // Removes whitespace at the start of value
  pos = 0;
  while (pos < tokens[1].length() && isspace(tokens[1][pos]))
    pos++;
  tokens[1].erase(0, pos);
  *key = tokens[0];
  *value = tokens[1];
  return true;
}

static bool ExpectLineFromStream(FileStream *stream,
                                 std::string *out) {
  StreamResult res = stream->ReadLine(out);
  if (res != SR_SUCCESS) {
    if (res != SR_EOS) {
      LOG(LS_ERROR) << "Error when reading from stream";
    } else {
      LOG(LS_ERROR) << "Incorrect number of lines in stream";
    }
    return false;
  }
  return true;
}

static void ExpectEofFromStream(FileStream *stream) {
  std::string unused;
  StreamResult res = stream->ReadLine(&unused);
  if (res == SR_SUCCESS) {
    LOG(LS_WARNING) << "Ignoring unexpected extra lines from stream";
  } else if (res != SR_EOS) {
    LOG(LS_WARNING) << "Error when checking for extra lines from stream";
  }
}

// For caching the lsb_release output (reading it invokes a sub-process and
// hence is somewhat expensive).
static std::string lsb_release_string;
static CriticalSection lsb_release_string_critsec;

std::string ReadLinuxLsbRelease() {
  CritScope cs(&lsb_release_string_critsec);
  if (!lsb_release_string.empty()) {
    // Have cached result from previous call.
    return lsb_release_string;
  }
  // No cached result. Run lsb_release and parse output.
  POpenStream lsb_release_output;
  if (!lsb_release_output.Open("lsb_release -idrcs", "r")) {
    LOG_ERR(LS_ERROR) << "Can't run lsb_release";
    return lsb_release_string;  // empty
  }
  // Read in the command's output and build the string.
  std::ostringstream sstr;
  std::string line;
  int wait_status;

  if (!ExpectLineFromStream(&lsb_release_output, &line)) {
    return lsb_release_string;  // empty
  }
  sstr << "DISTRIB_ID=" << line;

  if (!ExpectLineFromStream(&lsb_release_output, &line)) {
    return lsb_release_string;  // empty
  }
  sstr << " DISTRIB_DESCRIPTION=\"" << line << '"';

  if (!ExpectLineFromStream(&lsb_release_output, &line)) {
    return lsb_release_string;  // empty
  }
  sstr << " DISTRIB_RELEASE=" << line;

  if (!ExpectLineFromStream(&lsb_release_output, &line)) {
    return lsb_release_string;  // empty
  }
  sstr << " DISTRIB_CODENAME=" << line;

  // Should not be anything left.
  ExpectEofFromStream(&lsb_release_output);

  lsb_release_output.Close();
  wait_status = lsb_release_output.GetWaitStatus();
  if (wait_status == -1 ||
      !WIFEXITED(wait_status) ||
      WEXITSTATUS(wait_status) != 0) {
    LOG(LS_WARNING) << "Unexpected exit status from lsb_release";
  }

  lsb_release_string = sstr.str();

  return lsb_release_string;
}

std::string ReadLinuxUname() {
  struct utsname buf;
  if (uname(&buf) < 0) {
    LOG_ERR(LS_ERROR) << "Can't call uname()";
    return std::string();
  }
  std::ostringstream sstr;
  sstr << buf.sysname << " "
       << buf.release << " "
       << buf.version << " "
       << buf.machine;
  return sstr.str();
}

int ReadCpuMaxFreq() {
  FileStream fs;
  std::string str;
  if (!fs.Open(kCpuMaxFreqFile, "r") || SR_SUCCESS != fs.ReadLine(&str)) {
    return -1;
  }
  return atoi(str.c_str());
}

}  // namespace talk_base

#endif  // LINUX
