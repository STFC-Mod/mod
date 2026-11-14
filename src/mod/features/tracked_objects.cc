// The game screens and widgets the shortcuts and labels look up through unity::Newest and unity::Instances. A class
// left out here is never found there.

#include "features/features.h"

#include "game/chat.h"
#include "game/fleet.h"
#include "game/inventory.h"
#include "game/navigation.h"
#include "game/officers.h"
#include "game/ui.h"
#include "game/viewers.h"

#include <unity/tracker.h>

namespace
{
bool InstallObjectTracker()
{
  unity::Track<AllianceStarbaseObjectViewerWidget>();
  unity::Track<AnimatedRewardsScreenViewController>();
  unity::Track<ArmadaObjectViewerWidget>();
  // Pooled: bound and released rather than created and destroyed.
  unity::Track<ArtifactHallDetailsViewController>("OnDidBindCanvasContext", "OnAboutToReleaseCanvasContext");
  unity::Track<AssignShipsWidget>();
  unity::Track<CelestialObjectViewerWidget>();
  unity::Track<ElementSelectorViewController>();
  unity::Track<EmbassyObjectViewer>();
  unity::Track<FleetBarViewController>();
  unity::Track<FleetMeshSelector>();
  unity::Track<FullScreenChatViewController>();
  unity::Track<HousingObjectViewerWidget>();
  unity::Track<InventoryListViewController>();
  unity::Track<MiningObjectViewerWidget>();
  unity::Track<MissionsObjectViewerWidget>();
  unity::Track<NavigationInteractionUIViewController>();
  unity::Track<OfficerAssignmentViewController>();
  unity::Track<PreScanTargetWidget>();
  unity::Track<ShipManagementScreenStateViewController>();
  unity::Track<ShipManagementViewController>();
  unity::Track<StarNodeObjectViewerWidget>();
  // Each class is a set of its own and reports itself; the tracker serves whichever classes went in.
  return true;
}
} // namespace

const Feature kObjectTracker{"ObjectTracker", &Config::Patches::objecttracker, InstallObjectTracker};
