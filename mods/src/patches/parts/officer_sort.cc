#include "config.h"
#include "errormsg.h"

#include <il2cpp/il2cpp-functions.h>
#include <il2cpp/il2cpp_helper.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <cstring>

// Restores the "Below Deck Ability" (BDA) sort option removed from the game UI
// in m93.1. Hooks OfficerSortGenerators.InitializeOfficerSorters /
// InitializeAssignmentSorters and appends a BDA ProcessingOption after the game
// populates its own sort lists.

namespace {

constexpr char kBelowDeckKey[]           = "below_deck_ability";
constexpr char kBelowDeckAssignmentKey[] = "_below_deck_ability";

using SortMethodPtr = int (*)(void* obj1, void* obj2, void* delegatePtr);

struct OfficerSortState {
  IL2CppClassHelper* sortingPredicates     = nullptr;
  IL2CppClassHelper* sortFunction          = nullptr;
  IL2CppClassHelper* processingOption      = nullptr;
  IL2CppClassHelper* officerSortGenerators = nullptr;

  const MethodInfo* ascendingMethod  = nullptr;
  const MethodInfo* descendingMethod = nullptr;

  SortMethodPtr unlockedStatePtr = nullptr;
  SortMethodPtr bdaAscPtr        = nullptr;
  SortMethodPtr bdaDescPtr       = nullptr;
  SortMethodPtr indexAscPtr      = nullptr;

  IL2CppFieldHelper* sortFunctionDisplayKey        = nullptr;
  IL2CppFieldHelper* sortFunctionAscending         = nullptr;
  IL2CppFieldHelper* sortFunctionDescending        = nullptr;
  IL2CppFieldHelper* rosterSortersField            = nullptr;
  IL2CppFieldHelper* rosterOptionsField            = nullptr;
  IL2CppFieldHelper* assignmentOptionsField        = nullptr;
  IL2CppFieldHelper* processingOptionDisplayKey    = nullptr;
  IL2CppFieldHelper* processingOptionSortingOption = nullptr;

