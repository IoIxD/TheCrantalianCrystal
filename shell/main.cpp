#include "client/client.hpp"
#include "desktop/desktop.hpp"
#include <memory>
#include <thread>
#include <unistd.h>
#include <wayland-client.h>

int main() {
  auto client = std::make_shared<TCCClient>();

  // for testing until we have a program for launching windows
  std::thread konsole_thread([&]() { system("/usr/bin/konsole"); });

  std::thread desktop_thread([&]() {
    TCCDesktopClient desktop(client);
    desktop.run();
  });
  client->run();
}
