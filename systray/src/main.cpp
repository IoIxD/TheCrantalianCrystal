#include "systray.hpp"
#include <Mw/Milsko.h>

#include "dynload.hpp"

int main() {
  if (!dynload_setup::gtk()) {
    return 1;
  }

  bool has_wl = dynload_setup::wayland();
  bool has_dbus = dynload_setup::dbus();
  bool has_pulse = dynload_setup::pulse();

  MwLibraryInit();

  TCCSystrayClient systray = TCCSystrayClient(has_dbus, has_pulse, has_wl);

  systray.run();
}
