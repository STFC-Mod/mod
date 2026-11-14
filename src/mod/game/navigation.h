#pragma once

// System and galaxy navigation: camera zoom and pan, LOD, sections, bookmarks, course prompts.

#include "app.h"
#include "fleet.h"
#include "ui.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

enum class ZoomLevels : int32_t {
  None   = 0,
  Near   = 1,
  Middle = 2,
  Far    = 3,
};

struct NavigationLOD : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationLOD">;

  // Writes the backing field directly; the managed setter is private.
  void SetTargetLevel(ZoomLevels level)
  { SetField<"<TargetLevel>k__BackingField">(level); }
  void UpdateLOD(ZoomLevels level)
  { Call<"UpdateLOD">(level); }
};

enum class NodeDepth : int32_t;

struct NavigationPan : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationPan">;

  NodeDepth nodeDepth()
  { return Field<"_nodeDepth", NodeDepth>(); }

  unity::Vector2 lastDelta()
  { return Field<"_lastDelta", unity::Vector2>(); }
  void SetLastDelta(unity::Vector2 value)
  { SetField<"_lastDelta">(value); }
  bool panningStarted()
  { return Field<"_panningStarted", bool>(); }
  float deltaScaler()
  { return Field<"_deltaScaler", float>(); }
  void SetDeltaScaler(float value)
  { SetField<"_deltaScaler">(value); }
  float keyboardDeltaScaler()
  { return Field<"_keyboardDeltaScaler", float>(); }
  void SetKeyboardDeltaScaler(float value)
  { SetField<"_keyboardDeltaScaler">(value); }
  // Managed POI; only checked for null or cleared.
  void* trackingPOI()
  { return Field<"_trackingPOI", void*>(); }
  void SetTrackingPOI(void* value)
  { SetField<"_trackingPOI">(value); }

  float farMagRadiusRatioSystemNormal()
  { return Field<"_farMagRadiusRatioSystemNormal", float>(); }
  void SetFarMagRadiusRatioSystemExtended(float value)
  { SetField<"_farMagRadiusRatioSystemExtended">(value); }

  void MoveCamera(unity::Vector2 delta, bool isMomentum = false)
  { Call<"MoveCamera">(delta, isMomentum); }

  static bool BlockPan()
  { return Class::Field<"BlockPan", bool>(); }
};

enum class NodeDepth : int32_t {
  Galaxy       = 1,
  SolarSystem  = 2,
  PlanetSystem = 4,
  Starbase     = 8,
};

struct MathUtils {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.Client.Core", "MathUtils">;

  // The world point under the screen position `mouse`, as seen by `camera`.
  static unity::Vector3 GetMouseWorldPos(unity::Camera* camera, unity::Vector3 mouse)
  { return Class::Call<"GetMouseWorldPos", unity::Vector3>(camera, mouse); }
};

struct NavigationZoom : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationZoom">;

  float Distance()
  { return Get<"Distance", float>(); }
  void SetDistance(float value)
  { Set<"Distance">(value); }
  float NormalizedZoom()
  { return Get<"NormalizedZoom", float>(); }

  NodeDepth depth()
  { return Field<"_depth", NodeDepth>(); }
  ZoomLevels zoomLevel()
  { return Field<"_zoomLevel", ZoomLevels>(); }
  unity::Camera* sceneCamera()
  { return Field<"_sceneCamera", unity::Camera*>(); }

  void SetZoomtotal(float value)
  { SetField<"_zoomtotal">(value); }
  float minimum()
  { return Field<"_minimum", float>(); }
  float maximum()
  { return Field<"_maximum", float>(); }
  void SetMaximum(float value)
  { SetField<"_maximum">(value); }
  void SetZoomDelta(float value)
  { SetField<"_zoomDelta">(value); }
  float viewRadius()
  { return Field<"_viewRadius", float>(); }
  void SetLastZoomDelta(float value)
  { SetField<"_lastZoomDelta">(value); }
  void SetZoomLocation(unity::Vector2 value)
  { SetField<"_zoomLocation">(value); }
  void SetWorldPoint(unity::Vector3 value)
  { SetField<"_worldPoint">(value); }
  void SetFarRatioSystemNormal(float value)
  { SetField<"_farRatioSystemNormal">(value); }
  void SetFarRatioSystemExtended(float value)
  { SetField<"_farRatioSystemExtended">(value); }

  void ZoomCameraAtWorldPoint()
  { Call<"ZoomCameraAtWorldPoint">(); }
};

struct NavigationFleetWidget : Widget<FleetDeployedData> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationFleetWidget">;

  NavigationLOD* lod()
  { return Field<"_lod", NavigationLOD*>(); }
};

struct NavigationManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationManager">;

  void HideInteraction()
  { Call<"HideInteraction">(); }
};

