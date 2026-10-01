#pragma once

#include "registry.hpp"
#include "varlink_loader.hpp"

#include <optional>
#include <string>
#include <vector>

/*
 * The varlink side of the portals: net.ioi-xd.tcc.portal.*, see interfaces/.
 *
 * Each method is a static member below, with the TCCPortal as userdata, and
 * each interface has a file of its own that implements its methods and adds
 * it to the service. Methods that aren't implemented yet reply with noop().
 */

// Calls to a Subscribe method, kept open to send signals through.
class TCCPortalSubscribers {
  std::vector<VarlinkCall *> mCalls;

  static void closed(VarlinkCall *call, void *userdata);

public:
  // A Subscribe method, with the TCCPortalSubscribers as userdata.
  static long subscribe(VarlinkService *service, VarlinkCall *call,
                        VarlinkObject *parameters, uint64_t flags,
                        void *userdata);

  // Sends signal `name` to every subscriber.
  void emit(const char *name, VarlinkObject *args);
};

class TCCPortal {
  VarlinkService *mService = nullptr;

  TCCPortalSubscribers mInhibitSubscribers;
  TCCPortalSubscribers mNotificationSubscribers;
  TCCPortalSubscribers mSessionSubscribers;
  TCCPortalSubscribers mSettingsSubscribers;

  TCCRegistryConnection *mRegistry;
  // For SettingChanged.
  TCCRegistrySubscription *mRegistrySubscription = nullptr;
  // Dark Theme as last seen, to tell whether it changed.
  std::optional<bool> mDarkTheme;

  TCCPortal() = default;
  ~TCCPortal();

  // Replies with the JSON object `reply`, or with the interface's error named
  // by it.
  static long noop(VarlinkCall *call, const char *reply);
  // Logs a varlink_service_add_interface() failure.
  static bool checkInterface(const char *name, long error);

  bool addInhibit();
  bool addNotification();
  bool addRequest();
  bool addSession();
  bool addSettings();

  // Emits SettingChanged if `dark` is new.
  void updateDarkTheme(std::optional<bool> dark);

  static long InhibitInhibit(VarlinkService *service, VarlinkCall *call,
                             VarlinkObject *parameters, uint64_t flags,
                             void *userdata);
  static long InhibitCreateMonitor(VarlinkService *service, VarlinkCall *call,
                                   VarlinkObject *parameters, uint64_t flags,
                                   void *userdata);
  static long InhibitQueryEndResponse(VarlinkService *service,
                                      VarlinkCall *call,
                                      VarlinkObject *parameters, uint64_t flags,
                                      void *userdata);

  // net.ioi-xd.tcc.portal.Notification
  static long NotificationAddNotification(VarlinkService *service,
                                          VarlinkCall *call,
                                          VarlinkObject *parameters,
                                          uint64_t flags, void *userdata);
  static long NotificationRemoveNotification(VarlinkService *service,
                                             VarlinkCall *call,
                                             VarlinkObject *parameters,
                                             uint64_t flags, void *userdata);
  static long NotificationGetProperties(VarlinkService *service,
                                        VarlinkCall *call,
                                        VarlinkObject *parameters,
                                        uint64_t flags, void *userdata);

  // net.ioi-xd.tcc.portal.Request
  static long RequestClose(VarlinkService *service, VarlinkCall *call,
                           VarlinkObject *parameters, uint64_t flags,
                           void *userdata);

  // net.ioi-xd.tcc.portal.Session
  static long SessionClose(VarlinkService *service, VarlinkCall *call,
                           VarlinkObject *parameters, uint64_t flags,
                           void *userdata);
  static long SessionGetProperties(VarlinkService *service, VarlinkCall *call,
                                   VarlinkObject *parameters, uint64_t flags,
                                   void *userdata);

  // net.ioi-xd.tcc.portal.Settings
  static long SettingsReadAll(VarlinkService *service, VarlinkCall *call,
                              VarlinkObject *parameters, uint64_t flags,
                              void *userdata);
  static long SettingsRead(VarlinkService *service, VarlinkCall *call,
                           VarlinkObject *parameters, uint64_t flags,
                           void *userdata);
  static long SettingsGetProperties(VarlinkService *service, VarlinkCall *call,
                                    VarlinkObject *parameters, uint64_t flags,
                                    void *userdata);

public:
  TCCPortal(const TCCPortal &) = delete;
  TCCPortal &operator=(const TCCPortal &) = delete;

  static TCCPortal &get();

  // Starts serving on `address`.
  bool start(const char *address);
  // What to poll for, and what to call when it's readable.
  int fd();
  void process();

  // The same for tcc_registry, which may not be (or stay) up: subscribe until
  // that works, and in the meantime registryFd() is -1.
  bool subscribeRegistry();
  int registryFd();
  short registryEvents();
  void processRegistry(short revents);
};
