#ifndef _SIGNALTHREAD_H_
#define _SIGNALTHREAD_H_

#include "talk/base/thread.h"
#include "talk/base/sigslot.h"

namespace talk_base {

///////////////////////////////////////////////////////////////////////////////
// SignalThread - Base class for worker threads.  The main thread should call
//  Start() to begin work, and then follow one of these models:
//   Normal: Wait for SignalWorkDone, and then call Release to destroy.
//   Cancellation: Call Release(true), to abort the worker thread.
//   Fire-and-forget: Call Release(false), which allows the thread to run to
//    completion, and then self-destruct without further notification.
//  The subclass should override DoWork() to perform the background task.  By
//   periodically calling ContinueWork(), it can check for cancellation.
//   OnWorkStart and OnWorkDone can be overridden to do pre- or post-work
//   tasks in the context of the main thread.
///////////////////////////////////////////////////////////////////////////////

class SignalThread : protected MessageHandler {
public:
  SignalThread();

  // Context: Main Thread.  Call before Start to change the worker's priority.
  void SetPriority(ThreadPriority priority);

  // Context: Main Thread.  Call to begin the worker thread.
  void Start();

  // Context: Main Thread.  If the worker thread is not running, deletes the
  // object immediately.  Otherwise, asks the worker thread to abort processing,
  // and schedules the object to be deleted once the worker exits.
  // SignalWorkDone will not be signalled.
  void Destroy();

  // Context: Main Thread.  If the worker thread is complete, deletes the
  // object immediately.  Otherwise, schedules the object to be deleted once
  // the worker thread completes.  SignalWorkDone will be signalled.
  void Release();

  // Context: Main Thread.  Signalled when work is complete.
  sigslot::signal1<SignalThread *> SignalWorkDone;

  enum { ST_MSG_WORKER_DONE, ST_MSG_FIRST_AVAILABLE };

protected:
  virtual ~SignalThread();

  // Context: Main Thread.  Subclass should override to do pre-work setup.
  virtual void OnWorkStart() { }
  
  // Context: Worker Thread.  Subclass should override to do work.
  virtual void DoWork() = 0;

  // Context: Worker Thread.  Subclass should call periodically to
  // dispatch messages and determine if the thread should terminate.
  bool ContinueWork();

  // Context: Worker Thread.  Subclass should override when extra work is
  // needed to abort the worker thread.
  virtual void OnWorkStop() { }

  // Context: Main Thread.  Subclass should override to do post-work cleanup.
  virtual void OnWorkDone() { }
  
  // Context: Any Thread.  If subclass overrides, be sure to call the base
  // implementation.  Do not use (message_id < ST_MSG_FIRST_AVAILABLE)
  virtual void OnMessage(Message *msg);

private:
  friend class Worker;
  class Worker : public Thread {
  public:
    SignalThread* parent_;
    virtual void Run() { parent_->Run(); }
  };

  void Run();

  Thread* main_;
  Worker worker_;
  enum State { kInit, kRunning, kComplete, kStopping, kReleasing } state_;
};

///////////////////////////////////////////////////////////////////////////////

}  // namespace talk_base

#endif  // _SIGNALTHREAD_H_
