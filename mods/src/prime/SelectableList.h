#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

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
