#include "clock.hpp"

#include "dynload.hpp"

#include <cstdlib>

// argv[1], if it's there, is the wl_output global of the output the clock
// goes on.
int main(int argc, char **argv) {
  if (!dynload_setup::wayland() || !dynload_setup::egl() ||
      !dynload_setup::gl()) {
    return 1;
  }

  ClockWindow clock(argc > 1 ? strtoul(argv[1], NULL, 10) : 0);
  if (!clock.setup()) {
    return 1;
  }
  clock.run();

  return 0;
}
