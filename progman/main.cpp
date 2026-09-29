#include "gtk_loader.hpp"
#include "progman.hpp"
#include <Mw/Milsko.h>
#include <cstdio>

extern __attribute__((visibility("default"))) int
tcc_program_main(void (*close_callback)(void *user), void *user) {
  auto gtk = GtkLib::get();
  if (!gtk)
    return 1;

  gtk->gtk_init();
  MwLibraryInit();

  auto win = ProgmanWindow(close_callback, user);

  win.setup();

  win.run();

  return 0;
}
