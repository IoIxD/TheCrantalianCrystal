#include "mixer_window.hpp"
#include "systray.hpp"

#include <algorithm>

namespace {

constexpr int COLUMN_WIDTH = 76;
constexpr int HEIGHT = 182;
constexpr int SLIDER_TOP = 26;
constexpr int SLIDER_HEIGHT = 104;
constexpr int MUTE_TOP = 138;
constexpr int BUTTON_TOP = 158;
// Labels longer than this are cut short to fit the column.
constexpr size_t MAX_LABEL = 10;

std::string shortLabel(const std::string &label) {
  if (label.size() <= MAX_LABEL)
    return label;
  return label.substr(0, MAX_LABEL - 2) + "..";
}

} // namespace

MixerWindow::MixerWindow(MwWidget parent, int x, const SystrayMixer &mixer,
                         VolumeChanged onVolume, MuteChanged onMute)
    : mParent(parent), mX(x), mMixer(mixer), mOnVolume(std::move(onVolume)),
      mOnMute(std::move(onMute)) {
  build();
}

MixerWindow::~MixerWindow() { destroy(); }

bool MixerWindow::hasExpandButton(const SystrayMixer &mixer) {
  return mixer.expandable || !mixer.others.empty();
}

std::vector<SystrayMixerChannel> MixerWindow::shownChannels() const {
  std::vector<SystrayMixerChannel> channels = {mMixer.master};
  if (mExpanded)
    channels.insert(channels.end(), mMixer.others.begin(),
                    mMixer.others.end());
  return channels;
}

void MixerWindow::build() {
  // Columns of the popup before the current one, whose widgets are gone by
  // now since a step has passed. The current one's are kept for a step more.
  mOldColumns.clear();
  destroy();

  std::vector<SystrayMixerChannel> channels = shownChannels();
  // Expanded with nothing to show still gets a column, to say so.
  bool placeholder = mExpanded && mMixer.others.empty();
  int width = COLUMN_WIDTH * ((int)channels.size() + (placeholder ? 1 : 0));

  mPopup = MwVaCreateWidget(MwFrameClass, "mixer", mParent, mX, 0, width,
                            HEIGHT, NULL);
  for (size_t i = 0; i < channels.size(); i++)
    addColumn((int)i, channels[i]);

  if (placeholder) {
    int left = (int)channels.size() * COLUMN_WIDTH;
    const char *lines[] = {"No apps", "playing"};
    for (int i = 0; i < 2; i++)
      MwVaCreateWidget(MwLabelClass, "none", mPopup, left + 2,
                       SLIDER_TOP + SLIDER_HEIGHT / 2 - 16 + i * 16,
                       COLUMN_WIDTH - 4, 16, MwNtext, lines[i], MwNalignment,
                       MwALIGNMENT_CENTER, NULL);
  }

  if (hasExpandButton(mMixer)) {
    MwWidget expand = MwVaCreateWidget(
        MwButtonClass, "expand", mPopup, 6, BUTTON_TOP, COLUMN_WIDTH - 12, 20,
        MwNtext, mExpanded ? "<< Apps" : "Apps >>", NULL);
    MwAddUserHandler(expand, MwNactivateHandler, expandClicked, this);
  }

  // Opens upwards from the bar.
  popup_setup(mPopup, mX, -HEIGHT);
}

void MixerWindow::destroy() {
  if (!mPopup)
    return;
  // Takes the columns' widgets with it.
  MwDestroyWidget(mPopup);
  mPopup = nullptr;
  for (auto &column : mColumns)
    mOldColumns.push_back(std::move(column));
  mColumns.clear();
}

