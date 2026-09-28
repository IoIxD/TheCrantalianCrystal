#ifdef TCC_SYSTRAY_DBUS
#include "network_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>

namespace {

constexpr const char *NM_NAME = "org.freedesktop.NetworkManager";
constexpr const char *NM_PATH = "/org/freedesktop/NetworkManager";
constexpr const char *NM_IFACE = "org.freedesktop.NetworkManager";
constexpr const char *ACTIVE_IFACE =
    "org.freedesktop.NetworkManager.Connection.Active";
constexpr const char *DEVICE_IFACE = "org.freedesktop.NetworkManager.Device";
constexpr const char *WIRELESS_IFACE =
    "org.freedesktop.NetworkManager.Device.Wireless";
constexpr const char *AP_IFACE = "org.freedesktop.NetworkManager.AccessPoint";
constexpr const char *PROPS_IFACE = "org.freedesktop.DBus.Properties";
constexpr const char *ITEM_ID = "network";

// NMState
constexpr uint32_t STATE_CONNECTING = 40;
constexpr uint32_t STATE_CONNECTED_LOCAL = 50;
constexpr uint32_t STATE_CONNECTED_GLOBAL = 70;
// NMConnectivityState
constexpr uint32_t CONNECTIVITY_FULL = 4;
// NMDeviceType
constexpr uint32_t DEVICE_TYPE_WIFI = 2;

// Menu entry ids; networks are NETWORK_ENTRY + their index.
constexpr int WIFI_TOGGLE_ENTRY = 1;
constexpr int DISCONNECT_ENTRY = 2;
constexpr int NETWORK_ENTRY = 100;
constexpr size_t MAX_NETWORKS = 15;

bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

} // namespace

bool NetworkManagerIcon::connect() {
  if (!mBus.open(DBUS_BUS_SYSTEM))
    return false;

  mBus.setMessageHandler([this](DBusMessage *msg) { return handleMessage(msg); });
  mBus.addMatch("type='signal',sender='org.freedesktop.NetworkManager',"
                "interface='org.freedesktop.DBus.Properties',"
                "member='PropertiesChanged'");
  // To refetch if NetworkManager restarts.
  mBus.addMatch("type='signal',sender='org.freedesktop.DBus',"
                "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
                "arg0='org.freedesktop.NetworkManager'");
  fetch();
  return true;
}

bool NetworkManagerIcon::handleMessage(DBusMessage *msg) {
  DBusLib *d = mBus.lib();
  if (d->dbus_message_is_signal(msg, PROPS_IFACE, "PropertiesChanged")) {
    // Every NetworkManager object reports its changes, only the ones we show
    // matter.
    const char *path = d->dbus_message_get_path(msg);
    if (eq(path, NM_PATH) || mPrimary == path || mAccessPoint == path) {
      fetch();
      return true;
    }
    return false;
  }
  if (d->dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS,
                                "NameOwnerChanged")) {
    fetch();
    return true;
  }
  return false;
}

std::vector<std::string>
NetworkManagerIcon::readObjectPaths(DBusMessageIter *array) {
  DBusLib *d = mBus.lib();
  DBusMessageIter paths;
  std::vector<std::string> out;
  if (d->dbus_message_iter_get_arg_type(array) != DBUS_TYPE_ARRAY)
    return out;
  d->dbus_message_iter_recurse(array, &paths);
  while (d->dbus_message_iter_get_arg_type(&paths) == DBUS_TYPE_OBJECT_PATH) {
    const char *path;
    d->dbus_message_iter_get_basic(&paths, &path);
    out.emplace_back(path);
    d->dbus_message_iter_next(&paths);
  }
  return out;
}

