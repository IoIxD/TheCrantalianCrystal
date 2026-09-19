#include "client/client.hpp"
#include <unistd.h>
#include <wayland-client.h>

int main() {
  TCCClient client;

  if (fork() == 0) {
    execve("/usr/bin/konsole", NULL, environ);
  } else {
    client.run();
  }
}
