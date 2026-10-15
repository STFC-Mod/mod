#pragma once

// Game UI framework: widgets, view controllers, canvases, buttons, localization, toasts.

#include "unity.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

// Unity singletons expose a static Instance on their MonoSingleton`1<T> base.
template <typename T> struct MonoSingleton : il2cpp::Object {
  static T* Instance()
  { return T::Class::template Get<"Instance", T*>(); }
  // The singleton s_instance already holds, or null. Instance can search the scene or create a replacement.
  static T* Existing()
  { return T::Class::template Field<"s_instance", T*>(); }
};

// Digit.Client.UI.Widget`1. Members resolve on each widget's runtime class.
template <typename T> struct Widget : il2cpp::Object {
  T* Context()
  { return Get<"Context", T*>(); }
  bool enabled()
  { return Get<"enabled", bool>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
  bool IsActive()
  { return Call<"IsActive", bool>(); }
};

// Digit.Client.UI.ViewController`1. Members resolve on each controller's runtime class.
template <typename T> struct ViewController : il2cpp::Object {
  T* CanvasContext()
  { return Get<"CanvasContext", T*>(); }
};

enum class VisibilityState : int32_t // TypeDefIndex: 13743
{
  Unknown = 0,
  Show    = 1,
  Hide    = 2,
  Hidden  = 3,
  Visible = 4
};

struct VisibilityController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "VisibilityController">;

  // bool selects Show(bool)/Hide(bool) over the int entry-point overloads.
  void Show(bool instant = false)
  { Call<"Show">(instant); }
  void Hide(bool instant = false)
  { Call<"Hide">(instant); }

  VisibilityState state()
  { return Field<"_state", VisibilityState>(); }
  VisibilityState State()
  { return Get<"VisibilityState", VisibilityState>(); }

  // Shown or on its way in.
  bool IsShown()
  {
    const auto current = state();
    return current == VisibilityState::Visible || current == VisibilityState::Show;
  }
};

struct CanvasController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "CanvasController">;

  bool ResetFocus()
  { return Call<"ResetFocus", bool>(); }
  bool Visible()
  { return Get<"Visible", bool>(); }
  // The bound data context, of the class the canvas's view expects.
  il2cpp::Object* Context()
  { return Get<"Context", il2cpp::Object*>(); }
  bool get_enabled()
  { return Get<"enabled", bool>(); }
  bool m_Visible()
  { return Field<"m_visible", bool>(); }

  Il2CppString* name()
  { return Get<"name", Il2CppString*>(); }
  Transform* transform()
  { return Get<"transform", Transform*>(); }
  Canvas* canvas()
  { return Field<"m_canvas", Canvas*>(); }
};

inline CanvasController* GetCanvasControllerFromComponent(il2cpp::Object* component)
{
  if (!component)
    return nullptr;
  // Passing System.Type selects GetComponentInParent(Type) over the generic overload.
  return component->Call<"GetComponentInParent", CanvasController*>(CanvasController::Class::TypeObject());
}

// Digit.Client.UI.TextLocalizer: the label component that renders localized, parameterized text.
struct TextLocalizer : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "TextLocalizer">;

  // Declared on GraphicLocalizer<,>.
  Il2CppString* identifier()
  { return Field<"m_identifier", Il2CppString*>(); }
  // The backing field, not the SignificantDecimals property.
  int32_t significantDecimals()
  { return Field<"m_significantDecimals", int32_t>(); }
  void SetSignificantDecimals(int32_t value)
  { SetField<"m_significantDecimals">(value); }
  bool textOverride()
  { return Field<"_textOverride", bool>(); }
  // Declared on GraphicLocalizer<,>: the rendered graphic, resolved lazily even on inactive labels.
  il2cpp::Object* Target()
  { return Get<"Target", il2cpp::Object*>(); }

  Il2CppString* CurrentText()
  { return Call<"CurrentText", Il2CppString*>(); }
  void ClearTextOverride()
  { Call<"ClearTextOverride">(); }
  void OverrideLocalizedText(Il2CppString* text)
  { Call<"OverrideLocalizedText">(text); }
};