// Fetches NetworkManager's state, then the primary connection's, then its
// access point's, each only if there is one.
void NetworkManagerIcon::fetch() {
  DBusLib *d = mBus.lib();
  uint64_t fetch = ++mFetch;

  mBus.getAllProperties(
      NM_NAME, NM_PATH, NM_IFACE,
      [this, d, fetch](const char *name, DBusMessageIter *value) {
        if (fetch != mFetch)
          return;
        int type = d->dbus_message_iter_get_arg_type(value);
        if (eq(name, "State") && type == DBUS_TYPE_UINT32) {
          d->dbus_message_iter_get_basic(value, &mState);
        } else if (eq(name, "Connectivity") && type == DBUS_TYPE_UINT32) {
          d->dbus_message_iter_get_basic(value, &mConnectivity);
        } else if (eq(name, "WirelessEnabled") && type == DBUS_TYPE_BOOLEAN) {
          dbus_bool_t b;
          d->dbus_message_iter_get_basic(value, &b);
          mWirelessEnabled = b;
        } else if (eq(name, "PrimaryConnection") &&
                   type == DBUS_TYPE_OBJECT_PATH) {
          const char *path;
          d->dbus_message_iter_get_basic(value, &path);
          mPrimary = path;
        } else if (eq(name, "Devices")) {
          mDevices = readObjectPaths(value);
        }
      },
      [this, fetch](bool ok) {
        if (fetch != mFetch)
          return;
        if (!ok) {
          // e.g. NetworkManager isn't running.
          mState = 0;
          mPrimary = "/";
        }
        fetchPrimary(fetch);
      });
}

void NetworkManagerIcon::fetchPrimary(uint64_t fetch) {
  DBusLib *d = mBus.lib();
  mConnectionType.clear();
  mConnectionName.clear();
  mAccessPoint = "/";
  if (mPrimary == "/") {
    fetchAccessPoint(fetch);
    return;
  }

  mBus.getAllProperties(
      NM_NAME, mPrimary.c_str(), ACTIVE_IFACE,
      [this, d, fetch](const char *name, DBusMessageIter *value) {
        if (fetch != mFetch)
          return;
        int type = d->dbus_message_iter_get_arg_type(value);
        if (eq(name, "Type") && type == DBUS_TYPE_STRING) {
          const char *str;
          d->dbus_message_iter_get_basic(value, &str);
          mConnectionType = str;
        } else if (eq(name, "Id") && type == DBUS_TYPE_STRING) {
          const char *str;
          d->dbus_message_iter_get_basic(value, &str);
          mConnectionName = str;
        } else if (eq(name, "SpecificObject") &&
                   type == DBUS_TYPE_OBJECT_PATH) {
          const char *path;
          d->dbus_message_iter_get_basic(value, &path);
          mAccessPoint = path;
        }
      },
      [this, fetch](bool) {
        if (fetch == mFetch)
          fetchAccessPoint(fetch);
      });
}

void NetworkManagerIcon::fetchAccessPoint(uint64_t fetch) {
  DBusLib *d = mBus.lib();
  mStrength = 0;
  // A Wi-Fi connection's specific object is its access point; for other
  // types it's something else, or nothing.
  if (mConnectionType != "802-11-wireless" || mAccessPoint == "/") {
    update();
    return;
  }

  mBus.getAllProperties(
      NM_NAME, mAccessPoint.c_str(), AP_IFACE,
      [this, d, fetch](const char *name, DBusMessageIter *value) {
        if (fetch != mFetch)
          return;
        if (eq(name, "Strength") &&
            d->dbus_message_iter_get_arg_type(value) == DBUS_TYPE_BYTE) {
          unsigned char strength;
          d->dbus_message_iter_get_basic(value, &strength);
          mStrength = strength;
        }
      },
      [this, fetch](bool) {
        if (fetch == mFetch)
          update();
      });
}

