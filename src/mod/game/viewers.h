#pragma once

// Object viewer cards shown for targets in space, and their scan/engage buttons.

#include "fleet.h"
#include "navigation.h"
#include "ui.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct ParentObjectViewerViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "ParentObjectViewerViewController">;
  bool IsShowing()
  { return Get<"IsShowing", bool>(); }
};

struct ObjectViewerBaseWidget : Widget<NavigationInteractionUIContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ObjectViewer", "ObjectViewerBaseWidget">;

  ParentObjectViewerViewController* Parent()
  { return Get<"Parent", ParentObjectViewerViewController*>(); }
  void HideAllViewers()
  { Call<"HideAllViewers">(); }

  VisibilityController* visibilityController()
  { return Field<"_visibilityController", VisibilityController*>(); }
};

struct RewardsButtonWidget : Widget<BattleTargetData> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Combat", "RewardsButtonWidget">;

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

  void ValidateThenJoinArmada()
  { Call<"ValidateThenJoinArmada">(); }

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

  GenericButtonWidget* primaryButton()
  { return Field<"_primaryButton", GenericButtonWidget*>(); }

  void PressPrimaryButton()
  {
    auto* widget = primaryButton();
    if (!widget || !widget->isActiveAndEnabled()) {
      return;
    }
    auto* context  = widget->Context();
    auto* listener = widget->semaphoreButtonListener();
    if (context && context->Interactable() && listener) {
      listener->Press();
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
