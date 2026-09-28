#ifdef TCC_SYSTRAY_DBUS
#include "bluez_bluetooth.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>

namespace {

constexpr const char *BLUEZ_NAME = "org.bluez";
constexpr const char *ADAPTER_IFACE = "org.bluez.Adapter1";
constexpr const char *DEVICE_IFACE = "org.bluez.Device1";
constexpr const char *PROPS_IFACE = "org.freedesktop.DBus.Properties";
constexpr const char *OBJECT_MANAGER_IFACE =
    "org.freedesktop.DBus.ObjectManager";
constexpr const char *ITEM_ID = "bluetooth";

// Menu entry ids; devices are DEVICE_ENTRY + their index.
constexpr int POWER_ENTRY = 1;
constexpr int DEVICE_ENTRY = 100;

bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

} // namespace

bool BluezBluetooth::connect() {
  if (!mBus.open(DBUS_BUS_SYSTEM))
    return false;

  mBus.setMessageHandler([this](DBusMessage *msg) { return handleMessage(msg); });
  mBus.addMatch("type='signal',sender='org.bluez',"
                "interface='org.freedesktop.DBus.Properties',"
                "member='PropertiesChanged'");
  mBus.addMatch("type='signal',sender='org.bluez',"
                "interface='org.freedesktop.DBus.ObjectManager'");
  // To refetch if BlueZ restarts.
  mBus.addMatch("type='signal',sender='org.freedesktop.DBus',"
                "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
                "arg0='org.bluez'");
  fetch();
  return true;
}

bool BluezBluetooth::handleMessage(DBusMessage *msg) {
  DBusLib *d = mBus.lib();
  if (d->dbus_message_is_signal(msg, PROPS_IFACE, "PropertiesChanged")) {
    // Only adapters' and devices' changes matter.
    DBusMessageIter iter;
    const char *iface = nullptr;
    if (d->dbus_message_iter_init(msg, &iter) &&
        d->dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING)
      d->dbus_message_iter_get_basic(&iter, &iface);
    if (!eq(iface, ADAPTER_IFACE) && !eq(iface, DEVICE_IFACE))
      return false;
    fetch();
    return true;
  }
  if (d->dbus_message_is_signal(msg, OBJECT_MANAGER_IFACE,
                                "InterfacesAdded") ||
      d->dbus_message_is_signal(msg, OBJECT_MANAGER_IFACE,
                                "InterfacesRemoved") ||
      d->dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS, "NameOwnerChanged")) {
    fetch();
    return true;
  }
  return false;
}

void BluezBluetooth::fetch() {
  if (mFetching) {
    mFetchAgain = true;
    return;
  }
  mFetching = true;
  mFetchAgain = false;

  DBusLib *d = mBus.lib();
  DBusMessage *msg = d->dbus_message_new_method_call(
      BLUEZ_NAME, "/", OBJECT_MANAGER_IFACE, "GetManagedObjects");
  mBus.callAsync(msg, [this, d](DBusMessage *reply) {
    mFetching = false;

    DBusMessageIter iter;
    if (DBusClient::isError(d, reply) ||
        !d->dbus_message_iter_init(reply, &iter)) {
      // e.g. BlueZ isn't running.
      mAdapter.clear();
      mDevices.clear();
    } else {
      readObjects(&iter);
    }
    update();

    if (mFetchAgain)
      fetch();
  });
  d->dbus_message_unref(msg);
}

