#ifdef TCC_SYSTRAY_PULSE
#pragma once

#include "../lib/pulse_loader.hpp"
#include "../systray_protocol.hpp"

#include <string>
#include <vector>

// A built-in item for the default audio output's volume, or the default
// input's (the microphone), through the PulseAudio client library (which
// PipeWire also serves). There is no item while there is no sound server or
// device, and for the microphone, while nothing is recording.
//
// Middle click mutes, scrolling changes the volume, and clicking shows a menu
// with mute, a few volume levels and the devices to choose from.
class PulseVolume : public SystrayProtocol {
public:
  enum class Direction { Output, Input };

  explicit PulseVolume(Direction direction) : mDirection(direction) {}
  ~PulseVolume() override;

  const char *name() const override {
    return mDirection == Direction::Output ? "PulseAudio volume"
                                           : "PulseAudio microphone";
  }
  // Returns false if libpulse can't be loaded. A missing sound server is
  // fine, the item appears once it starts.
  bool connect() override;
  void poll() override;

  void activate(const std::string &id, int x, int y) override;
  void secondaryActivate(const std::string &id, int x, int y) override;
  void contextMenu(const std::string &id, int x, int y) override;
  void scroll(const std::string &id, int delta, bool horizontal) override;
  void menuEntryActivated(const std::string &id, int entryId) override;

private:
  // A sink (output) or source (input).
  struct Device {
    uint32_t index = PA_INVALID_INDEX;
    std::string name;
    std::string description;
    pa_cvolume volume;
    bool muted = false;
  };

  const Direction mDirection;

  PulseLib *mLib = nullptr;
  pa_mainloop *mMainloop = nullptr;
  pa_context *mContext = nullptr;
  // Set when the connection was lost, to make a new one on the next poll.
  bool mReconnect = false;
  // At most one refresh at a time, followed by one more if anything changed
  // meanwhile.
  bool mRefreshing = false;
  bool mRefreshAgain = false;

  std::string mDefaultDevice;
  std::vector<Device> mDevices;
  // Devices of the refresh still being collected.
  std::vector<Device> mNewDevices;
  // What the open menu's device entries refer to.
  std::vector<std::string> mMenuDevices;
  // For inputs: whether any app is recording from one, and the same for the
  // refresh still being collected.
  bool mRecording = false;
  bool mNewRecording = false;

  void createContext();
  void destroyContext();
  void refresh();
  void addDevice(uint32_t index, const char *name, const char *description,
                 const pa_cvolume &volume, bool muted);
  void devicesDone();
  void refreshDone();
  void update();

  const Device *defaultDevice() const;
  int percent(const Device &device) const;
  void setMuted(bool muted);
  void setPercent(int percent);
  void setDefault(const std::string &name);
  void finish(pa_operation *op);

  static void stateCallback(pa_context *context, void *self);
  static void subscribeCallback(pa_context *context,
                                pa_subscription_event_type_t type,
                                uint32_t index, void *self);
  static void serverInfoCallback(pa_context *context,
                                 const pa_server_info *info, void *self);
  static void sinkInfoCallback(pa_context *context, const pa_sink_info *info,
                               int eol, void *self);
  static void sourceInfoCallback(pa_context *context,
                                 const pa_source_info *info, int eol,
                                 void *self);
  static void sourceOutputInfoCallback(pa_context *context,
                                       const pa_source_output_info *info,
                                       int eol, void *self);
};
#endif
