#define _MILSKO
#define USE_WAYLAND
#include <Mw/Milsko.h>

MwWidget window_setup(MwRect *bounds) {
  MwWidget window =
      MwCreateWidget(MwWindowClass, NULL, NULL, MwDEFAULT, MwDEFAULT, 1, 32);

  MwGetScreenSize(window, bounds);

  MwLLBeginStateChange(window->lowlevel);
  MwVaApply(window, MwNwidth, bounds->width / 3, MwNheight, 32, NULL);
  MwLLMakeToolWindow(window->lowlevel);
  MwLLWaylandSetToolWindowType(window->lowlevel,
                               ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM);
  MwLLEndStateChange(window->lowlevel);

  MwVaApply(window, MwNx, 0, MwNy, bounds->height - 32, NULL);

  return window;
}

void popup_setup(MwWidget widget, int x, int y) {
  MwPoint point;
  point.x = x;
  point.y = y;

  MwLLBeginStateChange(widget->lowlevel);
  MwLLDetach(widget->lowlevel, &point);
  MwLLMakeToolWindow(widget->lowlevel);
  MwLLEndStateChange(widget->lowlevel);
}
