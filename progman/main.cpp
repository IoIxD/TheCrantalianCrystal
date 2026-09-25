#include "progman.hpp"
#include <Mw/Milsko.h>
#include <cstdio>
#include <gtk/gtk.h>

int main() {
  gtk_init();
  MwLibraryInit();

  ProgmanWindow window;

  window.run();

  return 0;
}