// Reads GetManagedObjects' a{oa{sa{sv}}}: every object, with the properties of
// each of its interfaces.
void BluezBluetooth::readObjects(DBusMessageIter *objects) {
  DBusLib *d = mBus.lib();
  DBusMessageIter entries, object, ifaces, iface;

  mAdapter.clear();
  mAdapterName.clear();
  mPowered = false;

  struct PairedDevice {
    Device device;
    std::string adapter;
    bool paired = false;
  };
  std::vector<PairedDevice> devices;

  if (d->dbus_message_iter_get_arg_type(objects) != DBUS_TYPE_ARRAY)
    return;
  d->dbus_message_iter_recurse(objects, &entries);
  while (d->dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
    const char *path;
    d->dbus_message_iter_recurse(&entries, &object);
    d->dbus_message_iter_get_basic(&object, &path);
    d->dbus_message_iter_next(&object);

    d->dbus_message_iter_recurse(&object, &ifaces);
    while (d->dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
      const char *name;
      d->dbus_message_iter_recurse(&ifaces, &iface);
      d->dbus_message_iter_get_basic(&iface, &name);
      d->dbus_message_iter_next(&iface);

      // Only the first adapter; systems rarely have more.
      if (eq(name, ADAPTER_IFACE) && mAdapter.empty()) {
        mAdapter = path;
        mBus.forEachProperty(
            &iface, [this, d](const char *prop, DBusMessageIter *value) {
              int type = d->dbus_message_iter_get_arg_type(value);
              if (eq(prop, "Powered") && type == DBUS_TYPE_BOOLEAN) {
                dbus_bool_t b;
                d->dbus_message_iter_get_basic(value, &b);
                mPowered = b;
              } else if (eq(prop, "Alias") && type == DBUS_TYPE_STRING) {
                const char *str;
                d->dbus_message_iter_get_basic(value, &str);
                mAdapterName = str;
              }
            });
      } else if (eq(name, DEVICE_IFACE)) {
        PairedDevice device;
        device.device.path = path;
        mBus.forEachProperty(
            &iface, [&device, d](const char *prop, DBusMessageIter *value) {
              int type = d->dbus_message_iter_get_arg_type(value);
              if (type == DBUS_TYPE_BOOLEAN) {
                dbus_bool_t b;
                d->dbus_message_iter_get_basic(value, &b);
                if (eq(prop, "Paired"))
                  device.paired = b;
                else if (eq(prop, "Connected"))
                  device.device.connected = b;
              } else if (eq(prop, "Alias") && type == DBUS_TYPE_STRING) {
                const char *str;
                d->dbus_message_iter_get_basic(value, &str);
                device.device.name = str;
              } else if (eq(prop, "Adapter") &&
                         type == DBUS_TYPE_OBJECT_PATH) {
                const char *str;
                d->dbus_message_iter_get_basic(value, &str);
                device.adapter = str;
              }
            });
        devices.push_back(std::move(device));
      }
      d->dbus_message_iter_next(&ifaces);
    }
    d->dbus_message_iter_next(&entries);
  }

  // Devices only after the adapter is known, since objects come in any order.
  mDevices.clear();
  for (auto &device : devices) {
    if (device.paired && device.adapter == mAdapter)
      mDevices.push_back(std::move(device.device));
  }
  std::sort(mDevices.begin(), mDevices.end(),
            [](const Device &a, const Device &b) { return a.name < b.name; });
}

void BluezBluetooth::update() {
  std::vector<SystrayItem> items;

  if (!mAdapter.empty()) {
    SystrayItem item;
    item.id = ITEM_ID;
    item.title = statusText();

    bool connected = std::any_of(mDevices.begin(), mDevices.end(),
                                 [](const Device &d) { return d.connected; });
    // Breeze names come first, then the freedesktop ones other themes have.
    if (!mPowered) {
      item.icon.name = "network-bluetooth-inactive";
      item.icon.fallbackNames = {"network-bluetooth-inactive-symbolic",
                                 "bluetooth-disabled",
                                 "bluetooth-disabled-symbolic"};
    } else if (connected) {
      item.icon.name = "network-bluetooth-activated";
      item.icon.fallbackNames = {"bluetooth-active", "bluetooth-active-symbolic",
                                 "network-bluetooth"};
    } else {
      item.icon.name = "network-bluetooth";
      item.icon.fallbackNames = {"bluetooth-active", "bluetooth-active-symbolic",
                                 "bluetooth"};
    }

    items.push_back(std::move(item));
  }

  if (items == mItems)
    return;
  mItems = std::move(items);
  itemsChanged();
}