void NetworkManagerIcon::update() {
  std::vector<SystrayItem> items;

  // No item at all when NetworkManager isn't around.
  if (mState != 0) {
    SystrayItem item;
    item.id = ITEM_ID;
    item.title = statusText();

    bool connected = mState >= STATE_CONNECTED_LOCAL;
    bool limited = connected && mConnectivity != CONNECTIVITY_FULL;
    bool wifi = mConnectionType == "802-11-wireless";

    // Breeze names come first, then the freedesktop ones other themes have.
    if (mState == STATE_CONNECTING) {
      item.icon.name = "network-wireless-acquiring";
      item.icon.fallbackNames = {"network-wireless-acquiring-symbolic",
                                 "network-idle"};
    } else if (connected && wifi) {
      int level = std::clamp((mStrength + 10) / 20 * 20, 0, 100);
      const char *signal = mStrength >= 80   ? "excellent"
                           : mStrength >= 55 ? "good"
                           : mStrength >= 30 ? "ok"
                           : mStrength >= 5  ? "weak"
                                             : "none";
      item.icon.name = std::format("network-wireless-{}", level);
      item.icon.fallbackNames = {
          std::format("network-wireless-signal-{}", signal),
          std::format("network-wireless-signal-{}-symbolic", signal),
      };
      if (limited) {
        item.icon.fallbackNames.insert(item.icon.fallbackNames.begin(),
                                       item.icon.name);
        item.icon.name += "-limited";
      }
    } else if (connected) {
      item.icon.name =
          limited ? "network-wired-activated-limited" : "network-wired-activated";
      item.icon.fallbackNames = {"network-wired-activated", "network-wired",
                                 "network-wired-symbolic"};
    } else {
      item.icon.name = mWirelessEnabled ? "network-wireless-disconnected"
                                        : "network-offline";
      item.icon.fallbackNames = {"network-offline", "network-offline-symbolic",
                                 "network-error"};
    }

    items.push_back(std::move(item));
  }

  if (items == mItems)
    return;
  mItems = std::move(items);
  itemsChanged();
}

std::string NetworkManagerIcon::statusText() const {
  if (mState == STATE_CONNECTING)
    return "Connecting...";
  if (mState < STATE_CONNECTED_LOCAL)
    return "Disconnected";

  std::string text = mConnectionName.empty()
                         ? "Connected"
                         : std::format("Connected to {}", mConnectionName);
  if (mConnectionType == "802-11-wireless")
    text += std::format(" ({}%)", mStrength);
  if (mState != STATE_CONNECTED_GLOBAL || mConnectivity != CONNECTIVITY_FULL)
    text += ", limited connectivity";
  return text;
}

void NetworkManagerIcon::activate(const std::string &id, int x, int y) {
  contextMenu(id, x, y);
}

void NetworkManagerIcon::contextMenu(const std::string &id, int x, int y) {
  (void)id;
  (void)x;
  (void)y;
  if (!mItems.empty())
    fetchMenu();
}

