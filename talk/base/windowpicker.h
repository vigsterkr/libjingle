// Copyright 2010 Google Inc. All Rights Reserved

//         thorcarpenter@google.com (Thor Carpenter)

#ifndef TALK_BASE_WINDOWPICKER_H_
#define TALK_BASE_WINDOWPICKER_H_

#include <list>
#include <string>

#include "talk/base/window.h"

namespace talk_base {

class WindowDescription {
 public:
  WindowDescription() : id_(kInvalidWindowId) {}
  WindowDescription(WindowId id, const std::string& title)
      : id_(id), title_(title) {
  }
  WindowId id() const {
    return id_;
  }
  const std::string& title() const {
    return title_;
  }

 private:
  WindowId id_;
  std::string title_;
};

typedef std::list<WindowDescription> WindowDescriptionList;

class WindowPicker {
 public:
  virtual ~WindowPicker() {}
  virtual bool Init() = 0;

  // TODO: Move this two methods to window.h when we no longer need to load
  // CoreGraphics dynamically.
  virtual bool IsVisible(WindowId id) = 0;
  virtual bool MoveToFront(WindowId id) = 0;

  // Gets a list of window description.
  // Returns true if successful.
  virtual bool GetWindowList(WindowDescriptionList* descriptions) = 0;
};

}  // namespace talk_base

#endif  // TALK_BASE_WINDOWPICKER_H_
