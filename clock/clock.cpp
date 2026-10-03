#include "clock.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace clock_config;

volatile sig_atomic_t ClockWindow::sQuitRequested = 0;

static double now() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

static double ease(double t) {
  t = std::clamp(t, 0.0, 1.0);
  return t * t * (3 - 2 * t);
}

ClockWindow::ClockWindow(uint32_t output_name) : mOutputName(output_name) {}

ClockWindow::~ClockWindow() {
  teardown_gl();
  teardown_surface();
}

void ClockWindow::quit_handler(int) { sQuitRequested = 1; }

bool ClockWindow::setup() {
  // What's behind the clock has to be copied before it's there.
  mHaveBackdrop = capture_backdrop(
      mOutputName, WINDOW_X - BACKDROP_MARGIN, WINDOW_Y - BACKDROP_MARGIN,
      WINDOW_WIDTH + 2 * BACKDROP_MARGIN, WINDOW_HEIGHT + 2 * BACKDROP_MARGIN,
      &mBackdrop);
  if (!mHaveBackdrop) {
    // Plain glass, then, which frosts to the same thing.
    const unsigned char tint = TINT * 255;
    mBackdrop.width = mBackdrop.height = 1;
    mBackdrop.pixels = {tint, tint, tint, 255};
  }

  // The shell asks the clock to go away with SIGTERM, and it fades out first.
  // (no SA_RESTART, so it interrupts waiting for events)
  struct sigaction sa = {};
  sa.sa_handler = quit_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);

  mDisplay = wl_display_connect(NULL);
  if (!mDisplay) {
    fprintf(stderr, "tcc_clock: couldn't connect to the compositor\n");
    return false;
  }
  if (!setup_surface() || !setup_egl() || !setup_gl()) {
    return false;
  }
  // (without it, there's just no date and time)
  setup_font();
  return true;
}

double ClockWindow::frost_strength(bool *finished) {
  double t = now();
  *finished = false;

  if (mFadeInStart < 0) {
    mFadeInStart = t;
  }
  double strength = mHaveBackdrop ? ease((t - mFadeInStart) / FADE_TIME) : 0;

  if (sQuitRequested && mFadeOutStart < 0) {
    mFadeOutStart = t;
    mFadeOutFrom = strength;
  }
  if (mFadeOutStart >= 0) {
    // (it takes less time to fade out from part way in)
    double length = FADE_TIME * mFadeOutFrom;
    double done = length > 0 ? (t - mFadeOutStart) / length : 1;
    *finished = done >= 1;
    strength = mFadeOutFrom * (1 - ease(done));
  }
  return strength;
}

double ClockWindow::shown(double strength) const {
  // (all the way, without a backdrop to fade in with)
  return mHaveBackdrop ? strength : 1;
}

double ClockWindow::blur_sigma(double shown) const {
  return (1 - shown) * MAX_BLUR * mScale;
}

bool ClockWindow::fading() const {
  return mHaveBackdrop &&
         (mFadeOutStart >= 0 || now() - mFadeInStart < FADE_TIME);
}

bool ClockWindow::update_time(time_t t) {
  const char *wday[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char date[sizeof(mDate.string)], time[sizeof(mTime.string)];

  if (t == mLastSecond) {
    return false;
  }
  mLastSecond = t;

  localtime_r(&t, &mTm);
  snprintf(date, sizeof(date), "%s %02d %s", mon[mTm.tm_mon], mTm.tm_mday,
           wday[mTm.tm_wday]);
  snprintf(time, sizeof(time), "%02d:%02d %s", mTm.tm_hour % 12, mTm.tm_min,
           mTm.tm_hour >= 12 ? "PM" : "AM");

  bool changed = false;
  if (strcmp(date, mDate.string) != 0) {
    strcpy(mDate.string, date);
    render_text(&mDate);
    changed = true;
  }
  if (strcmp(time, mTime.string) != 0) {
    // (the hands only move when this does)
    strcpy(mTime.string, time);
    render_text(&mTime);
    changed = true;
  }
  return changed;
}

void ClockWindow::run() {
  while (!mClosed) {
    bool finished;
    double strength = frost_strength(&finished);
    if (finished) {
      break;
    }
    if (update_time(::time(NULL))) {
      mChanged = true;
    }
    if (mChanged || strength != mStrength) {
      mChanged = false;
      mStrength = strength;
      draw(strength);
    }

    // Every frame while fading, and otherwise not until the next second (or
    // something happens, like being asked to quit).
    int timeout = 16;
    if (!fading()) {
      timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      timeout = 1000 - ts.tv_nsec / 1000000 + 1;
    }
    if (!dispatch(timeout)) {
      break;
    }
  }
}
