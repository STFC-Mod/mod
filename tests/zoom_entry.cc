#include "features/zoom_entry.h"

#include <cassert>

int main()
{
  ZoomEntry::Gate system, galaxy;
  // First view needs no SetViewParameters notification to schedule the default.
  system.Observe(true);
  galaxy.Observe(false);
  assert(!galaxy.Consume(true));
  assert(!system.Consume(false)); // Camera/range not ready yet.
  assert(system.Consume(true));
  system.Observe(true);
  assert(!system.Consume(true)); // Leave manual zoom alone on subsequent frames.

  // Re-entering a cached controller through depth alone schedules another default.
  system.Observe(false);
  system.Observe(true);
  assert(system.Consume(true));

  // Leaving before readiness cancels the request.
  system.pending = true;
  system.Observe(false);
  assert(!system.Consume(true));

  // An explicit entry notification can request a reset within the same depth.
  system.Observe(true);
  assert(system.Consume(true));
  system.pending = true;
  assert(system.Consume(true));

  // The cached controller can remain at system depth while another screen is visible. A shared-frame sample
  // records that interruption even if the controller receives no Update until the player comes back.
  system.Observe(true, false);
  assert(!system.Consume(true));
  system.Observe(true, true);
  assert(system.Consume(true));
  system.Observe(true, true);
  assert(!system.Consume(true));

  // Native transitions can still change the camera; do not consume a default until the view has settled.
  system.Observe(true, false);
  system.pending = true; // SetViewParameters ran before section completion.
  assert(!system.Consume(true));
  system.Observe(true, true);
  assert(system.Consume(true));
}
