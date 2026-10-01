/*
 * tcc_portal: xdg-desktop-portal backend for TheCrantalianCrystal. Started by
 * D-Bus activation when xdg-desktop-portal first needs it.
 *
 * The portals themselves are varlink interfaces (service/), served on
 * $XDG_RUNTIME_DIR/tcc-portal.varlink; D-Bus only sees them through a
 * generic bridge (bridge/).
 */
#include "bridge/bridge.hpp"
#include "dynload.hpp"
#include "service/portal.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

static const char *BUS_NAME = "org.freedesktop.impl.portal.desktop.tcc";

int main() {
  if (!dynload_setup::dbus() || !dynload_setup::varlink())
    return 1;

  const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
  if (!runtime_dir || !*runtime_dir) {
    fprintf(stderr, "tcc_portal: XDG_RUNTIME_DIR isn't set\n");
    return 1;
  }
  std::string socket_path = std::string(runtime_dir) + "/tcc-portal.varlink";
  std::string address = "unix:" + socket_path;

  DBusError err;
  dbus_error_init(&err);
  DBusConnection *bus = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
  if (!bus) {
    fprintf(stderr, "tcc_portal: could not connect to the session bus: %s\n",
            err.message);
    dbus_error_free(&err);
    return 1;
  }
  dbus_connection_set_exit_on_disconnect(bus, false);

  int ret = dbus_bus_request_name(bus, BUS_NAME, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                  &err);
  if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
    fprintf(stderr, "tcc_portal: could not own %s%s%s\n", BUS_NAME,
            dbus_error_is_set(&err) ? ": " : "",
            dbus_error_is_set(&err) ? err.message : "");
    dbus_error_free(&err);
    return 1;
  }

  // Owning the bus name means any socket there is a leftover.
  unlink(socket_path.c_str());
  TCCPortal &portal = TCCPortal::get();
  if (!portal.start(address.c_str()))
    return 1;

  // Anything that came in meanwhile waits for the first dispatch.
  TCCPortalBridge bridge(bus, address);
  if (!bridge.start())
    return 1;

  int bus_fd = -1;
  if (!dbus_connection_get_unix_fd(bus, &bus_fd))
    return 1;

  for (;;) {
    while (dbus_connection_dispatch(bus) == DBUS_DISPATCH_DATA_REMAINS)
      ;
    dbus_connection_flush(bus);

    std::vector<pollfd> fds = {{bus_fd, POLLIN, 0},
                               {portal.fd(), POLLIN, 0}};
    bridge.add_pollfds(fds);
    if (poll(fds.data(), fds.size(), -1) < 0) {
      if (errno == EINTR)
        continue;
      perror("tcc_portal: poll");
      break;
    }

    if (fds[0].revents && !dbus_connection_read_write(bus, 0))
      break;
    if (fds[1].revents)
      portal.process();
    bridge.dispatch(fds, 2);
  }

  unlink(socket_path.c_str());
  return 0;
}
