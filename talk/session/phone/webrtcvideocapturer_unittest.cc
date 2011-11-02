// libjingle
// Copyright 2004--2011 Google Inc.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//  3. The name of the author may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
// EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdio.h>
#include <vector>
#include "talk/base/gunit.h"
#include "talk/base/logging.h"
#include "talk/base/stringutils.h"
#include "talk/base/thread.h"
#include "talk/session/phone/testutils.h"
#include "talk/session/phone/videocommon.h"
#include "talk/session/phone/webrtcvideocapturer.h"

class FakeWebRtcVcmFactory;

// Fake class for mocking out webrtc::VideoCaptureModule.
class FakeVideoCaptureModule : public webrtc::VideoCaptureModule {
 public:
  FakeVideoCaptureModule(FakeWebRtcVcmFactory* factory, WebRtc_Word32 id)
      : factory_(factory),
        id_(id),
        callback_(NULL),
        running_(false),
        delay_(0) {
  }
  virtual int32_t Version(char* version,
                          uint32_t& remaining_buffer_in_bytes,
                          uint32_t& position) const {
    return 0;
  }
  virtual int32_t TimeUntilNextProcess() {
    return 0;
  }
  virtual int32_t Process() {
    return 0;
  }
  virtual WebRtc_Word32 ChangeUniqueId(const WebRtc_Word32 id) {
    id_ = id;
    return 0;
  }
  virtual WebRtc_Word32 RegisterCaptureDataCallback(
      webrtc::VideoCaptureDataCallback& callback) {
    callback_ = &callback;
    return 0;
  }
  virtual WebRtc_Word32 DeRegisterCaptureDataCallback() {
    callback_ = NULL;
    return 0;
  }
  virtual WebRtc_Word32 RegisterCaptureCallback(
      webrtc::VideoCaptureFeedBack& callback) {
    return -1;  // not implemented
  }
  virtual WebRtc_Word32 DeRegisterCaptureCallback() {
    return 0;
  }
  virtual WebRtc_Word32 StartCapture(
      const webrtc::VideoCaptureCapability& cap) {
    if (running_) return -1;
    cap_ = cap;
    running_ = true;
    return 0;
  }
  virtual WebRtc_Word32 StopCapture() {
    running_ = false;
    return 0;
  }
  virtual WebRtc_Word32 StartSendImage(const webrtc::VideoFrame& frame,
                                       WebRtc_Word32 framerate) {
    return -1;  // not implemented
  }
  virtual WebRtc_Word32 StopSendImage() {
    return 0;
  }
  virtual const WebRtc_UWord8* CurrentDeviceName() const {
    return NULL;  // not implemented
  }
  virtual bool CaptureStarted() {
    return running_;
  }
  virtual WebRtc_Word32 CaptureSettings(
      webrtc::VideoCaptureCapability& settings) {
    if (!running_) return -1;
    settings = cap_;
    return 0;
  }
  virtual WebRtc_Word32 SetCaptureDelay(WebRtc_Word32 delay) {
    delay_ = delay;
    return 0;
  }
  virtual WebRtc_Word32 CaptureDelay() {
    return delay_;
  }
  virtual WebRtc_Word32 SetCaptureRotation(
      webrtc::VideoCaptureRotation rotation) {
    return -1;  // not implemented
  }
  virtual VideoCaptureEncodeInterface* GetEncodeInterface(
      const webrtc::VideoCodec& codec) {
    return NULL;  // not implemented
  }
  virtual WebRtc_Word32 EnableFrameRateCallback(const bool enable) {
    return -1;  // not implemented
  }
  virtual WebRtc_Word32 EnableNoPictureAlarm(const bool enable) {
    return -1;  // not implemented
  }
  virtual int32_t AddRef() {
    return 0;
  }
  virtual int32_t Release() {
    delete this;
    return 0;
  }

  bool SendFrame(int w, int h) {
    if (!running_) return false;
    webrtc::VideoFrame sample;
    sample.SetWidth(w);
    sample.SetHeight(h);
    if (sample.VerifyAndAllocate(I420_SIZE(w, h)) == -1 ||
        sample.SetLength(sample.Size()) == -1) {
      return false;
    }
    if (callback_) {
      callback_->OnIncomingCapturedFrame(id_, sample,
                                         webrtc::kVideoCodecUnknown);
    }
    return true;
  }

 private:
  // Ref-counted, use Release() instead.
  ~FakeVideoCaptureModule();

  FakeWebRtcVcmFactory* factory_;
  int id_;
  webrtc::VideoCaptureDataCallback* callback_;
  bool running_;
  webrtc::VideoCaptureCapability cap_;
  int delay_;
};

