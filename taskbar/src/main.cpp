#include "window_setup.h"
#include <Mw/Milsko.h>

int main() {
  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  MwLibraryInit();

  MwWidget window = window_setup();

  MwLoop(window);
}
