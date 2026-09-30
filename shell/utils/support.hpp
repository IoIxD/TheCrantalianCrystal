#pragma once

/*
 * Checks for system services that some features depend on, so they can be
 * turned off when those aren't there.
 */
namespace tcc_support {
// Whether the system was booted with systemd (the same check as sd_booted()).
bool systemd();
// Whether libpam was loaded and has a service file for `service`. Without
// one PAM falls back to "other", which usually denies everything.
bool pam(const char *service);
} // namespace tcc_support
