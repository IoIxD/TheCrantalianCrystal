#pragma once

#include "systray_protocol.hpp"

#include <Mw/Milsko.h>

#include <memory>
#include <string>
#include <vector>

class TCCSystrayClient {
  MwWidget mWindow;
  std::vector<std::unique_ptr<SystrayProtocol>> mProtocols;

  void addProtocol(std::unique_ptr<SystrayProtocol> protocol);
  void itemsChanged(SystrayProtocol &protocol,
                    const std::vector<std::string> &items);

public:
  TCCSystrayClient();
  void run();
};

#ifdef __cplusplus
extern "C" {
#endif

MwWidget window_setup();

#ifdef __cplusplus
}
#endif
