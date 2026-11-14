#pragma once

// The game's UI framework (Digit.Client.UI): widgets, view controllers, canvases, buttons, lists, localization,
// shortcuts, message boxes and toasts.

#include "app.h"

#include <il2cpp/il2cpp.h>
#include <unity/input.h>
#include <unity/ui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

// Digit.Client.UI.Widget`1. Members resolve on each widget's runtime class, so this cannot derive from
// unity::Behaviour, which would bind them to Behaviour's class.
template <typename T> struct Widget : il2cpp::Object {
  T* Context()
  { return Get<"Context", T*>(); }
  bool enabled()
  { return Get<"enabled", bool>(); }
  bool isActiveAndEnabled()
  { return unity::ActiveAndEnabled(this); }
  bool IsActive()
  { return Call<"IsActive", bool>(); }
};

enum class VisibilityState : int32_t {
  Unknown = 0,
  Show    = 1,
  Hide    = 2,
  Hidden  = 3,
  Visible = 4,
};

struct VisibilityController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "VisibilityController">;

  // bool selects Show(bool)/Hide(bool) over the int entry-point overloads.
  void Show(bool instant = false)
  { Call<"Show">(instant); }
  void Hide(bool instant = false)
  { Call<"Hide">(instant); }

  // The field is the state as last applied; the property is what the controller reports, which can differ while a
  // transition runs. The property is called VisibilityState, a name this header gives the enum.
  VisibilityState state()
  { return Field<"_state", VisibilityState>(); }
  VisibilityState State()
  { return Get<"VisibilityState", VisibilityState>(); }

  // Shown or on its way in.
  bool IsShown()
  { return std::ranges::contains(std::array{VisibilityState::Visible, VisibilityState::Show}, state()); }
};

struct CanvasController : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "CanvasController">;

  bool Visible()
  { return Get<"Visible", bool>(); }
  // The bound data context, of the class the canvas's view expects.
  il2cpp::Object* Context()
  { return Get<"Context", il2cpp::Object*>(); }
  Il2CppString* name()
  { return Get<"name", Il2CppString*>(); }

  // The canvas a component is drawn on. Passing System.Type selects GetComponentInParent(Type) over the generic
  // overload.
  static CanvasController* Of(il2cpp::Object* component)
  { return component ? component->Call<"GetComponentInParent", CanvasController*>(Class::TypeObject()) : nullptr; }

  // `component` is on a canvas that is currently visible.
  static bool Shows(il2cpp::Object* component)
  {
    auto* canvas = Of(component);
    return canvas && canvas->Visible();
  }
  // `view` is active and enabled, and its canvas is visible: a hidden screen can keep active views.
  static bool OnScreen(unity::Behaviour* view)
  { return unity::ActiveAndEnabled(view) && Shows(view); }
};

// Digit.Client.UI.TextLocalizer: the label component that renders localized, parameterized text.
struct TextLocalizer : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "TextLocalizer">;

  // Declared on GraphicLocalizer<,>.
  Il2CppString* identifier()
  { return Field<"m_identifier", Il2CppString*>(); }
  // The backing field, not the SignificantDecimals property.
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

  // These localizers render on every UI update, so the identifier is compared without converting it.
  bool IdentifierIs(std::string_view name)
  { return il2cpp::Equals(identifier(), name); }
};

struct ColourTextLocalizer : TextLocalizer {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "ColourTextLocalizer">;
};

// Identifier and category that the localizer turns into display text. Construct with
// il2cpp::New<LocaleTextContext>(identifier, category).
struct LocaleTextContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "LocaleTextContext">;

  // Fills the {N} slots of the identifier itself, e.g. "ship_name_{0}".
  void ApplyIdentifierParameters(il2cpp::Array<Il2CppObject*>* parameters)
  { Call<"ApplyIdentifierParameters">(parameters); }
};

// Two ways into the game's localization, each used where the game itself uses it: LocaleUtilities for texts with
// parameters, such as toasts, and Localizer for plain identifiers, such as ship names.
struct LocaleUtilities {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Localization", "LocaleUtilities">;