struct ColourTextLocalizer : TextLocalizer {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "ColourTextLocalizer">;
};

// Identifier + category pair that the localizer turns into display text.
// Construct with il2cpp::New<LocaleTextContext>(identifier, category).
struct LocaleTextContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "LocaleTextContext">;

  // Fills the {N} slots of the identifier itself, e.g. "ship_name_{0}".
  void ApplyIdentifierParameters(il2cpp::Array<Il2CppObject*>* parameters)
  { Call<"ApplyIdentifierParameters">(parameters); }
};

struct LocaleUtilities {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Localization", "LocaleUtilities">;

  static Il2CppString* Localize(LocaleTextContext* context, bool localizeTextParams = false,
                                bool invariantCulture = false)
  { return Class::Call<"Localize", Il2CppString*>(context, localizeTextParams, invariantCulture); }
};

struct Localizer {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Localization", "Localizer">;

  // textCase is TextLocalizer.TextCase; 0 leaves the text as translated.
  static Il2CppString* Localise(LocaleTextContext* context, bool forceCommas = false, int32_t textCase = 0)
  { return Class::Call<"Localise", Il2CppString*>(context, forceCommas, textCase); }
};

struct LanguageManager : MonoSingleton<LanguageManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Localization", "LanguageManager">;

  bool TryGetTranslation(Il2CppString* category, int32_t key, Il2CppString** translatedText)
  {
    if (!translatedText)
      return false;
    return Call<"TryGetTranslation", bool(Il2CppString*, int32_t, Il2CppString*&)>(category, key, *translatedText);
  }

  bool Localize(Il2CppString*& translation, LocaleTextContext* context)
  { return Call<"Localize", bool(Il2CppString*&, LocaleTextContext*)>(translation, context); }

  static void ClearCache()
  { Class::Call<"ClearCache">(); }
};

struct Button : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.UI", "UnityEngine.UI", "Button">;

  void Press()
  { Call<"Press">(); }
};

struct SemaphoreListenerBase : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "SemaphoreListenerBase">;
};

struct SemaphoreButtonListener : SemaphoreListenerBase {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "SemaphoreButtonListener">;

  Button* TheButton()
  { return Field<"_button", Button*>(); }
  bool Interactable()
  { return Get<"Interactable", bool>(); }
  void SetInteractable(bool value)
  { Set<"Interactable">(value); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
};

struct GenericButtonContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "GenericButtonContext">;

  bool Interactable()
  { return Get<"Interactable", bool>(); }
  void SetInteractable(bool value)
  { Set<"Interactable">(value); }
};

struct GenericButtonWidget : Widget<GenericButtonContext> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "GenericButtonWidget">;

  SemaphoreButtonListener* SemaphoreListener()
  { return Field<"_semaphoreButtonListener", SemaphoreButtonListener*>(); }
  TextLocalizer* mainText()
  { return Field<"_mainText", TextLocalizer*>(); }
  bool Interactable()
  { return Get<"Interactable", bool>(); }
};

struct ButtonAndContextWrapper : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "ButtonAndContextWrapper">;

  GenericButtonWidget* Widget()
  { return Field<"Widget", GenericButtonWidget*>(); }
};

struct InputFieldWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "InputFieldWidget">;

  void Focus()
  { Call<"Focus">(); }

  TMP_InputField* input()
  { return Field<"_input", TMP_InputField*>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
};

// Digit.Client.UI list hierarchy: BaseListContainer <- SelectableListBase <- SelectableList <- TabBar.
// Data is a System.Collections.IList; elements are whatever the list was bound to.
struct BaseListContainer : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "BaseListContainer">;

  il2cpp::List<il2cpp::Object*>* data()
  { return Field<"_data", il2cpp::List<il2cpp::Object*>*>(); }
  int32_t Count()
  { return Get<"Count", int32_t>(); }
};

struct SelectableList : BaseListContainer {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "SelectableList">;

