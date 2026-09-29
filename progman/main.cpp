#include "gtk_loader.hpp"
#include "progman.hpp"
#include <Mw/Milsko.h>
#include <cstdio>

int main() {
  auto gtk = GtkLib::get();
  if (!gtk)
    return 1;

  gtk->gtk_init();
  MwLibraryInit();

  auto win = ProgmanWindow();

  win.setup();

  win.run();

  return 0;
}
