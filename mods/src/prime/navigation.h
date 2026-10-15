#pragma once

// System and galaxy navigation: camera zoom and pan, LOD, sections, bookmarks, course prompts.

#include "Hub.h"
#include "fleet.h"
#include "ui.h"
#include "unity.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

enum class ZoomLevels {
  None   = 0,
  Near   = 1,
  Middle = 2,
  Far    = 3,
};

struct NavigationLOD : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationLOD">;

  void UpdateLOD(ZoomLevels level)
  { Call<"UpdateLOD">(level); }

  // Writes the backing field directly; the managed setter is private.
  void SetTargetLevel(ZoomLevels level)
  { SetField<"<TargetLevel>k__BackingField">(level); }
};

#pragma pack(push, 1)
struct vec3 {
  float x, y, z;
};
#pragma pack(pop)

struct NavigationPan : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationPan">;

  void MoveCamera(vec2 delta, bool isMomentum = false)
  { Call<"MoveCamera">(delta, isMomentum); }

  static bool BlockPan()
  { return Class::Field<"BlockPan", bool>(); }

  Vector2 lastDelta()
  { return Field<"_lastDelta", Vector2>(); }
  void SetLastDelta(Vector2 value)
  { SetField<"_lastDelta">(value); }
  bool panningStarted()
  { return Field<"_panningStarted", bool>(); }
  float deltaScaler()
  { return Field<"_deltaScaler", float>(); }
  float keyboardDeltaScaler()
  { return Field<"_keyboardDeltaScaler", float>(); }
  // Managed POI; only checked for null or cleared.
  void* trackingPOI()
  { return Field<"_trackingPOI", void*>(); }
  void SetTrackingPOI(void* value)
  { SetField<"_trackingPOI">(value); }

  float farMagRadiusRatioSystemNormal()
  { return Field<"_farMagRadiusRatioSystemNormal", float>(); }
  void SetFarMagRadiusRatioSystemNormal(float value)
  { SetField<"_farMagRadiusRatioSystemNormal">(value); }
  float farMagRadiusRatioSystemExtended()
  { return Field<"_farMagRadiusRatioSystemExtended", float>(); }
  void SetFarMagRadiusRatioSystemExtended(float value)
  { SetField<"_farMagRadiusRatioSystemExtended">(value); }
  float nearMagRadiusRatioSystem()
  { return Field<"_nearMagRadiusRatioSystemNormal", float>(); }
};

enum class NodeDepth : int32_t {
  Galaxy       = 1,
  SolarSystem  = 2,
  PlanetSystem = 4,
  Starbase     = 8,
};

struct NavigationZoom : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationZoom">;

  void SetViewParameters(float radius, NodeDepth depth)
  { Call<"SetViewParameters">(radius, depth); }
  void ZoomCameraAtWorldPoint()
  { Call<"ZoomCameraAtWorldPoint">(); }

  float Distance()
  { return Get<"Distance", float>(); }
  void SetDistance(float value)
  { Set<"Distance">(value); }
  float NormalizedZoom()
  { return Get<"NormalizedZoom", float>(); }

  NodeDepth depth()
  { return Field<"_depth", NodeDepth>(); }
  void SetDepth(NodeDepth value)
  { SetField<"_depth">(value); }
  ZoomLevels zoomLevel()
  { return Field<"_zoomLevel", ZoomLevels>(); }
  Camera* sceneCamera()
  { return Field<"_sceneCamera", Camera*>(); }

  float storedZoomDistanceSystem()
  { return Field<"_storedZoomDistanceSystem", float>(); }
  void SetStoredZoomDistanceSystem(float value)
  { SetField<"_storedZoomDistanceSystem">(value); }
  float systemDefaultZoomRatio()
  { return Field<"_systemDefaultZoomRatio", float>(); }
  void SetSystemDefaultZoomRatio(float value)
  { SetField<"_systemDefaultZoomRatio">(value); }
  float actualDistance()
  { return Field<"_actualDistance", float>(); }
  void SetActualDistance(float value)
  { SetField<"_actualDistance">(value); }
  float zoomtotal()
  { return Field<"_zoomtotal", float>(); }
  void SetZoomtotal(float value)
  { SetField<"_zoomtotal">(value); }
  float minimum()
  { return Field<"_minimum", float>(); }
  void SetMinimum(float value)
  { SetField<"_minimum">(value); }
  float maximum()
  { return Field<"_maximum", float>(); }
  void SetMaximum(float value)
  { SetField<"_maximum">(value); }
  float zoomDelta()
  { return Field<"_zoomDelta", float>(); }
  void SetZoomDelta(float value)
  { SetField<"_zoomDelta">(value); }
  float viewRadius()
  { return Field<"_viewRadius", float>(); }
  void SetViewRadius(float value)
  { SetField<"_viewRadius">(value); }
  float lastZoomDelta()
  { return Field<"_lastZoomDelta", float>(); }
  void SetLastZoomDelta(float value)
  { SetField<"_lastZoomDelta">(value); }
  vec2 zoomLocation()
  { return Field<"_zoomLocation", vec2>(); }
  void SetZoomLocation(vec2 value)
  { SetField<"_zoomLocation">(value); }
  vec3 worldPoint()
  { return Field<"_worldPoint", vec3>(); }
  void SetWorldPoint(vec3 value)
  { SetField<"_worldPoint">(value); }
  float farRatioSystemNormal()
  { return Field<"_farRatioSystemNormal", float>(); }
  void SetFarRatioSystemNormal(float value)
  { SetField<"_farRatioSystemNormal">(value); }
  float farRatioSystemExtended()
  { return Field<"_farRatioSystemExtended", float>(); }
  void SetFarRatioSystemExtended(float value)
  { SetField<"_farRatioSystemExtended">(value); }
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

