#pragma once

#include "convert.hpp"
#include "introspection.hpp"

#include <functional>
#include <list>
#include <memory>
#include <poll.h>
#include <string>
#include <vector>

/*
 * Serves the org.freedesktop.impl.portal.* interfaces on D-Bus by forwarding
 * everything to their net.ioi-xd.tcc.portal.* varlink equivalents:
 *
 * - Method calls go to the method of the same name, with the D-Bus args as
 *   parameters of the same name; the reply's fields become the out args.
 *   Request and Session calls also get `path`, the object they were made on.
 * - Properties come from GetProperties and go to SetProperty.
 * - Signals come from replies to Subscribe.
 * - A varlink error Foo.Bar.<Name> becomes org.freedesktop.portal.Error.<Name>.
 *
 * It knows nothing about the portals beyond their introspection XML.
 */
class TCCPortalBridge {
  struct Call;

  DBusConnection *mBus;
  std::string mAddress;
  std::vector<TCCDBusInterface> mInterfaces;
  std::string mIntrospectXML;
  std::string mObjectIntrospectXML;
  // Varlink calls in flight, each on its own connection: a varlink
  // connection only does one call at a time, and some can take as long as
  // the user does.
  std::list<std::unique_ptr<Call>> mCalls;

  static DBusHandlerResult filter(DBusConnection *bus, DBusMessage *msg,
                                  void *userdata);
  void forward(DBusMessage *msg, const TCCDBusInterface &iface,
               const TCCDBusMember &method);
  void properties(DBusMessage *msg, const char *member);
  void subscribe(const TCCDBusInterface &iface);

  Call *call(const TCCDBusInterface &iface, const char *method,
             VarlinkObject *parameters, uint64_t flags);
  void send(DBusMessage *msg);

public:
  // `address` is where the varlink service listens.
  TCCPortalBridge(DBusConnection *bus, std::string address);
  ~TCCPortalBridge();

  // Call once the varlink service is up.
  bool start();
  // What to poll for the varlink side.
  void add_pollfds(std::vector<pollfd> &fds);
  // Handles the results of polling the fds from add_pollfds(), which start
  // at `first`.
  void dispatch(const std::vector<pollfd> &fds, size_t first);
};
