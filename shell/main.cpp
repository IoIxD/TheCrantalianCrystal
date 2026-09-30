#include "bluescreen/bluescreen.hpp"
#include "client/client.hpp"
#include <execinfo.h>
#include <format>
#include <memory>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <ucontext.h>
#include <unistd.h>

#include "dynload.hpp"

static std::shared_ptr<TCCClient> client;

static std::string fmt(const char *name, unsigned long long v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%-7s 0x%016llx", name, v);
  return buf;
}

static void sigsegv_handler(int sig, siginfo_t *si, void *unused) {
  std::vector<std::string> fullStacktrace;
  std::vector<std::pair<std::string, std::string>> registers;
  void *callstack[128];
  int i, frames = backtrace(callstack, 128);
  char **strs = backtrace_symbols(callstack, frames);
  fullStacktrace.push_back(std::format("AT ADDRESS {}", si->si_addr));
  for (i = 0; i < frames; ++i) {
    fullStacktrace.push_back(strs[i]);
    printf("%s\n", strs[i]);
  }
  free(strs);
  user_regs_struct r{};

  static const char *names[NGREG] = {
      "r8",  "r9",  "r10",    "r11", "r12",    "r13",     "r14", "r15",
      "rdi", "rsi", "rbp",    "rbx", "rdx",    "rax",     "rcx", "rsp",
      "rip", "efl", "csgsfs", "err", "trapno", "oldmask", "cr2"};
  ucontext_t ctx;
  getcontext(&ctx);

  for (int i = 0; i < NGREG; ++i) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "0x%016llx",
                  (unsigned long long)ctx.uc_mcontext.gregs[i]);
    registers.push_back(std::pair<std::string, std::string>(names[i], buf));
  }

  if (!getenv("TCC_BYPASS_BLUESCREEN")) {
    for (auto output : client->outputs()) {
      printf("%p\n", output);
      auto bluescreen =
          new TCCBluescreenClient(output, fullStacktrace, registers);

      bluescreen->run();
      client->terminate();
      client->run();
    }
  }
  exit(0);
}

int main() {
  if (!dynload_setup::gtk() || !dynload_setup::egl() || !dynload_setup::gl() ||
      !dynload_setup::wayland() || !dynload_setup::freetype()) {
    return 1;
  };

  char *p;
  char a;
  int pagesize;
  struct sigaction sa;

  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = sigsegv_handler;
  sigaction(SIGSEGV, &sa, NULL);

  client = std::make_shared<TCCClient>();

  client->run();
}
