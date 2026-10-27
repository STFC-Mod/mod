#include "settings/input_release.h"
#include <cassert>
#include <iostream>
int main()
{
  mod_settings::InputRelease release;
  assert(release.Tick(true, false));
  release.Hold(); // Typing/search focus owns input.
  for (int i = 0; i < 10; ++i)
    assert(!release.Tick(true, true)); // Blur/result navigation must still drain Q, W or mouse.
  assert(!release.Tick(false, false)); // Focus loss cannot prove key release.
  assert(!release.Tick(true, false));
  assert(!release.Tick(true, true)); // A held/repressed key resets the quiet frame.
  assert(!release.Tick(true, false));
  assert(release.Tick(true, false));
  release.Hold(); // Reopening takes ownership again without returning keys to gameplay.
  assert(release.active() && !release.Tick(true, false));
  assert(release.Tick(true, false));
  std::cout << "Input ownership survives blur/teardown/focus loss until supported keys release\n";
}
