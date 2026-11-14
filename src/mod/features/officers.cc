// [ui] officer_sort: brings back the "Below Deck Ability" sort option that client m93.1 removed from the officer
// roster and the officer assignment screen, by appending it to the sort menus after the game builds them.

#include "config.h"
#include "features/features.h"

#include "game/officers.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <string>

namespace
{
constexpr char kRosterKey[]     = "below_deck_ability";
constexpr char kAssignmentKey[] = "_below_deck_ability";

// --- Comparers ---

// The native code of SortingPredicates' static comparers. The game's own delegates call them through the managed
// signature; the ones the mod adds jump straight to CompareOfficers below, which chains them.
using OfficerComparer = int (*)(void* a, void* b, void* method);

il2cpp::Method  below_deck_ascending_method, below_deck_descending_method;
OfficerComparer below_deck_ascending = nullptr, below_deck_descending = nullptr;
// Tiebreakers; either may be missing, which the binding layer has already reported.
OfficerComparer unlocked_first = nullptr, by_index = nullptr;
// Func<object, object, int>, taken from the game's own options: there is no binding for the inflated generic class.
Il2CppClass* officer_func_class = nullptr;

OfficerComparer NativeComparer(il2cpp::Method method)
{ return reinterpret_cast<OfficerComparer>(method.Pointer()); }

// SortComparer.Compare always calls the _ascending delegate and negates it for Descending; _descending is never
// called. So each delegate is a complete comparator: unlocked officers first, then below-deck ability in the given
// direction, then roster index (reversed for the roster, whose default direction is Ascending).
template <OfficerComparer* BelowDeck, int kIndexSign> int CompareOfficers(void* a, void* b, void* /*method*/)
{
  if (unlocked_first)
    if (const int order = unlocked_first(a, b, nullptr))
      return order;
  if (*BelowDeck)
    if (const int order = (*BelowDeck)(a, b, nullptr))
      return order;
  return by_index ? kIndexSign * by_index(a, b, nullptr) : 0;
}

bool ResolveComparers()
{
  below_deck_ascending_method  = SortingPredicates::Method("OfficerSortByBelowDeckAbilityAscending", 2);
  below_deck_descending_method = SortingPredicates::Method("OfficerSortByBelowDeckAbilityDescending", 2);
  if (!below_deck_ascending_method || !below_deck_descending_method)
    return false;
  below_deck_ascending  = NativeComparer(below_deck_ascending_method);
  below_deck_descending = NativeComparer(below_deck_descending_method);
  unlocked_first        = NativeComparer(SortingPredicates::Method("OfficerSortByUnlockedStateAscending", 2));
  by_index              = NativeComparer(SortingPredicates::Method("OfficerSortByIndexAscending", 2));
  // The sort function and option are allocated without a constructor, so both classes must exist.
  return SortFunction::Class::Raw() && ProcessingOption::Class::Raw();
}

// The roster's default direction is Ascending, so its _ascending comparer puts below-deck officers first. The
// assignment screen defaults to Descending, so its _ascending comparer puts them last and SortComparer's negation
// brings them first.
SortFunction* NewSortFunction(const char* key, bool roster)
{
  const auto native    = [](auto compare) { return reinterpret_cast<Il2CppMethodPointer>(compare); };
  auto*      ascending = il2cpp::NewDelegate(
      officer_func_class, below_deck_ascending_method,
      native(roster ? &CompareOfficers<&below_deck_descending, -1> : &CompareOfficers<&below_deck_ascending, 1>));
  auto* descending = il2cpp::NewDelegate(
      officer_func_class, below_deck_descending_method,
      native(roster ? &CompareOfficers<&below_deck_ascending, -1> : &CompareOfficers<&below_deck_descending, 1>));
  if (!ascending || !descending)
    return nullptr;

  // SortFunction's parameterless constructor has no generated code; every field Compare reads is set here.
  auto* sort = il2cpp::Allocate<SortFunction>();
  sort->SetDisplayKey(il2cpp::NewString(key));
  sort->SetAscending(ascending);
  sort->SetDescending(descending);
  return sort;
}

// --- Sort menus ---

bool HasBelowDeckOption(il2cpp::List<ProcessingOption*>* options)
{
  return std::ranges::any_of(*options, [](ProcessingOption* option) {
    const auto key = option ? il2cpp::ToUtf8(option->displayKey()) : std::string();
    return key == kRosterKey || key == kAssignmentKey;
  });
}

// Appends the option to `options`. The roster builds its SortComparer from `sorters` rather than from the options,
// so the sort function goes there too when given.
void AddBelowDeckOption(il2cpp::List<ProcessingOption*>* options, const char* key, bool roster,
                        il2cpp::List<SortFunction*>* sorters = nullptr)
{
  if (!options) {
    spdlog::warn("[OfficerSort] the screen has no sort options; not adding Below Deck Ability");
    return;
  }
  if (!officer_func_class) {
    auto* first        = il2cpp::At(options, 0);
    auto* sort         = first ? first->sortingOption() : nullptr;
    auto* ascending    = sort ? sort->ascending() : nullptr;
    officer_func_class = ascending ? ascending->klass : nullptr;
    if (!officer_func_class) {
      spdlog::warn("[OfficerSort] no existing option shows the Func<object, object, int> class; not adding Below "
                   "Deck Ability");
      return;
    }
  }
  if (HasBelowDeckOption(options))
    return;

  auto* sort = NewSortFunction(key, roster);
  if (!sort) {
    spdlog::warn("[OfficerSort] could not create the Below Deck Ability sort function");
    return;
  }
  if (sorters)
    sorters->Add(sort);

  auto* option = il2cpp::Allocate<ProcessingOption>();
  option->SetDisplayKey(il2cpp::NewString(key));
  option->SetSortingOption(sort);
  options->Add(option);
}

void OfficerSortGenerators_InitializeOfficerSorters_Hook(auto original, OfficerSortGenerators* _this)
{
  original(_this);
  if (Config::Get().ui.officer_sort)
    AddBelowDeckOption(_this->rosterOptions(), kRosterKey, true, _this->rosterSorters());
}

void OfficerSortGenerators_InitializeAssignmentSorters_Hook(auto original, OfficerSortGenerators* _this)
{
  original(_this);
  if (Config::Get().ui.officer_sort)
    AddBelowDeckOption(_this->assignmentOptions(), kAssignmentKey, false);
}

bool InstallOfficerSort()
{
  if (!ResolveComparers())
    return false;
  il2cpp::HookSet hooks("OfficerSort");
  IL2CPP_STAGE(hooks, OfficerSortGenerators::Class::Method("InitializeOfficerSorters", 0),
               OfficerSortGenerators_InitializeOfficerSorters_Hook);
  IL2CPP_STAGE(hooks, OfficerSortGenerators::Class::Method("InitializeAssignmentSorters", 0),
               OfficerSortGenerators_InitializeAssignmentSorters_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kOfficerSort{"OfficerSort", &Config::Patches::officersorthooks, InstallOfficerSort};
