/*
 * Jingle call example
 * Copyright 2004--2005, Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Tempe Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <iomanip>
#include <time.h>

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iomanip>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#else
#include <direct.h>
//typedef _getcwd getcwd;
#include "talk/base/win32.h"
#endif

#include "talk/base/fileutils.h"
#include "talk/base/pathutils.h"
#include "talk/base/helpers.h"
#include "talk/base/httpclient.h"
#include "talk/base/logging.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/ssladapter.h"
#include "talk/xmpp/xmppclientsettings.h"
#include "talk/examples/login/xmppthread.h"
#include "talk/examples/login/xmppauth.h"
#include "talk/p2p/client/httpportallocator.h"
#include "talk/p2p/client/sessionmanagertask.h"
#include "talk/session/fileshare/fileshare.h"
#include "talk/examples/login/presencepushtask.h"
#include "talk/examples/login/presenceouttask.h"
#include "talk/examples/login/jingleinfotask.h"

#if defined(_MSC_VER) && (_MSC_VER < 1400)
// The following are necessary to properly link when compiling STL without
// /EHsc, otherwise known as C++ exceptions.
void __cdecl std::_Throw(const std::exception &) {}
std::_Prhand std::_Raise_handler = 0;
#endif

void SetConsoleEcho(bool on) {
#ifdef WIN32
  HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
  if ((hIn == INVALID_HANDLE_VALUE) || (hIn == NULL))
    return;

  DWORD mode;
  if (!GetConsoleMode(hIn, &mode))
    return;

  if (on) {
    mode = mode | ENABLE_ECHO_INPUT;
  } else {
    mode = mode & ~ENABLE_ECHO_INPUT;
  }

  SetConsoleMode(hIn, mode);
#else
  if (on)
    system("stty echo");
  else
    system("stty -echo");
#endif
}
class DebugLog : public sigslot::has_slots<> {
public:
  DebugLog() :
    debug_input_buf_(NULL), debug_input_len_(0), debug_input_alloc_(0),
    debug_output_buf_(NULL), debug_output_len_(0), debug_output_alloc_(0),
    censor_password_(false)
      {}
  char * debug_input_buf_;
  int debug_input_len_;
  int debug_input_alloc_;
  char * debug_output_buf_;
  int debug_output_len_;
  int debug_output_alloc_;
  bool censor_password_;

  void Input(const char * data, int len) {
    if (debug_input_len_ + len > debug_input_alloc_) {
      char * old_buf = debug_input_buf_;
      debug_input_alloc_ = 4096;
      while (debug_input_alloc_ < debug_input_len_ + len) {
        debug_input_alloc_ *= 2;
      }
      debug_input_buf_ = new char[debug_input_alloc_];
      memcpy(debug_input_buf_, old_buf, debug_input_len_);
      delete[] old_buf;
    }
    memcpy(debug_input_buf_ + debug_input_len_, data, len);
    debug_input_len_ += len;
    DebugPrint(debug_input_buf_, &debug_input_len_, false);
  }

  void Output(const char * data, int len) {
    if (debug_output_len_ + len > debug_output_alloc_) {
      char * old_buf = debug_output_buf_;
      debug_output_alloc_ = 4096;
      while (debug_output_alloc_ < debug_output_len_ + len) {
        debug_output_alloc_ *= 2;
      }
      debug_output_buf_ = new char[debug_output_alloc_];
      memcpy(debug_output_buf_, old_buf, debug_output_len_);
      delete[] old_buf;
    }
    memcpy(debug_output_buf_ + debug_output_len_, data, len);
    debug_output_len_ += len;
    DebugPrint(debug_output_buf_, &debug_output_len_, true);
  }

  static bool
  IsAuthTag(const char * str, size_t len) {
    if (str[0] == '<' && str[1] == 'a' &&
                         str[2] == 'u' &&
                         str[3] == 't' &&
                         str[4] == 'h' &&
                         str[5] <= ' ') {
      std::string tag(str, len);

      if (tag.find("mechanism") != std::string::npos)
        return true;

    }
    return false;
  }

  void
  DebugPrint(char * buf, int * plen, bool output) {
    int len = *plen;
    if (len > 0) {
      time_t tim = time(NULL);
      struct tm * now = localtime(&tim);
      char *time_string = asctime(now);
      if (time_string) {
        size_t time_len = strlen(time_string);
        if (time_len > 0) {
          time_string[time_len-1] = 0;    // trim off terminating \n
        }
      }
      LOG(INFO) << (output ? "SEND >>>>>>>>>>>>>>>>>>>>>>>>>" : "RECV <<<<<<<<<<<<<<<<<<<<<<<<<")
        << " : " << time_string;

      bool indent;
      int start = 0, nest = 3;
      for (int i = 0; i < len; i += 1) {
        if (buf[i] == '>') {
          if ((i > 0) && (buf[i-1] == '/')) {
            indent = false;
          } else if ((start + 1 < len) && (buf[start + 1] == '/')) {
            indent = false;
            nest -= 2;
          } else {
            indent = true;
          }

          // Output a tag
          LOG(INFO) << std::setw(nest) << " " << std::string(buf + start, i + 1 - start);

          if (indent)
            nest += 2;

          // Note if it's a PLAIN auth tag
	  if (IsAuthTag(buf + start, i + 1 - start)) {
	    censor_password_ = true;
	  }

          // incr
          start = i + 1;
        }

        if (buf[i] == '<' && start < i) {
	  if (censor_password_) {
	    LOG(INFO) << std::setw(nest) << " " << "## TEXT REMOVED ##";
	    censor_password_ = false;
	  }
	  else {
	    LOG(INFO) << std::setw(nest) << " " << std::string(buf + start, i - start);
	  }
          start = i;
        }
      }
      len = len - start;
      memcpy(buf, buf + start, len);
      *plen = len;
    }
  }

};

static DebugLog debug_log_;


class FileShareClient : public sigslot::has_slots<>, public talk_base::MessageHandler {
 public:
  FileShareClient(buzz::XmppClient *xmppclient, const buzz::Jid &send_to, const cricket::FileShareManifest *manifest, std::string root_dir) :
    xmpp_client_(xmppclient),
    root_dir_(root_dir),
    send_to_jid_(send_to),
    waiting_for_file_(send_to == buzz::JID_EMPTY),  
    manifest_(manifest) {}

  void OnStateChange(buzz::XmppEngine::State state) {
    switch (state) {
    case buzz::XmppEngine::STATE_START:
      std::cout << "Connecting..." << std::endl;
      break;
    case buzz::XmppEngine::STATE_OPENING:
      std::cout << "Logging in. " << std::endl;
      break;
    case buzz::XmppEngine::STATE_OPEN:
      std::cout << "Logged in as " << xmpp_client_->jid().Str() << std::endl;
      if (!waiting_for_file_)
        std::cout << "Waiting for " << send_to_jid_.Str() << std::endl;
      OnSignon();
      break;
    case buzz::XmppEngine::STATE_CLOSED:
      std::cout << "Logged out." << std::endl;
      break;
    }
  }

 private:

  enum {
    MSG_STOP,
  };
 
  void OnJingleInfo(const std::string & relay_token,
                    const std::vector<std::string> &relay_addresses,
                    const std::vector<talk_base::SocketAddress> &stun_addresses) {
    port_allocator_->SetStunHosts(stun_addresses);
    port_allocator_->SetRelayHosts(relay_addresses);
    port_allocator_->SetRelayToken(relay_token);
  }
							
  
  void OnStatusUpdate(const buzz::Status &status) {
    if (status.available() && status.fileshare_capability()) {

      // A contact's status has changed. If the person we're looking for is online and able to receive
      // files, send it.
      if (send_to_jid_.BareEquals(status.jid())) {
	std::cout << send_to_jid_.Str() << " has signed on." << std::endl;
	cricket::FileShareSession* share = file_share_session_client_->CreateFileShareSession();
	share->Share(status.jid(), const_cast<cricket::FileShareManifest*>(manifest_));
	send_to_jid_ = buzz::Jid("");
      }
      
    }
  }
  
  void OnMessage(talk_base::Message *m) {
    ASSERT(m->message_id == MSG_STOP);
    talk_base::Thread *thread = talk_base::ThreadManager::CurrentThread();
    delete session_;
    thread->Stop();
  }

  std::string filesize_to_string(unsigned int size) {
    double size_display;
    std::string format;
    std::stringstream ret;

    // the comparisons to 1000 * (2^(n10)) are intentional
    // it's so you don't see something like "1023 bytes",
    // instead you'll see ".9 KB"

    if (size < 1000) {
      format = "Bytes";
      size_display = size;
    } else if (size < 1000 * 1024) {
      format = "KiB";
      size_display = (double)size / 1024.0;
    } else if (size < 1000 * 1024 * 1024) {
      format = "MiB";
      size_display = (double)size / (1024.0 * 1024.0);
    } else {
      format = "GiB";
      size_display = (double)size / (1024.0 * 1024.0 * 1024.0);
    }
    
    ret << std::setprecision(1) << std::setiosflags(std::ios::fixed) << size_display << " " << format;    
    return ret.str();
  }
  
  void OnSessionState(cricket::FileShareState state) {
    talk_base::Thread *thread = talk_base::ThreadManager::CurrentThread();
    std::stringstream manifest_description;
	    
    switch(state) {
    case cricket::FS_OFFER:

      // The offer has been made; print a summary of it and, if it's an incoming transfer, accept it

      if (manifest_->size() == 1)
        manifest_description <<  session_->manifest()->item(0).name;
      else if (session_->manifest()->GetFileCount() && session_->manifest()->GetFolderCount())
        manifest_description <<  session_->manifest()->GetFileCount() << " files and " <<
    	           session_->manifest()->GetFolderCount() << " directories";
      else if (session_->manifest()->GetFileCount() > 0)
        manifest_description <<  session_->manifest()->GetFileCount() << " files";
      else
        manifest_description <<  session_->manifest()->GetFolderCount() << " directories"; 

      size_t filesize;
      if (!session_->GetTotalSize(filesize)) {
        manifest_description << " (Unknown size)";
      } else {
        manifest_description << " (" << filesize_to_string(filesize) << ")";
      }    
      if (session_->is_sender()) {
        std::cout << "Offering " << manifest_description.str()  << " to " << send_to_jid_.Str() << std::endl;
      } else if (waiting_for_file_) {
	std::cout << "Receiving " << manifest_description.str() << " from " << session_->jid().BareJid().Str() << std::endl;
	session_->Accept();
	waiting_for_file_ = false;
	
	// If this were a graphical client, we might want to go through the manifest, look for images,
	// and request previews. There are two ways to go about this:
	//
	// If we want to display the preview in a web browser (like the embedded IE control in Google Talk), we could call
	// GetImagePreviewUrl on the session, with the image's index in the manifest, the size, and a pointer to the URL.
	// This will cause the session to listen for HTTP requests on localhost, and set url to a localhost URL that any
	// web browser can use to get the image preview:
	//
	//      std::string url;
	//      session_->GetImagePreviewUrl(0, 100, 100, &url);
	//      url = std::string("firefox \"") + url + "\"";
	//      system(url.c_str());
	//
	// Alternately, you could use libjingle's own HTTP code with the FileShareSession's SocketPool interface to
	// write the image preview directly into a StreamInterface:
	//
	//	talk_base::HttpClient *client = new talk_base::HttpClient("pcp", session_);
	//	std::string path;
	//	session_->GetItemNetworkPath(0,1,&path);
	//	
	//	client->request().verb = talk_base::HV_GET;
	//	client->request().path = path + "?width=100&height=100";
	//	talk_base::FileStream *file = new talk_base::FileStream;
	//	file->Open("/home/username/foo.jpg", "wb");
	//	client->response().document.reset(file);
	//	client->start();
      }
      break;
    case cricket::FS_TRANSFER:
      std::cout << "File transfer started." << std::endl;
      break;
    case cricket::FS_COMPLETE:
      thread->Post(this, MSG_STOP);
      std::cout << std::endl << "File transfer completed." << std::endl;
      break;
    case cricket::FS_LOCAL_CANCEL:
    case cricket::FS_REMOTE_CANCEL:
      std::cout << std::endl << "File transfer cancelled." << std::endl;
      thread->Post(this, MSG_STOP);
      break;
    case cricket::FS_FAILURE:
      std::cout << std::endl << "File transfer failed." << std::endl;
      thread->Post(this, MSG_STOP);
      break;
    }
  }

  void OnUpdateProgress(cricket::FileShareSession *sess) {
    // Progress has occured on the transfer; update the UI
    
    size_t totalsize, progress;
    std::string itemname;
    unsigned int width = 79;

#ifndef WIN32
    struct winsize ws; 
    if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0))
      width = ws.ws_col;
#endif

    if(sess->GetTotalSize(totalsize) && sess->GetProgress(progress) && sess->GetCurrentItemName(&itemname)) {
      float percent = (float)progress / totalsize;
      unsigned int progressbar_width = (width * 4) / 5;
      
      const char *filename = itemname.c_str();
      std::cout.put('\r');
      for (unsigned int l = 0; l < width; l++) {
        if (l < percent * progressbar_width)
	  std::cout.put('#');
      	else if (l > progressbar_width && l < progressbar_width + 1 + strlen(filename))
      	  std::cout.put(filename[l-(progressbar_width + 1)]);
      	else
      	  std::cout.put(' ');
      }
      std::cout.flush();
    }
  }

  void OnResampleImage(std::string path, int width, int height, talk_base::HttpTransaction *trans) {  

    // The other side has requested an image preview. This is an asynchronous request. We should resize
    // the image to the requested size,and send that to ResampleComplete(). For simplicity, here, we
    // send back the original sized image. Note that because we don't recognize images in our manifest
    // this will never be called in pcp

    // Even if you don't resize images, you should implement this method and connect to the 
    // SignalResampleImage signal, just to return an error.    

    talk_base::FileStream *s = new talk_base::FileStream();
    if (s->Open(path.c_str(), "rb"))
      session_->ResampleComplete(s, trans, true);  
    else {
      delete s;
      session_->ResampleComplete(NULL, trans, false);
    }
  }
    
  void OnFileShareSessionCreate(cricket::FileShareSession *sess) {
    session_ = sess;
    sess->SignalState.connect(this, &FileShareClient::OnSessionState);
    sess->SignalNextFile.connect(this, &FileShareClient::OnUpdateProgress);
    sess->SignalUpdateProgress.connect(this, &FileShareClient::OnUpdateProgress);
    sess->SignalResampleImage.connect(this, &FileShareClient::OnResampleImage);
    sess->SetLocalFolder(root_dir_);
  }
  
  void OnSignon() {
    std::string client_unique = xmpp_client_->jid().Str();
    cricket::InitRandom(client_unique.c_str(), client_unique.size());

    buzz::PresencePushTask *presence_push_ = new buzz::PresencePushTask(xmpp_client_);
    presence_push_->SignalStatusUpdate.connect(this, &FileShareClient::OnStatusUpdate);
    presence_push_->Start();
    
    buzz::Status my_status;
    my_status.set_jid(xmpp_client_->jid());
    my_status.set_available(true);
    my_status.set_show(buzz::Status::SHOW_ONLINE);
    my_status.set_priority(0);
    my_status.set_know_capabilities(true);
    my_status.set_fileshare_capability(true);
    my_status.set_is_google_client(true);
    my_status.set_version("1.0.0.66");

    buzz::PresenceOutTask* presence_out_ =
      new buzz::PresenceOutTask(xmpp_client_);
    presence_out_->Send(my_status);
    presence_out_->Start();
    
    port_allocator_.reset(new cricket::HttpPortAllocator(&network_manager_, "pcp"));

    session_manager_.reset(new cricket::SessionManager(port_allocator_.get(), NULL));

    cricket::SessionManagerTask * session_manager_task = new cricket::SessionManagerTask(xmpp_client_, session_manager_.get());
    session_manager_task->EnableOutgoingMessages();
    session_manager_task->Start();
    
    buzz::JingleInfoTask *jingle_info_task = new buzz::JingleInfoTask(xmpp_client_);
    jingle_info_task->RefreshJingleInfoNow();
    jingle_info_task->SignalJingleInfo.connect(this, &FileShareClient::OnJingleInfo);
    jingle_info_task->Start();
    
    file_share_session_client_.reset(new cricket::FileShareSessionClient(session_manager_.get(), xmpp_client_->jid(), "pcp"));
    file_share_session_client_->SignalFileShareSessionCreate.connect(this, &FileShareClient::OnFileShareSessionCreate);
    session_manager_->AddClient(NS_GOOGLE_SHARE, file_share_session_client_.get());
  }
  
  talk_base::NetworkManager network_manager_;
  talk_base::scoped_ptr<cricket::HttpPortAllocator> port_allocator_;
  talk_base::scoped_ptr<cricket::SessionManager> session_manager_;
  talk_base::scoped_ptr<cricket::FileShareSessionClient> file_share_session_client_;
  buzz::XmppClient *xmpp_client_;
  buzz::Jid send_to_jid_;
  const cricket::FileShareManifest *manifest_;
  cricket::FileShareSession *session_;
  bool waiting_for_file_;
  std::string root_dir_;
};

static unsigned int get_dir_size(const char *directory) {
  unsigned int total = 0;
  talk_base::DirectoryIterator iter;
  talk_base::Pathname path;
  path.AppendFolder(directory);
  iter.Iterate(path.pathname());
  while (iter.Next())  {
    if (iter.Name() == "." || iter.Name() == "..")
      continue;
    if (iter.IsDirectory()) {
      path.AppendPathname(iter.Name());
      total += get_dir_size(path.pathname().c_str());
    }
    else
      total += iter.FileSize();
  }
  return total;
}

int main(int argc, char **argv) {
  talk_base::PhysicalSocketServer ss;
  int i;
  bool debug = false;
  bool send_mode = false;
  char cwd[256];
  getcwd(cwd, sizeof(cwd));
  for (i = 1; i < argc && *argv[i] == '-'; i++) {
    if (!strcmp(argv[i], "-d")) {
      debug = true;
    } else {
      std::cout << "USAGE: " << argv[0] << " [-d][-h] [FILE1 FILE2 ... FILE#] [JID]" << std::endl;
      std::cout << "  To send files, specify a list of files to send, followed by the JID of the recipient" << std::endl;
      std::cout << "  To receive files, specify no files or JID" << std::endl;
      std::cout << "COMMAND LINE ARGUMENTS" << std::endl;
      std::cout << "  -h -- Prints this help message" << std::endl;
      std::cout << "  -d -- Prints debug messages to stderr" << std::endl;
      exit(0);
    }
  }
  
  if (debug)
    talk_base::LogMessage::LogToDebug(talk_base::LS_VERBOSE);
  else
    talk_base::LogMessage::LogToDebug(talk_base::LS_ERROR + 1);


  talk_base::InitializeSSL();   
  XmppPump pump;
  buzz::Jid jid;
  buzz::XmppClientSettings xcs;
  talk_base::InsecureCryptStringImpl pass;
  std::string username;

  std::cout << "JID: ";
  std::cin >> username;
  jid = buzz::Jid(username);
  if (!jid.IsValid() || jid.node() == "") {
    printf("Invalid JID. JIDs should be in the form user@domain\n");
    return 1;
  }
  SetConsoleEcho(false);
  std::cout << "Password: ";
  std::cin >> pass.password();
  SetConsoleEcho(true);
  std::cout << std::endl;

  xcs.set_user(jid.node());
  xcs.set_resource("pcp");
  xcs.set_host(jid.domain());
  xcs.set_use_tls(true);
 
  xcs.set_pass(talk_base::CryptString(pass));
  xcs.set_server(talk_base::SocketAddress("talk.google.com", 5222));

  talk_base::Thread main_thread(&ss);
  talk_base::ThreadManager::SetCurrent(&main_thread);
 
  if (debug) {
    pump.client()->SignalLogInput.connect(&debug_log_, &DebugLog::Input);
    pump.client()->SignalLogOutput.connect(&debug_log_, &DebugLog::Output);
  }
  
  cricket::FileShareManifest *manifest = new cricket::FileShareManifest();
 
  for (;i < argc - 1;i++) {
    if (0) {
      printf("%s is not a valid file\n", argv[i]);
      continue;
    }
    send_mode = true;

    // Additionally, we should check for image files here, and call
    // AddImage on the manifest with their file size and image size.
    // The receiving client can then request previews of those images
    if (talk_base::Filesystem::IsFolder(std::string(argv[i]))) {
      manifest->AddFolder(argv[i], get_dir_size(argv[i]));
    } else {
      size_t size = 0;
      talk_base::Filesystem::GetFileSize(std::string(argv[i]), &size);
      manifest->AddFile(argv[i], size);
    }
  }
  buzz::Jid j;
  if (send_mode)
    j = buzz::Jid(argv[argc-1]);
  else
    j = buzz::JID_EMPTY;

  FileShareClient fs_client(pump.client(), j, manifest, cwd);

  pump.client()->SignalStateChange.connect(&fs_client, &FileShareClient::OnStateChange);

  pump.DoLogin(xcs, new XmppSocket(true), NULL);
  main_thread.Run();
  pump.DoDisconnect();
  
  return 0;
}
