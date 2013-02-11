/*
 * libjingle
 * Copyright 2012, Google Inc.
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
#include "talk/app/webrtc/datachannel.h"

#include <string>

#include "talk/app/webrtc/webrtcsession.h"
#include "talk/base/logging.h"
#include "talk/base/refcount.h"

namespace webrtc {

static size_t kMaxQueuedDataPackets = 100;

talk_base::scoped_refptr<DataChannel> DataChannel::Create(
    WebRtcSession* session,
    const std::string& label,
    const DataChannelInit* config) {
  talk_base::scoped_refptr<DataChannel> channel(
      new talk_base::RefCountedObject<DataChannel>(session, label));
  if (!channel->Init(config)) {
    return NULL;
  }
  return channel;
}

DataChannel::DataChannel(WebRtcSession* session, const std::string& label)
    : label_(label),
      observer_(NULL),
      state_(kConnecting),
      was_ever_writable_(false),
      session_(session),
      data_session_(NULL),
      send_ssrc_set_(false),
      send_ssrc_(0),
      receive_ssrc_set_(false),
      receive_ssrc_(0) {
}

bool DataChannel::Init(const DataChannelInit* config) {
  if (config && config->reliable) {
    LOG(LS_ERROR) << "reliable data channels are not implemented";
    return false;
  }
  return true;
}

DataChannel::~DataChannel() {
  ClearQueuedData();
}

void DataChannel::RegisterObserver(DataChannelObserver* observer) {
  observer_ = observer;
  DeliverQueuedData();
}

void DataChannel::UnregisterObserver() {
  observer_ = NULL;
}

bool DataChannel::reliable() const {
  return false;
}

uint64 DataChannel::buffered_amount() const {
  return 0;
}

void DataChannel::Close() {
  if (state_ == kClosed)
    return;
  send_ssrc_ = 0;
  send_ssrc_set_ = false;
  SetState(kClosing);
  UpdateState();
}

bool DataChannel::Send(const DataBuffer& buffer) {
  if (state_ != kOpen) {
    return false;
  }

  // TODO(perkj): Implement signaling of binary data once RtpDataEngine has
  // support for marking text vs. binary.
  if (buffer.binary) {
    LOG(LS_ERROR) << "SendBuffer: Sending of binary data is not implemented";
    return false;
  }
  cricket::SendDataParams send_params;
  send_params.ssrc = send_ssrc_;
  send_buffer_.assign(buffer.data.data(), buffer.data.length());
  return session_->data_channel()->SendData(send_params, send_buffer_);
}

void DataChannel::SetReceiveSsrc(uint32 receive_ssrc) {
  receive_ssrc_ = receive_ssrc;
  receive_ssrc_set_ = true;
  UpdateState();
}

// The remote peer request that this channel shall be closed.
void DataChannel::RemotePeerRequestClose() {
  receive_ssrc_set_ = false;
  send_ssrc_set_ = false;
  SetState(kClosing);
  UpdateState();
}

void DataChannel::SetSendSsrc(uint32 send_ssrc) {
  send_ssrc_ = send_ssrc;
  send_ssrc_set_ = true;
  UpdateState();
}

void DataChannel::UpdateState() {
  switch (state_) {
    case kConnecting: {
      if (HasNegotiationCompleted()) {
        if (!IsConnectedToDataSession()) {
          ConnectToDataSession();
        }
        if (was_ever_writable_) {
          SetState(kOpen);
          // If we have received buffers before the channel got writable.
          // Deliver them now.
          DeliverQueuedData();
        }
      }
      break;
    }
    case kOpen: {
      break;
    }
    case kClosing: {
      if (IsConnectedToDataSession()) {
        DisconnectFromDataSession();
      }
      if (HasNegotiationCompleted()) {
        SetState(kClosed);
      }
      break;
    }
    case kClosed:
      break;
  }
}

void DataChannel::SetState(DataState state) {
  state_ = state;
  if (observer_) {
    observer_->OnStateChange();
  }
}

void DataChannel::ConnectToDataSession() {
  ASSERT(session_->data_channel() != NULL);
  if (!session_->data_channel()) {
    LOG(LS_ERROR) << "The DataEngine does not exist.";
    return;
  }

  data_session_ = session_->data_channel();
  data_session_->SignalReadyToSendData.connect(this,
                                               &DataChannel::OnChannelReady);
  data_session_->SignalDataReceived.connect(this, &DataChannel::OnDataReceived);
}

void DataChannel::DisconnectFromDataSession() {
  data_session_->SignalReadyToSendData.disconnect(this);
  data_session_->SignalDataReceived.disconnect(this);
  data_session_ = NULL;
}

void DataChannel::DeliverQueuedData() {
  if (was_ever_writable_ && observer_) {
    while (!queued_data_.empty()) {
      DataBuffer* buffer = queued_data_.front();
      observer_->OnMessage(*buffer);
      queued_data_.pop();
      delete buffer;
    }
  }
}

void DataChannel::ClearQueuedData() {
  while (!queued_data_.empty()) {
    DataBuffer* buffer = queued_data_.front();
    queued_data_.pop();
    delete buffer;
  }
}

void DataChannel::OnDataReceived(cricket::DataChannel* channel,
                                 const cricket::ReceiveDataParams& params,
                                 const::std::string& data) {
  if (params.ssrc == receive_ssrc_) {
    talk_base::scoped_ptr<DataBuffer> buffer(new DataBuffer);
    buffer->data.SetData(data.c_str(), data.length());
    if (was_ever_writable_ && observer_) {
      observer_->OnMessage(*buffer.get());
    } else {
      if (queued_data_.size() > kMaxQueuedDataPackets) {
        ClearQueuedData();
      }
      queued_data_.push(buffer.release());
    }
  }
}

void DataChannel::OnChannelReady(bool writable) {
  if (!was_ever_writable_ && writable) {
    was_ever_writable_ = true;
    UpdateState();
  }
}

}  // namespace webrtc
