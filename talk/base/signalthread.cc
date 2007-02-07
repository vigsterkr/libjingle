#include "talk/base/common.h"
#include "talk/base/signalthread.h"

using namespace talk_base;

///////////////////////////////////////////////////////////////////////////////
// SignalThread
///////////////////////////////////////////////////////////////////////////////

SignalThread::SignalThread()
: main_(Thread::Current()), state_(kInit)
{
  worker_.parent_ = this;
}

SignalThread::~SignalThread() {
}

void SignalThread::SetPriority(ThreadPriority priority) {
  ASSERT(main_->IsCurrent());
  ASSERT(kInit == state_);
  worker_.SetPriority(priority);
}

void SignalThread::Start() {
  ASSERT(main_->IsCurrent());
  if (kInit == state_) {
    state_ = kRunning;
    OnWorkStart();
    worker_.Start();
  } else {
    ASSERT(false);
  }
}

void SignalThread::Destroy() {
  ASSERT(main_->IsCurrent());
  if ((kInit == state_) || (kComplete == state_)) {
    delete this;
  } else if (kRunning == state_) {
    state_ = kStopping;
    // A couple tricky issues here:
    // 1) Thread::Stop() calls Join(), which we don't want... we just want
    //    to stop the MessageQueue, which causes ContinueWork() to return false.
    // 2) OnWorkStop() must follow Stop(), so that when the thread wakes up
    //    due to OWS(), ContinueWork() will return false.
    worker_.MessageQueue::Stop();
    OnWorkStop();
  } else {
    ASSERT(false);
  }
}

void SignalThread::Release() {
  ASSERT(main_->IsCurrent());
  if (kComplete == state_) {
    delete this;
  } else if (kRunning == state_) {
    state_ = kReleasing;
  } else {
    // if (kInit == state_) use Destroy()
    ASSERT(false);
  }
}

bool SignalThread::ContinueWork() {
  ASSERT(worker_.IsCurrent());
  return worker_.ProcessMessages(0);
}

void SignalThread::OnMessage(Message *msg) {
  if (ST_MSG_WORKER_DONE == msg->message_id) {
    ASSERT(main_->IsCurrent());
    OnWorkDone();
    bool do_delete = false;
    if (kRunning == state_) {
      state_ = kComplete;
    } else {
      do_delete = true;
    }
    if (kStopping != state_) {
      SignalWorkDone(this);
    }
    if (do_delete) {
      delete this;
    }
  }
}

void SignalThread::Run() {
  DoWork();
  main_->Post(this, ST_MSG_WORKER_DONE);
}
