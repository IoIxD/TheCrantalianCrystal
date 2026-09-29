#ifdef TCC_HAS_DBUS
#pragma once

#include "../systray_protocol.hpp"
#include "dbus_client.hpp"

#include <cstdint>
#include <string>

// A built-in battery item, from UPower's DisplayDevice: the combined state of
// all of the system's batteries. There is no item when there is no battery.
//
// Clicking it shows a menu with the charge and time remaining.
class UPowerBattery : public SystrayProtocol {
public:
  const char *name() const override { return "UPower battery"; }
  // Connects to the system bus. Returns false if it is unavailable.
  bool connect() override;
  void poll() override { mBus.poll(); }
  int fd() const override { return mBus.fd(); }

  void activate(const std::string &id, int x, int y) override;
  void contextMenu(const std::string &id, int x, int y) override;

private:
  // UPower's Device.State.
  enum State : uint32_t {
    Unknown = 0,
    Charging = 1,
    Discharging = 2,
    Empty = 3,
    FullyCharged = 4,
    PendingCharge = 5,
    PendingDischarge = 6,
  };

  DBusClient mBus;

  bool mPresent = false;
  // UPower's Device.Type, 2 is a battery.
  uint32_t mType = 0;
  double mPercentage = 0;
  uint32_t mState = Unknown;
  // Seconds, 0 if unknown.
  int64_t mTimeToEmpty = 0;
  int64_t mTimeToFull = 0;
  // UPower's own suggestion, used if the theme lacks our preferred names.
  std::string mIconName;

  void fetch();
  bool handleMessage(DBusMessage *msg);
  void update();
  std::string stateText() const;
};
#endif
