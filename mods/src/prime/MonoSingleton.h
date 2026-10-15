#pragma once

#include <il2cpp/il2cpp.h>

// Unity singletons expose a static Instance on their MonoSingleton`1<T> base.
template <typename T> struct MonoSingleton : il2cpp::Object {
  static T* Instance()
  { return T::Class::template Get<"Instance", T*>(); }
  // The singleton s_instance already holds, or null. Instance can search the scene or create a replacement.
  static T* Existing()
  { return T::Class::template Field<"s_instance", T*>(); }
};
