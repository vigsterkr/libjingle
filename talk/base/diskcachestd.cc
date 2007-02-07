//
//  DiskCacheStd.cc
//  Macshroom
//
//  Created by Moishe Lettvin on 11/7/06.
//  Copyright (C) 2006 Google Inc. All rights reserved.
//
//

#include "diskcachestd.h"

namespace talk_base {

bool DiskCacheStd::InitializeEntries() {
  return false;
}

bool DiskCacheStd::PurgeFiles() {
  return false;
}

bool DiskCacheStd::FileExists(const std::string& filename) const {
  return false;
}

bool DiskCacheStd::DeleteFile(const std::string& filename) const {
  return false;
}

}