  static Il2CppString* Localize(LocaleTextContext* context, bool localizeTextParams = false,
                                bool invariantCulture = false)
  { return Class::Call<"Localize", Il2CppString*>(context, localizeTextParams, invariantCulture); }

  // `identifier` (such as "ship_name_{0}") in `category`, with its one slot filled by `parameter`. Empty when the
  // text cannot be built. An unknown identifier localizes to itself.
  static std::string Localize(std::string_view identifier, std::string_view category, Il2CppObject* parameter)
  {
    auto* parameters = parameter ? il2cpp::Array<Il2CppObject*>::New(il2cpp::ObjectClass(), 1) : nullptr;
    auto* context    = parameters
                           ? il2cpp::New<LocaleTextContext>(il2cpp::NewString(identifier), il2cpp::NewString(category))
                           : nullptr;
    if (!context)
      return {};
    parameters->Set(0, parameter);
    context->ApplyIdentifierParameters(parameters);
    return il2cpp::ToUtf8(Localize(context, false, false));
  }
};

struct Localizer {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Localization", "Localizer">;

  // textCase is TextLocalizer.TextCase; 0 leaves the text as translated.
  static Il2CppString* Localise(LocaleTextContext* context, bool forceCommas = false, int32_t textCase = 0)
  { return Class::Call<"Localise", Il2CppString*>(context, forceCommas, textCase); }
};

struct LanguageManager : MonoSingleton<LanguageManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Localization", "LanguageManager">;

  bool Localize(Il2CppString*& translation, LocaleTextContext* context)
  { return Call<"Localize", bool(Il2CppString*&, LocaleTextContext*)>(translation, context); }

  static void ClearCache()
  { Class::Call<"ClearCache">(); }
};

struct SemaphoreListenerBase : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "SemaphoreListenerBase">;
};

struct SemaphoreButtonListener : SemaphoreListenerBase {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "SemaphoreButtonListener">;

  unity::Button* button()
  { return Field<"_button", unity::Button*>(); }
  bool isActiveAndEnabled()
  { return unity::ActiveAndEnabled(this); }

  // Presses the listener's button, if it has one.
  void Press()
  {
    if (auto* target = button())
      target->Press();
  }
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

  SemaphoreButtonListener* semaphoreButtonListener()
  { return Field<"_semaphoreButtonListener", SemaphoreButtonListener*>(); }
  TextLocalizer* mainText()
  { return Field<"_mainText", TextLocalizer*>(); }
  bool Interactable()
  { return Get<"Interactable", bool>(); }

  // The Unity button behind the widget, or null.
  unity::Button* UnityButton()
  {
    auto* listener = semaphoreButtonListener();
    return listener ? listener->button() : nullptr;
  }
};

struct ButtonAndContextWrapper : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "ButtonAndContextWrapper">;

  GenericButtonWidget* Widget()
  { return Field<"Widget", GenericButtonWidget*>(); }
};

struct InputFieldWidget : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "InputFieldWidget">;

  void Focus()
  { Call<"Focus">(); }
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

  // Null when `index` is outside Data.
  il2cpp::Object* DataItem(int32_t index)
  { return il2cpp::At(Get<"Data", il2cpp::List<il2cpp::Object*>*>(), index); }

  // Regenerates the list's tiles from its current Data, through the game's SelectableListBase
  // .ClearAndGenerateContents; `provider` is the IDataContextProvider the tiles bind to.
  void Rebuild(il2cpp::Object* provider)
  {
    if (auto* data = Get<"Data", il2cpp::Object*>())
      Call<"ClearAndGenerateContents">(provider, data);
  }
};

struct TabBar : SelectableList {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "TabBar">;
};

struct TabBarViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "TabBarViewController">;

  TabBar* tabBar()
  { return Field<"_tabBar", TabBar*>(); }
};

struct UIFrameManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "UIFrameManager">;

  // The game clamps the width to the available space and the side frame's minimum.
  void ResizeSideFrame(float width)
  { Call<"ResizeSideFrame", void(float)>(width); }
};

