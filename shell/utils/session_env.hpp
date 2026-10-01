#pragma once

/*
 * Sets XDG_CURRENT_DESKTOP to TCC for us and our children, and hands it to
 * the D-Bus and systemd user activation environments so things they start
 * (xdg-desktop-portal in particular, which picks tcc-portals.conf by it) see
 * it too.
 *
 * The activation environments are left alone if DBus isn't available.
 */
void tcc_setup_session_env();
