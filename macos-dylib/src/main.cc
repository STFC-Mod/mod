// The macOS entry point: the loader injects this dylib into the game.

#include <syslog.h>

#include "patches/patches.h"

__attribute__((constructor)) void InjectedConstructor(int, const char** argv)
{
  syslog(LOG_ERR, "[+] dylib injected in %s\n", argv[0]);
  ApplyPatches();
}
