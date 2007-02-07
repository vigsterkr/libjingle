
//*********************************************************************
//* C_Base64 - a simple base64 encoder and decoder.
//*
//*     Copyright (c) 1999, Bob Withers - bwit@pobox.com
//*
//* This code may be freely used for any purpose, either personal
//* or commercial, provided the authors copyright notice remains
//* intact.
//*********************************************************************

#ifndef TALK_BASE_BASE64_H__
#define TALK_BASE_BASE64_H__

#include <string>

namespace talk_base {

class Base64
{
public:
  static std::string encode(const std::string & data);
  static std::string decode(const std::string & data);
  static std::string encodeFromArray(const char * data, size_t len);
private:
  static const std::string Base64::Base64Table;
  static const std::string::size_type Base64::DecodeTable[];
};

} // namespace talk_base

#endif // TALK_BASE_BASE64_H__
