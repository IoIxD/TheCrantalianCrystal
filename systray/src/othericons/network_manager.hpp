#ifdef TCC_SYSTRAY_DBUS
#pragma once

#include "../systray_protocol.hpp"
#include "dbus_client.hpp"

#include <cstdint>
#include <string>
#include <vector>

// A built-in network item from NetworkManager, showing the primary
// connection: Wi-Fi with its signal strength, wired, or disconnected.
//
// Clicking it shows a menu with the connection's status, a Wi-Fi toggle and
// the nearby Wi-Fi networks to connect to.
class NetworkManagerIcon : public SystrayProtocol {
public:
  const char *name() const override { return "NetworkManager"; }
  // Connects to the system bus. Returns false if it is unavailable.
  bool connect() override;
  void poll() override { mBus.poll(); }
  int fd() const override { return mBus.fd(); }

  void activate(const std::string &id, int x, int y) override;
  void contextMenu(const std::string &id, int x, int y) override;
  void menuEntryActivated(const std::string &id, int entryId) override;

private:
  struct Network {
    std::string ssid;
    std::string accessPoint;
    int strength = 0;
    bool secured = false;
  };

  DBusClient mBus;
  // Bumped on each fetch, so replies to an older one are dropped.
  uint64_t mFetch = 0;
  uint64_t mMenuFetch = 0;

  // NetworkManager's NMState and NMConnectivityState.
  uint32_t mState = 0;
  uint32_t mConnectivity = 0;
  bool mWirelessEnabled = false;
  std::vector<std::string> mDevices;
  // The primary active connection, "/" for none.
  std::string mPrimary = "/";
  std::string mConnectionType;
  std::string mConnectionName;
  // The Wi-Fi access point of the primary connection, "/" for none.
  std::string mAccessPoint = "/";
  int mStrength = 0;

  // What the open menu's entries refer to.
  std::string mWifiDevice;
  std::vector<Network> mMenuNetworks;

  bool handleMessage(DBusMessage *msg);
  void fetch();
  void fetchPrimary(uint64_t fetch);
  void fetchAccessPoint(uint64_t fetch);
  void update();

  void fetchMenu();
  void showNetworkMenu(const std::vector<Network> &networks);
  void connectTo(const Network &network);
  void setWirelessEnabled(bool enabled);
  void disconnect();

  std::string statusText() const;
  std::vector<std::string> readObjectPaths(DBusMessageIter *array);
};
#endif
