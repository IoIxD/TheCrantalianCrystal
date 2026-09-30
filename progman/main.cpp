#include "gtk_loader.hpp"
#include "progman.hpp"
#include <Mw/Milsko.h>
#include <cstdio>

#include "dynload.hpp"

// extern __attribute__((visibility("default"))) int
// tcc_program_main(void (*close_callback)(void *user), void *user) {
int main() {
  if (!dynload_setup::gtk() || !dynload_setup::egl() || !dynload_setup::gl() ||
      !dynload_setup::wayland() || !dynload_setup::freetype()) {
    return 1;
  };

  gtk_init();
  MwLibraryInit();

  auto win = ProgmanWindow(/*close_callback, user*/);

  win.setup();

  win.run();

  return 0;
}