// Finds the Wi-Fi device, then its access points, then shows the menu.
void NetworkManagerIcon::fetchMenu() {
  DBusLib *d = mBus.lib();
  uint64_t fetch = ++mMenuFetch;

  struct Devices {
    std::vector<std::string> wifi;
    size_t pending;
  };
  auto devices = std::make_shared<Devices>();
  devices->pending = mDevices.size();
  if (mDevices.empty() || !mWirelessEnabled) {
    mWifiDevice.clear();
    showNetworkMenu({});
    return;
  }

  auto findAccessPoints = [this, d, fetch](const std::string &device) {
    mWifiDevice = device;

    // For a fresher list next time; the scan takes a few seconds.
    DBusMessage *msg = d->dbus_message_new_method_call(
        NM_NAME, device.c_str(), WIRELESS_IFACE, "RequestScan");
    DBusMessageIter iter, options;
    d->dbus_message_iter_init_append(msg, &iter);
    d->dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}",
                                        &options);
    d->dbus_message_iter_close_container(&iter, &options);
    d->dbus_message_set_no_reply(msg, true);
    d->dbus_connection_send(mBus.connection(), msg, nullptr);
    d->dbus_message_unref(msg);

    msg = d->dbus_message_new_method_call(NM_NAME, device.c_str(),
                                          WIRELESS_IFACE, "GetAllAccessPoints");
    mBus.callAsync(msg, [this, d, fetch](DBusMessage *reply) {
      DBusMessageIter iter;
      if (fetch != mMenuFetch)
        return;
      if (DBusClient::isError(d, reply) ||
          !d->dbus_message_iter_init(reply, &iter)) {
        showNetworkMenu({});
        return;
      }
      std::vector<std::string> paths = readObjectPaths(&iter);
      if (paths.empty()) {
        showNetworkMenu({});
        return;
      }

      struct AccessPoints {
        std::vector<Network> networks;
        size_t pending;
      };
      auto aps = std::make_shared<AccessPoints>();
      aps->pending = paths.size();
      for (const auto &path : paths) {
        auto network = std::make_shared<Network>();
        network->accessPoint = path;
        mBus.getAllProperties(
            NM_NAME, path.c_str(), AP_IFACE,
            [this, d, network](const char *name, DBusMessageIter *value) {
              int type = d->dbus_message_iter_get_arg_type(value);
              if (eq(name, "Ssid") && type == DBUS_TYPE_ARRAY) {
                DBusMessageIter bytes;
                const char *data = nullptr;
                int len = 0;
                d->dbus_message_iter_recurse(value, &bytes);
                d->dbus_message_iter_get_fixed_array(&bytes, &data, &len);
                if (data)
                  network->ssid.assign(data, len);
              } else if (eq(name, "Strength") && type == DBUS_TYPE_BYTE) {
                unsigned char strength;
                d->dbus_message_iter_get_basic(value, &strength);
                network->strength = strength;
              } else if ((eq(name, "WpaFlags") || eq(name, "RsnFlags")) &&
                         type == DBUS_TYPE_UINT32) {
                uint32_t flags;
                d->dbus_message_iter_get_basic(value, &flags);
                if (flags)
                  network->secured = true;
              }
            },
            [this, fetch, aps, network](bool ok) {
              if (fetch != mMenuFetch)
                return;
              if (ok)
                aps->networks.push_back(*network);
              if (--aps->pending == 0)
                showNetworkMenu(aps->networks);
            });
      }
    });
    d->dbus_message_unref(msg);
  };

  for (const auto &device : mDevices) {
    mBus.getAllProperties(
        NM_NAME, device.c_str(), DEVICE_IFACE,
        [d, devices, device](const char *name, DBusMessageIter *value) {
          if (eq(name, "DeviceType") &&
              d->dbus_message_iter_get_arg_type(value) == DBUS_TYPE_UINT32) {
            uint32_t type;
            d->dbus_message_iter_get_basic(value, &type);
            if (type == DEVICE_TYPE_WIFI)
              devices->wifi.push_back(device);
          }
        },
        [this, fetch, devices, findAccessPoints](bool) {
          if (fetch != mMenuFetch || --devices->pending != 0)
            return;
          if (devices->wifi.empty()) {
            mWifiDevice.clear();
            showNetworkMenu({});
          } else {
            findAccessPoints(devices->wifi.front());
          }
        });
  }
}

void NetworkManagerIcon::showNetworkMenu(const std::vector<Network> &found) {
  if (mItems.empty())
    return;

  // One entry per network name, for its strongest access point.
  std::vector<Network> networks;
  for (const auto &network : found) {
    if (network.ssid.empty())
      continue;
    auto same = std::find_if(networks.begin(), networks.end(),
                             [&](const Network &n) { return n.ssid == network.ssid; });
    if (same == networks.end())
      networks.push_back(network);
    else if (network.strength > same->strength)
      *same = network;
  }
  std::sort(networks.begin(), networks.end(),
            [](const Network &a, const Network &b) {
              return a.strength > b.strength;
            });
  if (networks.size() > MAX_NETWORKS)
    networks.resize(MAX_NETWORKS);
  mMenuNetworks = networks;

  std::vector<SystrayMenuEntry> entries;

  SystrayMenuEntry status;
  status.label = statusText();
  status.enabled = false;
  entries.push_back(status);

  if (mPrimary != "/") {
    SystrayMenuEntry disconnect;
    disconnect.id = DISCONNECT_ENTRY;
    disconnect.label = "Disconnect";
    entries.push_back(disconnect);
  }

  SystrayMenuEntry separator;
  separator.type = SystrayMenuEntry::Type::Separator;
  entries.push_back(separator);

  SystrayMenuEntry wifi;
  wifi.id = WIFI_TOGGLE_ENTRY;
  wifi.label = "Wi-Fi";
  wifi.toggle = SystrayMenuEntry::Toggle::Checkmark;
  wifi.toggled = mWirelessEnabled;
  entries.push_back(wifi);

  if (!networks.empty())
    entries.push_back(separator);
  for (size_t i = 0; i < networks.size(); i++) {
    SystrayMenuEntry entry;
    entry.id = NETWORK_ENTRY + (int)i;
    entry.label = std::format("{} ({}%{})", networks[i].ssid,
                              networks[i].strength,
                              networks[i].secured ? ", secured" : "");
    entry.toggle = SystrayMenuEntry::Toggle::Radio;
    entry.toggled = mConnectionType == "802-11-wireless" &&
                    mConnectionName == networks[i].ssid;
    entries.push_back(entry);
  }

  showMenu(ITEM_ID, entries);
}

