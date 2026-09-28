#ifdef TCC_SYSTRAY_DBUS
#pragma once

#include "../systray_protocol.hpp"
#include "dbus_client.hpp"

#include <string>
#include <vector>

// A built-in Bluetooth item from BlueZ, for the first Bluetooth adapter. There
// is no item when there is no adapter.
//
// Clicking it shows a menu to turn Bluetooth on or off and to connect or
// disconnect paired devices.
class BluezBluetooth : public SystrayProtocol {
public:
  const char *name() const override { return "BlueZ"; }
  // Connects to the system bus. Returns false if it is unavailable.
  bool connect() override;
  void poll() override { mBus.poll(); }
  int fd() const override { return mBus.fd(); }

  void activate(const std::string &id, int x, int y) override;
  void contextMenu(const std::string &id, int x, int y) override;
  void menuEntryActivated(const std::string &id, int entryId) override;

private:
  struct Device {
    std::string path;
    std::string name;
    bool connected = false;
  };

  DBusClient mBus;
  // BlueZ reports changes in bursts (e.g. signal strength while scanning), so
  // at most one fetch runs at a time, followed by one more if anything
  // changed meanwhile.
  bool mFetching = false;
  bool mFetchAgain = false;

  // Empty when there is no adapter.
  std::string mAdapter;
  std::string mAdapterName;
  bool mPowered = false;
  // Paired devices of the adapter.
  std::vector<Device> mDevices;
  // What the open menu's device entries refer to.
  std::vector<Device> mMenuDevices;

  bool handleMessage(DBusMessage *msg);
  void fetch();
  void readObjects(DBusMessageIter *objects);
  void update();

  std::string statusText() const;
  void setPowered(bool powered);
  void toggleConnection(const Device &device);
};
#endif
