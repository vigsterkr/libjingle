/*
 * libjingle
 * Copyright 2004--2005, Google Inc.
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

#ifndef TALK_BASE_MESSAGEQUEUE_H__
#define TALK_BASE_MESSAGEQUEUE_H__

#include <vector>
#include <queue>
#include <algorithm>
#include "talk/base/basictypes.h"
#include "talk/base/criticalsection.h"
#include "talk/base/socketserver.h"
#include "talk/base/time.h"

namespace talk_base {

struct Message;
class MessageQueue;
class MessageHandler;

// MessageQueueManager does cleanup of of message queues

class MessageQueueManager {
public:
  static MessageQueueManager* Instance();

  void Add(MessageQueue *message_queue);
  void Remove(MessageQueue *message_queue);
  void Clear(MessageHandler *handler);

private:
  MessageQueueManager();
  ~MessageQueueManager();

  static MessageQueueManager* instance_;
  // This list contains 'active' MessageQueues.
  std::vector<MessageQueue *> message_queues_;
  CriticalSection crit_;
};

// Messages get dispatched to a MessageHandler

class MessageHandler {
public:
  virtual ~MessageHandler() {
    MessageQueueManager::Instance()->Clear(this);
  }

  virtual void OnMessage(Message *pmsg) = 0;
};

// Derive from this for specialized data
// App manages lifetime, except when messages are purged

class MessageData {
public:
  MessageData() {}
  virtual ~MessageData() {}
};

template <class T>
class TypedMessageData : public MessageData {
public:
  TypedMessageData(const T& data) : data_(data) { }
  const T& data() const { return data_; }
  T& data() { return data_; }
private:
  T data_;
};

template<class T>
inline MessageData* WrapMessageData(const T& data) {
  return new TypedMessageData<T>(data);
}

template<class T>
inline const T& UseMessageData(MessageData* data) {
  return static_cast< TypedMessageData<T>* >(data)->data();
}

template<class T>
class DisposeData : public MessageData {
public:
  DisposeData(T* data) : data_(data) { }
  virtual ~DisposeData() { delete data_; }
private:
  T* data_;
};

const uint32 MQID_ANY = static_cast<uint32>(-1);
const uint32 MQID_DISPOSE = static_cast<uint32>(-2);

// No destructor

struct Message {
  Message() {
    memset(this, 0, sizeof(*this));
  }
  MessageHandler *phandler;
  uint32 message_id;
  MessageData *pdata;
  uint32 ts_sensitive;
};

// DelayedMessage goes into a priority queue, sorted by trigger time

class DelayedMessage {
public:
  DelayedMessage(int cmsDelay, Message *pmsg) {
    cmsDelay_ = cmsDelay;
    msTrigger_ = GetMillisecondCount() + cmsDelay;
    msg_ = *pmsg;
  }

  bool operator< (const DelayedMessage& dmsg) const {
    return dmsg.msTrigger_ < msTrigger_;
  }

  int cmsDelay_; // for debugging
  uint32 msTrigger_;
  Message msg_;
};

class MessageQueue {
public:
  MessageQueue(SocketServer* ss = 0);
  virtual ~MessageQueue();

  SocketServer* socketserver() { return ss_; }
  void set_socketserver(SocketServer* ss);

  // Note: The behavior of MessageQueue has changed.  When a MQ is stopped,
  // futher Posts and Sends will fail.  However, any pending Sends and *ready*
  // Posts (as opposed to unexpired delayed Posts) will be delivered before
  // Get (or Peek) returns false.  By guaranteeing delivery of those messages,
  // we eliminate the race condition when an MessageHandler and MessageQueue
  // may be destroyed independently of each other.

  virtual void Stop();
  virtual bool IsStopping();
  virtual void Restart();

  // Get() will process I/O until:
  //  1) A message is available (returns true)
  //  2) cmsWait seconds have elapsed (returns false)
  //  3) Stop() is called (returns false)
  virtual bool Get(Message *pmsg, int cmsWait = kForever);
  virtual bool Peek(Message *pmsg, int cmsWait = 0);
  virtual void Post(MessageHandler *phandler, uint32 id = 0,
      MessageData *pdata = NULL, bool time_sensitive = false);
  virtual void PostDelayed(int cmsDelay, MessageHandler *phandler,
      uint32 id = 0, MessageData *pdata = NULL);
  virtual void Clear(MessageHandler *phandler, uint32 id = MQID_ANY);
  virtual void Dispatch(Message *pmsg);
  virtual void ReceiveSends();
  virtual int GetDelay();

  // Internally posts a message which causes the doomed object to be deleted
  template<class T> void Dispose(T* doomed) {
    if (doomed) {
      Post(NULL, MQID_DISPOSE, new talk_base::DisposeData<T>(doomed));
    }
  }

protected:
  void EnsureActive();

  SocketServer* ss_;
  bool new_ss;
  bool fStop_;
  bool fPeekKeep_;
  Message msgPeek_;
  // A message queue is active if it has ever had a message posted to it.
  // This also corresponds to being in MessageQueueManager's global list.
  bool active_;
  std::queue<Message> msgq_;
  std::priority_queue<DelayedMessage> dmsgq_;
  CriticalSection crit_;
};

} // namespace talk_base

#endif // TALK_BASE_MESSAGEQUEUE_H__
