#include "systray.hpp"
#include <Mw/Milsko.h>

#include "dynload.hpp"

int main() {
  if (!dynload_setup::dbus() || !dynload_setup::pulse() ||
      !dynload_setup::wayland() || !dynload_setup::gtk()) {
    return 1;
  }

  MwLibraryInit();

  TCCSystrayClient systray;

  systray.run();
}