void MixerWindow::addColumn(int index, const SystrayMixerChannel &channel) {
  int left = index * COLUMN_WIDTH;
  auto column = std::make_unique<Column>();
  column->window = this;
  column->channel = channel.id;

  column->label = MwVaCreateWidget(MwLabelClass, "label", mPopup, left + 2, 6,
                                   COLUMN_WIDTH - 4, 16, MwNalignment,
                                   MwALIGNMENT_CENTER, NULL);
  column->slider = MwVaCreateWidget(
      MwScrollBarClass, "slider", mPopup, left + COLUMN_WIDTH / 2 - 8,
      SLIDER_TOP, 16, SLIDER_HEIGHT, MwNorientation, MwVERTICAL,
      MwNshowArrows, 0, MwNminValue, 0, NULL);
  column->mute = MwVaCreateWidget(MwCheckBoxClass, "mute", mPopup, left + 10,
                                  MUTE_TOP, 14, 14, NULL);
  MwVaCreateWidget(MwLabelClass, "muteLabel", mPopup, left + 28, MUTE_TOP - 1,
                   COLUMN_WIDTH - 30, 16, MwNtext, "Mute", NULL);

  setColumn(*column, channel);
  MwAddUserHandler(column->slider, MwNchangedHandler, sliderChanged,
                   column.get());
  MwAddUserHandler(column->mute, MwNchangedHandler, muteChanged, column.get());
  mColumns.push_back(std::move(column));
}

// Shows a channel's values. Setting them doesn't call the changed handlers.
void MixerWindow::setColumn(Column &column, const SystrayMixerChannel &channel) {
  // The slider runs top to bottom, so it holds how far the volume is from
  // the top.
  column.max = std::max(100, channel.volume);
  MwVaApply(column.label, MwNtext, shortLabel(channel.label).c_str(), NULL);
  MwVaApply(column.slider, MwNmaxValue, column.max, MwNareaShown,
            column.max / 10, MwNvalue, column.max - channel.volume, NULL);
  MwVaApply(column.mute, MwNchecked, channel.muted ? 1 : 0, NULL);
}

void MixerWindow::update(const SystrayMixer &mixer) {
  auto ids = [](const SystrayMixer &m) {
    std::vector<int> ids = {m.master.id};
    for (const auto &channel : m.others)
      ids.push_back(channel.id);
    return ids;
  };
  bool sameChannels = ids(mixer) == ids(mMixer);
  bool hadButton = hasExpandButton(mMixer);
  mMixer = mixer;

  // Streams come and go, which changes the columns (or the expand button).
  if ((mExpanded && !sameChannels) || hadButton != hasExpandButton(mixer)) {
    if (!hasExpandButton(mixer))
      mExpanded = false;
    build();
    return;
  }

  std::vector<SystrayMixerChannel> channels = shownChannels();
  for (auto &column : mColumns) {
    auto channel = std::find_if(
        channels.begin(), channels.end(),
        [&](const SystrayMixerChannel &c) { return c.id == column->channel; });
    if (channel == channels.end())
      continue;
    // Leaves the slider alone while it is where the user put it, so that
    // the server echoing a change doesn't fight a drag.
    if (MwGetInteger(column->slider, MwNvalue) !=
            column->max - channel->volume ||
        column->max != std::max(100, channel->volume))
      setColumn(*column, *channel);
    else
      MwVaApply(column->mute, MwNchecked, channel->muted ? 1 : 0, NULL);
  }
}

void MWAPI MixerWindow::sliderChanged(MwWidget handle, void *user, void *call) {
  (void)call;
  auto &column = *static_cast<Column *>(user);
  int volume = column.max - MwGetInteger(handle, MwNvalue);
  column.window->mOnVolume(column.channel, std::clamp(volume, 0, column.max));
}

void MWAPI MixerWindow::muteChanged(MwWidget handle, void *user, void *call) {
  (void)call;
  auto &column = *static_cast<Column *>(user);
  column.window->mOnMute(column.channel, MwGetInteger(handle, MwNchecked) != 0);
}

void MWAPI MixerWindow::expandClicked(MwWidget handle, void *user,
                                      void *call) {
  (void)handle;
  (void)call;
  auto *window = static_cast<MixerWindow *>(user);
  window->mExpanded = !window->mExpanded;
  // Rebuilt rather than resized, the popup's size is fixed once shown.
  window->build();
}