struct ScreenManager : MonoSingleton<ScreenManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "ScreenManager">;

  // Every registered canvas, visible or not.
  il2cpp::List<CanvasController*>* screens()
  { return Field<"_screens", il2cpp::List<CanvasController*>*>(); }
  unity::CanvasScaler* canvasRootScaler()
  { return Field<"m_canvasRootScaler", unity::CanvasScaler*>(); }
  UIFrameManager* frameManager()
  { return Field<"_frameManager", UIFrameManager*>(); }

  // The first live T on a visible registered canvas whose data context is a T::Context. Only the canvas that owns
  // that context is searched: the top canvas and the scene root can belong to unrelated overlays.
  template <typename T> static T* View()
  {
    struct Context {
      using Class = typename T::Context;
    };
    auto* manager = Existing();
    for (auto* canvas : il2cpp::Items(unity::Alive(manager) ? manager->screens() : nullptr)) {
      auto* context = unity::Alive(canvas) ? canvas->Context() : nullptr;
      if (!context || !context->Is<Context>() || !canvas->Visible())
        continue;
      auto* object = canvas->gameObject();
      if (auto* view = object ? object->template GetComponentInChildren<T>() : nullptr; unity::Alive(view))
        return view;
    }
    return nullptr;
  }
};

struct ShortcutsManager : MonoSingleton<ShortcutsManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.GameInput", "ShortcutsManager">;

  bool ShowKeybindings()
  { return Get<"ShowKeybindings", bool>(); }
  void SetShowKeybindings(bool value)
  { Set<"ShowKeybindings">(value); }

  // Runs one of the On*Action(InputAction.CallbackContext) handlers, as the game's own shortcut for it would.
  template <il2cpp::Name Action> void Run()
  { Call<Action>(unity::InputActionCallbackContext{}); }

  // The private static gate the game's own shortcut actions check (tutorial, modals, text input).
  static bool CanUseShortcuts()
  { return Class::Get<"CanUseShortcuts", bool>(); }
};

// The badge next to a button that shows the key of one of the game's input actions.
struct ShortcutKeybindHint : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.GameInput", "ShortcutKeybindHint">;

  unity::InputActionReference* inputAction()
  { return Field<"_inputAction", unity::InputActionReference*>(); }
  TextLocalizer* keyTextLocalizer()
  { return Field<"_keyTextLocalizer", TextLocalizer*>(); }
  void UpdateText()
  { Call<"UpdateText">(); }
};

// Digit.Client.UI.MessageBox. Only hooked.
using GameMessageBox = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "MessageBox">;

// MessageBox.ButtonResult
enum class MessageBoxResult : int32_t {
  Confirm = 1,
  Yes     = 3,
};

struct MessageBoxContext : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "MessageBoxContext">;

  // Action<MessageBox.ButtonResult>
  Il2CppDelegate* OnSelection()
  { return Field<"OnSelection", Il2CppDelegate*>(); }
};

