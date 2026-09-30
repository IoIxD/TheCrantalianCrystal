#define USE_WAYLAND
#define _MILSKO
#include <Mw/Milsko.h>
void tcc_scr_tool_window_setup(MwWidget window, MwRect bounds) {
  MwLLBeginStateChange(window->lowlevel);
  MwLLMakeToolWindow(window->lowlevel);
  MwLLWaylandSetToolWindowType(window->lowlevel, ZWLR_LAYER_SHELL_V1_LAYER_TOP);
  MwVaApply(window, MwNx, 0, MwNy, 0, MwNwidth, bounds.width, MwNheight,
            bounds.height, NULL);
  MwLLEndStateChange(window->lowlevel);
  MwVaApply(window, MwNx, 0, MwNy, 0, MwNwidth, bounds.width, MwNheight,
            bounds.height, NULL);
}
