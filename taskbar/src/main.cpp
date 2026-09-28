#include "window_setup.h"
#include <Mw/Milsko.h>

int main() {
  MwLibraryInit();

  MwWidget window = window_setup();

  MwLoop(window);
}
