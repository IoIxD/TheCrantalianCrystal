#ifdef TCC_SYSTRAY_RIVER
#include "wayland_loader.hpp"

#include <cstdarg>
#include <cstdio>
#include <dlfcn.h>
#include <initializer_list>

bool WaylandLib::load() {
  for (const char *soname :
       {"libwayland-client.so.0", "libwayland-client.so"}) {
    mHandle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
    if (mHandle)
      break;
  }
  if (!mHandle) {
    fprintf(stderr, "tcc_systray: could not load libwayland-client: %s\n",
            dlerror());
    return false;
  }

#define X(name)                                                                \
  name = reinterpret_cast<decltype(name)>(dlsym(mHandle, #name));              \
  if (!name) {                                                                 \
    fprintf(stderr, "tcc_systray: libwayland-client is missing symbol %s\n",   \
            #name);                                                            \
    dlclose(mHandle);                                                          \
    mHandle = nullptr;                                                         \
    return false;                                                              \
  }
  TCC_WAYLAND_FUNCS(X)
#undef X

  return true;
}

WaylandLib *WaylandLib::get() {
  static WaylandLib lib;
  static bool loaded = lib.load();
  return loaded ? &lib : nullptr;
}

// The functions the protocol headers' inline wrappers call, forwarded to the
// dynamically loaded library.
extern "C" {

// WL_CLOSURE_MAX_ARGS in libwayland
#define MAX_ARGS 20

// The variadic arguments are read the way libwayland does it, using the
// request's signature, and passed on to the array version.
struct wl_proxy *wl_proxy_marshal_flags(struct wl_proxy *proxy, uint32_t opcode,
                                        const struct wl_interface *interface,
                                        uint32_t version, uint32_t flags, ...) {
  auto wl = WaylandLib::get();
  const char *signature =
      wl->wl_proxy_get_interface(proxy)->methods[opcode].signature;

  union wl_argument args[MAX_ARGS];
  int count = 0;

  va_list ap;
  va_start(ap, flags);
  for (const char *c = signature; *c && count < MAX_ARGS; c++) {
    switch (*c) {
    case 'i':
      args[count++].i = va_arg(ap, int32_t);
      break;
    case 'u':
      args[count++].u = va_arg(ap, uint32_t);
      break;
    case 'f':
      args[count++].f = va_arg(ap, wl_fixed_t);
      break;
    case 's':
      args[count++].s = va_arg(ap, const char *);
      break;
    case 'o':
      args[count++].o = va_arg(ap, struct wl_object *);
      break;
    case 'n':
      args[count++].o = va_arg(ap, struct wl_object *);
      break;
    case 'a':
      args[count++].a = va_arg(ap, struct wl_array *);
      break;
    case 'h':
      args[count++].h = va_arg(ap, int32_t);
      break;
    default:
      /* a '?' (nullable) or the version number the signature starts with */
      break;
    }
  }
  va_end(ap);

  return wl->wl_proxy_marshal_array_flags(proxy, opcode, interface, version,
                                          flags, args);
}

int wl_proxy_add_listener(struct wl_proxy *proxy, void (**implementation)(void),
                          void *data) {
  return WaylandLib::get()->wl_proxy_add_listener(proxy, implementation, data);
}

void wl_proxy_destroy(struct wl_proxy *proxy) {
  WaylandLib::get()->wl_proxy_destroy(proxy);
}

uint32_t wl_proxy_get_version(struct wl_proxy *proxy) {
  return WaylandLib::get()->wl_proxy_get_version(proxy);
}
}
#endif
