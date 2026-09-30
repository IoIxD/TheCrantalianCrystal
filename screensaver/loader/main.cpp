#include "Mw/Core.h"
#include "Mw/StringDefs.h"
#include "Mw/Widget/OpenGL.h"
#include <Mw/Milsko.h>
#include <dlfcn.h>
#include <filesystem>
#include <format>
#include <string>

struct Screensaver {
  void *lib = nullptr;
  void *(*init)() = nullptr;
  void (*draw)(void *ctx) = nullptr;
  void (*free)(void *ctx) = nullptr;
  void *ctx = nullptr;

  Screensaver(std::string name) {
    char dest[PATH_MAX];
    memset(dest, 0, sizeof(dest));
    if (readlink("/proc/self/exe", dest, PATH_MAX) == -1) {
      perror("readlink");
    }
    std::filesystem::path fs = dest;

    std::filesystem::path path =
        fs.parent_path() / std::format("lib{}.so", name);

    lib = dlopen(path.string().c_str(), RTLD_LAZY | RTLD_NOW);
    if (!lib) {
      printf("lib %s not found\n", path.string().c_str());
    }
    init = (decltype(init))dlsym(lib, "tcc_scr_init");
    draw = (decltype(draw))dlsym(lib, "tcc_scr_draw");
    free = (decltype(free))dlsym(lib, "tcc_scr_free");

    if (init) {
      ctx = init();
    }
  };

  ~Screensaver() {
    if (free)
      free(ctx);

    dlclose(lib);
  }
};

static struct LoaderContext {
  MwWidget window;
  MwWidget opengl;
  MwRect bounds;
  Screensaver *scr;

  static void MWAPI tick(MwWidget handle, void *user, void *call) {
    LoaderContext *ctx = (LoaderContext *)user;

    MwOpenGLMakeCurrent(ctx->opengl);

    if (ctx->scr->draw)
      ctx->scr->draw(ctx->scr);

    MwOpenGLSwapBuffer(ctx->opengl);
  };
} ctx;

extern "C" {
void tcc_scr_tool_window_setup(MwWidget window, MwRect bounds);
}

int main() {
  MwLibraryInit();

  ctx.window = MwVaCreateWidget(MwWindowClass, NULL, NULL, MwDEFAULT, MwDEFAULT,
                                640, 480, MwNtitle, "Screensaver", NULL);
  MwGetScreenSize(ctx.window, &ctx.bounds);

  // tcc_scr_tool_window_setup(ctx.window, ctx.bounds);

  /* todo: loading custom ones */
  ctx.scr = new Screensaver("tcc_scr_tunnel");

  ctx.opengl = MwCreateWidget(MwOpenGLClass, NULL, ctx.window, 0, 0,
                              ctx.bounds.width, ctx.bounds.height);
  MwAddUserHandler(ctx.window, MwNtickHandler, LoaderContext::tick, &ctx);

  MwLoop(ctx.window);
}
