// The macOS entry point: the loader injects this dylib into the game.

#include <syslog.h>

#include "mod.h"

// Before the mod's log exists, failures go to the system log: the game's stderr goes nowhere a user can see.
void ReportStartupFailure(const char* message)
{ syslog(LOG_ERR, "stfc-community-mod: %s", message); }

__attribute__((constructor)) void InjectedConstructor(int, const char** argv)
{
  syslog(LOG_INFO, "stfc-community-mod: loaded into %s", argv[0]);
  StartMod();
}
