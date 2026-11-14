#pragma once

// The officer roster and officer assignment screens, and the sort options their lists offer.

#include "prime/ui.h"

#include <il2cpp/il2cpp.h>

struct OfficerAssignmentViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.OfficerAssignment", "OfficerAssignmentViewController">;

  InputFieldWidget* inputField()
  { return Field<"_inputField", InputFieldWidget*>(); }
  GenericButtonWidget* officerPresetsButton()
  { return Field<"_officerPresetsButton", GenericButtonWidget*>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
};

// The static comparers the officer lists sort with.
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

// One entry in a list's sort menu.
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

// Builds the sort menus of the officer roster and the officer assignment screen.
struct OfficerSortGenerators : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Officers", "OfficerSortGenerators">;

  il2cpp::List<SortFunction*>* rosterSorters()
  { return Field<"_rosterSorters", il2cpp::List<SortFunction*>*>(); }
  il2cpp::List<ProcessingOption*>* rosterOptions()
  { return Field<"_rosterOptions", il2cpp::List<ProcessingOption*>*>(); }
  il2cpp::List<ProcessingOption*>* assignmentOptions()
  { return Field<"_assignmentOptions", il2cpp::List<ProcessingOption*>*>(); }
};