// A plain MonoBehaviour with its own static Instance, not a MonoSingleton.
struct NavigationSectionManager : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationSectionManager">;

  // Named after the property, so the type needs `::` here.
  ::NavigationManager* NavigationManager()
  { return Get<"NavigationManager", ::NavigationManager*>(); }

  static NavigationSectionManager* Instance()
  { return Class::Get<"Instance", NavigationSectionManager*>(); }
  static void ChangeNavigationSection(SectionID sectionId, int64_t systemId = -1)
  { Class::Call<"ChangeNavigationSection">(sectionId, systemId); }

  // Closes whatever interaction menu is open in space before the mod acts on a fleet.
  static void HideInteraction()
  {
    auto* section = Instance();
    if (auto* navigation = section ? section->NavigationManager() : nullptr)
      navigation->HideInteraction();
  }
};

struct NavigationInteractionUIContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationInteractionUIContext">;
};

struct SetCourseWidget : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "SetCourseWidget">;

  VisibilityController* visibilityController()
  { return Field<"_visibilityController", VisibilityController*>(); }
  SemaphoreButtonListener* joinWaveDefenseButton()
  { return Field<"_joinWaveDefenseButton", SemaphoreButtonListener*>(); }
};

struct NavigationInteractionUIViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationInteractionUIViewController">;

  bool IsSetCourseVisible()
  {
    auto* widget     = Field<"_setCourseWidget", SetCourseWidget*>();
    auto* visibility = widget ? widget->visibilityController() : nullptr;
    return visibility && visibility->IsShown() && widget->isActiveAndEnabled();
  }
  void OnSetCourseButtonClick()
  { Call<"OnSetCourseButtonClick">(); }

  // Presses the set-course widget's wave defense join button when it is listening. Button.Press keeps Unity's
  // active and interactable checks and the game's own join callback.
  bool TryPressJoinWaveDefense()
  {
    auto* widget   = IsSetCourseVisible() ? Field<"_setCourseWidget", SetCourseWidget*>() : nullptr;
    auto* listener = widget ? widget->joinWaveDefenseButton() : nullptr;
    if (!listener || !listener->isActiveAndEnabled())
      return false;
    listener->Press();
    return true;
  }
};

struct PlanetViewUtils : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "PlanetViewUtils">;

  // The planet's 2D sprite, a Component. Calling this also runs any detour on get_FlatRenderable.
  unity::Component* FlatRenderable()
  { return static_cast<unity::Component*>(Get<"FlatRenderable", Il2CppObject*>()); }
};

struct CoordinateSearchContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Bookmarks", "CoordinateSearchContext">;

  void SetDefaultXCoordinate(Il2CppString* value)
  { Set<"DefaultXCoordinate">(value); }
  void SetDefaultYCoordinate(Il2CppString* value)
  { Set<"DefaultYCoordinate">(value); }
  void SetDefaultSystemInput(Il2CppString* value)
  { Set<"DefaultSystemInput">(value); }
};

struct BookmarksManager : MonoSingleton<BookmarksManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Bookmarks", "BookmarksManager">;

  void ViewBookmarks()
  { Call<"ViewBookmarks">(); }

  void ViewCoordinateSearch()
  {
    auto* context = il2cpp::New<CoordinateSearchContext>();
    // The binding already reported the missing class or constructor; plain bookmarks are the next best view.
    if (!context) {
      ViewBookmarks();
      return;
    }

    auto* zero = il2cpp::NewString("0");
    context->SetDefaultXCoordinate(zero);
    context->SetDefaultYCoordinate(zero);
    context->SetDefaultSystemInput(il2cpp::NewString(""));
    Hub::GotoSection(SectionID::Bookmarks_Search_Coordinates, context);
  }
};

struct CoursePromptPopupDataContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "CoursePromptPopupDataContext">;

  bool HasInstantWarp()
  { return Field<"HasInstantWarp", bool>(); }
  // A public field, named after it, so the type needs `::` here.
  ::CourseData* CourseData()
  { return Field<"CourseData", ::CourseData*>(); }
};

struct CoursePromptPopupWidget : Widget<CoursePromptPopupDataContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "CoursePromptPopupWidget">;

  void InitiateRegularWarp()
  { Call<"InitiateRegularWarp">(); }
  void OnInstantWarpButtonClick()
  { Call<"OnInstantWarpButtonClick">(); }
};

struct CoursePromptPopupViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "CoursePromptPopupViewController">;

  CoursePromptPopupWidget* coursePromptPopupWidget()
  { return Field<"_coursePromptPopupWidget", CoursePromptPopupWidget*>(); }
};

// TouchKit's touch; a held touch reports Stationary, which the pan ignores.
struct TKTouch : il2cpp::Object {
  using Class = il2cpp::Class<"TouchKit", "", "TKTouch">;

  unity::TouchPhase phase()
  { return Field<"phase", unity::TouchPhase>(); }
  void SetPhase(unity::TouchPhase phase)
  { SetField<"phase">(phase); }
};
