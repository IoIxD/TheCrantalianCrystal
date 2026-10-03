#pragma once

#include "../systray_protocol.hpp"

#include <chrono>
#include <string>

// Battery item that reads from /sys/class/power_supply
// Plugging and unplugging is picked up from kernel uevents right away, but
// most drivers don't send them as the charge changes, so it is also reread
// periodically.
// Clicking it shows a menu with the charge and time remaining.
class SysfsBattery : public SystrayProtocol {
public:
  SysfsBattery() = default;
  ~SysfsBattery() override;

  const char *name() const override { return "sysfs battery"; }
  // Returns false if there is no power_supply class at all.
  bool connect() override;
  void poll() override;
  int fd() const override { return mUevents; }

  void activate(const std::string &id, int x, int y) override;
  void contextMenu(const std::string &id, int x, int y) override;

private:
  enum class State { Unknown, Charging, Discharging, NotCharging, Full };

  // Netlink socket for kernel uevents, or -1 if it couldn't be opened, in
  // which case we only reread periodically.
  int mUevents = -1;
  std::chrono::steady_clock::time_point mLastRead;
  // After a uevent, until when to keep rereading quickly: the battery's
  // status can settle seconds after the charger's event, with no event of
  // its own.
  std::chrono::steady_clock::time_point mSettleUntil;

  bool mPresent = false;
  double mPercentage = 0;
  State mState = State::Unknown;
  // Whether a charger is connected, whether or not it is charging.
  bool mOnline = false;
  // Seconds, 0 if unknown.
  long mTimeToEmpty = 0;
  long mTimeToFull = 0;

  bool drainUevents();
  void read();
  void update();
  std::string stateText() const;
};
