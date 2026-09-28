#define _MILSKO
#define USE_WAYLAND
#include <Mw/Milsko.h>

MwWidget window_setup() {
  MwWidget window =
      MwCreateWidget(MwWindowClass, NULL, NULL, MwDEFAULT, MwDEFAULT, 1, 32);

  MwRect bounds;
  MwGetScreenSize(window, &bounds);

  MwLLBeginStateChange(window->lowlevel);
  MwVaApply(window, MwNwidth, bounds.width / 3, MwNheight, 32, NULL);
  MwLLMakeToolWindow(window->lowlevel);
  MwLLEndStateChange(window->lowlevel);

  MwVaApply(window, MwNx, 0, MwNy, bounds.height - 32, NULL);

  return window;
}
