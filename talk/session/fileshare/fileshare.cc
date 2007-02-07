/*
 * libjingle
 * Copyright 2004--2006, Google Inc.
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

#include "talk/session/fileshare/fileshare.h"

#include "talk/base/httpcommon-inl.h"

#include "talk/base/fileutils.h"
#include "talk/base/streamutils.h"
#include "talk/base/event.h"
#include "talk/base/helpers.h"
#include "talk/base/httpclient.h"
#include "talk/base/httpserver.h"
#include "talk/base/pathutils.h"
#include "talk/base/socketstream.h"
#include "talk/base/stringdigest.h"
#include "talk/base/stringencode.h"
#include "talk/base/stringutils.h"
#include "talk/base/tarstream.h"
#include "talk/base/thread.h"
#include "talk/session/tunnel/pseudotcpchannel.h"
#include "talk/session/tunnel/tunnelsessionclient.h"

///////////////////////////////////////////////////////////////////////////////
// <description xmlns="http://www.google.com/session/share">
//   <manifest>
//     <file size='341'>
//       <name>foo.txt</name>
//     </file>
//     <file size='51321'>
//       <name>foo.jpg</name>
//       <image width='480' height='320'/>
//     </file>
//     <folder>
//       <name>stuff</name>
//     </folder>
//   </manifest>
//   <protocol>
//     <http>
//       <url name='source-path'>/temporary/23A53F01/</url>
//       <url name='preview-path'>/temporary/90266EA1/</url>
//     </http>
//     <raw/>
//   </protocol>
// </description>
// <p:transport xmns:p="p2p"/>
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Constants and private functions
///////////////////////////////////////////////////////////////////////////////

const std::string NS_GOOGLE_SHARE("http://www.google.com/session/share");

namespace {

const buzz::QName QN_SHARE_DESCRIPTION(true, NS_GOOGLE_SHARE, "description");
const buzz::QName QN_SHARE_MANIFEST(true, NS_GOOGLE_SHARE, "manifest");
const buzz::QName QN_SHARE_FOLDER(true, NS_GOOGLE_SHARE, "folder");
const buzz::QName QN_SHARE_FILE(true, NS_GOOGLE_SHARE, "file");
const buzz::QName QN_SHARE_NAME(true, NS_GOOGLE_SHARE, "name");
const buzz::QName QN_SHARE_IMAGE(true, NS_GOOGLE_SHARE, "image");
const buzz::QName QN_SHARE_PROTOCOL(true, NS_GOOGLE_SHARE, "protocol");
const buzz::QName QN_SHARE_HTTP(true, NS_GOOGLE_SHARE, "http");
const buzz::QName QN_SHARE_URL(true, NS_GOOGLE_SHARE, "url");
const buzz::QName QN_SHARE_CHANNEL(true, NS_GOOGLE_SHARE, "channel");
const buzz::QName QN_SHARE_COMPLETE(true, NS_GOOGLE_SHARE, "complete");

const buzz::QName QN_SIZE(true, buzz::STR_EMPTY, "size");
const buzz::QName QN_WIDTH(true, buzz::STR_EMPTY, "width");
const buzz::QName QN_HEIGHT(true, buzz::STR_EMPTY, "height");

const std::string kHttpSourcePath("source-path");
const std::string kHttpPreviewPath("preview-path");

const size_t kMinImageSize = 16U;
const size_t kMaxImageSize = 0x8000U; // (32k)
const size_t kMaxPreviewSize = 1024;
// Wait 10 seconds to see if any new proxies get established
const uint32 kProxyWait = 10000; 

const int MSG_RETRY = 1;
const uint32 kFileTransferEnableRetryMs = 1000 * 60 * 4; // 4 minutes

const std::string MIME_OCTET_STREAM("application/octet-stream");

enum {
  MSG_PROXY_WAIT,
};

bool AllowedImageDimensions(size_t width, size_t height) {
  return (width >= kMinImageSize) && (width <= kMaxImageSize)
      && (height >= kMinImageSize) && (height <= kMaxImageSize);
}

}  // anon namespace

namespace cricket {

///////////////////////////////////////////////////////////////////////////////
// FileShareManifest
///////////////////////////////////////////////////////////////////////////////

void
FileShareManifest::AddFile(const std::string& name, size_t size) {
  Item i = { T_FILE, name, size };
  items_.push_back(i);
}

void
FileShareManifest::AddImage(const std::string& name, size_t size,
                            size_t width, size_t height) {
  Item i = { T_IMAGE, name, size, width, height };
  items_.push_back(i);
}

void
FileShareManifest::AddFolder(const std::string& name, size_t size) {
  Item i = { T_FOLDER, name, size };
  items_.push_back(i);
}

size_t
FileShareManifest::GetItemCount(Type t) const {
  size_t count = 0;
  for (size_t i=0; i<items_.size(); ++i) {
    if (items_[i].type == t)
      ++count;
  }
  return count;
}

///////////////////////////////////////////////////////////////////////////////
// FileShareSession
///////////////////////////////////////////////////////////////////////////////

FileShareSession::FileShareSession(cricket::Session* session, const std::string &user_agent)
  : session_(session), state_(FS_NONE),
    is_closed_(false),
    is_sender_(false), manifest_(NULL), pool_(this), http_client_(NULL), 
    http_server_(NULL), 
    transfer_connection_id_(talk_base::HTTP_INVALID_CONNECTION_ID),
    counter_(NULL), item_transferring_(0), bytes_transferred_(0),
    local_cancel_(false), local_listener_(NULL), remote_listener_(NULL),
    next_channel_id_(1), user_agent_(user_agent) {
  session_->SignalState.connect(this, &FileShareSession::OnSessionState);
  session_->SignalInfoMessage.connect(this,
    &FileShareSession::OnSessionInfoMessage);
  session_->SignalChannelGone.connect(this,
    &FileShareSession::OnSessionChannelGone);
}

FileShareSession::~FileShareSession() {
  ASSERT(FS_NONE != state_);
  // If we haven't closed, do cleanup now.
  if (!IsClosed()) {
    if (!IsComplete()) {
      state_ = FS_FAILURE;
    }
    DoClose(true);
  }
  if (session_) {
    // Make sure we don't get future state changes on this session.
    session_->SignalState.disconnect(this);
    session_->SignalInfoMessage.disconnect(this);
    session_ = NULL;
  }
  
  for (TransactionList::const_iterator trans_it = transactions_.begin();
       trans_it != transactions_.end(); ++trans_it) {
    (*trans_it)->response()->set_error(talk_base::HC_NOT_FOUND);
    http_server_->Respond(*trans_it);
  }
  
  delete http_client_;
  delete http_server_;
  delete manifest_;
  delete local_listener_;
  delete remote_listener_;
}

bool
FileShareSession::IsComplete() const {
  return (state_ >= FS_COMPLETE);
}

bool
FileShareSession::IsClosed() const {
  return is_closed_;
}

FileShareState
FileShareSession::state() const {
  return state_;
}

bool
FileShareSession::is_sender() const {
  ASSERT(FS_NONE != state_);
  return is_sender_;
}

const buzz::Jid&
FileShareSession::jid() const {
  ASSERT(FS_NONE != state_);
  return jid_;
}

const FileShareManifest*
FileShareSession::manifest() const {
  ASSERT(FS_NONE != state_);
  return manifest_;
}

const std::string&
FileShareSession::local_folder() const {
  ASSERT(!local_folder_.empty());
  return local_folder_;
}

void
FileShareSession::Share(const buzz::Jid& jid, FileShareManifest* manifest) {
  ASSERT(FS_NONE == state_);
  ASSERT(NULL != session_);

  http_server_ = new talk_base::HttpServer;
  http_server_->SignalHttpRequest.connect(this,
    &FileShareSession::OnHttpRequest);
  http_server_->SignalHttpRequestComplete.connect(this,
    &FileShareSession::OnHttpRequestComplete);
  http_server_->SignalConnectionClosed.connect(this,
    &FileShareSession::OnHttpConnectionClosed);

  FileShareDescription* desc = new FileShareDescription;
  desc->supports_http = true;
  desc->manifest = *manifest;
  GenerateTemporaryPrefix(&desc->source_path);
  GenerateTemporaryPrefix(&desc->preview_path);
  session_->Initiate(jid.Str(), NULL, desc);

  delete manifest;
}

void
FileShareSession::Accept() {
  ASSERT(FS_OFFER == state_);
  ASSERT(NULL != session_);
  ASSERT(NULL != manifest_);

  ASSERT(!http_client_);
  ASSERT(item_transferring_ == 0);
  http_client_ = new talk_base::HttpClient(user_agent_,
                                           &pool_);
  http_client_->SignalHttpClientComplete.connect(this,
    &FileShareSession::OnHttpClientComplete);
  http_client_->SignalHttpClientClosed.connect(this,
    &FileShareSession::OnHttpClientClosed);

  // The receiver now has a need for the http_server_, when previewing already
  // downloaded content.
  http_server_ = new talk_base::HttpServer;
  http_server_->SignalHttpRequest.connect(this,
    &FileShareSession::OnHttpRequest);
  http_server_->SignalHttpRequestComplete.connect(this,
    &FileShareSession::OnHttpRequestComplete);
  http_server_->SignalConnectionClosed.connect(this,
    &FileShareSession::OnHttpConnectionClosed);

  FileShareDescription* desc = new FileShareDescription;
  desc->supports_http = description()->supports_http;
  session_->Accept(desc);

  SetState(FS_TRANSFER, false);
  NextDownload();
}

void
FileShareSession::Decline() {
  ASSERT(FS_OFFER == state_);
  ASSERT(NULL != session_);
  local_cancel_ = true;
  session_->Reject();
}

void
FileShareSession::Cancel() {
  ASSERT(!IsComplete());
  ASSERT(NULL != session_);
  local_cancel_ = true;
  session_->Terminate();
}

bool
FileShareSession::GetItemUrl(size_t index, std::string* url) {
  return GetItemBaseUrl(index, false, url);
}

bool FileShareSession::GetImagePreviewUrl(size_t index, size_t width,
                                          size_t height, std::string* url) {
  if (!GetItemBaseUrl(index, true, url))
    return false;

  if (FileShareManifest::T_IMAGE != manifest_->item(index).type) {
    ASSERT(false);
    return false;
  }

  char query[256];
  talk_base::sprintfn(query, ARRAY_SIZE(query), "?width=%u&height=%u",
                      width, height);
  url->append(query);
  return true;
}

void FileShareSession::ResampleComplete(talk_base::StreamInterface *i, talk_base::HttpTransaction *trans, bool success) {
  bool found = false;
  for (TransactionList::const_iterator trans_it = transactions_.begin();
       trans_it != transactions_.end(); ++trans_it) {
    if (*trans_it == trans) {
      found = true;
      break;
    }
  }
  
  if (!found)
    return;
  
  transactions_.remove(trans);
  
  if (success) {
      trans->response()->set_success(MIME_OCTET_STREAM, i);
      http_server_->Respond(trans);
   
  }
  trans->response()->set_error(talk_base::HC_NOT_FOUND);
  http_server_->Respond(trans);
}

bool FileShareSession::GetProgress(size_t& bytes) const {
  bool known = true;
  bytes = bytes_transferred_;
  if (counter_) {
    size_t current_size = manifest_->item(item_transferring_).size;
    size_t current_pos = counter_->GetByteCount();
    if (current_size == FileShareManifest::SIZE_UNKNOWN) {
      known = false;
    } else if (current_pos > current_size) {
      // Don't allow the size of a 'known' item to be reported as larger than
      // it claimed to be.
      ASSERT(false);
      current_pos = current_size;
    }
    bytes += current_pos;
  }
  return known;
}

bool FileShareSession::GetTotalSize(size_t& bytes) const {
  bool known = true;
  bytes = 0;
  for (size_t i=0; i<manifest_->size(); ++i) {
    if (manifest_->item(i).size == FileShareManifest::SIZE_UNKNOWN) {
      // We make files of unknown length worth a single byte.
      known = false;
      bytes += 1;
    } else {
      bytes += manifest_->item(i).size;
    }
  }
  return known;
}

bool FileShareSession::GetCurrentItemName(std::string* name) {
  if (FS_TRANSFER != state_) {
    name->clear();
    return false;
  }
  ASSERT(item_transferring_ < manifest_->size());
  if (transfer_name_.empty()) {
    const FileShareManifest::Item& item = manifest_->item(item_transferring_);
    *name = item.name;
  } else {
    *name = transfer_name_;
  }
  return !name->empty();
}

// StreamPool Implementation

talk_base::StreamInterface* FileShareSession::RequestConnectedStream(
    const talk_base::SocketAddress& remote, int* err) {
  ASSERT(remote.IPAsString() == jid_.Str());
  ASSERT(!IsClosed());
  ASSERT(NULL != session_);
  if (!session_) {
    if (err)
      *err = -1;
    return NULL;
  }

  char channel_name[64];
  talk_base::sprintfn(channel_name, ARRAY_SIZE(channel_name),
                      "private-%u", next_channel_id_++);
  if (err)
    *err = 0;
  return CreateChannel(channel_name);
}

void FileShareSession::ReturnConnectedStream(
    talk_base::StreamInterface* stream) {
  talk_base::Thread::Current()->Dispose(stream);
}

// MessageHandler Implementation

void FileShareSession::OnMessage(talk_base::Message* msg) {
  if (MSG_PROXY_WAIT == msg->message_id) {
    LOG_F(LS_INFO) << "MSG_PROXY_WAIT";
    if (proxies_.empty() && IsComplete() && !IsClosed()) {
      DoClose(true);
    }
  }
}

// Session Signals

void FileShareSession::OnSessionState(cricket::Session* session,
                                      cricket::Session::State state) {
  // Once we are complete, state changes are meaningless.
  if (!IsComplete()) {
    switch (state) {
    case cricket::Session::STATE_SENTINITIATE:
    case cricket::Session::STATE_RECEIVEDINITIATE:
      OnInitiate();
      break;
    case cricket::Session::STATE_SENTACCEPT:
    case cricket::Session::STATE_RECEIVEDACCEPT:
    case cricket::Session::STATE_INPROGRESS:
      SetState(FS_TRANSFER, false);
      break;
    case cricket::Session::STATE_SENTREJECT:
    case cricket::Session::STATE_SENTTERMINATE:  
    case cricket::Session::STATE_DEINIT:
      if (local_cancel_) {
        SetState(FS_LOCAL_CANCEL, false);
      } else {
        SetState(FS_REMOTE_CANCEL, false);
      }
      break;
    case cricket::Session::STATE_RECEIVEDTERMINATE:
      if (is_sender()) {
        // If we are the sender, and the receiver downloaded the correct number
        // of bytes, then we assume the transfer was successful.  We've
        // introduced support for explicit completion notification
        // (QN_SHARE_COMPLETE), but it's not mandatory at this point, so we need
        // this as a fallback.
        size_t total_bytes;
        GetTotalSize(total_bytes);
        if (bytes_transferred_ >= total_bytes) {
          SetState(FS_COMPLETE, false);
          break;
        }
      }
      // Fall through
    case cricket::Session::STATE_RECEIVEDREJECT:
      SetState(FS_REMOTE_CANCEL, false);
      break;
    case cricket::Session::STATE_INIT:
    case cricket::Session::STATE_SENTMODIFY:
    case cricket::Session::STATE_RECEIVEDMODIFY:
    case cricket::Session::STATE_SENTREDIRECT:
    default:
      // These states should not occur.
      ASSERT(false);
      break;
    }
  }

  if (state == cricket::Session::STATE_DEINIT) {
    if (!IsClosed()) {
      DoClose(false);
    }
    session_ = NULL;
  }
}

void FileShareSession::OnSessionInfoMessage(cricket::Session* session,
    const cricket::Session::XmlElements& els) {
  if (IsClosed())
    return;
  ASSERT(NULL != session_);
  for (size_t i=0; i<els.size(); ++i) {
    if (is_sender() && (els[i]->Name() == QN_SHARE_CHANNEL)) {
      if (els[i]->HasAttr(buzz::QN_NAME)) {
        cricket::PseudoTcpChannel* channel =
          new cricket::PseudoTcpChannel(talk_base::Thread::Current(), session_);
        VERIFY(channel->Connect(els[i]->Attr(buzz::QN_NAME)));
        talk_base::StreamInterface* stream = channel->GetStream();
        http_server_->HandleConnection(stream);
      }
    } else if (is_sender() && (els[i]->Name() == QN_SHARE_COMPLETE)) {
      // Normal file transfer has completed, but receiver may still be getting
      // previews.
      if (!IsComplete()) {
        SetState(FS_COMPLETE, true);
      }
    } else {
      LOG(LS_WARNING) << "Unknown FileShareSession info message: "
                      << els[i]->Name().Merged();
    }
  }
}

void FileShareSession::OnSessionChannelGone(cricket::Session* session,
                                            const std::string& name) {
  LOG_F(LS_WARNING) << "(" << name << ")";
  ASSERT(session == session_);
  if (cricket::TransportChannel* channel = session->GetChannel(name)) {
    session->DestroyChannel(channel);
  }
}

// HttpClient Signals

void FileShareSession::OnHttpClientComplete(talk_base::HttpClient* http,
                                            int err) {
  LOG_F(LS_INFO) << "(" << err << ", " << http->response().scode << ")";
  ASSERT(http == http_client_);
  ASSERT(NULL != session_);

  transfer_name_.clear();
  counter_ = NULL;  // counter_ is deleted by HttpClient
  http->response().document.reset();
  bool success = (err == 0) && (http->response().scode == talk_base::HC_OK);

  const FileShareManifest::Item& item = manifest_->item(item_transferring_);
  talk_base::Pathname local_name;
  local_name.SetFilename(item.name);
  local_name.SetFolder(local_folder_);

  if (local_name.pathname() != transfer_path_) {
    const bool is_folder = (item.type == FileShareManifest::T_FOLDER);
    if (success && !talk_base::CreateUniqueFile(local_name, false)) {
      LOG(LS_ERROR) << "Couldn't rename downloaded file: "
                    << local_name.pathname();
      success = false;
    }

    talk_base::Pathname temp_name(transfer_path_);
    if (is_folder) {
      // The folder we want is a subdirectory of the transfer_path_.
      temp_name.AppendFolder(item.name);
    }

    if (!talk_base::Filesystem::MoveFile(temp_name.pathname(), local_name.pathname())) {
      success = false;
      LOG(LS_ERROR) << "Couldn't move downloaded file from '"
		    << temp_name.pathname() << "' to '"
		    << local_name.pathname();
    }
  
    if (success && is_folder) {
      talk_base::Filesystem::DeleteFile(transfer_path_);
    }
  }

  if (!success) {
      if (!talk_base::Filesystem::DeleteFile(transfer_path_)) {
        LOG(LS_ERROR) << "Couldn't delete downloaded file: " << transfer_path_;
      }
      if (!IsComplete()) {
	SetState(FS_FAILURE, false);
      }
      return;
  }

  // We may have skipped over some items (if they are directories, or otherwise
  // failed.  resize ensures that we populate the skipped entries with empty
  // strings.
  stored_location_.resize(item_transferring_ + 1);
  stored_location_[item_transferring_] = local_name.pathname();

  // bytes_transferred_ represents the size of items which have completely
  // transferred, and is added to the progress of the currently transferring
  // items.
  if (item.size == FileShareManifest::SIZE_UNKNOWN) {
    bytes_transferred_ += 1;
  } else {
    bytes_transferred_ += item.size;
  }
  item_transferring_ += 1;
  NextDownload();
}

void FileShareSession::OnHttpClientClosed(talk_base::HttpClient* http,
                                          int err) {
  LOG_F(LS_INFO) << "(" << err << ")";
}

// HttpServer Signals

void FileShareSession::OnHttpRequest(talk_base::HttpServer* server,
                                     talk_base::HttpTransaction* transaction) {
  LOG_F(LS_INFO) << "(" << transaction->request()->path << ")";
  ASSERT(server == http_server_);

  std::string path, query;
  size_t query_start = transaction->request()->path.find('?');
  if (query_start != std::string::npos) {
    path = transaction->request()->path.substr(0, query_start);
    query = transaction->request()->path.substr(query_start + 1);
  } else {
    path = transaction->request()->path;
  }

  talk_base::Pathname remote_name(path);
  bool preview = (preview_path_ == remote_name.folder());
  bool original = (source_path_ == remote_name.folder());

  std::string requested_file(remote_name.filename());
  talk_base::transform(requested_file, requested_file.size(), requested_file,
                       talk_base::url_decode);

  size_t item_index;
  const FileShareManifest::Item* item = NULL;
  if (preview || original) {
    for (size_t i=0; i<manifest_->size(); ++i) {
      LOG(LS_INFO) << "++++ " << manifest_->item(i).name + " " << requested_file;
      if (manifest_->item(i).name == requested_file) {
        item_index = i;
        item = &manifest_->item(item_index);
        break;
      }
    }
  }

  talk_base::StreamInterface* stream = NULL;
  std::string mime_type(MIME_OCTET_STREAM);

  if (!item) {
    // Fall through  
  } else if (preview) {
    // Only image previews allowed
    unsigned int width = 0, height = 0;
    if ((item->type == FileShareManifest::T_IMAGE)
        && !query.empty()
        && (sscanf(query.c_str(), "width=%u&height=%u",
                   &width, &height) == 2)) {
      width = talk_base::_max<unsigned int>(1, talk_base::_min(width, kMaxPreviewSize));
      height = talk_base::_max<unsigned int>(1, talk_base::_min(height, kMaxPreviewSize));
      std::string pathname;
      if (is_sender_) {
        talk_base::Pathname local_path;
        local_path.SetFolder(local_folder_);
        local_path.SetFilename(item->name);
        pathname = local_path.pathname();
      } else if ((item_index < stored_location_.size())
                 && !stored_location_[item_index].empty()) {
        pathname = stored_location_[item_index];
      }
      if (!pathname.empty()) {
	transactions_.push_back(transaction);
	SignalResampleImage(pathname, width, height, transaction);
      }
    }
  } else if (item->type == FileShareManifest::T_FOLDER) {
    talk_base::Pathname local_path;
    local_path.SetFolder(local_folder_);
    local_path.AppendFolder(item->name);
    talk_base::TarStream* tar = new talk_base::TarStream;
    VERIFY(tar->AddFilter(local_path.folder_name()));
 if (tar->Open(local_path.parent_folder(), true)) {
      stream = tar;
      tar->SignalNextEntry.connect(this, &FileShareSession::OnNextEntry);
      mime_type = "application/x-tar";
    } else {
      delete tar;
    }
  } else if ((item->type == FileShareManifest::T_FILE)
             || (item->type == FileShareManifest::T_IMAGE)) {
    talk_base::Pathname local_path;
    local_path.SetFolder(local_folder_);
    local_path.SetFilename(item->name);
    talk_base::FileStream* file = new talk_base::FileStream;
    LOG(LS_INFO) << "opening file " << local_path.pathname();
    if (file->Open(local_path.pathname().c_str(), "rb")) {
      LOG(LS_INFO) << "File opened";
      stream = file;
    } else {
      delete file;
    }
  }

  if (!stream) {
    transaction->response()->set_error(talk_base::HC_NOT_FOUND);
  } else if (original) {
    // We should never have more than one original request pending at a time
    ASSERT(NULL == counter_);
    StreamCounter* counter = new StreamCounter(stream);
    counter->SignalUpdateByteCount.connect(this, &FileShareSession::OnUpdateBytes);
    transaction->response()->set_success(mime_type.c_str(), counter);
    transfer_connection_id_ = transaction->connection_id();
    item_transferring_ = item_index;
    counter_ = counter;
  } else {
    // Note: in the preview case, we don't set counter_, so the transferred
    // bytes won't be shown as progress, and won't trigger a state change.
    transaction->response()->set_success(mime_type.c_str(), stream);
  }

  LOG_F(LS_INFO) << "Result: " << transaction->response()->scode;
  http_server_->Respond(transaction);
}

void FileShareSession::OnHttpRequestComplete(talk_base::HttpServer* server,
    talk_base::HttpTransaction* transaction, int err) {
  LOG_F(LS_INFO) << "(" << transaction->request()->path << ", " << err << ")";
  ASSERT(server == http_server_);

  // We only care about transferred originals
  if (transfer_connection_id_ != transaction->connection_id())
    return;

  ASSERT(item_transferring_ < manifest_->size());
  ASSERT(NULL != counter_);

  transfer_connection_id_ = talk_base::HTTP_INVALID_CONNECTION_ID;
  transfer_name_.clear();
  counter_ = NULL;

  if (err == 0) {
    const FileShareManifest::Item& item = manifest_->item(item_transferring_);
    if (item.size == FileShareManifest::SIZE_UNKNOWN) {
      bytes_transferred_ += 1;
    } else {
      bytes_transferred_ += item.size;
    }
  }
}

void FileShareSession::OnHttpConnectionClosed(talk_base::HttpServer* server,
    int err, talk_base::StreamInterface* stream) {
  LOG_F(LS_INFO) << "(" << err << ")";
  talk_base::Thread::Current()->Dispose(stream);
}

// TarStream Signals

void FileShareSession::OnNextEntry(const std::string& name, size_t size) {
  LOG_F(LS_VERBOSE) << "(" << name << ", " << size << ")";
  transfer_name_ = name;
  SignalNextFile(this);
}

// Socket Signals

void FileShareSession::OnProxyAccept(talk_base::AsyncSocket* socket) {
 bool is_remote;
  if (socket == remote_listener_) {
    is_remote = true;
    ASSERT(NULL != session_);
  } else if (socket == local_listener_) {
    is_remote = false;
  } else {
    ASSERT(false);
    return;
  }

  while (talk_base::AsyncSocket* accepted =
           static_cast<talk_base::AsyncSocket*>(socket->Accept(NULL))) {

    // Check if connection is from localhost.
    if (accepted->GetRemoteAddress().ip() != 0x7F000001) {
      delete accepted;
      continue;
    }

    LOG_F(LS_VERBOSE) << (is_remote ? "[remote]" : "[local]");

    if (is_remote) {
      char channel_name[64];
      talk_base::sprintfn(channel_name, ARRAY_SIZE(channel_name),
                          "proxy-%u", next_channel_id_++);
      talk_base::StreamInterface* remote =
        (NULL != session_) ? CreateChannel(channel_name) : NULL;
      if (!remote) {
        LOG_F(LS_WARNING) << "CreateChannel(" << channel_name << ") failed";
        delete accepted;
        continue;
      }

      talk_base::StreamInterface* local = new talk_base::SocketStream(accepted);
      StreamRelay* proxy = new StreamRelay(local, remote, 64 * 1024);
      proxy->SignalClosed.connect(this, &FileShareSession::OnProxyClosed);
      proxies_.push_back(proxy);
      proxy->Circulate();
      talk_base::Thread::Current()->Clear(this, MSG_PROXY_WAIT);
    } else {
      talk_base::StreamInterface* local = new talk_base::SocketStream(accepted);
      http_server_->HandleConnection(local);
    }
  }
}

void FileShareSession::OnProxyClosed(StreamRelay* proxy, int error) {
  ProxyList::iterator it = std::find(proxies_.begin(), proxies_.end(), proxy);
  if (it == proxies_.end()) {
    ASSERT(false);
    return;
  }

  LOG_F(LS_VERBOSE) << "(" << error << ")";

  proxies_.erase(it);
  talk_base::Thread::Current()->Dispose(proxy);

  if (proxies_.empty() && IsComplete() && !IsClosed()) {
    talk_base::Thread::Current()->PostDelayed(kProxyWait, this, MSG_PROXY_WAIT);
  }
}


void FileShareSession::OnUpdateBytes(size_t count) {
  SignalUpdateProgress(this);
}

// Internal Helpers

void FileShareSession::GenerateTemporaryPrefix(std::string* prefix) {
  std::string data = cricket::CreateRandomString(32);
  ASSERT(NULL != prefix);
  prefix->assign("/temporary/");
  prefix->append(talk_base::MD5(data));
  prefix->append("/");
}

void FileShareSession::GetItemNetworkPath(size_t index, bool preview,
                                          std::string* path) {
  ASSERT(index < manifest_->size());
  ASSERT(NULL != path);

  // preview_path_ and source_path_ are url path segments, which are composed
  // with the address of the localhost p2p proxy to provide a url which IE can
  // use.

  std::string ue_name;
  const std::string& name = manifest_->item(index).name;
  talk_base::transform(ue_name, name.length() * 3, name, talk_base::url_encode);

  talk_base::Pathname pathname;
  pathname.SetFolder(preview ? preview_path_ : source_path_);
  pathname.SetFilename(ue_name);
  *path = pathname.pathname();
}

bool FileShareSession::GetItemBaseUrl(size_t index, bool preview,
                                      std::string* url) {
  // This function composes a URL to the referenced item.  It may be a local
  // file url (file:///...), or a remote peer url relayed through localhost
  // (http://...)

  ASSERT(NULL != url);
  if (index >= manifest_->size()) {
    ASSERT(false);
    return false;
  }

  const FileShareManifest::Item& item = manifest_->item(index);

  bool is_remote;
  if (is_sender_) {
    if (!preview) {
      talk_base::Pathname path(local_folder_);
      path.SetFilename(item.name);
      *url = path.url();
      return true;
    }
    is_remote = false;
  } else {
    if ((index < stored_location_.size()) && !stored_location_[index].empty()) {
      if (!preview) {
        *url = talk_base::Pathname(stored_location_[index]).url();
        return true;
      }
      // Note: Using the local downloaded files as a source for previews is
      // desireable, because it means that previews can be regenerated if IE's
      // cached versions get flushed for some reason, and the remote side is
      // not available.  However, it has the downside that IE _must_ regenerate
      // the preview locally, which takes time, memory and CPU.  Eventually,
      // we will unify the remote and local cached copy through some sort of
      // smart http proxying.  In the meantime, always use the remote url, to
      // eliminate the annoying transition from remote to local caching.
      //is_remote = false;
      is_remote = true;
    } else {
      is_remote = true;
    }
  }

  talk_base::SocketAddress address;
  if (!GetProxyAddress(address, is_remote))
    return false;

  std::string path;
  GetItemNetworkPath(index, preview, &path);
  talk_base::Url<char> make_url(path.c_str(),
                                address.IPAsString().c_str(), 
                                address.port());
  *url = make_url.url();
  return true;
}

bool FileShareSession::GetProxyAddress(talk_base::SocketAddress& address,
                                       bool is_remote) {
  talk_base::AsyncSocket*& proxy_listener =
    is_remote ? remote_listener_ : local_listener_;

  if (!proxy_listener) {
    talk_base::AsyncSocket* listener =
      talk_base::Thread::Current()->socketserver()
                                  ->CreateAsyncSocket(SOCK_STREAM);
    if (!listener)
      return false;

    talk_base::SocketAddress bind_address("127.0.0.1", 0);

    if ((listener->Bind(bind_address) != 0)
        || (listener->Listen(5) != 0)) {
      delete listener;
      return false;
    }

    LOG(LS_INFO) << "Proxy listener available @ "
                 << listener->GetLocalAddress().ToString();

    listener->SignalReadEvent.connect(this, &FileShareSession::OnProxyAccept);
    proxy_listener = listener;
  }

  if (proxy_listener->GetState() == talk_base::Socket::CS_CLOSED) {
    if (is_remote) {
      address = remote_listener_address_;
      return true;
    }
    return false;
  }

  address = proxy_listener->GetLocalAddress();
  return !address.IsAny();
}

talk_base::StreamInterface* FileShareSession::CreateChannel(
    const std::string& channel_name) {
  ASSERT(NULL != session_);

  // Send a heads-up for our new channel
  cricket::Session::XmlElements els;
  buzz::XmlElement* xel_channel = new buzz::XmlElement(QN_SHARE_CHANNEL, true);
  xel_channel->AddAttr(buzz::QN_NAME, channel_name);
  els.push_back(xel_channel);
  session_->SendInfoMessage(els);

  cricket::PseudoTcpChannel* channel =
    new cricket::PseudoTcpChannel(talk_base::Thread::Current(), session_);
  VERIFY(channel->Connect(channel_name));
  return channel->GetStream();
}

void FileShareSession::SetState(FileShareState state, bool prevent_close) {
  if (state == state_)
    return;

  if (IsComplete()) {
    // Entering a completion state is permanent.
    ASSERT(false);
    return;
  }

  state_ = state;
  if (IsComplete()) {
    // All completion states auto-close except for FS_COMPLETE
    bool close = (state_ > FS_COMPLETE) || !prevent_close;
    if (close) {
      DoClose(true);
    }
  }

  SignalState(state_);
}

void FileShareSession::OnInitiate() {
  // Cache the variables we will need, in case session_ goes away
  is_sender_ = session_->initiator();
  jid_ = buzz::Jid(session_->remote_name());
  manifest_ = new FileShareManifest(description()->manifest);
  source_path_ = description()->source_path;
  preview_path_ = description()->preview_path;

  if (local_folder_.empty()) {
    LOG(LS_ERROR) << "FileShareSession - no local folder, using temp";
    talk_base::Pathname temp_folder;
    talk_base::Filesystem::GetTemporaryFolder(temp_folder, true, NULL);
    local_folder_ = temp_folder.pathname();
  }
  LOG(LS_INFO) << session_->state();
  SetState(FS_OFFER, false);
}

void FileShareSession::NextDownload() {
  if (FS_TRANSFER != state_)
    return;

  if (item_transferring_ >= manifest_->size()) {
    // Notify the other side that transfer has completed
    cricket::Session::XmlElements els;
    els.push_back(new buzz::XmlElement(QN_SHARE_COMPLETE, true));
    session_->SendInfoMessage(els);
    SetState(FS_COMPLETE, !proxies_.empty());
    return;
  }

  const FileShareManifest::Item& item = manifest_->item(item_transferring_);
  if ((item.type != FileShareManifest::T_FILE)
      && (item.type != FileShareManifest::T_IMAGE)
      && (item.type != FileShareManifest::T_FOLDER)) {
    item_transferring_ += 1;
    NextDownload();
    return;
  }

  const bool is_folder = (item.type == FileShareManifest::T_FOLDER);
  talk_base::Pathname temp_name;
  temp_name.SetFilename(item.name);
  if (!talk_base::CreateUniqueFile(temp_name, !is_folder)) {
    SetState(FS_FAILURE, false);
    return;
  }

  talk_base::StreamInterface* stream = NULL;
  if (is_folder) {
    // Convert unique filename into unique foldername
    temp_name.AppendFolder(temp_name.filename());
    temp_name.SetFilename("");
    talk_base::TarStream* tar = new talk_base::TarStream;
    // Note: the 'target' directory will be a subdirectory of the transfer_path_
    talk_base::Pathname target;
    target.SetFolder(item.name);
    tar->AddFilter(target.pathname());
    if (!tar->Open(temp_name.pathname(), false)) {
      delete tar;
      SetState(FS_FAILURE, false);
      return;
    }
    stream = tar;
    tar->SignalNextEntry.connect(this, &FileShareSession::OnNextEntry);
  } else {
    talk_base::FileStream* file = new talk_base::FileStream;
    if (!file->Open(temp_name.pathname().c_str(), "wb")) {
      delete file;
      talk_base::Filesystem::DeleteFile(temp_name);
      SetState(FS_FAILURE, false);
      return;
    }
    stream = file;
  }

  ASSERT(NULL != stream);
  transfer_path_ = temp_name.pathname();

  std::string remote_path;
  GetItemNetworkPath(item_transferring_, false, &remote_path);

  StreamCounter* counter = new StreamCounter(stream);
  counter->SignalUpdateByteCount.connect(this, &FileShareSession::OnUpdateBytes);
  counter_ = counter;

  http_client_->reset();
  http_client_->set_server(talk_base::SocketAddress(jid_.Str(), 0, false));
  http_client_->request().verb = talk_base::HV_GET;
  http_client_->request().path = remote_path;
  http_client_->response().document.reset(counter);
  http_client_->start();
}


const FileShareSession::FileShareDescription* FileShareSession::description()
const {
  ASSERT(NULL != session_);
  const cricket::SessionDescription* desc =
    session_->initiator() ? session_->description()
                          : session_->remote_description();
  return static_cast<const FileShareDescription*>(desc);
}

void FileShareSession::DoClose(bool terminate) {
  ASSERT(!is_closed_);
  ASSERT(IsComplete());
  ASSERT(NULL != session_);

  is_closed_ = true;

  if (http_client_) {
    http_client_->reset();
  }
  if (http_server_) {
    http_server_->CloseAll(true);
    // Currently, CloseAll doesn't result in OnHttpRequestComplete callback.
    // If we change that, the following resetting won't be necessary.
    transfer_connection_id_ = talk_base::HTTP_INVALID_CONNECTION_ID;
    transfer_name_.clear();
    counter_ = NULL;
  }
  // 'reset' and 'CloseAll' cause counter_ to clear.
  ASSERT(NULL == counter_);

  if (remote_listener_) {
    // Cache the address for the remote_listener_, so that we can continue to
    // present a consistent URL for remote previews, which is necessary for IE
    // to continue using its cached copy.
    remote_listener_address_ = remote_listener_->GetLocalAddress();
    remote_listener_->Close();
    LOG(LS_INFO) << "Proxy listener closed @ "
                 << remote_listener_address_.ToString();
  }

  if (terminate) {
    session_->Terminate();
  }
}

//////////////////////////////
/// FileShareSessionClient //
////////////////////////////

void FileShareSessionClient::OnSessionCreate(cricket::Session* session,
                                      bool received_initiate) {
  VERIFY(sessions_.insert(session).second);
  if (received_initiate) {
    FileShareSession* share = new FileShareSession(session, user_agent_);
    SignalFileShareSessionCreate(share);
    UNUSED(share);  // FileShareSession registers itself with the UI
  }
}

void FileShareSessionClient::OnSessionDestroy(cricket::Session* session) {
  VERIFY(1 == sessions_.erase(session));
}

const cricket::SessionDescription* FileShareSessionClient::CreateSessionDescription(
    const buzz::XmlElement* element) {
  FileShareSession::FileShareDescription* share_desc =
    new FileShareSession::FileShareDescription;

  if (element->Name() != QN_SHARE_DESCRIPTION)
    return share_desc;

  const buzz::XmlElement* manifest = element->FirstNamed(QN_SHARE_MANIFEST);
  const buzz::XmlElement* protocol = element->FirstNamed(QN_SHARE_PROTOCOL);

  if (!manifest || !protocol)
    return share_desc;

  for (const buzz::XmlElement* item = manifest->FirstElement();
       item != NULL; item = item->NextElement()) {
    bool is_folder;
    if (item->Name() == QN_SHARE_FOLDER) {
      is_folder = true;
    } else if (item->Name() == QN_SHARE_FILE) {
      is_folder = false;
    } else {
      continue;
    }
    std::string name;
    if (const buzz::XmlElement* el_name = item->FirstNamed(QN_SHARE_NAME)) {
      name = el_name->BodyText();
    }
    if (name.empty()) {
      continue;
    }
    size_t size = FileShareManifest::SIZE_UNKNOWN;
    if (item->HasAttr(QN_SIZE)) {
      size = strtoul(item->Attr(QN_SIZE).c_str(), NULL, 10);
    }
    if (is_folder) {
      share_desc->manifest.AddFolder(name, size);
    } else {
      // Check if there is a valid image description for this file.
      if (const buzz::XmlElement* image = item->FirstNamed(QN_SHARE_IMAGE)) {
        if (image->HasAttr(QN_WIDTH) && image->HasAttr(QN_HEIGHT)) {
          size_t width = strtoul(image->Attr(QN_WIDTH).c_str(), NULL, 10);
          size_t height = strtoul(image->Attr(QN_HEIGHT).c_str(), NULL, 10);
          if (AllowedImageDimensions(width, height)) {
            share_desc->manifest.AddImage(name, size, width, height);
            continue;
          }
        }
      }
      share_desc->manifest.AddFile(name, size);
    }
  }

  if (const buzz::XmlElement* http = protocol->FirstNamed(QN_SHARE_HTTP)) {
    share_desc->supports_http = true;
    for (const buzz::XmlElement* url = http->FirstNamed(QN_SHARE_URL);
         url != NULL; url = url->NextNamed(QN_SHARE_URL)) {
      if (url->Attr(buzz::QN_NAME) == kHttpSourcePath) {
        share_desc->source_path = url->BodyText();
      } else if (url->Attr(buzz::QN_NAME) == kHttpPreviewPath) {
        share_desc->preview_path = url->BodyText();
      }
    }
  }

  return share_desc;
}

buzz::XmlElement* FileShareSessionClient::TranslateSessionDescription(
    const cricket::SessionDescription* description) {

  const FileShareSession::FileShareDescription* share_desc =
    static_cast<const FileShareSession::FileShareDescription*>(description);

  scoped_ptr<buzz::XmlElement> el(new buzz::XmlElement(QN_SHARE_DESCRIPTION,
                                                       true));

  const FileShareManifest& manifest = share_desc->manifest;
  el->AddElement(new buzz::XmlElement(QN_SHARE_MANIFEST));
  for (size_t i=0; i<manifest.size(); ++i) {
    const FileShareManifest::Item& item = manifest.item(i);
    buzz::QName qname;
    if (item.type == FileShareManifest::T_FOLDER) {
      qname = QN_SHARE_FOLDER;
    } else if ((item.type == FileShareManifest::T_FILE)
               || (item.type == FileShareManifest::T_IMAGE)) {
      qname = QN_SHARE_FILE;
    } else {
      ASSERT(false);
      continue;
    }
    el->AddElement(new buzz::XmlElement(qname), 1);
    if (item.size != FileShareManifest::SIZE_UNKNOWN) {
      char buffer[256];
      talk_base::sprintfn(buffer, sizeof(buffer), "%lu", item.size);
      el->AddAttr(QN_SIZE, buffer, 2);
    }
    buzz::XmlElement* el_name = new buzz::XmlElement(QN_SHARE_NAME);
    el_name->SetBodyText(item.name);
    el->AddElement(el_name, 2);
    if ((item.type == FileShareManifest::T_IMAGE)
        && AllowedImageDimensions(item.width, item.height)) {
      el->AddElement(new buzz::XmlElement(QN_SHARE_IMAGE), 2);
      char buffer[256];
      talk_base::sprintfn(buffer, sizeof(buffer), "%lu", item.width);
      el->AddAttr(QN_WIDTH, buffer, 3);
      talk_base::sprintfn(buffer, sizeof(buffer), "%lu", item.height);
      el->AddAttr(QN_HEIGHT, buffer, 3);
    }
  }

  el->AddElement(new buzz::XmlElement(QN_SHARE_PROTOCOL));
  if (share_desc->supports_http) {
    el->AddElement(new buzz::XmlElement(QN_SHARE_HTTP), 1);
    if (!share_desc->source_path.empty()) {
      buzz::XmlElement* url = new buzz::XmlElement(QN_SHARE_URL);
      url->SetAttr(buzz::QN_NAME, kHttpSourcePath);
      url->SetBodyText(share_desc->source_path);
      el->AddElement(url, 2);
    }
    if (!share_desc->preview_path.empty()) {
      buzz::XmlElement* url = new buzz::XmlElement(QN_SHARE_URL);
      url->SetAttr(buzz::QN_NAME, kHttpPreviewPath);
      url->SetBodyText(share_desc->preview_path);
      el->AddElement(url, 2);
    }
  }

  return el.release();
}

FileShareSession *FileShareSessionClient::CreateFileShareSession() {
  cricket::Session* session = sm_->CreateSession(jid_.Str(),
						 NS_GOOGLE_SHARE);
  FileShareSession* share = new FileShareSession(session, user_agent_);
  SignalFileShareSessionCreate(share);
  return share;
}


} // namespace cricket