std::string BluezBluetooth::statusText() const {
  if (!mPowered)
    return "Bluetooth is off";

  std::vector<std::string> connected;
  for (const auto &device : mDevices)
    if (device.connected)
      connected.push_back(device.name);
  if (connected.empty())
    return "Bluetooth is on";
  if (connected.size() == 1)
    return std::format("Connected to {}", connected[0]);
  return std::format("Connected to {} devices", connected.size());
}

void BluezBluetooth::activate(const std::string &id, int x, int y) {
  contextMenu(id, x, y);
}

void BluezBluetooth::contextMenu(const std::string &id, int x, int y) {
  (void)x;
  (void)y;
  if (mItems.empty())
    return;

  std::vector<SystrayMenuEntry> entries;

  SystrayMenuEntry status;
  status.label = statusText();
  status.enabled = false;
  entries.push_back(status);

  SystrayMenuEntry separator;
  separator.type = SystrayMenuEntry::Type::Separator;
  entries.push_back(separator);

  SystrayMenuEntry power;
  power.id = POWER_ENTRY;
  power.label = "Bluetooth";
  power.toggle = SystrayMenuEntry::Toggle::Checkmark;
  power.toggled = mPowered;
  entries.push_back(power);

  // Devices can only be connected while the adapter is on.
  mMenuDevices = mPowered ? mDevices : std::vector<Device>{};
  if (!mMenuDevices.empty())
    entries.push_back(separator);
  for (size_t i = 0; i < mMenuDevices.size(); i++) {
    SystrayMenuEntry entry;
    entry.id = DEVICE_ENTRY + (int)i;
    entry.label = mMenuDevices[i].name;
    entry.toggle = SystrayMenuEntry::Toggle::Checkmark;
    entry.toggled = mMenuDevices[i].connected;
    entries.push_back(entry);
  }

  showMenu(id, entries);
}

void BluezBluetooth::menuEntryActivated(const std::string &id, int entryId) {
  (void)id;
  if (entryId == POWER_ENTRY)
    setPowered(!mPowered);
  else if (entryId >= DEVICE_ENTRY &&
           entryId - DEVICE_ENTRY < (int)mMenuDevices.size())
    toggleConnection(mMenuDevices[entryId - DEVICE_ENTRY]);
}

void BluezBluetooth::setPowered(bool powered) {
  DBusLib *d = mBus.lib();
  if (mAdapter.empty())
    return;

  DBusMessage *msg = d->dbus_message_new_method_call(
      BLUEZ_NAME, mAdapter.c_str(), PROPS_IFACE, "Set");
  DBusMessageIter iter, variant;
  const char *iface = ADAPTER_IFACE;
  const char *prop = "Powered";
  dbus_bool_t value = powered;
  d->dbus_message_iter_init_append(msg, &iter);
  d->dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);
  d->dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &prop);
  d->dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b", &variant);
  d->dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
  d->dbus_message_iter_close_container(&iter, &variant);

  mBus.callAsync(msg, [d](DBusMessage *reply) {
    if (DBusClient::isError(d, reply))
      fprintf(stderr, "tcc_systray: could not toggle Bluetooth\n");
  });
  d->dbus_message_unref(msg);
}

void BluezBluetooth::toggleConnection(const Device &device) {
  DBusLib *d = mBus.lib();
  const char *method = device.connected ? "Disconnect" : "Connect";
  DBusMessage *msg = d->dbus_message_new_method_call(
      BLUEZ_NAME, device.path.c_str(), DEVICE_IFACE, method);

  std::string name = device.name;
  bool connecting = !device.connected;
  // Connecting can take a while, e.g. while the device wakes up.
  mBus.callAsync(
      msg,
      [d, name, connecting](DBusMessage *reply) {
        if (DBusClient::isError(d, reply))
          fprintf(stderr, "tcc_systray: could not %s %s\n",
                  connecting ? "connect to" : "disconnect from", name.c_str());
      },
      30000);
  d->dbus_message_unref(msg);
}
#endif
