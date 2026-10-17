#pragma once

#if (defined(_WIN32) && defined(_M_X64)) || defined(__APPLE__)
#include <il2cpp-tabledefs.h>
#include <il2cpp/il2cpp_checked.h>
#include <il2cpp/runtime.h>
#include <stdexcept>

namespace mod_settings::native
{
struct Root {
  Il2CppGCHandle handle = nullptr;
  explicit Root(Il2CppObject* object, bool weak = false)
  {
    if (object)
      handle = weak ? il2cpp_gchandle_new_weakref(object, false) : il2cpp_gchandle_new(object, false);
    if (object && !handle)
      throw std::runtime_error("settings root");
  }
  ~Root()
  {
    if (handle)
      il2cpp_gchandle_free(handle);
  }
  Root(const Root&) = delete;
  Il2CppObject* get() const
  { return handle ? il2cpp_gchandle_get_target(handle) : nullptr; }
};

// Shared native boundary helpers. No setting state, views or hook installation.
void          Warn(const char* operation);
bool          Type(const Il2CppType* type, int expected);
bool          Instance(const MethodInfo* method, int count, int result);
void          ValidateHookFit(const MethodInfo* method);
bool          Reference(const Il2CppType* type);
FieldInfo*    Field(Il2CppClass* cls, const char* name);
Il2CppObject* ReadField(Il2CppObject* object, FieldInfo* field);
Il2CppObject* Invoke(const MethodInfo* method, Il2CppObject* object, void** args = nullptr);
Il2CppObject* Call(Il2CppObject* object, const char* name, int count = 0, void** args = nullptr);
using Il2CppChecked::Boolean;
bool          Equals(Il2CppObject* value, const char* ascii);
Il2CppObject* Target(Il2CppGCHandle handle);
void          Free(Il2CppGCHandle& handle);
void          SetActive(Il2CppObject* object, bool value);
Il2CppObject* MakeDelegate(Il2CppClass* cls, Il2CppObject* director, const MethodInfo* method);
int           Count(Il2CppObject* list);
Il2CppObject* Item(Il2CppObject* list, int index);
bool          HasLabel(Il2CppObject* row, const char* id);
} // namespace mod_settings::native

#endif
