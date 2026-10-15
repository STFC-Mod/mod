// The game screens and widgets the shortcuts and labels look up through unity::Newest and unity::Instances. A class
// left out here is never found there.

#include "patches/patches.h"

#include "prime/chat.h"
#include "prime/fleet.h"
#include "prime/inventory.h"
#include "prime/navigation.h"
#include "prime/officers.h"
#include "prime/ui.h"
#include "prime/viewers.h"

#include <unity/tracker.h>

namespace
{
void Install()
{
  unity::Track<PreScanTargetWidget>();
  unity::Track<FleetBarViewController>();
  unity::Track<FleetMeshSelector>();
  unity::Track<AllianceStarbaseObjectViewerWidget>();
  unity::Track<AnimatedRewardsScreenViewController>();
  unity::Track<ArmadaObjectViewerWidget>();
  unity::Track<ArtifactHallDetailsViewController>("OnDidBindCanvasContext", "OnAboutToReleaseCanvasContext");
  unity::Track<AssignShipsWidget>();
  unity::Track<CelestialObjectViewerWidget>();
  unity::Track<EmbassyObjectViewer>();
  unity::Track<FullScreenChatViewController>();
  unity::Track<HousingObjectViewerWidget>();
  unity::Track<InventoryListViewController>();
  unity::Track<MiningObjectViewerWidget>();
  unity::Track<MissionsObjectViewerWidget>();
  unity::Track<NavigationInteractionUIViewController>();
  unity::Track<OfficerAssignmentViewController>();
  unity::Track<ElementSelectorViewController>();
  unity::Track<ShipManagementViewController>();
  unity::Track<ShipManagementScreenStateViewController>();
  unity::Track<StarNodeObjectViewerWidget>();
}
} // namespace

const Feature kObjectTracker{"ObjectTracker", &Config::Patches::objecttracker, Install};
