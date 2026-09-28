#define _MILSKO
#define USE_WAYLAND
#include <Mw/Milsko.h>

MwWidget window_setup() {
  MwWidget window =
      MwCreateWidget(MwWindowClass, NULL, NULL, MwDEFAULT, MwDEFAULT, 1, 16);

  MwRect bounds;
  MwGetScreenSize(window, &bounds);
  MwVaApply(window, MwNwidth, bounds.width / 3, NULL);

  MwLLBeginStateChange(window->lowlevel);
  MwLLMakeToolWindow(window->lowlevel);
  MwLLWaylandSetToolWindowType(window->lowlevel,
                               ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM);
  MwLLEndStateChange(window->lowlevel);

  return window;
}