// Fake class for mocking out webrtc::VideoCaptureModule::DeviceInfo.
class FakeDeviceInfo : public webrtc::VideoCaptureModule::DeviceInfo {
 public:
  struct Device {
    Device(const std::string& n, const std::string& i) : name(n), id(i) {}
    std::string name;
    std::string id;
    std::string product;
    std::vector<webrtc::VideoCaptureCapability> caps;
  };
  FakeDeviceInfo() {}
  void AddDevice(const std::string& device_name, const std::string& device_id) {
    devices_.push_back(Device(device_name, device_id));
  }
  void AddCapability(const std::string& device_id,
                     const webrtc::VideoCaptureCapability& cap) {
    Device* dev = GetDeviceById(
        reinterpret_cast<const WebRtc_UWord8*>(device_id.c_str()));
    if (!dev) return;
    dev->caps.push_back(cap);
  }
  virtual WebRtc_UWord32 NumberOfDevices() {
    return devices_.size();
  }
  virtual WebRtc_Word32 GetDeviceName(WebRtc_UWord32 device_num,
                                      WebRtc_UWord8* device_name,
                                      WebRtc_UWord32 device_name_len,
                                      WebRtc_UWord8* device_id,
                                      WebRtc_UWord32 device_id_len,
                                      WebRtc_UWord8* product_id,
                                      WebRtc_UWord32 product_id_len) {
    Device* dev = GetDeviceByIndex(device_num);
    if (!dev) return -1;
    talk_base::strcpyn(reinterpret_cast<char*>(device_name), device_name_len,
                       dev->name.c_str());
    talk_base::strcpyn(reinterpret_cast<char*>(device_id), device_id_len,
                       dev->id.c_str());
    if (product_id) {
      talk_base::strcpyn(reinterpret_cast<char*>(product_id), product_id_len,
                         dev->product.c_str());
    }
    return 0;
  }
  virtual WebRtc_Word32 NumberOfCapabilities(const WebRtc_UWord8* device_id) {
    Device* dev = GetDeviceById(device_id);
    if (!dev) return -1;
    return dev->caps.size();
  }
  virtual WebRtc_Word32 GetCapability(const WebRtc_UWord8* device_id,
                                      const WebRtc_UWord32 device_cap_num,
                                      webrtc::VideoCaptureCapability& cap) {
    Device* dev = GetDeviceById(device_id);
    if (!dev) return -1;
    if (device_cap_num >= dev->caps.size()) return -1;
    cap = dev->caps[device_cap_num];
    return 0;
  }
  virtual WebRtc_Word32 GetOrientation(const WebRtc_UWord8* device_id,
                                       webrtc::VideoCaptureRotation& rotation) {
    return -1;  // not implemented
  }
  virtual WebRtc_Word32 GetBestMatchedCapability(
      const WebRtc_UWord8* device_id,
      const webrtc::VideoCaptureCapability requested,
      webrtc::VideoCaptureCapability& resulting) {
    return -1;  // not implemented
  }
  virtual WebRtc_Word32 DisplayCaptureSettingsDialogBox(
      const WebRtc_UWord8* device_id, const WebRtc_UWord8* dialog_title,
      void* parent, WebRtc_UWord32 x, WebRtc_UWord32 y) {
    return -1;  // not implemented
  }

  Device* GetDeviceByIndex(size_t num) {
    return (num < devices_.size()) ? &devices_[num] : NULL;
  }
  Device* GetDeviceById(const WebRtc_UWord8* device_id) {
    for (size_t i = 0; i < devices_.size(); ++i) {
      if (devices_[i].id == reinterpret_cast<const char*>(device_id)) {
        return &devices_[i];
      }
    }
    return NULL;
  }

 private:
  std::vector<Device> devices_;
};

// Factory class to allow the fakes above to be injected into
// WebRtcVideoCapturer.
class FakeWebRtcVcmFactory : public cricket::WebRtcVcmFactoryInterface {
 public:
  virtual webrtc::VideoCaptureModule* Create(int module_id,
                                             const WebRtc_UWord8* device_id) {
    if (!device_info.GetDeviceById(device_id)) return NULL;
    FakeVideoCaptureModule* module =
        new FakeVideoCaptureModule(this, module_id);
    modules.push_back(module);
    return module;
  }
  virtual webrtc::VideoCaptureModule::DeviceInfo* CreateDeviceInfo(int id) {
    return &device_info;
  }
  virtual void DestroyDeviceInfo(webrtc::VideoCaptureModule::DeviceInfo* info) {
  }
  void OnDestroyed(webrtc::VideoCaptureModule* module) {
    std::remove(modules.begin(), modules.end(), module);
  }
  FakeDeviceInfo device_info;
  std::vector<FakeVideoCaptureModule*> modules;
};

