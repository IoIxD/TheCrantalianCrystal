#pragma once

#include "systray_protocol.hpp"

#include <Mw/Milsko.h>

#include <functional>
#include <memory>
#include <vector>

// A popup with a Windows 95 style volume control: a label, a vertical slider
// and a mute checkbox for the master channel, and a button that widens the
// window with one such column per other channel (e.g. per application).
class MixerWindow {
public:
  // Called when the user changes a channel.
  using VolumeChanged = std::function<void(int channel, int volume)>;
  using MuteChanged = std::function<void(int channel, bool muted)>;

  // Opens above the bar, with its left edge at x in parent.
  MixerWindow(MwWidget parent, int x, const SystrayMixer &mixer,
              VolumeChanged onVolume, MuteChanged onMute);
  ~MixerWindow();
  MixerWindow(const MixerWindow &) = delete;
  MixerWindow &operator=(const MixerWindow &) = delete;

  // Shows new values, rebuilding the window if the channels changed.
  void update(const SystrayMixer &mixer);

private:
  struct Column {
    MixerWindow *window;
    int channel;
    // The slider's range, at least 100 but more if the volume is above it.
    int max;
    MwWidget label;
    MwWidget slider;
    MwWidget mute;
  };

  MwWidget mParent;
  int mX;
  SystrayMixer mMixer;
  VolumeChanged mOnVolume;
  MuteChanged mOnMute;
  bool mExpanded = false;

  MwWidget mPopup = nullptr;
  // Pointers, since the widgets' handlers hold on to them.
  std::vector<std::unique_ptr<Column>> mColumns;
  // Columns of a destroyed popup, whose widgets Milsko frees on its next step.
  std::vector<std::unique_ptr<Column>> mOldColumns;

  void build();
  void destroy();
  void addColumn(int index, const SystrayMixerChannel &channel);
  // The channels currently shown.
  std::vector<SystrayMixerChannel> shownChannels() const;
  static void setColumn(Column &column, const SystrayMixerChannel &channel);
  static bool hasExpandButton(const SystrayMixer &mixer);

  static void MWAPI sliderChanged(MwWidget handle, void *user, void *call);
  static void MWAPI muteChanged(MwWidget handle, void *user, void *call);
  static void MWAPI expandClicked(MwWidget handle, void *user, void *call);
};