  int32_t SelectedIndex()
  { return Get<"SelectedIndex", int32_t>(); }

  // Declared on SelectableListBase. The int overload is picked over RequestSelect(Component, bool).
  void RequestSelect(int32_t index, bool simulated = false)
  { Call<"RequestSelect">(index, simulated); }

  il2cpp::Object* DataItem(int32_t index)
  {
    auto* list = Get<"Data", il2cpp::List<il2cpp::Object*>*>();
    return list ? (*list)[index] : nullptr;
  }
};

struct TabBar : SelectableList {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "TabBar">;

  // GenericButtonContext[]
  il2cpp::Array<il2cpp::Object*>* defaultOptions()
  { return Field<"_defaultOptions", il2cpp::Array<il2cpp::Object*>*>(); }
};

struct TabBarViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "TabBarViewController">;

  TabBar* tabBar()
  { return Field<"_tabBar", TabBar*>(); }
};

struct ShortcutsManager : MonoSingleton<ShortcutsManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.GameInput", "ShortcutsManager">;

  bool ShowKeybindings()
  { return Get<"ShowKeybindings", bool>(); }
  void SetShowKeybindings(bool value)
  { Set<"ShowKeybindings">(value); }
  // Private static gate the native shortcut actions check (tutorial, modals, text input).
  static bool CanUseShortcuts()
  { return Class::Get<"CanUseShortcuts", bool>(); }
};

struct UIFrameManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "UIFrameManager">;

  // The game clamps the width to the available space and the side frame's minimum. False when the call failed.
  bool ResizeSideFrame(float width)
  { return TryCall<"ResizeSideFrame", void(float)>(width); }
};

struct ScreenManager : MonoSingleton<ScreenManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "ScreenManager">;

  static CanvasController* GetTopCanvas(bool visibleOnly = false)
  { return Class::Call<"GetTopCanvas", CanvasController*>(visibleOnly); }

  // Every registered canvas, visible or not.
  il2cpp::List<CanvasController*>* screens()
  { return Field<"_screens", il2cpp::List<CanvasController*>*>(); }
  CanvasScaler* canvasRootScaler()
  { return Field<"m_canvasRootScaler", CanvasScaler*>(); }
  float minimumCanvasScaleFactor()
  { return Field<"_minimumCanvasScaleFactor", float>(); }
  float maximumCanvasScaleFactor()
  { return Field<"_maximumCanvasScaleFactor", float>(); }
  UIFrameManager* frameManager()
  { return Field<"_frameManager", UIFrameManager*>(); }
};

enum ToastState {
  All                       = -1,
  Standard                  = 0,
  FactionWarning            = 1,
  FactionLevelUp            = 2,
  FactionLevelDown          = 3,
  FactionDiscovered         = 4,
  IncomingAttack            = 5,
  IncomingAttackFaction     = 6,
  FleetBattle               = 7,
  StationBattle             = 8,
  StationVictory            = 9,
  Victory                   = 10,
  Defeat                    = 11,
  StationDefeat             = 12,
  Tournament                = 14,
  ArmadaCreated             = 15,
  ArmadaCanceled            = 16,
  ArmadaIncomingAttack      = 17,
  ArmadaBattleWon           = 18,
  ArmadaBattleLost          = 19,
  DiplomacyUpdated          = 20,
  JoinedTakeover            = 21,
  CompetitorJoinedTakeover  = 22,
  AbandonedTerritory        = 23,
  TakeoverVictory           = 24,
  TakeoverDefeat            = 25,
  TreasuryProgress          = 26,
  TreasuryFull              = 27,
  Achievement               = 28,
  AssaultVictory            = 29,
  AssaultDefeat             = 30,
  ChallengeComplete         = 31,
  ChallengeFailed           = 32,
  StrikeHit                 = 33,
  StrikeDefeat              = 34,
  WarchestProgress          = 35,
  WarchestFull              = 36,
  PartialVictory            = 37,
  ArenaTimeLeft             = 38,
  ChainedEventScored        = 39,
  FleetPresetApplied        = 40,
  SurgeWarmUpEnded              = 41,
  SurgeHostileGroupDefeated     = 42,
  SurgeTimeLeft                 = 43,
  QueueForLeaseActivated        = 44,
  QueueForLeaseExpired          = 45,
  PermanentQueuePurchased       = 46,
  OutpostStartedOrEnded         = 47,
  CrossAllianceArmadaVictory    = 48,
  CrossAllianceArmadaDefeat     = 49,
  CrossAllianceArmadaPartialVictory = 50,
  FactionWeeklyEventsProgress   = 51,
  FactionWeeklyEventsComplete   = 52,
  ArmadaPlayerBlocked           = 53,
  ArmadaPlayerUnblocked         = 54,
  DynamicCrisisUpdate           = 55,
  DynamicCrisisFailed           = 56,
  DynamicCrisisCompleted        = 57,
  GalacticAnomalySystemEntered  = 58,
};