FakeVideoCaptureModule::~FakeVideoCaptureModule() {
  factory_->OnDestroyed(this);
}

static const std::string kTestDeviceName = "JuberTech FakeCam Q123";
static const std::string kTestDeviceId = "foo://bar/baz";

class WebRtcVideoCapturerTest : public testing::Test {
 public:
  WebRtcVideoCapturerTest()
      : factory_(new FakeWebRtcVcmFactory),
        capturer_(new cricket::WebRtcVideoCapturer(factory_)),
        listener_(capturer_.get()) {
    factory_->device_info.AddDevice(kTestDeviceName, kTestDeviceId);
    // add a VGA/I420 capability
    webrtc::VideoCaptureCapability vga;
    vga.width = 640;
    vga.height = 480;
    vga.maxFPS = 30;
    vga.rawType = webrtc::kVideoI420;
    factory_->device_info.AddCapability(kTestDeviceId, vga);
  }

 protected:
  FakeWebRtcVcmFactory* factory_;  // owned by capturer_
  talk_base::scoped_ptr<cricket::WebRtcVideoCapturer> capturer_;
  cricket::VideoCapturerListener listener_;
};

TEST_F(WebRtcVideoCapturerTest, TestNotOpened) {
  EXPECT_EQ("", capturer_->GetId());
  EXPECT_EQ(NULL, capturer_->GetSupportedFormats());
  EXPECT_TRUE(capturer_->GetCaptureFormat() == NULL);
  EXPECT_FALSE(capturer_->IsRunning());
}

TEST_F(WebRtcVideoCapturerTest, TestBadInit) {
  EXPECT_FALSE(capturer_->Init(cricket::Device("bad-name", "bad-id")));
  EXPECT_FALSE(capturer_->IsRunning());
}

TEST_F(WebRtcVideoCapturerTest, TestInit) {
  EXPECT_TRUE(capturer_->Init(cricket::Device(kTestDeviceName, kTestDeviceId)));
  EXPECT_EQ(kTestDeviceId, capturer_->GetId());
  EXPECT_TRUE(NULL != capturer_->GetSupportedFormats());
  ASSERT_EQ(1U, capturer_->GetSupportedFormats()->size());
  EXPECT_EQ(640, (*capturer_->GetSupportedFormats())[0].width);
  EXPECT_EQ(480, (*capturer_->GetSupportedFormats())[0].height);
  EXPECT_TRUE(capturer_->GetCaptureFormat() == NULL);  // not started yet
  EXPECT_FALSE(capturer_->IsRunning());
}

TEST_F(WebRtcVideoCapturerTest, TestInitVcm) {
  EXPECT_TRUE(capturer_->Init(factory_->Create(0,
      reinterpret_cast<const WebRtc_UWord8*>(kTestDeviceId.c_str()))));
}

TEST_F(WebRtcVideoCapturerTest, TestCapture) {
  EXPECT_TRUE(capturer_->Init(cricket::Device(kTestDeviceName, kTestDeviceId)));
  cricket::VideoFormat format(
      capturer_->GetSupportedFormats()->at(0));
  EXPECT_EQ(cricket::CR_PENDING, capturer_->Start(format));
  EXPECT_TRUE(capturer_->IsRunning());
  ASSERT_TRUE(capturer_->GetCaptureFormat() != NULL);
  EXPECT_EQ(format, *capturer_->GetCaptureFormat());
  EXPECT_EQ_WAIT(cricket::CR_SUCCESS, listener_.start_result(), 1000);
  EXPECT_TRUE(factory_->modules[0]->SendFrame(640, 480));
  EXPECT_TRUE_WAIT(listener_.frame_count() > 0, 5000);
  EXPECT_EQ(capturer_->GetCaptureFormat()->fourcc, listener_.frame_fourcc());
  EXPECT_EQ(640, listener_.frame_width());
  EXPECT_EQ(480, listener_.frame_height());
  EXPECT_EQ(cricket::CR_FAILURE, capturer_->Start(format));
  capturer_->Stop();
  EXPECT_FALSE(capturer_->IsRunning());
  EXPECT_TRUE(capturer_->GetCaptureFormat() == NULL);
}

TEST_F(WebRtcVideoCapturerTest, TestCaptureWithoutInit) {
  cricket::VideoFormat format;
  EXPECT_EQ(cricket::CR_NO_DEVICE, capturer_->Start(format));
  EXPECT_TRUE(capturer_->GetCaptureFormat() == NULL);
  EXPECT_FALSE(capturer_->IsRunning());
}
