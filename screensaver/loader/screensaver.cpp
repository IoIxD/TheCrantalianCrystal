#include "screensaver.hpp"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <format>
#include <limits.h>
#include <unistd.h>

Screensaver::Screensaver(std::string name) {
  char dest[PATH_MAX];
  memset(dest, 0, sizeof(dest));
  if (readlink("/proc/self/exe", dest, PATH_MAX) == -1) {
    perror("readlink");
  }
  std::filesystem::path fs = dest;

  std::filesystem::path path = fs.parent_path() / std::format("lib{}.so", name);

  lib = dlopen(path.string().c_str(), RTLD_LAZY | RTLD_NOW);
  if (!lib) {
    printf("lib %s not found\n", path.string().c_str());
    return;
  }
  init = (decltype(init))dlsym(lib, "tcc_scr_init");
  draw = (decltype(draw))dlsym(lib, "tcc_scr_draw");
  free = (decltype(free))dlsym(lib, "tcc_scr_free");

  if (init) {
    ctx = init();
  }
};

Screensaver::~Screensaver() {
  if (free)
    free(ctx);

  if (lib)
    dlclose(lib);
}
