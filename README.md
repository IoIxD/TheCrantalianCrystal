# TheCrantalianCrystal

River-based window manager for Linux that's functionally inspired on windows 3.1

better readme coming soon!

## Dependencies

In-line with what I do for all my Linux projects now, all shared libraries are dynloaded and certain libraries are treated as "optional" (features will be missing). All are required are build time.

**Required Runtime Dependencies:**

- glib 2.0 and gio 2.0
- gtk4
- wayland-client
- GL, EGL, and wayland-egl
- Freetype2
- varlink
- Milsko (bundled)
- xkbcommon

**Optional Dependencies:**

- Pulseaudio (systray audio mixer)
- DBus (systray icons)
- pam, systemd (Screen locking)
- gbm (Screensaver on the lock screen)
- kwallet (XDG Secrets Portal)
