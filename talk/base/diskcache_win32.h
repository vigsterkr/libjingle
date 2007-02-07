//
//  DiskCacheWin32.h
//  Macshroom
//
//  Created by Moishe Lettvin on 11/7/06.
//  Copyright (C) 2006 Google Inc. All rights reserved.
//
//

#ifndef TALK_BASE_DISKCACHEWIN32_H__
#define TALK_BASE_DISKCACHEWIN32_H__

#include "talk/base/diskcache.h"

namespace talk_base {

class DiskCacheWin32 : public DiskCache {
 protected:
  virtual bool InitializeEntries();
  virtual bool PurgeFiles();

  virtual bool FileExists(const std::string& filename) const;
  virtual bool DeleteFile(const std::string& filename) const;
};

}

#endif  // TALK_BASE_DISKCACHEWIN32_H__
