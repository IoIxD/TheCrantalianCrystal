#include "sysfs_battery.hpp"

#include <linux/netlink.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>

namespace {

constexpr const char *POWER_SUPPLY_DIR = "/sys/class/power_supply";
constexpr const char *ITEM_ID = "battery";

// Most drivers only send uevents for plugging and unplugging, not as the
// charge changes.
constexpr auto REREAD_INTERVAL = std::chrono::seconds(10);
// How quickly, and for how long, to reread after a uevent. On plugging in,
// a battery can go Discharging, Not charging, then Charging over several
// seconds after the last event.
constexpr auto SETTLE_INTERVAL = std::chrono::seconds(1);
constexpr auto SETTLE_DURATION = std::chrono::seconds(15);

// A power supply's uevent file, without the POWER_SUPPLY_ prefixes.
std::map<std::string, std::string>
readUevent(const std::filesystem::path &dir) {
  std::map<std::string, std::string> props;
  std::ifstream file(dir / "uevent");
  std::string line;
  while (std::getline(file, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    std::string key = line.substr(0, eq);
    if (key.starts_with("POWER_SUPPLY_"))
      key.erase(0, strlen("POWER_SUPPLY_"));
    props[key] = line.substr(eq + 1);
  }
  return props;
}

// A property as a number, or NAN if it is missing. The kernel reports energy
// in µWh, charge in µAh, power in µW, current in µA and voltage in µV.
double number(const std::map<std::string, std::string> &props,
              const char *key) {
  auto it = props.find(key);
  if (it == props.end() || it->second.empty())
    return NAN;
  char *end;
  double value = strtod(it->second.c_str(), &end);
  return *end == '\0' ? value : NAN;
}

} // namespace

SysfsBattery::~SysfsBattery() {
  if (mUevents >= 0)
    close(mUevents);
}

bool SysfsBattery::connect() {
  std::error_code ec;
  if (!std::filesystem::is_directory(POWER_SUPPLY_DIR, ec))
    return false;

  // Kernel uevents can be received without privileges.
  mUevents = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                    NETLINK_KOBJECT_UEVENT);
  if (mUevents >= 0) {
    sockaddr_nl addr = {};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = 1;
    if (bind(mUevents, (sockaddr *)&addr, sizeof(addr)) < 0) {
      perror("tcc_systray: binding uevent socket");
      close(mUevents);
      mUevents = -1;
    }
  } else {
    perror("tcc_systray: opening uevent socket");
  }

  read();
  return true;
}

void SysfsBattery::poll() {
  auto now = std::chrono::steady_clock::now();
  if (drainUevents()) {
    mSettleUntil = now + SETTLE_DURATION;
    read();
    return;
  }
  auto interval = now < mSettleUntil ? SETTLE_INTERVAL : REREAD_INTERVAL;
  if (now - mLastRead >= interval)
    read();
}

// Returns whether any power supply changed.
bool SysfsBattery::drainUevents() {
  if (mUevents < 0)
    return false;

  bool changed = false;
  char buf[4096];
  ssize_t len;
  while ((len = recv(mUevents, buf, sizeof(buf) - 1, 0)) > 0) {
    buf[len] = '\0';
    // "ACTION@DEVPATH" then NUL separated KEY=VALUE pairs.
    for (char *p = buf; p < buf + len; p += strlen(p) + 1) {
      if (strcmp(p, "SUBSYSTEM=power_supply") == 0) {
        changed = true;
        break;
      }
    }
  }
  return changed;
}

