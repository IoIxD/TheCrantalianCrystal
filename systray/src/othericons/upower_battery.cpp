#ifdef TCC_SYSTRAY_DBUS
#include "upower_battery.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>

namespace {

constexpr const char *UPOWER_NAME = "org.freedesktop.UPower";
constexpr const char *DEVICE_PATH =
    "/org/freedesktop/UPower/devices/DisplayDevice";
constexpr const char *DEVICE_IFACE = "org.freedesktop.UPower.Device";
constexpr const char *ITEM_ID = "battery";

bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

std::string formatDuration(int64_t seconds) {
  int64_t minutes = (seconds + 30) / 60;
  return std::format("{}:{:02}", minutes / 60, minutes % 60);
}

} // namespace

bool UPowerBattery::connect() {
  if (!mBus.open(DBUS_BUS_SYSTEM))
    return false;

  mBus.setMessageHandler([this](DBusMessage *msg) { return handleMessage(msg); });
  mBus.addMatch("type='signal',sender='org.freedesktop.UPower',"
                "path='/org/freedesktop/UPower/devices/DisplayDevice',"
                "interface='org.freedesktop.DBus.Properties',"
                "member='PropertiesChanged'");
  // To refetch if UPower restarts.
  mBus.addMatch("type='signal',sender='org.freedesktop.DBus',"
                "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
                "arg0='org.freedesktop.UPower'");
  fetch();
  return true;
}

bool UPowerBattery::handleMessage(DBusMessage *msg) {
  DBusLib *d = mBus.lib();
  if (d->dbus_message_is_signal(msg, "org.freedesktop.DBus.Properties",
                                "PropertiesChanged") &&
      eq(d->dbus_message_get_path(msg), DEVICE_PATH)) {
    // Simpler than applying the changes, and they're rare.
    fetch();
    return true;
  }
  if (d->dbus_message_is_signal(msg, DBUS_INTERFACE_DBUS,
                                "NameOwnerChanged")) {
    fetch();
    return true;
  }
  return false;
}

void UPowerBattery::fetch() {
  DBusLib *d = mBus.lib();
  mBus.getAllProperties(
      UPOWER_NAME, DEVICE_PATH, DEVICE_IFACE,
      [this, d](const char *name, DBusMessageIter *value) {
        int type = d->dbus_message_iter_get_arg_type(value);
        if (eq(name, "IsPresent") && type == DBUS_TYPE_BOOLEAN) {
          dbus_bool_t b;
          d->dbus_message_iter_get_basic(value, &b);
          mPresent = b;
        } else if (eq(name, "Type") && type == DBUS_TYPE_UINT32) {
          d->dbus_message_iter_get_basic(value, &mType);
        } else if (eq(name, "Percentage") && type == DBUS_TYPE_DOUBLE) {
          d->dbus_message_iter_get_basic(value, &mPercentage);
        } else if (eq(name, "State") && type == DBUS_TYPE_UINT32) {
          d->dbus_message_iter_get_basic(value, &mState);
        } else if (eq(name, "TimeToEmpty") && type == DBUS_TYPE_INT64) {
          d->dbus_message_iter_get_basic(value, &mTimeToEmpty);
        } else if (eq(name, "TimeToFull") && type == DBUS_TYPE_INT64) {
          d->dbus_message_iter_get_basic(value, &mTimeToFull);
        } else if (eq(name, "IconName") && type == DBUS_TYPE_STRING) {
          const char *str;
          d->dbus_message_iter_get_basic(value, &str);
          mIconName = str;
        }
      },
      [this](bool ok) {
        // e.g. UPower isn't running.
        if (!ok)
          mPresent = false;
        update();
      });
}

void UPowerBattery::update() {
  std::vector<SystrayItem> items;

  if (mPresent && mType == 2) {
    SystrayItem item;
    item.id = ITEM_ID;
    item.title = std::format("Battery: {}%, {}", std::lround(mPercentage),
                             stateText());

    bool charging = mState == Charging || mState == FullyCharged ||
                    mState == PendingCharge;
    int level = (int)std::lround(mPercentage / 10) * 10;
    const char *suffix = charging ? "-charging" : "";

    // Themes name these differently: Breeze uses battery-060-charging,
    // Adwaita battery-level-60-charging-symbolic, and UPower suggests the
    // generic freedesktop names.
    item.icon.name = std::format("battery-{:03}{}", level, suffix);
    item.icon.fallbackNames = {
        std::format("battery-level-{}{}-symbolic", level, suffix),
        mIconName,
        "battery",
    };
    if (mPercentage <= 10 && !charging)
      item.status = SystrayItem::Status::NeedsAttention;

    items.push_back(std::move(item));
  }

  if (items == mItems)
    return;
  mItems = std::move(items);
  itemsChanged();
}

std::string UPowerBattery::stateText() const {
  switch (mState) {
  case Charging:
    return mTimeToFull > 0
               ? std::format("charging, {} until full",
                             formatDuration(mTimeToFull))
               : "charging";
  case Discharging:
    return mTimeToEmpty > 0
               ? std::format("{} remaining", formatDuration(mTimeToEmpty))
               : "discharging";
  case Empty:
    return "empty";
  case FullyCharged:
    return "fully charged";
  case PendingCharge:
    return "not charging";
  case PendingDischarge:
    return "waiting to discharge";
  default:
    return "unknown state";
  }
}

void UPowerBattery::activate(const std::string &id, int x, int y) {
  contextMenu(id, x, y);
}

void UPowerBattery::contextMenu(const std::string &id, int x, int y) {
  (void)x;
  (void)y;
  if (mItems.empty())
    return;

  // Informational only, so nothing in it can be chosen.
  SystrayMenuEntry charge;
  charge.label = std::format("Battery: {}%", std::lround(mPercentage));
  charge.enabled = false;

  SystrayMenuEntry state;
  state.label = stateText();
  state.label[0] = (char)toupper(state.label[0]);
  state.enabled = false;

  showMenu(id, {charge, state});
}
#endif
