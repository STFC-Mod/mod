#pragma once

#include <il2cpp/il2cpp_helper.h>

#include "ShipManagementViewContext.h"

#include <cstring>

struct ShipManagementViewController {
public:
  __declspec(property(get = __get_isActiveAndEnabled)) bool isActiveAndEnabled;

  // m_context lives on the generic ViewController<T> base, so its offset is only
  // discoverable by walking the class hierarchy.
  ShipManagementViewContext* Context()
  {
    static auto* field = [] {
      for (auto* cls = get_class_helper().get_cls(); cls != nullptr; cls = il2cpp_class_get_parent(cls)) {
        void* iterator = nullptr;
        while (auto* candidate = il2cpp_class_get_fields(cls, &iterator)) {
          const auto* name = il2cpp_field_get_name(candidate);
          if (name != nullptr && std::strcmp(name, "m_context") == 0) {
            return candidate;
          }
        }
      }
      return static_cast<FieldInfo*>(nullptr);
    }();
    return field != nullptr ? *(ShipManagementViewContext**)((ptrdiff_t)this + field->offset) : nullptr;
  }

  bool __get_isActiveAndEnabled()
  {
    static auto property = get_class_helper().GetProperty("isActiveAndEnabled");
    return property.Get<bool>(this);
  }

  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Ships", "ShipManagementViewController");
    return class_helper;
  }

private:
  friend class ObjectFinder<ShipManagementViewController>;
};
