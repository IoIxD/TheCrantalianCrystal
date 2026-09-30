#include "support.hpp"

#include "pam_loader.hpp"

#include <string>
#include <unistd.h>

namespace tcc_support {
bool systemd() { return access("/run/systemd/system/", F_OK) == 0; }

bool pam(const char *service) {
  if (!PAM_LIB || !PAM_LIB->handle) {
    return false;
  }
  for (const char *dir : {"/etc/pam.d/", "/usr/lib/pam.d/"}) {
    if (access((std::string(dir) + service).c_str(), F_OK) == 0) {
      return true;
    }
  }
  return false;
}
} // namespace tcc_support