  Il2CppClass* funcClass = nullptr;
  bool         valid     = false;
};

OfficerSortState& State()
{
  static OfficerSortState s;
  return s;
}

// SortComparer.Compare always calls the _ascending delegate; for Descending
// direction it negates the result. The _descending delegate is never called.
// All logic lives in the *Ascending wrappers so that:
//   direct call  = ascending behavior
//   negated call = descending behavior

int RosterAscendingWrapper(void* obj1, void* obj2, void* /*delegatePtr*/)
{
  auto& s = State();
  if (s.unlockedStatePtr) {
    int r = s.unlockedStatePtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  if (s.bdaDescPtr) {
    int r = s.bdaDescPtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  return s.indexAscPtr ? -s.indexAscPtr(obj1, obj2, nullptr) : 0;
}

int RosterDescendingWrapper(void* obj1, void* obj2, void* /*delegatePtr*/)
{
  auto& s = State();
  if (s.unlockedStatePtr) {
    int r = s.unlockedStatePtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  if (s.bdaAscPtr) {
    int r = s.bdaAscPtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  return s.indexAscPtr ? -s.indexAscPtr(obj1, obj2, nullptr) : 0;
}

int AssignmentAscendingWrapper(void* obj1, void* obj2, void* /*delegatePtr*/)
{
  auto& s = State();
  if (s.unlockedStatePtr) {
    int r = s.unlockedStatePtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  if (s.bdaAscPtr) {
    int r = s.bdaAscPtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  return s.indexAscPtr ? s.indexAscPtr(obj1, obj2, nullptr) : 0;
}

int AssignmentDescendingWrapper(void* obj1, void* obj2, void* /*delegatePtr*/)
{
  auto& s = State();
  if (s.unlockedStatePtr) {
    int r = s.unlockedStatePtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  if (s.bdaDescPtr) {
    int r = s.bdaDescPtr(obj1, obj2, nullptr);
    if (r != 0) return r;
  }
  return s.indexAscPtr ? s.indexAscPtr(obj1, obj2, nullptr) : 0;
}

bool InitializeState()
{
  auto& s = State();

  s.sortingPredicates = new IL2CppClassHelper(
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.Core", "SortingPredicates"));
  if (!s.sortingPredicates->get_cls()) {
    ErrorMsg::MissingHelper("Digit.Client.Core", "SortingPredicates");
    return false;
  }

  s.sortFunction = new IL2CppClassHelper(
      il2cpp_get_class_helper("Digit.Client.PrimeLib.Runtime", "Digit.Client.Core", "SortFunction"));
  if (!s.sortFunction->get_cls()) {
    ErrorMsg::MissingHelper("Digit.Client.Core", "SortFunction");
    return false;
  }

  auto sortComparer = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.Sorting", "SortComparer");
  if (!sortComparer.get_cls()) {
    ErrorMsg::MissingHelper("Digit.Client.Sorting", "SortComparer");
    return false;
  }

  // GetNestedType() reads the nestedTypes array directly and crashes on this
  // class; use the safe iterator API instead.
  Il2CppClass* processingOptionCls = nullptr;
  void*        iter                = nullptr;
  while (auto* nested = il2cpp_class_get_nested_types(sortComparer.get_cls(), &iter)) {
    if (nested->name && std::strcmp(nested->name, "ProcessingOption") == 0) {
      processingOptionCls = nested;
      break;
    }
  }
  s.processingOption = new IL2CppClassHelper(processingOptionCls);
  if (!s.processingOption->get_cls()) {
    ErrorMsg::MissingHelper("Digit.Client.Sorting", "SortComparer.ProcessingOption");
    return false;
  }

  s.officerSortGenerators = new IL2CppClassHelper(
      il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Officers", "OfficerSortGenerators"));
  if (!s.officerSortGenerators->get_cls()) {
    ErrorMsg::MissingHelper("Digit.Prime.Officers", "OfficerSortGenerators");
    return false;
  }

  s.ascendingMethod = s.sortingPredicates->GetMethodInfo("OfficerSortByBelowDeckAbilityAscending", 2);
  if (!s.ascendingMethod) {
    ErrorMsg::MissingStaticMethod("SortingPredicates", "OfficerSortByBelowDeckAbilityAscending");
    return false;
  }
  s.descendingMethod = s.sortingPredicates->GetMethodInfo("OfficerSortByBelowDeckAbilityDescending", 2);
  if (!s.descendingMethod) {
    ErrorMsg::MissingStaticMethod("SortingPredicates", "OfficerSortByBelowDeckAbilityDescending");
    return false;
  }

  const MethodInfo* unlockedStateMethod =
      s.sortingPredicates->GetMethodInfo("OfficerSortByUnlockedStateAscending", 2);
  if (!unlockedStateMethod) {
    spdlog::warn("[OfficerSort] OfficerSortByUnlockedStateAscending not found; locked officers may not sort to bottom");
  } else {
    s.unlockedStatePtr = reinterpret_cast<SortMethodPtr>(unlockedStateMethod->methodPointer);
  }

  const MethodInfo* indexMethod = s.sortingPredicates->GetMethodInfo("OfficerSortByIndexAscending", 2);
  if (!indexMethod) {
    spdlog::warn("[OfficerSort] OfficerSortByIndexAscending not found; no index tiebreaker");
  } else {
    s.indexAscPtr = reinterpret_cast<SortMethodPtr>(indexMethod->methodPointer);
  }

  s.bdaAscPtr  = reinterpret_cast<SortMethodPtr>(s.ascendingMethod->methodPointer);
  s.bdaDescPtr = reinterpret_cast<SortMethodPtr>(s.descendingMethod->methodPointer);

  s.sortFunctionDisplayKey  = new IL2CppFieldHelper(s.sortFunction->GetField("_displayKey"));
  s.sortFunctionAscending   = new IL2CppFieldHelper(s.sortFunction->GetField("_ascending"));
  s.sortFunctionDescending  = new IL2CppFieldHelper(s.sortFunction->GetField("_descending"));
  if (!s.sortFunctionDisplayKey->isValidHelper() || !s.sortFunctionAscending->isValidHelper() ||
      !s.sortFunctionDescending->isValidHelper()) {
    spdlog::error("[OfficerSort] SortFunction field layout changed");
    return false;
  }

  s.rosterSortersField     = new IL2CppFieldHelper(s.officerSortGenerators->GetField("_rosterSorters"));
  s.rosterOptionsField     = new IL2CppFieldHelper(s.officerSortGenerators->GetField("_rosterOptions"));
  s.assignmentOptionsField = new IL2CppFieldHelper(s.officerSortGenerators->GetField("_assignmentOptions"));
  if (!s.rosterSortersField->isValidHelper() || !s.rosterOptionsField->isValidHelper() ||
      !s.assignmentOptionsField->isValidHelper()) {
    spdlog::error("[OfficerSort] OfficerSortGenerators field layout changed");
    return false;
  }

  s.processingOptionDisplayKey    = new IL2CppFieldHelper(s.processingOption->GetField("_displayKey"));
  s.processingOptionSortingOption = new IL2CppFieldHelper(s.processingOption->GetField("_sortingOption"));
  if (!s.processingOptionDisplayKey->isValidHelper() || !s.processingOptionSortingOption->isValidHelper()) {
    spdlog::error("[OfficerSort] ProcessingOption field layout changed");
    return false;
  }

  s.valid = true;
  return true;
}

Il2CppClass* DiscoverFuncClassFromList(void* existingList)
{
  if (!existingList) return nullptr;

  auto* listClass = il2cpp_object_get_class(reinterpret_cast<Il2CppObject*>(existingList));
  if (!listClass) return nullptr;

  auto* getItem = il2cpp_class_get_method_from_name(listClass, "get_Item", 1);
  if (!getItem) return nullptr;

  Il2CppException* exc        = nullptr;
  int32_t          index      = 0;
  void*            idxArgs[1] = {&index};
  auto* firstOption = il2cpp_runtime_invoke(getItem, existingList, idxArgs, &exc);
  if (exc || !firstOption) return nullptr;

  auto& s = State();
  auto  sortingOptionField = s.processingOption->GetField("_sortingOption");
  if (!sortingOptionField.isValidHelper()) return nullptr;

  auto* sortFn = *reinterpret_cast<void**>(reinterpret_cast<char*>(firstOption) + sortingOptionField.offset());
  if (!sortFn) return nullptr;

  auto* ascendingDelegate =
      *reinterpret_cast<Il2CppObject**>(reinterpret_cast<char*>(sortFn) + s.sortFunctionAscending->offset());
  if (!ascendingDelegate) return nullptr;

  return il2cpp_object_get_class(ascendingDelegate);
}

// Called via invoker_method to bypass il2cpp_runtime_invoke, which fails for
// static methods ("Delegate to an instance method cannot have null 'this'").
Il2CppObject* CreateSortDelegate(const MethodInfo* sortMethod, Il2CppGCHandle& outHandle)
{
  auto& s = State();
  if (!s.funcClass || !sortMethod) return nullptr;

  auto* del = il2cpp_object_new(s.funcClass);
  if (!del) {
    spdlog::warn("[OfficerSort] il2cpp_object_new failed for sort delegate");
    return nullptr;
  }

  outHandle = il2cpp_gchandle_new(del, false);
  if (!outHandle) {
    spdlog::warn("[OfficerSort] gchandle_new failed for sort delegate");
    return nullptr;
  }

  auto* ctor = il2cpp_class_get_method_from_name(s.funcClass, ".ctor", 2);
  if (ctor && ctor->invoker_method) {
    void* ctorArgs[2] = {nullptr, (void*)&sortMethod};
    ctor->invoker_method(ctor->methodPointer, ctor, del, ctorArgs, nullptr);
    return del;
  }

  spdlog::warn("[OfficerSort] no .ctor on Func class; cannot create delegate");
  return nullptr;
}

// forRoster=true:  roster defaults to Ascending, so _ascending produces BDA-first.
// forRoster=false: assignment defaults to Descending, so _ascending produces
//                  non-BDA-first (SortComparer negates it, yielding BDA-first).
Il2CppObject* CreateBelowDeckSortFunction(void* existingList, const char* displayKey,
                                          bool forRoster, Il2CppGCHandle& outHandle)
{
  auto& s = State();

  Il2CppGCHandle ascHandle  = 0;
  Il2CppGCHandle descHandle = 0;
  auto* ascDelegate  = CreateSortDelegate(s.ascendingMethod, ascHandle);
  auto* descDelegate = CreateSortDelegate(s.descendingMethod, descHandle);
  if (!ascDelegate || !descDelegate) {
    if (ascHandle)  il2cpp_gchandle_free(ascHandle);
    if (descHandle) il2cpp_gchandle_free(descHandle);
    spdlog::warn("[OfficerSort] failed to create below-deck sort delegates");
    return nullptr;
  }

  auto* ascDel  = reinterpret_cast<Il2CppDelegate*>(ascDelegate);
  auto* descDel = reinterpret_cast<Il2CppDelegate*>(descDelegate);
  if (forRoster) {
    ascDel->method_ptr  = reinterpret_cast<Il2CppMethodPointer>(&RosterAscendingWrapper);
    descDel->method_ptr = reinterpret_cast<Il2CppMethodPointer>(&RosterDescendingWrapper);
  } else {
    ascDel->method_ptr  = reinterpret_cast<Il2CppMethodPointer>(&AssignmentAscendingWrapper);
    descDel->method_ptr = reinterpret_cast<Il2CppMethodPointer>(&AssignmentDescendingWrapper);
  }

  auto* sortFn = il2cpp_object_new(s.sortFunction->get_cls());
  if (!sortFn) {
    il2cpp_gchandle_free(ascHandle);
    il2cpp_gchandle_free(descHandle);
    spdlog::warn("[OfficerSort] il2cpp_object_new failed for SortFunction");
    return nullptr;
  }

  outHandle = il2cpp_gchandle_new(sortFn, false);
  if (!outHandle) {
    il2cpp_gchandle_free(ascHandle);
    il2cpp_gchandle_free(descHandle);
    return nullptr;
  }

  auto* displayKeyStr = il2cpp_string_new(displayKey);
  *reinterpret_cast<void**>(reinterpret_cast<char*>(sortFn) + s.sortFunctionDisplayKey->offset()) = displayKeyStr;
  *reinterpret_cast<void**>(reinterpret_cast<char*>(sortFn) + s.sortFunctionAscending->offset())  = ascDelegate;
  *reinterpret_cast<void**>(reinterpret_cast<char*>(sortFn) + s.sortFunctionDescending->offset()) = descDelegate;

  if (ascHandle)  il2cpp_gchandle_free(ascHandle);
  if (descHandle) il2cpp_gchandle_free(descHandle);

  return sortFn;
}

bool ListAlreadyHasBelowDeck(void* list)
{
  if (!list) return false;
  auto* listClass = il2cpp_object_get_class(reinterpret_cast<Il2CppObject*>(list));
  if (!listClass) return false;

  auto* getCount = il2cpp_class_get_method_from_name(listClass, "get_Count", 0);
  auto* getItem  = il2cpp_class_get_method_from_name(listClass, "get_Item", 1);
  if (!getCount || !getItem) return false;

  Il2CppException* exc = nullptr;
  auto* countObj = il2cpp_runtime_invoke(getCount, list, nullptr, &exc);
  if (exc || !countObj) return false;
  auto count = *reinterpret_cast<int32_t*>(il2cpp_object_unbox(countObj));

  auto& s = State();
  auto  displayKeyField = s.processingOption->GetField("_displayKey");
  if (!displayKeyField.isValidHelper()) return false;

  for (int32_t i = 0; i < count; ++i) {
    void* idxArgs[1] = {&i};
    exc = nullptr;
    auto* option = il2cpp_runtime_invoke(getItem, list, idxArgs, &exc);
    if (exc || !option) continue;

    auto* keyPtr =
        *reinterpret_cast<Il2CppString**>(reinterpret_cast<char*>(option) + displayKeyField.offset());
    if (!keyPtr) continue;

    auto len = keyPtr->length;
    auto matchesKey = [&](const char* key) {
      auto keyLen = (int32_t)strlen(key);
      if (len != keyLen) return false;
      for (int32_t j = 0; j < len; ++j) {
        if ((char)keyPtr->chars[j] != key[j]) return false;
      }
      return true;
    };
    if (matchesKey(kBelowDeckKey) || matchesKey(kBelowDeckAssignmentKey)) return true;
  }
  return false;
}

// If sortersList is non-null, also adds the raw SortFunction to it (the roster
// screen builds its SortComparer from _rosterSorters, not _rosterOptions).
void AppendBelowDeckOption(void* list, const char* displayKey, bool forRoster, void* sortersList = nullptr)
{
  if (!list) {
    spdlog::warn("[OfficerSort] AppendBelowDeckOption: list is null");
    return;
  }

  auto& s = State();
  if (!s.valid) return;

  if (!s.funcClass) {
    s.funcClass = DiscoverFuncClassFromList(list);
    if (!s.funcClass) {
      spdlog::warn("[OfficerSort] could not discover Func<object,object,int> class; skipping below-deck option");
      return;
    }
  }

  if (ListAlreadyHasBelowDeck(list)) return;

  Il2CppGCHandle sortFnHandle = 0;
  auto* sortFn = CreateBelowDeckSortFunction(list, displayKey, forRoster, sortFnHandle);
  if (!sortFn) {
    if (sortFnHandle) il2cpp_gchandle_free(sortFnHandle);
    return;
  }

  if (sortersList) {
    auto* sortersListClass = il2cpp_object_get_class(reinterpret_cast<Il2CppObject*>(sortersList));
    auto* sortersAddMethod = sortersListClass ? il2cpp_class_get_method_from_name(sortersListClass, "Add", 1) : nullptr;
    if (sortersAddMethod) {
      void* sortersAddArgs[1] = {sortFn};
      Il2CppException* sortersExc = nullptr;
      il2cpp_runtime_invoke(sortersAddMethod, sortersList, sortersAddArgs, &sortersExc);
      if (sortersExc) spdlog::warn("[OfficerSort] sorters list.Add raised an exception");
    }
  }

  auto* option = il2cpp_object_new(s.processingOption->get_cls());
  if (!option) {
    spdlog::warn("[OfficerSort] il2cpp_object_new failed for ProcessingOption");
    il2cpp_gchandle_free(sortFnHandle);
    return;
  }

  Il2CppGCHandle optionHandle = il2cpp_gchandle_new(option, false);
  if (!optionHandle) {
    il2cpp_gchandle_free(sortFnHandle);
    return;
  }

  auto* idStr = il2cpp_string_new(displayKey);
  *reinterpret_cast<void**>(reinterpret_cast<char*>(option) + s.processingOptionDisplayKey->offset())    = idStr;
  *reinterpret_cast<void**>(reinterpret_cast<char*>(option) + s.processingOptionSortingOption->offset()) = sortFn;

  auto* listClass = il2cpp_object_get_class(reinterpret_cast<Il2CppObject*>(list));
  auto* addMethod = listClass ? il2cpp_class_get_method_from_name(listClass, "Add", 1) : nullptr;
  if (!addMethod) {
    spdlog::warn("[OfficerSort] list has no Add method; cannot insert below-deck option");
    il2cpp_gchandle_free(optionHandle);
    il2cpp_gchandle_free(sortFnHandle);
    return;
  }

  void* addArgs[1] = {option};
  Il2CppException* exc = nullptr;
  il2cpp_runtime_invoke(addMethod, list, addArgs, &exc);
  if (exc) spdlog::warn("[OfficerSort] list.Add raised an exception; below-deck option not inserted");

  il2cpp_gchandle_free(optionHandle);
  il2cpp_gchandle_free(sortFnHandle);
}

void InitializeOfficerSorters_Hook(auto original, void* _this)
{
  original(_this);
  if (!Config::Get().officer_sort) return;
  auto& s = State();
  auto* optionsList = *reinterpret_cast<void**>(reinterpret_cast<char*>(_this) + s.rosterOptionsField->offset());
  auto* sortersList = *reinterpret_cast<void**>(reinterpret_cast<char*>(_this) + s.rosterSortersField->offset());
  AppendBelowDeckOption(optionsList, kBelowDeckKey, true, sortersList);
}

void InitializeAssignmentSorters_Hook(auto original, void* _this)
{
  original(_this);
  if (!Config::Get().officer_sort) return;
  auto& s = State();
  auto* list = *reinterpret_cast<void**>(reinterpret_cast<char*>(_this) + s.assignmentOptionsField->offset());
  AppendBelowDeckOption(list, kBelowDeckAssignmentKey, false);
}

} // namespace

void InstallOfficerSortHooks()
{
  if (!InitializeState()) {
    spdlog::error("[OfficerSort] initialization failed; hooks not installed");
    return;
  }

  auto& s = State();

  auto rosterInitPtr = s.officerSortGenerators->GetMethod("InitializeOfficerSorters", 0);
  if (!rosterInitPtr) {
    ErrorMsg::MissingMethod("OfficerSortGenerators", "InitializeOfficerSorters");
    return;
  }

  auto assignmentInitPtr = s.officerSortGenerators->GetMethod("InitializeAssignmentSorters", 0);
  if (!assignmentInitPtr) {
    ErrorMsg::MissingMethod("OfficerSortGenerators", "InitializeAssignmentSorters");
    return;
  }

  SPUD_STATIC_DETOUR(rosterInitPtr, InitializeOfficerSorters_Hook);
  SPUD_STATIC_DETOUR(assignmentInitPtr, InitializeAssignmentSorters_Hook);
  spdlog::info("Officer sort: restored Below Deck Ability sort option");
}
