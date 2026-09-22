#include "client.hpp"
#include <cstdio>
#include <cstdlib>

#include <assert.h>
#include <csignal>
#include <string>

#include "font.hpp"

TCCClient::TCCClient() {
  mDisplay = wl_display_connect(NULL);
  if (mDisplay == nullptr) {
    fprintf(stderr, "failed to connect to Wayland server\n");
    exit(1);
  }

  // Avoid passing WAYLAND_DEBUG on to our children.
  // It only matters if it's set when the display is created.
  unsetenv("WAYLAND_DEBUG");

  // Ensure children (spawned terminals, etc.) are automatically reaped.
  signal(SIGCHLD, SIG_IGN);

  mRegistry = wl_display_get_registry(mDisplay);

  wl_registry_add_listener(mRegistry, &mRegistryListener, this);
  if (wl_display_roundtrip(mDisplay) == -1) {
    fprintf(stderr, "roundtrip failed\n");
    raise(SIGTRAP);
    return;
  }

  if (mRiverWindowManager == nullptr || mRiverXKBBinding == nullptr) {
    fprintf(stderr, "river_window_manager_v1 or river_xkb_bindings_v1 "
                    "not supported by the Wayland server\n");
    exit(1);
  }

  /* freetype2 */
  assert(FT_Init_FreeType(&mFTLibrary) == 0);
  assert(FT_New_Memory_Face(mFTLibrary, OpenSans_Regular.data(),
                            OpenSans_Regular.size(), 0, &mFTFaceNormal) == 0);
  assert(FT_New_Memory_Face(mFTLibrary, OpenSans_Semibold.data(),
                            OpenSans_Semibold.size(), 0, &mFTFaceBold) == 0);
  FT_Set_Pixel_Sizes(mFTFaceNormal, 0, 13);
  FT_Set_Pixel_Sizes(mFTFaceBold, 0, 13);
}

TCCClient::~TCCClient() {
  assert(FT_Done_Face(mFTFaceNormal) == 0);
  assert(FT_Done_FreeType(mFTLibrary) == 0);
}

void TCCClient::registry_global(void *data, struct wl_registry *wl_registry,
                                uint32_t name, const char *interface,
                                uint32_t version) {
  TCCClient *client = (TCCClient *)data;
  std::string inter = interface;

  if (inter == wl_compositor_interface.name) {
    client->mCompositor = (wl_compositor *)wl_registry_bind(
        client->mRegistry, name, &wl_compositor_interface, version);
    client->mRiverWindowSurface =
        wl_compositor_create_surface(client->mCompositor);

  } else if (inter == river_window_manager_v1_interface.name) {
    assert(version >= 4);
    client->mRiverWindowManager = (river_window_manager_v1 *)wl_registry_bind(
        client->mRegistry, name, &river_window_manager_v1_interface, 4);
    river_window_manager_v1_add_listener(
        client->mRiverWindowManager, &client->mRiverWindowManagementListener,
        client);
  } else if (inter == river_layer_shell_v1_interface.name) {
    wl_registry_bind(client->mRegistry, name, &river_layer_shell_v1_interface,
                     version);
  } else if (inter == river_input_manager_v1_interface.name) {
    client->mRiverInputManager = (river_input_manager_v1 *)wl_registry_bind(
        client->mRegistry, name, &river_input_manager_v1_interface, 1);
  } else if (inter == wl_seat_interface.name) {
    // Bound later, once river_seat_v1.wl_seat tells us which seat it is.
    client->mWlSeatVersions[name] = version;
  } else if (inter == wp_cursor_shape_manager_v1_interface.name) {
    client->mCursorShapeManager =
        (wp_cursor_shape_manager_v1 *)wl_registry_bind(
            client->mRegistry, name, &wp_cursor_shape_manager_v1_interface, 1);
  } else if (inter == river_xkb_bindings_v1_interface.name) {
    client->mRiverXKBBinding = (river_xkb_bindings_v1 *)wl_registry_bind(
        client->mRegistry, name, &river_xkb_bindings_v1_interface, 1);
  }

  else {
    if (inter.find("river") != -1) {
      printf("unhandled: %s\n", interface);
    }
  }
};
void TCCClient::global_remove(void *data, struct wl_registry *wl_registry,
                              uint32_t name) {
  TCCClient *client = (TCCClient *)data;
  client->mWlSeatVersions.erase(name);
};

void TCCClient::run() {
  if (!mRiverWindowManager || !mRiverXKBBinding) {
    fprintf(stderr, "river_window_manager_v1 or river_xkb_bindings_v1 "
                    "not supported by the Wayland server\n");
    exit(1);
  }

  while (mRunning) {
    if (wl_display_dispatch(mDisplay) < 0) {
      fprintf(stderr, "dispatch failed\n");
      exit(1);
    }
  }
}
