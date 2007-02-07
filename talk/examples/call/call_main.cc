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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <time.h>
#include <iomanip>
#include "talk/base/logging.h"
#include "talk/base/physicalsocketserver.h"
#include "talk/base/ssladapter.h"
#include "talk/xmpp/xmppclientsettings.h"
#include "talk/examples/login/xmppthread.h"
#include "talk/examples/login/xmppauth.h"
#include "talk/examples/call/callclient.h"
#include "talk/examples/call/console.h"

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


int main(int argc, char **argv) {
  // This app has three threads. The main thread will run the XMPP client, 
  // which will print to the screen in its own thread. A second thread 
  // will get input from the console, parse it, and pass the appropriate
  // message back to the XMPP client's thread. A third thread is used
  // by PhoneSessionClient as its worker thread.

  bool debug = false;
  if (argc > 1 && !strcmp(argv[1], "-d"))
    debug = true;
  
  if (debug)
    talk_base::LogMessage::LogToDebug(talk_base::LS_VERBOSE);


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
  xcs.set_resource("call");
  xcs.set_host(jid.domain());
  xcs.set_use_tls(true);
 
  xcs.set_pass(talk_base::CryptString(pass));
  xcs.set_server(talk_base::SocketAddress("talk.google.com", 5222));
  printf("Logging in as %s\n", jid.Str().c_str());

  talk_base::PhysicalSocketServer ss;

  CallClient *client = new CallClient(pump.client());
  
  talk_base::Thread main_thread(&ss);
  talk_base::ThreadManager::SetCurrent(&main_thread);
  Console *console = new Console(&main_thread, client);
  client->SetConsole(console);
  talk_base::Thread *console_thread = new talk_base::Thread(&ss);
  console_thread->Start();
  console_thread->Post(console, MSG_START);

  if (debug) {
    pump.client()->SignalLogInput.connect(&debug_log_, &DebugLog::Input);
    pump.client()->SignalLogOutput.connect(&debug_log_, &DebugLog::Output);
  }

  pump.DoLogin(xcs, new XmppSocket(true), NULL);
  main_thread.Run();

  return 0;
}