// Digit.Prime.HUD.ToastState: what a toast banner reports.
enum class ToastState : int32_t {
  Standard                          = 0,
  FactionWarning                    = 1,
  FactionLevelUp                    = 2,
  FactionLevelDown                  = 3,
  FactionDiscovered                 = 4,
  IncomingAttack                    = 5,
  IncomingAttackFaction             = 6,
  FleetBattle                       = 7,
  StationBattle                     = 8,
  StationVictory                    = 9,
  Victory                           = 10,
  Defeat                            = 11,
  StationDefeat                     = 12,
  Tournament                        = 14,
  ArmadaCreated                     = 15,
  ArmadaCanceled                    = 16,
  ArmadaIncomingAttack              = 17,
  ArmadaBattleWon                   = 18,
  ArmadaBattleLost                  = 19,
  DiplomacyUpdated                  = 20,
  JoinedTakeover                    = 21,
  CompetitorJoinedTakeover          = 22,
  AbandonedTerritory                = 23,
  TakeoverVictory                   = 24,
  TakeoverDefeat                    = 25,
  TreasuryProgress                  = 26,
  TreasuryFull                      = 27,
  Achievement                       = 28,
  AssaultVictory                    = 29,
  AssaultDefeat                     = 30,
  ChallengeComplete                 = 31,
  ChallengeFailed                   = 32,
  StrikeHit                         = 33,
  StrikeDefeat                      = 34,
  WarchestProgress                  = 35,
  WarchestFull                      = 36,
  PartialVictory                    = 37,
  ArenaTimeLeft                     = 38,
  ChainedEventScored                = 39,
  FleetPresetApplied                = 40,
  SurgeWarmUpEnded                  = 41,
  SurgeHostileGroupDefeated         = 42,
  SurgeTimeLeft                     = 43,
  QueueForLeaseActivated            = 44,
  QueueForLeaseExpired              = 45,
  PermanentQueuePurchased           = 46,
  OutpostStartedOrEnded             = 47,
  CrossAllianceArmadaVictory        = 48,
  CrossAllianceArmadaDefeat         = 49,
  CrossAllianceArmadaPartialVictory = 50,
  FactionWeeklyEventsProgress       = 51,
  FactionWeeklyEventsComplete       = 52,
  ArmadaPlayerBlocked               = 53,
  ArmadaPlayerUnblocked             = 54,
  DynamicCrisisUpdate               = 55,
  DynamicCrisisFailed               = 56,
  DynamicCrisisCompleted            = 57,
  GalacticAnomalySystemEntered      = 58,
};

struct Toast : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "Toast">;

  ToastState State()
  { return Get<"State", ToastState>(); }
  il2cpp::Array<Il2CppObject*>* TextParameters()
  { return Get<"TextParameters", il2cpp::Array<Il2CppObject*>*>(); }
  LocaleTextContext* TextLocaleTextContext()
  { return static_cast<LocaleTextContext*>(Get<"TextLocaleTextContext", Il2CppObject*>()); }
  // A different type per toast state: BattleResultHeader, EventModel, ArmadaCreatedData, ...
  il2cpp::Object* Data()
  { return Get<"Data", il2cpp::Object*>(); }
};

// Queues toast banners for display.
struct ToastObserver : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "ToastObserver">;
};

struct AnimatedRewardsScreenViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Missions.UI", "AnimatedRewardsScreenViewController">;

  bool IsActive()
  { return Call<"IsActive", bool>(); }
  void GoBackToLastSection()
  { Call<"GoBackToLastSection">(); }
};

// The -/+ stepper on upgrade and quantity screens.
struct ElementSelectorViewController : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.UI", "ElementSelectorViewController">;

  void PressDecrement()
  {
    if (auto* button = Field<"_decrementButton", unity::Button*>())
      button->Press();
  }
  void PressIncrement()
  {
    if (auto* button = Field<"_incrementButton", unity::Button*>())
      button->Press();
  }
};

struct ArtifactHallDetailsViewController : unity::Behaviour {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.ArtifactHall", "ArtifactHallDetailsViewController">;

  void PressLeftArrow()
  {
    if (auto* listener = Field<"_leftArrowButton", SemaphoreButtonListener*>())
      listener->Press();
  }
  void PressRightArrow()
  {
    if (auto* listener = Field<"_rightArrowButton", SemaphoreButtonListener*>())
      listener->Press();
  }
};

// The cargo bar's "X of Y" label. Its fields are declared on LegacyProgressBar and Widget`1; lookups walk the
// parents.
struct LegacyComparableProgressBar : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Stats", "LegacyComparableProgressBar">;

  TextLocalizer* currentValue_X_of_Y()
  { return Field<"_currentValue_X_of_Y", TextLocalizer*>(); }
  // IProgressData
  il2cpp::Object* context()
  { return Field<"m_context", il2cpp::Object*>(); }
  bool snapToValue()
  { return Field<"_snapToValue", bool>(); }
  void SetSnapToValue(bool value)
  { SetField<"_snapToValue">(value); }
};

// Digit.Client.UI.MessageUtility: static helpers that open the game's message popups. Only hooked.
using MessageUtility = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "MessageUtility">;
