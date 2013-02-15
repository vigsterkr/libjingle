/*
 * libjingle
 * Copyright 2013, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Hints for future visitors:
// This entire file is an implementation detail of the org.webrtc Java package,
// the most interesting bits of which are org.webrtc.PeerConnection{,Factory}.
// The layout of this file is roughly:
// - various helper C++ functions & classes that wrap Java counterparts and
//   expose a C++ interface that can be passed to the C++ PeerConnection APIs
// - implementations of methods declared "static" in the Java package (named
//   things like Java_org_webrtc_OMG_Can_This_Name_Be_Any_Longer, prescribed by
//   the JNI spec).
//
// Lifecycle notes: objects are owned where they will be called; in other words
// FooObservers are owned by C++-land, and user-callable objects (e.g.
// PeerConnection and VideoTrack) are owned by Java-land.
// When this file allocates C++ RefCountInterfaces it AddRef()s an artificial
// ref simulating the jlong held in Java-land, and then Release()s the ref in
// the respective free call.  Sometimes this AddRef is implicit in the
// construction of a scoped_refptr<> which is then .release()d.
// Any persistent (non-local) references from C++ to Java must be global or weak
// (in which case they must be checked before use)!
//
// Exception notes: pretty much all JNI calls can throw Java exceptions, so each
// call through a JNIEnv* pointer needs to be followed by an ExceptionCheck()
// call.  In this file this is done in CHECK_EXCEPTION, making for much easier
// debugging in case of failure (the alternative is to wait for control to
// return to the Java frame that called code in this file, at which point it's
// impossible to tell which JNI call broke).

#include <jni.h>
#undef JNIEXPORT
#define JNIEXPORT __attribute__((visibility("default")))

#include <map>

#include "talk/app/webrtc/peerconnectioninterface.h"
#include "talk/app/webrtc/videosourceinterface.h"
#include "talk/base/logging.h"
#include "talk/media/base/videocapturer.h"
#include "talk/media/base/videorenderer.h"
#include "talk/media/devices/videorendererfactory.h"
#include "talk/media/webrtc/webrtcvideocapturer.h"
#include "third_party/icu/public/common/unicode/unistr.h"

using icu::UnicodeString;
using webrtc::AudioSourceInterface;
using webrtc::AudioTrackInterface;
using webrtc::CreateSessionDescriptionObserver;
using webrtc::IceCandidateInterface;
using webrtc::LocalMediaStreamInterface;
using webrtc::MediaConstraintsInterface;
using webrtc::MediaSourceInterface;
using webrtc::MediaStreamInterface;
using webrtc::MediaStreamTrackInterface;
using webrtc::PeerConnectionFactoryInterface;
using webrtc::PeerConnectionInterface;
using webrtc::PeerConnectionObserver;
using webrtc::SessionDescriptionInterface;
using webrtc::SetSessionDescriptionObserver;
using webrtc::VideoRendererInterface;
using webrtc::VideoSourceInterface;
using webrtc::VideoTrackInterface;
using webrtc::VideoRendererInterface;

// Abort the process if |x| is false, emitting |msg|.
#define CHECK(x, msg) if (x) {} else {                                  \
  LOG(LS_ERROR) << __FILE__ << ":" << __LINE__ << ": " << msg; \
  abort();                                                     \
  }
// Abort the process if |jni| has a Java exception pending, emitting |msg|.
#define CHECK_EXCEPTION(jni, msg) if (0) {} else {      \
  if (jni->ExceptionCheck()) { \
    jni->ExceptionDescribe();  \
    jni->ExceptionClear(); \
    CHECK(0, msg); \
  } \
  }

namespace {

// JNIEnv-helper methods that CHECK success: no Java exception thrown and found
// object/class/method/field is non-null.
jmethodID GetMethodID(
    JNIEnv* jni, jclass c, const char* name, const char* signature) {
  jmethodID m = jni->GetMethodID(c, name, signature);
  CHECK_EXCEPTION(jni,
                  "error during GetMethodID: " << name << ", " << signature);
  CHECK(m, name << ", " << signature);
  return m;
}

jmethodID GetStaticMethodID(
    JNIEnv* jni, jclass c, const char* name, const char* signature) {
  jmethodID m = jni->GetStaticMethodID(c, name, signature);
  CHECK_EXCEPTION(jni,
                  "error during GetStaticMethodID: "
                  << name << ", " << signature);
  CHECK(m, name << ", " << signature);
  return m;
}

jfieldID GetFieldID(
    JNIEnv* jni, jclass c, const char* name, const char* signature) {
  jfieldID f = jni->GetFieldID(c, name, signature);
  CHECK_EXCEPTION(jni, "error during GetFieldID");
  CHECK(f, name << ", " << signature);
  return f;
}

jclass FindClass(JNIEnv* jni, const char* name) {
  jclass c = jni->FindClass(name);
  CHECK_EXCEPTION(jni, "error during FindClass");
  CHECK(c, name);
  return c;
}

jclass GetObjectClass(JNIEnv* jni, jobject object) {
  jclass c = jni->GetObjectClass(object);
  CHECK_EXCEPTION(jni, "error during GetObjectClass");
  CHECK(c, "");
  return c;
}

jobject GetObjectField(JNIEnv* jni, jobject object, jfieldID id) {
  jobject o = jni->GetObjectField(object, id);
  CHECK_EXCEPTION(jni, "error during GetObjectField");
  CHECK(o, "");
  return o;
}

jlong GetLongField(JNIEnv* jni, jobject object, jfieldID id) {
  jlong l = jni->GetLongField(object, id);
  CHECK_EXCEPTION(jni, "error during GetLongField");
  CHECK(l, "");
  return l;
}

jobject NewGlobalRef(JNIEnv* jni, jobject o) {
  jobject ret = jni->NewGlobalRef(o);
  CHECK_EXCEPTION(jni, "error during NewGlobalRef");
  CHECK(ret, "");
  return ret;
}

void DeleteGlobalRef(JNIEnv* jni, jobject o) {
  jni->DeleteGlobalRef(o);
  CHECK_EXCEPTION(jni, "error during DeleteGlobalRef");
}

// Return the (singleton) Java Enum object corresponding to |index|;
// |state_class_fragment| is something like "MediaSource$State".
jobject JavaEnumFromIndex(
    JNIEnv* jni, const std::string& state_class_fragment, int index) {
  std::string state_class_name = "org/webrtc/" + state_class_fragment;
  jclass state_class = FindClass(jni, state_class_name.c_str());
  jmethodID state_values_id = GetStaticMethodID(
      jni, state_class, "values", ("()[L" + state_class_name  + ";").c_str());
  jobjectArray state_values = (jobjectArray)jni->CallStaticObjectMethod(
      state_class, state_values_id);
  CHECK_EXCEPTION(jni, "error during CallStaticObjectMethod");
  jobject ret = jni->GetObjectArrayElement(state_values, index);
  CHECK_EXCEPTION(jni, "error during GetObjectArrayElement");
  return ret;
}

// Given a jweak reference, allocate a (strong) local reference scoped to the
// lifetime of this object if the weak reference is still valid, or NULL
// otherwise.
class ScopedLocalRef {
 public:
  ScopedLocalRef(JNIEnv* jni, jweak ref)
      : jni_(jni), ref_(jni_->NewLocalRef(ref)) {
    CHECK_EXCEPTION(jni, "error during NewLocalRef");
  }
  ~ScopedLocalRef() {
    if (ref_) {
      jni_->DeleteLocalRef(ref_);
      CHECK_EXCEPTION(jni_, "error during DeleteLocalRef");
    }
  }
  jobject ref() { return ref_; }

 private:
  JNIEnv* const jni_;
  jobject const ref_;
};

// Given a UTF-8 encoded |native| string return a new (UTF-16) jstring.
static jstring JavaStringFromStdString(JNIEnv* jni, const std::string& native) {
  UnicodeString ustr(UnicodeString::fromUTF8(native));
  jstring jstr = jni->NewString(ustr.getBuffer(), ustr.length());
  CHECK_EXCEPTION(jni, "error during NewString");
  return jstr;
}

// Given a (UTF-16) jstring return a new UTF-8 native string.
static std::string JavaToStdString(JNIEnv* jni, const jstring& j_string) {
  const jchar* jchars = jni->GetStringChars(j_string, NULL);
  CHECK_EXCEPTION(jni, "Error during GetStringChars");
  UnicodeString ustr(jchars, jni->GetStringLength(j_string));
  CHECK_EXCEPTION(jni, "Error during GetStringLength");
  jni->ReleaseStringChars(j_string, jchars);
  CHECK_EXCEPTION(jni, "Error during ReleaseStringChars");
  std::string ret;
  return ustr.toUTF8String(ret);
}

class ConstraintsWrapper;

// Adapter between the C++ PeerConnectionObserver interface and the Java
// PeerConnection.Observer interface.  Wraps an instance of the Java interface
// and dispatches C++ callbacks to Java.
class PCOJava : public PeerConnectionObserver {
 public:
  PCOJava(JNIEnv* jni, jobject j_observer)
      : j_observer_global_(NewGlobalRef(jni, j_observer)),
        j_observer_class_((jclass)NewGlobalRef(
            jni, GetObjectClass(jni, j_observer_global_))),
        j_media_stream_class_((jclass)NewGlobalRef(
            jni, FindClass(jni, "org/webrtc/MediaStream"))),
        j_media_stream_ctor_(GetMethodID(jni,
            j_media_stream_class_, "<init>", "(J)V")),
        j_audio_track_class_((jclass)NewGlobalRef(
            jni, FindClass(jni, "org/webrtc/AudioTrack"))),
        j_audio_track_ctor_(GetMethodID(
            jni, j_audio_track_class_, "<init>", "(J)V")),
        j_video_track_class_((jclass)NewGlobalRef(
            jni, FindClass(jni, "org/webrtc/VideoTrack"))),
        j_video_track_ctor_(GetMethodID(jni,
            j_video_track_class_, "<init>", "(J)V")) {
    CHECK(!jni->GetJavaVM(&jvm_), "Failed to GetJavaVM");
  }

  virtual ~PCOJava() {
    DeleteGlobalRef(jni(), j_observer_global_);
  }

  virtual void OnIceCandidate(const IceCandidateInterface* candidate) {
    std::string sdp;
    CHECK(candidate->ToString(&sdp), "got so far: " << sdp);
    jclass candidate_class = FindClass(jni(), "org/webrtc/IceCandidate");
    jmethodID ctor = GetMethodID(jni(), candidate_class,
        "<init>", "(Ljava/lang/String;ILjava/lang/String;)V");
    jobject j_candidate = jni()->NewObject(
        candidate_class, ctor,
        JavaStringFromStdString(jni(), candidate->sdp_mid()),
        candidate->sdp_mline_index(),
        JavaStringFromStdString(jni(), sdp));
    CHECK_EXCEPTION(jni(), "error during NewObject");
    jmethodID m = GetMethodID(jni(), j_observer_class_,
                              "onIceCandidate", "(Lorg/webrtc/IceCandidate;)V");
    jni()->CallVoidMethod(j_observer_global_, m, j_candidate);
    CHECK_EXCEPTION(jni(), "error during CallVoidMethod");
  }

  virtual void OnError() {
    jmethodID m = GetMethodID(jni(), j_observer_class_, "onError", "(V)V");
    jni()->CallVoidMethod(j_observer_global_, m);
    CHECK_EXCEPTION(jni(), "error during CallVoidMethod");
  }

  virtual void OnSignalingChange(
      PeerConnectionInterface::SignalingState new_state) {
    // TODO(bemasc): Update JNI to use this new signaling state callback.
  }

  virtual void OnStateChange(StateType stateChanged) {
    jmethodID m = GetMethodID(
        jni(), j_observer_class_, "onStateChange",
        "(Lorg/webrtc/PeerConnection$Observer$StateType;)V");
    jni()->CallVoidMethod(j_observer_global_, m, JavaEnumFromIndex(
        jni(), "PeerConnection$Observer$StateType", stateChanged));
    CHECK_EXCEPTION(jni(), "error during CallVoidMethod");
  }

  virtual void OnIceConnectionChange(
      PeerConnectionInterface::IceConnectionState new_state) {
    // TODO(bemasc): Update JNI to match ice state changes.
  }

  virtual void OnIceGatheringChange(
      PeerConnectionInterface::IceGatheringState new_state) {
    // TODO(bemasc): Update JNI to match ice state changes.
  }

  virtual void OnAddStream(MediaStreamInterface* stream) {
    jobject j_stream = jni()->NewObject(
        j_media_stream_class_, j_media_stream_ctor_, (jlong)stream);
    CHECK_EXCEPTION(jni(), "error during NewObject");

    for (size_t i = 0; i < stream->audio_tracks()->count(); ++i) {
      AudioTrackInterface* track = stream->audio_tracks()->at(i);
      jstring id = JavaStringFromStdString(jni(), track->id());
      jobject j_track = jni()->NewObject(
          j_audio_track_class_, j_audio_track_ctor_, (jlong)track, id);
      CHECK_EXCEPTION(jni(), "error during NewObject");
      jfieldID audio_tracks_id = GetFieldID(jni(),
          j_media_stream_class_, "audioTracks", "Ljava/util/List;");
      jobject audio_tracks = GetObjectField(jni(), j_stream, audio_tracks_id);
      jmethodID add = GetMethodID(jni(),
          GetObjectClass(jni(), audio_tracks), "add", "(Ljava/lang/Object;)Z");
      jboolean added = jni()->CallBooleanMethod(audio_tracks, add, j_track);
      CHECK_EXCEPTION(jni(), "error during CallBooleanMethod");
      CHECK(added, "");
    }

    for (size_t i = 0; i < stream->video_tracks()->count(); ++i) {
      VideoTrackInterface* track = stream->video_tracks()->at(i);
      jstring id = JavaStringFromStdString(jni(), track->id());
      jobject j_track = jni()->NewObject(
          j_video_track_class_, j_video_track_ctor_, (jlong)track, id);
      CHECK_EXCEPTION(jni(), "error during NewObject");
      jfieldID video_tracks_id = GetFieldID(jni(),
          j_media_stream_class_, "videoTracks", "Ljava/util/List;");
      jobject video_tracks = GetObjectField(jni(), j_stream, video_tracks_id);
      jmethodID add = GetMethodID(jni(),
          GetObjectClass(jni(), video_tracks), "add", "(Ljava/lang/Object;)Z");
      jboolean added = jni()->CallBooleanMethod(video_tracks, add, j_track);
      CHECK_EXCEPTION(jni(), "error during CallBooleanMethod");
      CHECK(added, "");
    }
    streams_[stream] = jni()->NewWeakGlobalRef(j_stream);
    CHECK_EXCEPTION(jni(), "error during NewWeakGlobalRef");

    jmethodID m = GetMethodID(jni(),
        j_observer_class_, "onAddStream", "(Lorg/webrtc/MediaStream;)V");
    jni()->CallVoidMethod(j_observer_global_, m, j_stream);
    CHECK_EXCEPTION(jni(), "error during CallVoidMethod");
  }

  virtual void OnRemoveStream(MediaStreamInterface* stream) {
    NativeToJavaStreamsMap::iterator it = streams_.find(stream);
    CHECK(it != streams_.end(), "unexpected stream: " << std::hex << stream);

    ScopedLocalRef s(jni(), it->second);
    streams_.erase(it);
    if (!s.ref())
      return;

    jmethodID m = GetMethodID(jni(),
        j_observer_class_, "onRemoveStream", "(Lorg/webrtc/MediaStream;)V");
    jni()->CallVoidMethod(j_observer_global_, m, s.ref());
    CHECK_EXCEPTION(jni(), "error during CallVoidMethod");
  }

  void SetConstraints(ConstraintsWrapper* constraints) {
    CHECK(!constraints_.get(), "constraints already set!");
    constraints_.reset(constraints);
  }

  const ConstraintsWrapper* constraints() { return constraints_.get(); }

 private:
  JNIEnv* jni() {
    JNIEnv* jni;
    CHECK(!jvm_->AttachCurrentThread(reinterpret_cast<void**>(&jni), NULL),
          "Failed to attach thread");
    return jni;
  }

  JavaVM* jvm_;
  const jobject j_observer_global_;
  const jclass j_observer_class_;
  const jclass j_media_stream_class_;
  const jmethodID j_media_stream_ctor_;
  const jclass j_audio_track_class_;
  const jmethodID j_audio_track_ctor_;
  const jclass j_video_track_class_;
  const jmethodID j_video_track_ctor_;
  typedef std::map<void*, jweak> NativeToJavaStreamsMap;
  NativeToJavaStreamsMap streams_;  // C++ -> Java streams.
  talk_base::scoped_ptr<ConstraintsWrapper> constraints_;
};

// Wrapper for a Java MediaConstraints object.  Copies all needed data so when
// the constructor returns the Java object is no longer needed.
class ConstraintsWrapper : public MediaConstraintsInterface {
 public:
  ConstraintsWrapper(JNIEnv* jni, jobject j_constraints) {
    PopulateConstraintsFromJavaPairList(
        jni, j_constraints, "mandatory", &mandatory_);
    PopulateConstraintsFromJavaPairList(
        jni, j_constraints, "optional", &optional_);
  }

  virtual ~ConstraintsWrapper() {}

  // MediaConstraintsInterface.
  virtual const Constraints& GetMandatory() const { return mandatory_; }
  virtual const Constraints& GetOptional() const { return optional_; }

 private:
  // Helper for translating a List<Pair<String, String>> to a Constraints.
  static void PopulateConstraintsFromJavaPairList(
      JNIEnv* jni, jobject j_constraints,
      const char* field_name, Constraints* field) {
    jfieldID j_id = GetFieldID(jni,
        GetObjectClass(jni, j_constraints), field_name, "Ljava/util/List;");
    jobject j_list = GetObjectField(jni, j_constraints, j_id);
    jmethodID j_iterator_id = GetMethodID(jni,
        GetObjectClass(jni, j_list), "iterator", "()Ljava/util/Iterator;");
    jobject j_iterator = jni->CallObjectMethod(j_list, j_iterator_id);
    CHECK_EXCEPTION(jni, "error during CallObjectMethod");
    jmethodID j_has_next = GetMethodID(jni,
        GetObjectClass(jni, j_iterator), "hasNext", "()Z");
    jmethodID j_next = GetMethodID(jni,
        GetObjectClass(jni, j_iterator), "next", "()Ljava/lang/Object;");
    while (jni->CallBooleanMethod(j_iterator, j_has_next)) {
      CHECK_EXCEPTION(jni, "error during CallBooleanMethod");
      jobject entry = jni->CallObjectMethod(j_iterator, j_next);
      CHECK_EXCEPTION(jni, "error during CallObjectMethod");
      jmethodID get_key = GetMethodID(jni,
          GetObjectClass(jni, entry), "getKey", "()Ljava/lang/String;");
      jstring j_key = reinterpret_cast<jstring>(
          jni->CallObjectMethod(entry, get_key));
      CHECK_EXCEPTION(jni, "error during CallObjectMethod");
      jmethodID get_value = GetMethodID(jni,
          GetObjectClass(jni, entry), "getValue", "()Ljava/lang/String;");
      jstring j_value = reinterpret_cast<jstring>(
          jni->CallObjectMethod(entry, get_value));
      CHECK_EXCEPTION(jni, "error during CallObjectMethod");
      field->push_back(Constraint(JavaToStdString(jni, j_key),
                                  JavaToStdString(jni, j_value)));
    }
    CHECK_EXCEPTION(jni, "error during CallBooleanMethod");
  }

  Constraints mandatory_;
  Constraints optional_;
};

static jobject JavaSdpFromNativeSdp(
    JNIEnv* jni, const SessionDescriptionInterface* desc) {
  std::string sdp;
  CHECK(desc->ToString(&sdp), "got so far: " << sdp);
  jstring j_description = JavaStringFromStdString(jni, sdp);

  jclass j_type_class = FindClass(
      jni, "org/webrtc/SessionDescription$Type");
  jmethodID j_type_from_canonical = GetStaticMethodID(
      jni, j_type_class, "fromCanonicalForm",
      "(Ljava/lang/String;)Lorg/webrtc/SessionDescription$Type;");
  jobject j_type = jni->CallStaticObjectMethod(
      j_type_class, j_type_from_canonical,
      JavaStringFromStdString(jni, desc->type()));
  CHECK_EXCEPTION(jni, "error during CallObjectMethod");

  jclass j_sdp_class = FindClass(jni, "org/webrtc/SessionDescription");
  jmethodID j_sdp_ctor = GetMethodID(
      jni, j_sdp_class, "<init>",
      "(Lorg/webrtc/SessionDescription$Type;Ljava/lang/String;)V");
  jobject j_sdp = jni->NewObject(
      j_sdp_class, j_sdp_ctor, j_type, j_description);
  CHECK_EXCEPTION(jni, "error during NewObject");
  return j_sdp;
}

template <class T>  // T is one of {Create,Set}SessionDescriptionObserver.
class SdpObserverWrapper : public T {
 public:
  SdpObserverWrapper(JNIEnv* jni, jobject j_observer,
                     ConstraintsWrapper* constraints)
      : constraints_(constraints),
        j_observer_global_(NewGlobalRef(jni, j_observer)),
        j_observer_class_((jclass)NewGlobalRef(
            jni, GetObjectClass(jni, j_observer))) {
    CHECK(!jni->GetJavaVM(&jvm_), "Failed to GetJavaVM");
  }

  ~SdpObserverWrapper() {
    DeleteGlobalRef(jni(), j_observer_global_);
    DeleteGlobalRef(jni(), j_observer_class_);
  }

  virtual void OnSuccess() {
    jmethodID m = GetMethodID(jni(), j_observer_class_, "onSuccess", "()V");
    jni()->CallVoidMethod(j_observer_global_, m);
    CHECK_EXCEPTION(jni(), "error during CallVoidMethod");
  }

  virtual void OnSuccess(SessionDescriptionInterface* desc) {
    jmethodID m = GetMethodID(
        jni(), j_observer_class_, "onSuccess",
        "(Lorg/webrtc/SessionDescription;)V");
    jobject j_sdp = JavaSdpFromNativeSdp(jni(), desc);
    jni()->CallVoidMethod(j_observer_global_, m, j_sdp);
    CHECK_EXCEPTION(jni(), "error during CallVoidMethod");
  }

  virtual void OnFailure(const std::string& error) {
    jmethodID m = GetMethodID(jni(),
        j_observer_class_, "onFailure", "(Ljava/lang/String;)V");
    jni()->CallVoidMethod(j_observer_global_, m,
                          JavaStringFromStdString(jni(), error));
    CHECK_EXCEPTION(jni(), "error during CallVoidMethod");
  }

 private:
  JNIEnv* jni() {
    JNIEnv* jni;
    CHECK(!jvm_->AttachCurrentThread(reinterpret_cast<void**>(&jni), NULL),
          "Failed to attach thread");
    return jni;
  }

  talk_base::scoped_ptr<ConstraintsWrapper> constraints_;
  JavaVM* jvm_;
  const jobject j_observer_global_;
  const jclass j_observer_class_;
};

typedef SdpObserverWrapper<CreateSessionDescriptionObserver>
    CreateSdpObserverWrapper;
typedef SdpObserverWrapper<SetSessionDescriptionObserver> SetSdpObserverWrapper;

// Adapter presenting a cricket::VideoRenderer as a
// webrtc::VideoRendererInterface.
class VideoRendererWrapper : public VideoRendererInterface {
 public:
  explicit VideoRendererWrapper(cricket::VideoRenderer* renderer)
      : renderer_(renderer) {}
  ~VideoRendererWrapper() {}

  virtual void SetSize(int width, int height) {
    const bool kNotReserved = false;  // What does this param mean??
    renderer_->SetSize(width, height, kNotReserved);
  }

  virtual void RenderFrame(const cricket::VideoFrame* frame) {
    renderer_->RenderFrame(frame);
  }

 private:
  talk_base::scoped_ptr<cricket::VideoRenderer> renderer_;
};

}  // anonymous namespace


// Convenience macro defining JNI-accessible methods in the org.webrtc package.
// Eliminates unnecessary boilerplate and line-wraps, reducing visual clutter.
#define JOW(rettype, name) extern "C" rettype JNIEXPORT JNICALL \
  Java_org_webrtc_##name

JOW(void, PeerConnection_freePeerConnection)(JNIEnv*, jclass, jlong j_p) {
  reinterpret_cast<PeerConnectionInterface*>(j_p)->Release();
}

JOW(void, PeerConnection_freeObserver)(JNIEnv*, jclass, jlong j_p) {
  PCOJava* p = reinterpret_cast<PCOJava*>(j_p);
  delete p;
}

JOW(void, MediaSource_free)(JNIEnv*, jclass, jlong j_p) {
  reinterpret_cast<MediaSourceInterface*>(j_p)->Release();
}

JOW(void, VideoCapturer_free)(JNIEnv*, jclass, jlong j_p) {
  delete reinterpret_cast<cricket::VideoCapturer*>(j_p);
}

JOW(void, VideoRenderer_free)(JNIEnv*, jclass, jlong j_p) {
  delete reinterpret_cast<VideoRendererWrapper*>(j_p);
}

JOW(void, MediaStreamTrack_free)(JNIEnv*, jclass, jlong j_p) {
  reinterpret_cast<MediaStreamTrackInterface*>(j_p)->Release();
}

JOW(jstring, MediaStream_nativeLabel)(JNIEnv* jni, jclass, jlong j_p) {
  return JavaStringFromStdString(
      jni, reinterpret_cast<MediaStreamInterface*>(j_p)->label());
}

JOW(void, MediaStream_free)(JNIEnv*, jclass, jlong j_p) {
  reinterpret_cast<MediaStreamInterface*>(j_p)->Release();
}

JOW(jlong, PeerConnectionFactory_nativeCreateObserver)(
    JNIEnv * jni, jclass, jobject j_observer) {
  return (jlong)new PCOJava(jni, j_observer);
}

JOW(jlong, PeerConnectionFactory_nativeCreatePeerConnectionFactory)(
    JNIEnv*, jclass) {
  talk_base::scoped_refptr<PeerConnectionFactoryInterface> factory(
      webrtc::CreatePeerConnectionFactory());
  return (jlong)factory.release();
}

JOW(void, PeerConnectionFactory_freeFactory)(JNIEnv*, jclass, jlong j_p) {
  reinterpret_cast<PeerConnectionFactoryInterface*>(j_p)->Release();
}

JOW(jlong, PeerConnectionFactory_nativeCreateLocalMediaStream)(
    JNIEnv* jni, jclass, jlong native_factory, jstring label) {
  talk_base::scoped_refptr<PeerConnectionFactoryInterface> factory(
      reinterpret_cast<PeerConnectionFactoryInterface*>(native_factory));
  talk_base::scoped_refptr<LocalMediaStreamInterface> stream(
      factory->CreateLocalMediaStream(JavaToStdString(jni, label)));
  return (jlong)stream.release();
}

JOW(jlong, PeerConnectionFactory_nativeCreateVideoSource)(
    JNIEnv* jni, jclass, jlong native_factory, jlong native_capturer,
    jobject j_constraints) {
  talk_base::scoped_ptr<ConstraintsWrapper> constraints(
      new ConstraintsWrapper(jni, j_constraints));
  talk_base::scoped_refptr<PeerConnectionFactoryInterface> factory(
      reinterpret_cast<PeerConnectionFactoryInterface*>(native_factory));
  talk_base::scoped_refptr<VideoSourceInterface> source(
      factory->CreateVideoSource(
          reinterpret_cast<cricket::VideoCapturer*>(native_capturer),
          constraints.get()));
  return (jlong)source.release();
}

JOW(jlong, PeerConnectionFactory_nativeCreateVideoTrack)(
    JNIEnv* jni, jclass, jlong native_factory, jstring id,
    jlong native_source) {
  talk_base::scoped_refptr<PeerConnectionFactoryInterface> factory(
      reinterpret_cast<PeerConnectionFactoryInterface*>(native_factory));
  talk_base::scoped_refptr<VideoTrackInterface> track(
      factory->CreateVideoTrack(
          JavaToStdString(jni, id),
          reinterpret_cast<VideoSourceInterface*>(native_source)));
  return (jlong)track.release();
}

JOW(jlong, PeerConnectionFactory_nativeCreateAudioTrack)(
    JNIEnv* jni, jclass, jlong native_factory, jstring id) {
  talk_base::scoped_refptr<PeerConnectionFactoryInterface> factory(
      reinterpret_cast<PeerConnectionFactoryInterface*>(native_factory));
  talk_base::scoped_refptr<AudioTrackInterface> track(
      factory->CreateAudioTrack(JavaToStdString(jni, id), NULL));
  return (jlong)track.release();
}

static void JavaIceServersToJsepIceServers(
    JNIEnv* jni, jobject j_ice_servers,
    PeerConnectionInterface::IceServers* ice_servers) {
  jclass list_class = GetObjectClass(jni, j_ice_servers);
  jmethodID iterator_id = GetMethodID(
      jni, list_class, "iterator", "()Ljava/util/Iterator;");
  jobject iterator = jni->CallObjectMethod(j_ice_servers, iterator_id);
  CHECK_EXCEPTION(jni, "error during CallObjectMethod");
  jmethodID iterator_has_next = GetMethodID(
      jni, GetObjectClass(jni, iterator), "hasNext", "()Z");
  jmethodID iterator_next = GetMethodID(
      jni, GetObjectClass(jni, iterator), "next", "()Ljava/lang/Object;");
  while (jni->CallBooleanMethod(iterator, iterator_has_next)) {
    CHECK_EXCEPTION(jni, "error during CallBooleanMethod");
    jobject j_ice_server = jni->CallObjectMethod(iterator, iterator_next);
    CHECK_EXCEPTION(jni, "error during CallObjectMethod");
    jclass j_ice_server_class = GetObjectClass(jni, j_ice_server);
    jfieldID j_ice_server_uri_id =
        GetFieldID(jni, j_ice_server_class, "uri", "Ljava/lang/String;");
    jfieldID j_ice_server_password_id =
        GetFieldID(jni, j_ice_server_class, "password", "Ljava/lang/String;");
    jstring uri = reinterpret_cast<jstring>(
        GetObjectField(jni, j_ice_server, j_ice_server_uri_id));
    jstring password = reinterpret_cast<jstring>(
        GetObjectField(jni, j_ice_server, j_ice_server_password_id));
    PeerConnectionInterface::IceServer server;
    server.uri = JavaToStdString(jni, uri);
    server.password = JavaToStdString(jni, password);
    ice_servers->push_back(server);
  }
  CHECK_EXCEPTION(jni, "error during CallBooleanMethod");
}

JOW(jlong, PeerConnectionFactory_nativeCreatePeerConnection)(
    JNIEnv *jni, jclass, jlong factory, jobject j_ice_servers,
    jobject j_constraints, jlong observer_p) {
  talk_base::scoped_refptr<PeerConnectionFactoryInterface> f(
      reinterpret_cast<PeerConnectionFactoryInterface*>(factory));
  PeerConnectionInterface::IceServers servers;
  JavaIceServersToJsepIceServers(jni, j_ice_servers, &servers);
  PCOJava* observer = reinterpret_cast<PCOJava*>(observer_p);
  observer->SetConstraints(new ConstraintsWrapper(jni, j_constraints));
  talk_base::scoped_refptr<PeerConnectionInterface> pc(f->CreatePeerConnection(
      servers, observer->constraints(), observer));
  return (jlong)pc.release();
}

static talk_base::scoped_refptr<PeerConnectionInterface> ExtractNativePC(
    JNIEnv* jni, jobject j_pc) {
  jfieldID native_pc_id = GetFieldID(jni,
      GetObjectClass(jni, j_pc), "nativePeerConnection", "J");
  jlong j_p = GetLongField(jni, j_pc, native_pc_id);
  return talk_base::scoped_refptr<PeerConnectionInterface>(
      reinterpret_cast<PeerConnectionInterface*>(j_p));
}

JOW(jobject, PeerConnection_getLocalDescription)(JNIEnv* jni, jobject j_pc) {
  return JavaSdpFromNativeSdp(
      jni, ExtractNativePC(jni, j_pc)->local_description());
}

JOW(jobject, PeerConnection_getRemoteDescription)(JNIEnv* jni, jobject j_pc) {
  return JavaSdpFromNativeSdp(
      jni, ExtractNativePC(jni, j_pc)->remote_description());
}

JOW(void, PeerConnection_createOffer)(
    JNIEnv* jni, jobject j_pc, jobject j_observer, jobject j_constraints) {
  ConstraintsWrapper* constraints =
      new ConstraintsWrapper(jni, j_constraints);
  talk_base::scoped_refptr<CreateSdpObserverWrapper> observer(
      new talk_base::RefCountedObject<CreateSdpObserverWrapper>(
          jni, j_observer, constraints));
  ExtractNativePC(jni, j_pc)->CreateOffer(observer, constraints);
}

JOW(void, PeerConnection_createAnswer)(
    JNIEnv* jni, jobject j_pc, jobject j_observer, jobject j_constraints) {
  ConstraintsWrapper* constraints =
      new ConstraintsWrapper(jni, j_constraints);
  talk_base::scoped_refptr<CreateSdpObserverWrapper> observer(
      new talk_base::RefCountedObject<CreateSdpObserverWrapper>(
          jni, j_observer, constraints));
  ExtractNativePC(jni, j_pc)->CreateAnswer(observer, constraints);
}

// Helper to create a SessionDescriptionInterface from a SessionDescription.
static SessionDescriptionInterface* JavaSdpToNativeSdp(
    JNIEnv* jni, jobject j_sdp) {
  jfieldID j_type_id = GetFieldID(
      jni, GetObjectClass(jni, j_sdp), "type",
      "Lorg/webrtc/SessionDescription$Type;");
  jobject j_type = GetObjectField(jni, j_sdp, j_type_id);
  jmethodID j_canonical_form_id = GetMethodID(
      jni, GetObjectClass(jni, j_type), "canonicalForm",
      "()Ljava/lang/String;");
  jstring j_type_string = (jstring)jni->CallObjectMethod(
      j_type, j_canonical_form_id);
  CHECK_EXCEPTION(jni, "error during CallObjectMethod");
  std::string std_type = JavaToStdString(jni, j_type_string);

  jfieldID j_description_id = GetFieldID(
      jni, GetObjectClass(jni, j_sdp), "description", "Ljava/lang/String;");
  jstring j_description = (jstring)GetObjectField(jni, j_sdp, j_description_id);
  std::string std_description = JavaToStdString(jni, j_description);

  return webrtc::CreateSessionDescription(
      std_type, std_description, NULL);
}

JOW(void, PeerConnection_setLocalDescription)(
    JNIEnv* jni, jobject j_pc,
    jobject j_observer, jobject j_sdp) {
  talk_base::scoped_refptr<SetSdpObserverWrapper> observer(
      new talk_base::RefCountedObject<SetSdpObserverWrapper>(
          jni, j_observer, reinterpret_cast<ConstraintsWrapper*>(NULL)));
  ExtractNativePC(jni, j_pc)->SetLocalDescription(
      observer, JavaSdpToNativeSdp(jni, j_sdp));
}

JOW(void, PeerConnection_setRemoteDescription)(
    JNIEnv* jni, jobject j_pc,
    jobject j_observer, jobject j_sdp) {
  talk_base::scoped_refptr<SetSdpObserverWrapper> observer(
      new talk_base::RefCountedObject<SetSdpObserverWrapper>(
          jni, j_observer, reinterpret_cast<ConstraintsWrapper*>(NULL)));
  ExtractNativePC(jni, j_pc)->SetRemoteDescription(
      observer, JavaSdpToNativeSdp(jni, j_sdp));
}

JOW(jboolean, PeerConnection_updateIce)(
    JNIEnv* jni, jobject j_pc, jobject j_ice_servers, jobject j_constraints) {
  PeerConnectionInterface::IceServers ice_servers;
  JavaIceServersToJsepIceServers(jni, j_ice_servers, &ice_servers);
  talk_base::scoped_ptr<ConstraintsWrapper> constraints(
      new ConstraintsWrapper(jni, j_constraints));
  CHECK(ExtractNativePC(jni, j_pc)->UpdateIce(
      ice_servers, constraints.get()), "");
}

JOW(jboolean, PeerConnection_nativeAddIceCandidate)(
    JNIEnv* jni, jobject j_pc, jstring j_sdp_mid,
    jint j_sdp_mline_index, jstring j_candidate_sdp) {
  std::string sdp_mid = JavaToStdString(jni, j_sdp_mid);
  std::string sdp = JavaToStdString(jni, j_candidate_sdp);
  talk_base::scoped_ptr<IceCandidateInterface> candidate(
      webrtc::CreateIceCandidate(sdp_mid, j_sdp_mline_index, sdp, NULL));
  CHECK(ExtractNativePC(jni, j_pc)->AddIceCandidate(candidate.get()), "");
}

JOW(jboolean, PeerConnection_nativeAddLocalStream)(
    JNIEnv* jni, jobject j_pc, jlong native_stream, jobject j_constraints) {
  talk_base::scoped_ptr<ConstraintsWrapper> constraints(
      new ConstraintsWrapper(jni, j_constraints));
  return ExtractNativePC(jni, j_pc)->AddStream(
      reinterpret_cast<MediaStreamInterface*>(native_stream),
      constraints.get());
}

JOW(void, PeerConnection_nativeRemoveLocalStream)(
    JNIEnv* jni, jobject j_pc, jlong native_stream) {
  ExtractNativePC(jni, j_pc)->RemoveStream(
      reinterpret_cast<MediaStreamInterface*>(native_stream));
}

JOW(jobject, PeerConnection_signalingState)(JNIEnv* jni, jobject j_pc) {
  PeerConnectionInterface::SignalingState state =
      ExtractNativePC(jni, j_pc)->signaling_state();
  return JavaEnumFromIndex(jni, "PeerConnection$SignalingState", state);
}

JOW(jobject, PeerConnection_iceState)(JNIEnv* jni, jobject j_pc) {
  PeerConnectionInterface::IceState state =
      ExtractNativePC(jni, j_pc)->ice_state();
  return JavaEnumFromIndex(jni, "PeerConnection$IceState", state);
}

JOW(jobject, MediaSource_nativeState)(JNIEnv* jni, jclass, jlong j_p) {
  talk_base::scoped_refptr<MediaSourceInterface> p(
      reinterpret_cast<MediaSourceInterface*>(j_p));
  return JavaEnumFromIndex(jni, "MediaSource$State", p->state());
}

JOW(jlong, VideoCapturer_nativeCreateVideoCapturer)(
    JNIEnv* jni, jclass, jstring j_device_name) {
  std::string device_name = JavaToStdString(jni, j_device_name);
  talk_base::scoped_ptr<cricket::DeviceManagerInterface> device_manager(
      cricket::DeviceManagerFactory::Create());
  CHECK(device_manager->Init(), "DeviceManager::Init() failed");
  cricket::Device device;
  if (!device_manager->GetVideoCaptureDevice(device_name, &device))
    return 0;
  talk_base::scoped_ptr<cricket::VideoCapturer> capturer(
      device_manager->CreateVideoCapturer(device));
  return (jlong)capturer.release();
}

JOW(jlong, VideoRenderer_nativeCreateVideoRenderer)(
    JNIEnv* jni, jclass, int x, int y) {
  talk_base::scoped_ptr<VideoRendererWrapper> renderer(new VideoRendererWrapper(
      cricket::VideoRendererFactory::CreateGuiVideoRenderer(x, y)));
  return (jlong)renderer.release();
}

JOW(jstring, MediaStreamTrack_nativeId)(JNIEnv* jni, jclass, jlong j_p) {
  talk_base::scoped_refptr<MediaStreamTrackInterface> p(
      reinterpret_cast<MediaStreamTrackInterface*>(j_p));
  return JavaStringFromStdString(jni, p->id());
}

JOW(jstring, MediaStreamTrack_nativeKind)(JNIEnv* jni, jclass, jlong j_p) {
  talk_base::scoped_refptr<MediaStreamTrackInterface> p(
      reinterpret_cast<MediaStreamTrackInterface*>(j_p));
  return JavaStringFromStdString(jni, p->kind());
}

JOW(jboolean, MediaStreamTrack_nativeEnabled)(JNIEnv* jni, jclass, jlong j_p) {
  talk_base::scoped_refptr<MediaStreamTrackInterface> p(
      reinterpret_cast<MediaStreamTrackInterface*>(j_p));
  return p->enabled();
}

JOW(jobject, MediaStreamTrack_nativeState)(JNIEnv* jni, jclass, jlong j_p) {
  talk_base::scoped_refptr<MediaStreamTrackInterface> p(
      reinterpret_cast<MediaStreamTrackInterface*>(j_p));
  return JavaEnumFromIndex(jni, "MediaStreamTrack$State", p->state());
}

JOW(jboolean, MediaStreamTrack_nativeSetState)(
    JNIEnv* jni, jclass, jlong j_p, jint j_new_state) {
  talk_base::scoped_refptr<MediaStreamTrackInterface> p(
      reinterpret_cast<MediaStreamTrackInterface*>(j_p));
  MediaStreamTrackInterface::TrackState new_state =
      (MediaStreamTrackInterface::TrackState)j_new_state;
  return p->set_state(new_state);
}

JOW(jboolean, MediaStreamTrack_nativeSetEnabled)(
    JNIEnv* jni, jclass, jlong j_p, jboolean enabled) {
  talk_base::scoped_refptr<MediaStreamTrackInterface> p(
      reinterpret_cast<MediaStreamTrackInterface*>(j_p));
  return p->set_enabled(enabled);
}

JOW(jboolean, LocalMediaStream_nativeAddAudioTrack)(
    JNIEnv* jni, jclass, jlong pointer, jlong j_audio_track_pointer) {
  talk_base::scoped_refptr<LocalMediaStreamInterface> stream(
      reinterpret_cast<LocalMediaStreamInterface*>(pointer));
  talk_base::scoped_refptr<AudioTrackInterface> track(
      reinterpret_cast<AudioTrackInterface*>(j_audio_track_pointer));
  return stream->AddTrack(track);
}

JOW(jboolean, LocalMediaStream_nativeAddVideoTrack)(
    JNIEnv* jni, jclass, jlong pointer, jlong j_video_track_pointer) {
  talk_base::scoped_refptr<LocalMediaStreamInterface> stream(
      reinterpret_cast<LocalMediaStreamInterface*>(pointer));
  talk_base::scoped_refptr<VideoTrackInterface> track(
      reinterpret_cast<VideoTrackInterface*>(j_video_track_pointer));
  return stream->AddTrack(track);
}

JOW(void, VideoTrack_nativeAddRenderer)(
    JNIEnv* jni, jclass,
    jlong j_video_track_pointer, jlong j_renderer_pointer) {
  talk_base::scoped_refptr<VideoTrackInterface> track(
      reinterpret_cast<VideoTrackInterface*>(j_video_track_pointer));
  track->AddRenderer(
      reinterpret_cast<VideoRendererInterface*>(j_renderer_pointer));
}

JOW(void, VideoTrack_nativeRemoveRenderer)(
    JNIEnv* jni, jclass,
    jlong j_video_track_pointer, jlong j_renderer_pointer) {
  talk_base::scoped_refptr<VideoTrackInterface> track(
      reinterpret_cast<VideoTrackInterface*>(j_video_track_pointer));
  track->RemoveRenderer(
      reinterpret_cast<VideoRendererInterface*>(j_renderer_pointer));
}
