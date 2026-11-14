#include "config.h"
#include "str_utils.h"

#include <il2cpp/il2cpp.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <string>

// Restores the "Below Deck Ability" (BDA) sort option removed from the game UI
// in m93.1. Hooks OfficerSortGenerators.InitializeOfficerSorters /
// InitializeAssignmentSorters and appends a BDA ProcessingOption after the game
// populates its own sort lists.

namespace {

constexpr char kBelowDeckKey[]           = "below_deck_ability";
constexpr char kBelowDeckAssignmentKey[] = "_below_deck_ability";

using SortingPredicates = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Core", "SortingPredicates">;

struct SortFunction : il2cpp::Object {
  using Class = il2cpp::Class<"Digit.Client.PrimeLib.Runtime", "Digit.Client.Core", "SortFunction">;

  // Func<object, object, int> delegates.
  il2cpp::Object* ascending()
  { return Field<"_ascending", il2cpp::Object*>(); }
  void SetAscending(Il2CppObject* value)
  { SetField<"_ascending">(value); }
  void SetDescending(Il2CppObject* value)
  { SetField<"_descending">(value); }
  void SetDisplayKey(Il2CppString* value)
  { SetField<"_displayKey">(value); }
};

struct ProcessingOption : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.Sorting", "SortComparer/ProcessingOption">;

  Il2CppString* displayKey()
  { return Field<"_displayKey", Il2CppString*>(); }
  void SetDisplayKey(Il2CppString* value)
  { SetField<"_displayKey">(value); }
  SortFunction* sortingOption()
  { return Field<"_sortingOption", SortFunction*>(); }
  void SetSortingOption(SortFunction* value)
  { SetField<"_sortingOption">(value); }
};

struct OfficerSortGenerators : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Officers", "OfficerSortGenerators">;

  il2cpp::List<SortFunction*>* rosterSorters()
  { return Field<"_rosterSorters", il2cpp::List<SortFunction*>*>(); }
  il2cpp::List<ProcessingOption*>* rosterOptions()
  { return Field<"_rosterOptions", il2cpp::List<ProcessingOption*>*>(); }
  il2cpp::List<ProcessingOption*>* assignmentOptions()
  { return Field<"_assignmentOptions", il2cpp::List<ProcessingOption*>*>(); }
};

// Native code of the static SortingPredicates comparers. The delegates below are bound to the BDA
// predicates but jump to the wrappers, which chain these directly.
using SortMethodPtr = int (*)(void* obj1, void* obj2, void* delegatePtr);

il2cpp::Method g_bdaAscMethod;
il2cpp::Method g_bdaDescMethod;
SortMethodPtr  g_unlockedStatePtr = nullptr;
SortMethodPtr  g_bdaAscPtr        = nullptr;
SortMethodPtr  g_bdaDescPtr       = nullptr;
SortMethodPtr  g_indexAscPtr      = nullptr;
// Func<object, object, int>, discovered from the game's own options; there is no binding for the
// inflated generic class.
Il2CppClass*   g_funcClass        = nullptr;

SortMethodPtr PointerOf(il2cpp::Method method)
{ return method ? reinterpret_cast<SortMethodPtr>(method.Info()->methodPointer) : nullptr; }

// SortComparer.Compare always calls the _ascending delegate; for Descending
// direction it negates the result. The _descending delegate is never called.
// All logic lives in the *Ascending wrappers so that:
//   direct call  = ascending behavior
//   negated call = descending behavior

