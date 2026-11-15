#pragma once

#include <il2cpp/il2cpp_helper.h>

struct PopulatedSystemData {
public:
  __declspec(property(get = __get__backdrop)) void* _backdrop;
  __declspec(property(get = __get__systemId)) int64_t _systemId;
  __declspec(property(get = __get__VariationCollection)) void* VariationCollection;

private:
  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Navigation", "PopulatedSystemData");
    return class_helper;
  }

public:
  void* __get__VariationCollection()
  {
    static auto field = get_class_helper().GetField("<VariationCollection>k__BackingField");
    return field.get_info() ? *(void**)((ptrdiff_t)this + field.offset()) : nullptr;
  }

  void* __get__backdrop()
  {
    static auto field = get_class_helper().GetField("_backdrop");
    return *(void**)((ptrdiff_t)this + field.offset());
  }

  int64_t __get__systemId()
  {
    static auto field = get_class_helper().GetField("_systemId");
    return *(int64_t*)((ptrdiff_t)this + field.offset());
  }
};