void NetworkManagerIcon::menuEntryActivated(const std::string &id,
                                            int entryId) {
  (void)id;
  if (entryId == WIFI_TOGGLE_ENTRY) {
    setWirelessEnabled(!mWirelessEnabled);
  } else if (entryId == DISCONNECT_ENTRY) {
    disconnect();
  } else if (entryId >= NETWORK_ENTRY &&
             entryId - NETWORK_ENTRY < (int)mMenuNetworks.size()) {
    connectTo(mMenuNetworks[entryId - NETWORK_ENTRY]);
  }
}

void NetworkManagerIcon::connectTo(const Network &network) {
  DBusLib *d = mBus.lib();
  if (mWifiDevice.empty())
    return;
  std::string device = mWifiDevice;

  // With no connection given, NetworkManager activates the best saved one for
  // the access point.
  DBusMessage *msg = d->dbus_message_new_method_call(
      NM_NAME, NM_PATH, NM_IFACE, "ActivateConnection");
  const char *none = "/";
  const char *dev = device.c_str();
  const char *ap = network.accessPoint.c_str();
  d->dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &none,
                              DBUS_TYPE_OBJECT_PATH, &dev,
                              DBUS_TYPE_OBJECT_PATH, &ap, DBUS_TYPE_INVALID);

  mBus.callAsync(msg, [this, d, device, network](DBusMessage *reply) {
    if (!DBusClient::isError(d, reply))
      return;

    // Nothing saved for it yet, so add a connection. For secured networks
    // NetworkManager asks a secret agent (e.g. Plasma's) for the password.
    DBusMessage *msg = d->dbus_message_new_method_call(
        NM_NAME, NM_PATH, NM_IFACE, "AddAndActivateConnection");
    DBusMessageIter iter, settings;
    const char *dev = device.c_str();
    const char *ap = network.accessPoint.c_str();
    d->dbus_message_iter_init_append(msg, &iter);
    d->dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sa{sv}}",
                                        &settings);
    d->dbus_message_iter_close_container(&iter, &settings);
    d->dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &dev);
    d->dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &ap);

    std::string ssid = network.ssid;
    mBus.callAsync(msg, [d, ssid](DBusMessage *reply) {
      if (DBusClient::isError(d, reply))
        fprintf(stderr, "tcc_systray: could not connect to %s\n", ssid.c_str());
    });
    d->dbus_message_unref(msg);
  });
  d->dbus_message_unref(msg);
}

void NetworkManagerIcon::setWirelessEnabled(bool enabled) {
  DBusLib *d = mBus.lib();
  DBusMessage *msg =
      d->dbus_message_new_method_call(NM_NAME, NM_PATH, PROPS_IFACE, "Set");
  DBusMessageIter iter, variant;
  const char *iface = NM_IFACE;
  const char *prop = "WirelessEnabled";
  dbus_bool_t value = enabled;
  d->dbus_message_iter_init_append(msg, &iter);
  d->dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);
  d->dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);
  d->dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
  d->dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
  d->dbus_message_iter_close_container(&iter, &variant);

  mBus.callAsync(msg, [d](DBusMessage *reply) {
    if (DBusClient::isError(d, reply))
      fprintf(stderr, "tcc_systray: could not toggle Wi-Fi\n");
  });
  d->dbus_message_unref(msg);
}

void NetworkManagerIcon::disconnect() {
  DBusLib *d = mBus.lib();
  if (mPrimary == "/")
    return;

  DBusMessage *msg = d->dbus_message_new_method_call(
      NM_NAME, NM_PATH, NM_IFACE, "DeactivateConnection");
  const char *path = mPrimary.c_str();
  d->dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &path,
                              DBUS_TYPE_INVALID);
  mBus.callAsync(msg, [d](DBusMessage *reply) {
    if (DBusClient::isError(d, reply))
      fprintf(stderr, "tcc_systray: could not disconnect\n");
  });
  d->dbus_message_unref(msg);
}
#endif