int RosterAscendingWrapper(void* obj1, void* obj2, void* /*delegatePtr*/)
{
  if (g_unlockedStatePtr) {
    int r = g_unlockedStatePtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  if (g_bdaDescPtr) {
    int r = g_bdaDescPtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  return g_indexAscPtr ? -g_indexAscPtr(obj1, obj2, nullptr) : 0;
}

int RosterDescendingWrapper(void* obj1, void* obj2, void* /*delegatePtr*/)
{
  if (g_unlockedStatePtr) {
    int r = g_unlockedStatePtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  if (g_bdaAscPtr) {
    int r = g_bdaAscPtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  return g_indexAscPtr ? -g_indexAscPtr(obj1, obj2, nullptr) : 0;
}

int AssignmentAscendingWrapper(void* obj1, void* obj2, void* /*delegatePtr*/)
{
  if (g_unlockedStatePtr) {
    int r = g_unlockedStatePtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  if (g_bdaAscPtr) {
    int r = g_bdaAscPtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  return g_indexAscPtr ? g_indexAscPtr(obj1, obj2, nullptr) : 0;
}

int AssignmentDescendingWrapper(void* obj1, void* obj2, void* /*delegatePtr*/)
{
  if (g_unlockedStatePtr) {
    int r = g_unlockedStatePtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  if (g_bdaDescPtr) {
    int r = g_bdaDescPtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  return g_indexAscPtr ? g_indexAscPtr(obj1, obj2, nullptr) : 0;
}

bool InitializePredicates()
{
  g_bdaAscMethod  = SortingPredicates::Method("OfficerSortByBelowDeckAbilityAscending", 2);
  g_bdaDescMethod = SortingPredicates::Method("OfficerSortByBelowDeckAbilityDescending", 2);
  if (!g_bdaAscMethod || !g_bdaDescMethod)
    return false;
  g_bdaAscPtr  = PointerOf(g_bdaAscMethod);
  g_bdaDescPtr = PointerOf(g_bdaDescMethod);

  // The tiebreakers are optional; the library has already reported a missing one.
  g_unlockedStatePtr = PointerOf(SortingPredicates::Method("OfficerSortByUnlockedStateAscending", 2));
  g_indexAscPtr      = PointerOf(SortingPredicates::Method("OfficerSortByIndexAscending", 2));

  // Options are allocated without a constructor below, so both classes must exist.
  return SortFunction::Class::Raw() && ProcessingOption::Class::Raw();
}

Il2CppClass* DiscoverFuncClassFromList(il2cpp::List<ProcessingOption*>* list)
{
  if (list->Count() < 1)
    return nullptr;
  auto* firstOption = (*list)[0];
  auto* sortFn      = firstOption ? firstOption->sortingOption() : nullptr;
  auto* ascending   = sortFn ? sortFn->ascending() : nullptr;
  return ascending ? ascending->klass : nullptr;
}

// forRoster=true:  roster defaults to Ascending, so _ascending produces BDA-first.
// forRoster=false: assignment defaults to Descending, so _ascending produces
//                  non-BDA-first (SortComparer negates it, yielding BDA-first).
SortFunction* CreateBelowDeckSortFunction(const char* displayKey, bool forRoster)
{
  auto* ascending = il2cpp::NewDelegate(
      g_funcClass, g_bdaAscMethod,
      reinterpret_cast<Il2CppMethodPointer>(forRoster ? &RosterAscendingWrapper : &AssignmentAscendingWrapper));
  auto* descending = il2cpp::NewDelegate(
      g_funcClass, g_bdaDescMethod,
      reinterpret_cast<Il2CppMethodPointer>(forRoster ? &RosterDescendingWrapper : &AssignmentDescendingWrapper));
  if (!ascending || !descending)
    return nullptr;

  // SortFunction's parameterless constructor has no generated code; set every field Compare reads.
  auto* sort = static_cast<SortFunction*>(il2cpp_object_new(SortFunction::Class::Raw()));
  sort->SetDisplayKey(il2cpp_string_new(displayKey));
  sort->SetAscending(ascending);
  sort->SetDescending(descending);
  return sort;
}

bool ListAlreadyHasBelowDeck(il2cpp::List<ProcessingOption*>* list)
{
  for (auto* option : *list) {
    auto* key = option ? option->displayKey() : nullptr;
    if (!key)
      continue;
    const auto text = to_string(key);
    if (text == kBelowDeckKey || text == kBelowDeckAssignmentKey)
      return true;
  }
  return false;
}

// If sortersList is non-null, also adds the raw SortFunction to it (the roster
// screen builds its SortComparer from _rosterSorters, not _rosterOptions).
void AppendBelowDeckOption(il2cpp::List<ProcessingOption*>* list, const char* displayKey, bool forRoster,
                           il2cpp::List<SortFunction*>* sortersList = nullptr)
{
  if (!list) {
    spdlog::warn("[OfficerSort] AppendBelowDeckOption: list is null");
    return;
  }

  if (!g_funcClass) {
    g_funcClass = DiscoverFuncClassFromList(list);
    if (!g_funcClass) {
      spdlog::warn("[OfficerSort] could not discover Func<object,object,int> class; skipping below-deck option");
      return;
    }
  }

  if (ListAlreadyHasBelowDeck(list)) return;

  auto* sortFn = CreateBelowDeckSortFunction(displayKey, forRoster);
  if (!sortFn) {
    spdlog::warn("[OfficerSort] failed to create sort function");
    return;
  }

  if (sortersList)
    sortersList->Add(sortFn);

  auto* option = static_cast<ProcessingOption*>(il2cpp_object_new(ProcessingOption::Class::Raw()));
  option->SetDisplayKey(il2cpp_string_new(displayKey));
  option->SetSortingOption(sortFn);
  list->Add(option);
}

void InitializeOfficerSorters_Hook(auto original, OfficerSortGenerators* _this)
{
  original(_this);
  if (!Config::Get().officer_sort) return;
  AppendBelowDeckOption(_this->rosterOptions(), kBelowDeckKey, true, _this->rosterSorters());
}

void InitializeAssignmentSorters_Hook(auto original, OfficerSortGenerators* _this)
{
  original(_this);
  if (!Config::Get().officer_sort) return;
  AppendBelowDeckOption(_this->assignmentOptions(), kBelowDeckAssignmentKey, false);
}

} // namespace

void InstallOfficerSortHooks()
{
  if (!InitializePredicates())
    return;

  auto roster     = OfficerSortGenerators::Class::Method("InitializeOfficerSorters", 0);
  auto assignment = OfficerSortGenerators::Class::Method("InitializeAssignmentSorters", 0);
  if (!roster || !assignment)
    return;

  SPUD_STATIC_DETOUR(roster.Pointer(), InitializeOfficerSorters_Hook);
  SPUD_STATIC_DETOUR(assignment.Pointer(), InitializeAssignmentSorters_Hook);
  spdlog::info("Officer sort: restored Below Deck Ability sort option");
}