void SysfsBattery::read() {
  mLastRead = std::chrono::steady_clock::now();

  int batteries = 0;
  // Totals over the batteries that report energy (or charge and voltage).
  double energyNow = 0, energyFull = 0, power = 0;
  bool allHaveEnergy = true;
  // For batteries that only report a percentage.
  double capacitySum = 0;
  bool anyCharging = false, anyDischarging = false, anyNotCharging = false;
  bool allFull = true;
  bool online = false;

  std::error_code ec;
  for (const auto &entry :
       std::filesystem::directory_iterator(POWER_SUPPLY_DIR, ec)) {
    auto props = readUevent(entry.path());
    // Peripherals, e.g. a wireless mouse, don't power the system.
    if (props["SCOPE"] == "Device")
      continue;
    // Chargers, e.g. AC or USB-C.
    if (props["TYPE"] != "Battery") {
      online |= props["ONLINE"] == "1";
      continue;
    }
    if (props["PRESENT"] == "0")
      continue;
    batteries++;

    const std::string &status = props["STATUS"];
    anyCharging |= status == "Charging";
    anyDischarging |= status == "Discharging";
    anyNotCharging |= status == "Not charging";
    allFull &= status == "Full";

    double now = number(props, "ENERGY_NOW");
    double full = number(props, "ENERGY_FULL");
    double rate = number(props, "POWER_NOW");
    // Some batteries report charge instead, convert it to energy.
    double voltage = number(props, "VOLTAGE_MIN_DESIGN");
    if (!(voltage > 0))
      voltage = number(props, "VOLTAGE_NOW");
    if (!(now >= 0 && full > 0) && voltage > 0) {
      double chargeNow = number(props, "CHARGE_NOW");
      double chargeFull = number(props, "CHARGE_FULL");
      if (chargeNow >= 0 && chargeFull > 0) {
        now = chargeNow * voltage / 1e6;
        full = chargeFull * voltage / 1e6;
      }
    }
    if (std::isnan(rate)) {
      double current = number(props, "CURRENT_NOW");
      double voltageNow = number(props, "VOLTAGE_NOW");
      if (!std::isnan(current) && voltageNow > 0)
        rate = current * voltageNow / 1e6;
    }

    if (now >= 0 && full > 0) {
      energyNow += now;
      energyFull += full;
      // Some drivers report it negative while discharging.
      if (!std::isnan(rate))
        power += std::fabs(rate);
    } else {
      allHaveEnergy = false;
    }

    double capacity = number(props, "CAPACITY");
    if (std::isnan(capacity) && now >= 0 && full > 0)
      capacity = now / full * 100;
    capacitySum += capacity >= 0 ? capacity : 0;
  }

  mPresent = batteries > 0;
  mOnline = online;
  if (mPresent) {
    if (allHaveEnergy)
      mPercentage = std::min(energyNow / energyFull * 100, 100.0);
    else
      mPercentage = capacitySum / batteries;

    // One battery charging while another discharges happens on laptops
    // that drain them in turn, and the system as a whole is charging then.
    if (anyCharging)
      mState = State::Charging;
    else if (anyDischarging)
      mState = State::Discharging;
    else if (allFull)
      mState = State::Full;
    else if (anyNotCharging)
      mState = State::NotCharging;
    else
      mState = State::Unknown;

    mTimeToEmpty = mTimeToFull = 0;
    if (allHaveEnergy && power > 0) {
      // Energy is in µWh and power in µW, so this is hours.
      if (mState == State::Discharging)
        mTimeToEmpty = std::lround(energyNow / power * 3600);
      else if (mState == State::Charging)
        mTimeToFull = std::lround((energyFull - energyNow) / power * 3600);
    }
  }

  update();
}

void SysfsBattery::update() {
  std::vector<SystrayItem> items;

  if (mPresent) {
    SystrayItem item;
    item.id = ITEM_ID;
    item.title =
        std::format("Battery: {}%, {}", std::lround(mPercentage), stateText());

    // The battery's status can lag behind plugging in, so going by the
    // charger too shows it right away.
    bool charging = mState == State::Charging || mState == State::Full ||
                    (mOnline && mState != State::Discharging);
    int level = (int)std::lround(mPercentage / 10) * 10;
    const char *suffix = charging ? "-charging" : "";

    // Themes name these differently: Breeze uses battery-060-charging,
    // Adwaita battery-level-60-charging-symbolic, and older themes only have
    // the generic freedesktop names.
    item.icon.name = std::format("battery-{:03}{}", level, suffix);
    item.icon.fallbackNames = {
        std::format("battery-level-{}{}-symbolic", level, suffix),
        mPercentage <= 10   ? "battery-caution"
        : mPercentage <= 30 ? "battery-low"
        : mPercentage <= 70 ? "battery-good"
                            : "battery-full",
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

std::string SysfsBattery::stateText() const {
  auto formatDuration = [](long seconds) -> std::string {
    long minutes = (seconds + 30) / 60;
    return std::format("{}:{:02}", minutes / 60, minutes % 60);
  };

  switch (mState) {
  case State::Charging:
    return mTimeToFull > 0 ? std::format("charging, {} until full",
                                         formatDuration(mTimeToFull))
                           : "charging";
  case State::Discharging:
    return mTimeToEmpty > 0
               ? std::format("{} remaining", formatDuration(mTimeToEmpty))
               : "discharging";
  case State::Full:
    return "fully charged";
  case State::NotCharging:
    return mOnline ? "plugged in, not charging" : "not charging";
  default:
    return mOnline ? "plugged in" : "unknown state";
  }
}

void SysfsBattery::activate(const std::string &id, int x, int y) {
  contextMenu(id, x, y);
}

void SysfsBattery::contextMenu(const std::string &id, int x, int y) {
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
