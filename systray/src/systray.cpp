#include "systray.hpp"
#ifdef TCC_SYSTRAY_DBUS
#include "dbus/sni_watcher.hpp"
#endif

#include <cstdio>

TCCSystrayClient::TCCSystrayClient() {
  mWindow = window_setup();

#ifdef TCC_SYSTRAY_DBUS
  addProtocol(std::make_unique<StatusNotifierWatcher>());
#endif

  if (mProtocols.empty())
    fprintf(stderr, "tcc_systray: systray icons will not be available\n");
}

void TCCSystrayClient::addProtocol(std::unique_ptr<SystrayProtocol> protocol) {
  protocol->setItemsChangedCallback(
      [this](SystrayProtocol &protocol, const std::vector<std::string> &items) {
        itemsChanged(protocol, items);
      });
  if (!protocol->connect()) {
    fprintf(stderr, "tcc_systray: %s is unavailable\n", protocol->name());
    return;
  }
  mProtocols.push_back(std::move(protocol));
}

void TCCSystrayClient::itemsChanged(SystrayProtocol &protocol,
                                    const std::vector<std::string> &items) {
  fprintf(stderr, "tcc_systray: %zu %s item(s)\n", items.size(),
          protocol.name());
  for (const auto &item : items)
    fprintf(stderr, "  %s\n", item.c_str());
}

void TCCSystrayClient::run() {
  while (!MwWindowShouldClose(mWindow)) {
    for (auto &protocol : mProtocols)
      protocol->poll();
    MwStep(mWindow);
  };
}
