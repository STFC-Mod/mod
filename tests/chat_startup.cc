#include "patches/chat_startup.h"
#include <cassert>
#include <iostream>

int main()
{
  ChatStartup::Gate gate;
  // Slow initial load must never open, regardless of elapsed wall time.
  assert(!gate.Tick(false, false, 0));
  assert(!gate.Tick(false, false, 100));
  assert(!gate.Tick(true, false, 101));
  assert(!gate.Tick(true, false, 103.99));
  // A transition or lost readiness restarts the full settling period.
  assert(!gate.Tick(false, false, 104));
  assert(!gate.Tick(true, false, 110));
  assert(!gate.Tick(true, false, 112.99));
  assert(gate.Tick(true, false, 113));
  assert(!gate.Tick(true, false, 200)); // User closes chat: do not reopen.
  gate = {}; // Login/reload permits one new open.
  assert(!gate.Tick(true, false, 201));
  assert(gate.Tick(true, false, 204));
  gate = {};
  assert(!gate.Tick(false, true, 205)); // Manual chat wins, even before readiness.
  assert(!gate.Tick(true, false, 300));
  assert(!gate.Tick(true, false, 303));
  std::cout << "Chat startup readiness regression tests passed\n";
}