struct Toast : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "Toast">;

  il2cpp::Array<Il2CppObject*>* get_TextParameters()
  { return Get<"TextParameters", il2cpp::Array<Il2CppObject*>*>(); }
  // LocaleTextContext
  void* get_TextLocaleTextContext()
  { return Get<"TextLocaleTextContext", void*>(); }
  Il2CppObject* get_Data()
  { return Get<"Data", Il2CppObject*>(); }
  int get_State()
  { return Get<"State", ToastState>(); }
};

struct InterstitialViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Interstitial", "InterstitialViewController">;

  void CloseWhenReady()
  { Call<"CloseWhenReady">(); }
};

struct AnimatedRewardsScreenViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Missions.UI", "AnimatedRewardsScreenViewController">;

  void GoBackToLastSection()
  { Call<"GoBackToLastSection">(); }
  void OnCollectClicked()
  { Call<"OnCollectClicked">(); }
  bool IsActive()
  { return Call<"IsActive", bool>(); }
};

struct ElementSelectorViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.UI", "ElementSelectorViewController">;

  Button* decrementButton()
  { return Field<"_decrementButton", Button*>(); }
  Button* incrementButton()
  { return Field<"_incrementButton", Button*>(); }

  // Unity's internal call, so a missing binding never hides the selector hotkeys.
  bool isActiveAndEnabled()
  {
    static auto get_isActiveAndEnabled =
        il2cpp::ResolveICall<bool(ElementSelectorViewController*)>("UnityEngine.Behaviour::get_isActiveAndEnabled()");
    if (get_isActiveAndEnabled) {
      return get_isActiveAndEnabled(this);
    }
    return true;
  }

  void Decrement()
  { Call<"Decrement">(); }
  void Increment()
  { Call<"Increment">(); }

  void PressDecrement()
  {
    if (auto btn = decrementButton()) {
      btn->Press();
    }
  }

  void PressIncrement()
  {
    if (auto btn = incrementButton()) {
      btn->Press();
    }
  }
};

struct ArtifactHallDetailsViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ArtifactHall", "ArtifactHallDetailsViewController">;

  // Treat the controller as active if the icall is unavailable, so the arrow hotkeys still try.
  bool isActiveAndEnabled()
  {
    static auto get_isActiveAndEnabled = il2cpp::ResolveICall<bool(ArtifactHallDetailsViewController*)>(
        "UnityEngine.Behaviour::get_isActiveAndEnabled()");
    return get_isActiveAndEnabled ? get_isActiveAndEnabled(this) : true;
  }

  void PressLeftArrow()
  { Press(leftArrowButton()); }
  void PressRightArrow()
  { Press(rightArrowButton()); }

  SemaphoreButtonListener* leftArrowButton()
  { return Field<"_leftArrowButton", SemaphoreButtonListener*>(); }
  SemaphoreButtonListener* rightArrowButton()
  { return Field<"_rightArrowButton", SemaphoreButtonListener*>(); }

private:
  static void Press(SemaphoreButtonListener* listener)
  {
    if (auto* button = listener ? listener->TheButton() : nullptr)
      button->Press();
  }
};
