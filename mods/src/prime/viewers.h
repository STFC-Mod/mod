#pragma once

// Object viewer cards shown for targets in space, and their scan/engage buttons.

#include "fleet.h"
#include "navigation.h"
#include "ui.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct ParentObjectViewerViewController : ViewController<NavigationInteractionUIContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "ParentObjectViewerViewController">;

  void ActivateViewerHUDState(bool isHUDOpen = false)
  { Call<"ActivateViewerHUDState">(isHUDOpen); }
  void TriggerInstantHide()
  { Call<"TriggerInstantHide">(); }
  void TriggerShow()
  { Call<"TriggerShow">(); }

  VisibilityController* visibilityController()
  { return Field<"_visibilityController", VisibilityController*>(); }
  bool IsShowing()
  { return Get<"IsShowing", bool>(); }
};

struct ObjectViewerBaseWidget : Widget<NavigationInteractionUIContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "ObjectViewerBaseWidget">;

  void HideAllViewers()
  { Call<"HideAllViewers">(); }

  VisibilityController* visibilityController()
  { return Field<"_visibilityController", VisibilityController*>(); }
  ParentObjectViewerViewController* Parent()
  { return Get<"Parent", ParentObjectViewerViewController*>(); }
  bool IsInfoShown()
  { return Get<"IsInfoShown", bool>(); }
};

struct RewardsButtonWidget : Widget<BattleTargetData> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Combat", "RewardsButtonWidget">;

  void RewardsClicked()
  { Call<"RewardsClicked">(); }

  VisibilityController* rewardsController()
  { return Field<"_rewardsController", VisibilityController*>(); }
};

struct ScanEngageButtonsWidget : Widget<BattleTargetData> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Combat", "ScanEngageButtonsWidget">;

  void OnArmadaButtonClicked()
  { Call<"OnArmadaButtonClicked">(); }
  void OnEngageButtonClicked()
  { Call<"OnEngageButtonClicked">(); }
  void OnScanButtonClicked()
  { Call<"OnScanButtonClicked">(); }
};

struct PreScanTargetWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Combat", "PreScanTargetWidget">;

  void ValidateThenCreateArmada()
  { Call<"ValidateThenCreateArmada">(); }
  void OnAddToQueueClickedEventHandler()
  { Call<"OnAddToQueueClickedEventHandler">(); }
  // Declared on ObjectViewerBaseWidget.
  void OnInfoClick()
  { Call<"OnInfoClick">(); }

  ScanEngageButtonsWidget* scanEngageButtonsWidget()
  { return Field<"_scanEngageButtonsWidget", ScanEngageButtonsWidget*>(); }
  RewardsButtonWidget* rewardsButtonWidget()
  { return Field<"_rewardsButtonWidget", RewardsButtonWidget*>(); }
  GenericButtonWidget* addToQueueButtonWidget()
  { return Field<"_addToQueueButtonWidget", GenericButtonWidget*>(); }
  GenericButtonWidget* armadaAttackButton()
  { return Field<"_armadaAttackButton", GenericButtonWidget*>(); }
};

struct AllianceStarbaseObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "AllianceStarbaseObjectViewerWidget">;
};

struct ArmadaObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "ArmadaObjectViewerWidget">;

  void SetCourseToArmada()
  { Call<"SetCourseToArmada">(); }
  void ValidateThenJoinArmada()
  { Call<"ValidateThenJoinArmada">(); }
  void JoinArmada()
  { Call<"JoinArmada">(); }
  bool IsAllowedToJoinArmada(FleetPlayerData* fleet)
  { return Call<"IsAllowedToJoinArmada", bool>(fleet); }

  GenericButtonContext* joinContext()
  { return Field<"_joinContext", GenericButtonContext*>(); }
};

struct CelestialObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "CelestialObjectViewerWidget">;
};

struct EmbassyObjectViewer : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "EmbassyObjectViewer">;
};

struct HousingObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "HousingObjectViewerWidget">;
};

// MiningObjectViewerWidget.OccupiedState, a private nested enum.
enum class OccupiedState : int32_t { NotOccupied = 0, LocalPlayerOccupied = 1, OtherPlayerOccupied = 2 };

struct MiningObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "MiningObjectViewerWidget">;

  void MineClicked()
  { Call<"MineClicked">(); }

  ScanEngageButtonsWidget* scanEngageButtonsWidget()
  { return Field<"_scanEngageButtonsWidget", ScanEngageButtonsWidget*>(); }
};

struct MissionsObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "MissionsObjectViewerWidget">;
};

struct WaveDefenseObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "WaveDefenseObjectViewerWidget">;

  // Every active viewer of this class in the loaded scenes.
  static il2cpp::Array<WaveDefenseObjectViewerWidget*>* FindActiveViewers()
  {
    auto* type = Class::TypeObject();
    return type ? UnityObject::Class::Call<"FindObjectsOfType", il2cpp::Array<WaveDefenseObjectViewerWidget*>*>(type)
                : nullptr;
  }

  GenericButtonWidget* primaryButton()
  { return Field<"_primaryButton", GenericButtonWidget*>(); }

  void PressPrimaryButton()
  {
    auto* widget = primaryButton();
    if (!widget || !widget->isActiveAndEnabled()) {
      return;
    }
    auto* context  = widget->Context();
    auto* listener = widget->SemaphoreListener();
    if (context && context->Interactable() && listener) {
      if (auto* button = listener->TheButton()) {
        button->Press();
      }
    }
  }
};

struct StarNodeObjectViewerWidget : ObjectViewerBaseWidget {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "StarNodeObjectViewerWidget">;

  void OnViewButtonActivation()
  { Call<"OnViewButtonActivation">(); }
  void InitiateWarp()
  { Call<"InitiateWarp">(); }
  GenericButtonWidget* visitButton()
  { return Field<"_visitButton", GenericButtonWidget*>(); }
};
