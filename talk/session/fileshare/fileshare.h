#ifndef TALK_APP_WIN32_FILESHARE_H__
#define TALK_APP_WIN32_FILESHARE_H__
#include "talk/base/messagequeue.h"
#include "talk/base/socketpool.h"
#include "talk/base/stringutils.h"
#include "talk/base/sigslot.h"
#include "talk/p2p/base/session.h"
#include "talk/p2p/base/sessiondescription.h"
#include "talk/xmpp/jid.h"

class StreamCounter;
class StreamRelay;

namespace talk_base {
  class HttpClient;
  class HttpServer;
  class HttpTransaction;
}

extern const std::string NS_GOOGLE_SHARE;


namespace cricket {

///////////////////////////////////////////////////////////////////////////////
// FileShareManifest
///////////////////////////////////////////////////////////////////////////////

class FileShareManifest {
public:
  enum Type { T_FILE, T_IMAGE, T_FOLDER };
  enum { SIZE_UNKNOWN = talk_base::SIZE_UNKNOWN };

  struct Item {
    Type type;
    std::string name;
    size_t size, width, height;
  };
  typedef std::vector<Item> ItemList;

  inline bool empty() const { return items_.empty(); }
  inline size_t size() const { return items_.size(); }
  inline const Item& item(size_t index) const { return items_[index]; }

  void AddFile(const std::string& name, size_t size);
  void AddImage(const std::string& name, size_t size,
                size_t width, size_t height);
  void AddFolder(const std::string& name, size_t size);

