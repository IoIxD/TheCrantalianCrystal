#include "client.hpp"
#include <cstdio>
#include <cstdlib>

#include <assert.h>
#include <cerrno>
#include <csignal>
#include <poll.h>
#include <string>

#include "../desktop/desktop.hpp"
#include "../lock/lock.hpp"
#include "../utils/glyph.hpp"
#include "../varlink/daemon.hpp"
#include <dlfcn.h>

TCCClient::TCCClient() {
  mDisplay = wl_display_connect(NULL);
  if (mDisplay == nullptr) {
    fprintf(stderr, "failed to connect to Wayland server\n");
    raise(SIGTRAP);
  }

  // Avoid passing WAYLAND_DEBUG on to our children.
  // It only matters if it's set when the display is created.
  unsetenv("WAYLAND_DEBUG");

  // Children we spawn are reaped in run(). SIGCHLD isn't ignored for that
  // since it would break anything else in this process that waits on its own
  // subprocesses (e.g. gdk-pixbuf's glycin loaders); the no-op handler just
  // interrupts poll() so exited children get reaped promptly.
  struct sigaction sa = {};
  sa.sa_handler = [](int) {};
  sigemptyset(&sa.sa_mask);
  sigaction(SIGCHLD, &sa, nullptr);

  mLock = new TCCLock(this);

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
    raise(SIGTRAP);
  }

  GlyphManager::Init();

  mLock->start();

  mVarlink = new TCCClientVarlink(this);
  mThread = new std::thread([&]() { mVarlink->run(); });

  // g_desktop_app_info_new();
}

TCCClient::~TCCClient() { GlyphManager::Deinit(); }

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

  } else if (inter == wl_subcompositor_interface.name) {
    client->mSubcompositor = (wl_subcompositor *)wl_registry_bind(
        client->mRegistry, name, &wl_subcompositor_interface, 1);
  } else if (inter == wl_shm_interface.name) {
    client->mShm = (wl_shm *)wl_registry_bind(client->mRegistry, name,
                                              &wl_shm_interface, 1);
  } else if (inter == river_window_manager_v1_interface.name) {
    assert(version >= 4);
    client->mRiverWindowManager = (river_window_manager_v1 *)wl_registry_bind(
        client->mRegistry, name, &river_window_manager_v1_interface, 4);
    river_window_manager_v1_add_listener(
        client->mRiverWindowManager, &client->mRiverWindowManagementListener,
        client);
  } else if (inter == river_layer_shell_v1_interface.name) {
    client->mRiverLayerShell = (river_layer_shell_v1 *)wl_registry_bind(
        client->mRegistry, name, &river_layer_shell_v1_interface, version);
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
  } else if (inter == ext_session_lock_manager_v1_interface.name) {
    client->mLock->bind_manager(name);
  } else if (inter == zwp_linux_dmabuf_v1_interface.name) {
    client->mLock->bind_dmabuf(name, version);
  } else if (inter == ext_idle_notifier_v1_interface.name) {
    client->mLock->bind_idle_notifier(name);
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
    raise(SIGTRAP);
  }

  while (mRunning) {
    reap_children();

    std::vector<pollfd> fds;
    std::vector<wl_display *> displays = {mDisplay};

    for (Output *output : mOutputs) {
      if (output->desktop_client) {
        // output->desktop_client->step();
        displays.push_back(output->desktop_client->display());
      }
    }

    for (wl_display *display : displays) {
      while (wl_display_prepare_read(display) != 0) {
        if (wl_display_dispatch_pending(display) < 0) {
          fprintf(stderr, "dispatch failed\n");
          raise(SIGTRAP);
        }
      }
      wl_display_flush(display);
      fds.push_back({wl_display_get_fd(display), POLLIN, 0});
    }

    // After the displays', so fds[i] still lines up with displays[i].
    mLock->add_poll_fds(fds);

    if (poll(fds.data(), fds.size(), -1) < 0) {
      if (errno != EINTR) {
        perror("poll");
        raise(SIGTRAP);
      }
      for (pollfd &fd : fds) {
        fd.revents = 0;
      }
    }

    for (size_t i = displays.size(); i-- > 0;) {
      wl_display *display = displays[i];
      if (fds[i].revents & (POLLERR | POLLHUP)) {
        wl_display_cancel_read(display);
        fprintf(stderr, "wayland connection lost\n");
        raise(SIGTRAP);
      }
      if (fds[i].revents & POLLIN) {
        if (wl_display_read_events(display) < 0) {
          fprintf(stderr, "read events failed\n");
          raise(SIGTRAP);
        }
      } else {
        wl_display_cancel_read(display);
      }
      if (wl_display_dispatch_pending(display) < 0) {
        fprintf(stderr, "dispatch failed: %d\n", wl_display_get_error(display));
        raise(SIGTRAP);
      }
    }

    mLock->process();
  }
}

void TCCClient::lock() { mLock->request_lock(); }

void TCCClient::launch_initial_components() {
  launch_component("tcc_registry");
  launch_component("tcc_portal");
  launch_component("tcc_systray");
  launch_component("tcc_progman");
  launch_component("tcc_desktop_varlink_daemon");
  launch_kwallet();
  // mProgmanThread = std::thread([&]() {
  //   extern __attribute__((visibility("default"))) int tcc_program_main(
  //       void (*close_callback)(void *user), void *user);
  //   tcc_program_main(
  //       +[](void *user) {
  //         TCCClient *cli = (TCCClient *)user;
  //         cli->terminate();
  //       },
  //       this);
  // });
}
