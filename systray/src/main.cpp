#include "systray.hpp"
#include <Mw/Milsko.h>

int main() {
  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  MwLibraryInit();

  TCCSystrayClient systray;

  systray.run();
}