  size_t GetItemCount(Type t) const;
  inline size_t GetFileCount() const { return GetItemCount(T_FILE); }
  inline size_t GetImageCount() const { return GetItemCount(T_IMAGE); }
  inline size_t GetFolderCount() const { return GetItemCount(T_FOLDER); }

private:
  ItemList items_;
};


enum FileShareState {
  FS_NONE,          // Initialization
  FS_OFFER,         // Offer extended
  FS_TRANSFER,      // In progress
  FS_COMPLETE,      // Completed successfully
  FS_LOCAL_CANCEL,  // Local side cancelled
  FS_REMOTE_CANCEL, // Remote side cancelled
  FS_FAILURE        // An error occurred during transfer
};


class FileShareSession
  : public talk_base::StreamPool,
    public talk_base::MessageHandler,
    public sigslot::has_slots<> {
public:
  struct FileShareDescription : public cricket::SessionDescription {
    FileShareManifest manifest;
    bool supports_http;
    std::string source_path;
    std::string preview_path;
    FileShareDescription() : supports_http(false) { }
  };

  FileShareSession(cricket::Session* session, const std::string &user_agent);
  virtual ~FileShareSession();

  bool IsComplete() const;
  bool IsClosed() const;
  FileShareState state() const;
  sigslot::signal1<FileShareState> SignalState;
  sigslot::signal1<FileShareSession*> SignalNextFile;
  sigslot::signal1<FileShareSession*> SignalUpdateProgress;
  sigslot::signal4<std::string, int, int, talk_base::HttpTransaction*> SignalResampleImage;

  void ResampleComplete(talk_base::StreamInterface *si, talk_base::HttpTransaction *trans, bool success);

  bool is_sender() const;
  const buzz::Jid& jid() const;
  const FileShareManifest* manifest() const;
  const std::string& local_folder() const;

  void SetLocalFolder(const std::string& folder) { local_folder_ = folder; }
  void Share(const buzz::Jid& jid, FileShareManifest* manifest);

  void Accept();
  void Decline();
  void Cancel();

  bool GetItemUrl(size_t index, std::string* url);
  bool GetImagePreviewUrl(size_t index, size_t width, size_t height,
                          std::string* url);
  // Returns true if the transferring item size is known
  bool GetProgress(size_t& bytes) const;
  // Returns true if the total size is known
  bool GetTotalSize(size_t& bytes) const;
  // Returns true if currently transferring item name is known
  bool GetCurrentItemName(std::string* name);

  // TODO: Eliminate this eventually?
  cricket::Session* session() { return session_; }

  // StreamPool Interface
  virtual talk_base::StreamInterface* 
    RequestConnectedStream(const talk_base::SocketAddress& remote, int* err);
  virtual void ReturnConnectedStream(talk_base::StreamInterface* stream);

  // MessageHandler Interface
  virtual void OnMessage(talk_base::Message* msg);

  void GetItemNetworkPath(size_t index, bool preview, std::string* path);

private:
  typedef std::list<StreamRelay*> ProxyList;
  typedef std::list<talk_base::HttpTransaction*> TransactionList;

  // Session Signals
  void OnSessionState(cricket::Session* session, cricket::Session::State state);
  void OnSessionInfoMessage(cricket::Session* session,
                            const cricket::Session::XmlElements& els);
  void OnSessionChannelGone(cricket::Session* session,
                            const std::string& name);

  // HttpClient Signals
  void OnHttpClientComplete(talk_base::HttpClient* http, int err);
  void OnHttpClientClosed(talk_base::HttpClient* http, int err);

  // HttpServer Signals
  void OnHttpRequest(talk_base::HttpServer* server,
                     talk_base::HttpTransaction* transaction);
  void OnHttpRequestComplete(talk_base::HttpServer* server,
                             talk_base::HttpTransaction* transaction,
                             int err);
  void OnHttpConnectionClosed(talk_base::HttpServer* server,
                              int err,
                              talk_base::StreamInterface* stream);

  // TarStream Signals
  void OnNextEntry(const std::string& name, size_t size);

  // Socket Signals
  void OnProxyAccept(talk_base::AsyncSocket* socket);
  void OnProxyClosed(StreamRelay* proxy, int error);

  // StreamCounterSignals
  void OnUpdateBytes(size_t count);

  // Internal Helpers
  void GenerateTemporaryPrefix(std::string* prefix);
  bool GetItemBaseUrl(size_t index, bool preview, std::string* url);
  bool GetProxyAddress(talk_base::SocketAddress& address, bool is_remote);
  talk_base::StreamInterface* CreateChannel(const std::string& channel_name);
  void SetState(FileShareState state, bool prevent_close);
  void OnInitiate();
  void NextDownload();
  const FileShareDescription* description() const;
  void DoClose(bool terminate);

  cricket::Session* session_;
  FileShareState state_;
  bool is_closed_;
  bool is_sender_;
  buzz::Jid jid_;
  FileShareManifest* manifest_;
  std::string source_path_;
  std::string preview_path_;
  std::string local_folder_;

  // The currently active p2p streams to our peer
  talk_base::StreamCache pool_;
  // The http client state (client only)
  talk_base::HttpClient* http_client_;
  // The http server state (server only)
  talk_base::HttpServer* http_server_;
  // The connection id of the currently transferring file (server)
  int transfer_connection_id_;
  // The counter for the currently transferring file
  const StreamCounter* counter_;
  // The number of manifest items that have successfully transferred
  size_t item_transferring_;
  // The byte count of successfully transferred items
  size_t bytes_transferred_;
  // Where the currently transferring item is being (temporarily) saved (client)
  std::string transfer_path_;
  // The name of the currently transferring item
  std::string transfer_name_;
  // Where the files are saved after transfer (client)
  std::vector<std::string> stored_location_;
  // Was it a local cancel?  Or a remote cancel?
  bool local_cancel_;
  // Proxy socket for local IE http requests
  talk_base::AsyncSocket* local_listener_;
  // Proxy socket for remote IE http requests
  talk_base::AsyncSocket* remote_listener_;
  // Cached address of remote_listener_
  talk_base::SocketAddress remote_listener_address_;
  // Uniqueness for channel names
  size_t next_channel_id_;
  // Proxy relays
  ProxyList proxies_;
  std::string user_agent_;
  TransactionList transactions_;
};

class FileShareSessionClient :  public SessionClient
{
 public:
  FileShareSessionClient(SessionManager *sm, buzz::Jid jid, const std::string &user_agent) : sm_(sm), jid_(jid),
    user_agent_(user_agent) {}
  virtual void OnSessionCreate(cricket::Session* session,
                               bool received_initiate);
  virtual void OnSessionDestroy(cricket::Session* session);
  virtual const cricket::SessionDescription* CreateSessionDescription(const buzz::XmlElement* element);
  virtual buzz::XmlElement* TranslateSessionDescription(const cricket::SessionDescription* description);
  FileShareSession *CreateFileShareSession();

  sigslot::signal1<FileShareSession*> SignalFileShareSessionCreate;
  sigslot::signal1<FileShareSession*> SignalFileShareSessionDestroy;

 private:
  SessionManager *sm_;
  buzz::Jid jid_;
  friend class FileShareSession;
  typedef std::set<cricket::Session*> SessionSet;
  SessionSet sessions_;
  std::string user_agent_;
};

}  // namespace cricket

#endif  // TALK_APP_WIN32_FILESHARE_H__
