#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

#include <string>
#include <iostream>

#include <AsyncCppApplication.h>
#include <AsyncConfig.h>
#include <AsyncTimer.h>
#include <AsyncFdWatch.h>

#include <version/SVXLINK.h>

#include "LocalRx.h"
#include "LocalTx.h"
#include "MsgHandler.h"
#include "SimplexLogic.h"
#include "RepeaterLogic.h"

using namespace std;
using namespace Async;
using namespace SigC;


LocalTx *tx = 0;
Logic *logic = 0;
MsgHandler *msg_handler = 0;



void squelch_open(bool is_open)
{
  printf("Squlech %s\n", is_open ? "open" : "closed");
  
}


int audio_received(short *samples, int count)
{
  printf("Received %d samples...\n", count);
  return count;
}


void all_audio_flushed(Timer *t)
{
  printf("All audio flushed. samplesToWrite=%d\n",
      	  tx->samplesToWrite());
  delete t;
  msg_handler->playMsg("Core", "online");
}


void all_msgs_written(void)
{
  printf("Message has been written. samplesToWrite=%d\n",
      	  tx->samplesToWrite());
  Timer *timer = new Timer(5000);
  timer->expired.connect(slot(&all_audio_flushed));
}


void dtmf_digit_detected(char digit)
{
  printf("DTMF digit detected: %c\n", digit);
  
}


void stdinHandler(FdWatch *w)
{
  char buf[1];
  int cnt = ::read(STDIN_FILENO, buf, 1);
  if (cnt == -1)
  {
    fprintf(stderr, "*** error reading from stdin\n");
    Application::app().quit();
    return;
  }
  else if (cnt == 0)
  {
    Application::app().quit();
    return;
  }
  
  switch (toupper(buf[0]))
  {
    case 'Q':
      Application::app().quit();
      break;
    
    case '\n':
      putchar('\n');
      break;
    
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7':
    case '8': case '9': case 'A': case 'B':
    case 'C': case 'D': case '*': case '#':
      logic->dtmfDigitDetected(buf[0]);
      break;
    
    default:
      break;
  }
  
}


void initialize_logics(Config &cfg)
{
  string logics;
  if (!cfg.getValue("GLOBAL", "LOGICS", logics) || logics.empty())
  {
    cerr << "*** ERROR: Config variable GLOBAL/LOGICS is not set\n";
    exit(1);
  }

  string::iterator comma;
  string::iterator begin = logics.begin();
  do
  {
    comma = find(begin, logics.end(), ',');
    string logic_name;
    if (comma == logics.end())
    {
      logic_name = string(begin, logics.end());
    }
    else
    {
      logic_name = string(begin, comma);
      begin = comma + 1;
    }
    
    string logic_type;
    if (!cfg.getValue(logic_name, "TYPE", logic_type) || logic_type.empty())
    {
      cerr << "*** ERROR: Logic TYPE not specified for logic \""
      	   << logic_name << "\". Skipping...\n";
      continue;
    }
    if (logic_type == "Simplex")
    {
      logic = new SimplexLogic(cfg, logic_name);
    }
    else if (logic_type == "Repeater")
    {
      logic = new RepeaterLogic(cfg, logic_name);
    }
    else
    {
      cerr << "*** ERROR: Unknown logic type \"" << logic_type
      	   << "\"specified.\n";
      continue;
    }
    if (!logic->initialize())
    {
      cerr << "*** ERROR: Could not initialize Logic object \""
      	   << logic_name << "\". Skipping...\n";
      delete logic;
    }
    
  } while (comma != logics.end());
}


int main(int argc, char **argv)
{
  cout << "SvxLink v" SVXLINK_VERSION " (" __DATE__ ") starting up...\n";
  
  CppApplication app;
  
  char *home_dir = getenv("HOME");
  if (home_dir == NULL)
  {
    home_dir = ".";
  }
  
  string cfg_filename(home_dir);
  cfg_filename += "/.svxlinkrc";
  Config cfg;
  if (cfg.open(cfg_filename))
  {
    cout << "Using config file: " << cfg_filename << endl;
  }
  else
  {
    cfg_filename = "/etc/svxlink.conf";
    if (cfg.open(cfg_filename))
    {
      cout << "Using config file: " << cfg_filename << endl;
    }
    else
    {
      cerr << "*** Error: Could not open config file. Tried both "
      	   << "\"" << home_dir << "/.svxlinkrc\" and "
	   << "\"/etc/svxlink.conf\"\n";
      exit(1);
    }
  }
  
#if 1
  initialize_logics(cfg);
  
  struct termios termios, org_termios;
  tcgetattr(STDIN_FILENO, &org_termios);
  termios = org_termios;
  termios.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &termios);

  FdWatch *stdin_watch = new FdWatch(STDIN_FILENO, FdWatch::FD_WATCH_RD);
  stdin_watch->activity.connect(slot(&stdinHandler));

  app.exec();
  
  delete stdin_watch;
  tcsetattr(STDIN_FILENO, TCSANOW, &org_termios);

  delete logic;
  
#else

  LocalRx rx(cfg, "RxTelcom");
  if (!rx.initialize())
  {
    printf("*** Error: Could not initialize RX object\n");
    exit(1);
  }
  rx.squelchOpen.connect(slot(&squelch_open));
  rx.audioReceived.connect(slot(&audio_received));
  rx.dtmfDigitDetected.connect(slot(&dtmf_digit_detected));
  rx.mute(true);
  
  tx = new LocalTx(cfg, "TxTelcom");
  if (!tx->initialize())
  {
    printf("*** Error: Could not initialize TX object\n");
    exit(1);
  }
  tx->transmit(true);
  
  msg_handler = new MsgHandler;
  msg_handler->writeAudio.connect(slot(tx, &Tx::transmitAudio));
  tx->transmitBufferFull.connect(
      	  slot(msg_handler, &MsgHandler::writeBufferFull));
  //msg_handler->playMsg("Default", "test");
  msg_handler->playMsg("Core", "online");
  msg_handler->allMsgsWritten.connect(slot(&all_msgs_written));
  
  app.exec();
  
#endif
  
  return 0;
  
} /* main */