struct NavigationSectionManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationSectionManager">;

  static void ChangeNavigationSection(SectionID sectionId, int64_t systemId = -1)
  { Class::Call<"ChangeNavigationSection">(sectionId, systemId); }

  static NavigationSectionManager* Instance()
  { return Class::Get<"Instance", NavigationSectionManager*>(); }

  // Not NavigationManager(): that would hide the type inside this struct.
  NavigationManager* SNavigationManager()
  { return Get<"NavigationManager", NavigationManager*>(); }

  // Closes whatever interaction menu is open in space before the mod acts on a fleet.
  static void HideInteraction()
  {
    auto* section = Instance();
    if (auto* navigation = section ? section->SNavigationManager() : nullptr)
      navigation->HideInteraction();
  }
};

struct NavigationInteractionUIContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationInteractionUIContext">;
};

struct SetCourseWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "SetCourseWidget">;
  VisibilityController* visibilityController()
  { return Field<"_visibilityController", VisibilityController*>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  SemaphoreButtonListener* joinWaveDefenseButton()
  { return Field<"_joinWaveDefenseButton", SemaphoreButtonListener*>(); }
};

struct NavigationInteractionUIViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "NavigationInteractionUIViewController">;

  bool TryPressJoinWaveDefense()
  {
    if (!IsSetCourseVisible()) {
      return false;
    }
    auto* widget   = Field<"_setCourseWidget", SetCourseWidget*>();
    auto* listener = widget ? widget->joinWaveDefenseButton() : nullptr;
    if (!listener || !listener->isActiveAndEnabled()) {
      return false;
    }
    // Button.Press preserves Unity's active/interactable checks and the native join callback.
    if (auto* button = listener->TheButton()) {
      button->Press();
    }
    return true;
  }

  bool IsSetCourseVisible()
  {
    auto* widget = Field<"_setCourseWidget", SetCourseWidget*>();
    auto* visibility = widget ? widget->visibilityController() : nullptr;
    return visibility && (visibility->state() == VisibilityState::Visible || visibility->state() == VisibilityState::Show)
           && widget->isActiveAndEnabled();
  }
  void OnSetCourseButtonClick()
  { Call<"OnSetCourseButtonClick">(); }
};

struct OrbitFrameProvider : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.CameraController", "OrbitFrameProvider">;

  // Declared on AbstractOrbitFrameProvider.
  float RotationAngleDelta()
  { return Field<"_rotationAngleDelta", float>(); }
  void SetRotationAngleDelta(float value)
  { SetField<"_rotationAngleDelta">(value); }
};

struct PopulatedSystemData : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "PopulatedSystemData">;

  void* backdrop()
  { return Field<"_backdrop", void*>(); }
  int64_t systemId()
  { return Field<"_systemId", int64_t>(); }
};

struct PlanetViewUtils : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "PlanetViewUtils">;

  // Managed FlatRenderable. Calling this also runs any detour on get_FlatRenderable.
  void* GetFlatRenderable()
  { return Get<"FlatRenderable", void*>(); }

  PopulatedSystemData* popData()
  { return Field<"_popData", PopulatedSystemData*>(); }
  Transform* activeSystemParent()
  { return Field<"_activeSystemParent", Transform*>(); }
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

    auto* zero = il2cpp_string_new("0");
    context->SetDefaultXCoordinate(zero);
    context->SetDefaultYCoordinate(zero);
    context->SetDefaultSystemInput(il2cpp_string_new(""));
    Hub::get_SectionManager()->TriggerSectionChange(SectionID::Bookmarks_Search_Coordinates, context, false, false,
                                                    true);
  }
};

struct CoursePromptPopupDataContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "CoursePromptPopupDataContext">;

  bool HasInstantWarp()
  { return Field<"HasInstantWarp", bool>(); }
  CourseData* GetCourseData()
  { return Field<"CourseData", CourseData*>(); }
};

struct CoursePromptPopupWidget : Widget<CoursePromptPopupDataContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Navigation", "CoursePromptPopupWidget">;

  GenericButtonWidget* LeftButton()
  { return Field<"_leftButton", GenericButtonWidget*>(); }
  GenericButtonWidget* RightButton()
  { return Field<"_rightButton", GenericButtonWidget*>(); }
  void InitiateRegularWarp()
  { Call<"InitiateRegularWarp">(); }
  void OnInstantWarpButtonClick()
  { Call<"OnInstantWarpButtonClick">(); }
};

struct CoursePromptPopupViewController : ViewController<CoursePromptPopupDataContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "CoursePromptPopupViewController">;

  CoursePromptPopupWidget* PopupWidget()
  { return Field<"_coursePromptPopupWidget", CoursePromptPopupWidget*>(); }
};
