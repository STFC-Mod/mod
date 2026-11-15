#pragma once

#include "il2cpp-functions.h"

#include <il2cpp-api-types.h>
#include <il2cpp-class-internals.h>
#include <il2cpp-config.h>
#include <il2cpp-object-internals.h>
#include <utils/Il2CppHashMap.h>

#include <EASTL/span.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#if !_WIN32
#include <syslog.h>
#include <unistd.h>
#endif

class IL2CppPropertyHelper
{
public:
  IL2CppPropertyHelper(Il2CppClass* cls, const PropertyInfo* propInfo)
  {
    this->cls      = cls;
    this->propInfo = propInfo;
  }

  bool isValidHelper()
  {
#if DEBUG
    return true;
#else
    return this->cls != nullptr && propInfo != nullptr;
#endif
  }

  template <typename T> void SetRaw(void* _this, T& v)
  {
    if (!this->propInfo) {
      return;
    }

    auto set_method         = il2cpp_property_get_set_method((PropertyInfo*)this->propInfo);
    auto set_method_virtual = il2cpp_object_get_virtual_method((Il2CppObject*)_this, set_method);

    Il2CppException* exception = nullptr;
    void*            params[1] = {&v};

    il2cpp_runtime_invoke(set_method_virtual, _this, params, &exception);
  }

  template <typename T = void> T* GetRaw(void* _this)
  {
    if (!this->propInfo) {
      return nullptr;
    }

    auto get_method         = il2cpp_property_get_get_method((PropertyInfo*)this->propInfo);
    auto get_method_virtual = il2cpp_object_get_virtual_method((Il2CppObject*)_this, get_method);

    Il2CppException* exception = nullptr;
    auto             result    = il2cpp_runtime_invoke(get_method_virtual, _this, nullptr, &exception);

    if (exception) {
      return nullptr;
    }

    return (T*)(result);
  }

  template <typename T> T* Get(void* _this)
  {
    auto r = GetRaw<Il2CppObject>(_this);
    return !r ? nullptr : (T*)(il2cpp_object_unbox(r));
  }

  template <typename T> T* GetUnboxedSelf(void* _this)
  {
    auto r = GetRaw<Il2CppObject>(il2cpp_object_unbox((Il2CppObject*)_this));
    return !r ? nullptr : (T*)(il2cpp_object_unbox(r));
  }

private:
  Il2CppClass*        cls;
  const PropertyInfo* propInfo;
};

class IL2CppFieldHelper
{
public:
  IL2CppFieldHelper(Il2CppClass* cls, FieldInfo* fieldInfo)
  {
    this->cls       = cls;
    this->fieldInfo = fieldInfo;
  }

  bool isValidHelper()
  {
#if DEBUG
    return true;
#else
    return this->cls != nullptr && fieldInfo != nullptr;
#endif
  }

  FieldInfo* get_info() const
  {
    return this->fieldInfo;
  }

  inline ptrdiff_t offset() const
  {
    return this->fieldInfo->offset;
  }

private:
  Il2CppClass* cls;
  FieldInfo*   fieldInfo;
};

class IL2CppStaticFieldHelper
{
public:
  IL2CppStaticFieldHelper(Il2CppClass* cls, FieldInfo* fieldInfo)
  {
    this->cls       = cls;
    this->fieldInfo = fieldInfo;
  }

  template <typename T> inline T Get() const
  {
    T v;
    il2cpp_field_static_get_value(this->fieldInfo, &v);
    return v;
  }

private:
  Il2CppClass* cls;
  FieldInfo*   fieldInfo;
};

class IL2CppClassHelper
{
public:
  IL2CppClassHelper(Il2CppClass* cls)
  {
    this->cls = cls;
  }

  template <typename T> T* New()
  {
    return (T*)il2cpp_object_new(this->cls);
  }

  void* GetType()
  {
    auto obj = il2cpp_type_get_object(&this->cls->byval_arg);
    return obj;
  }

  bool isValidHelper()
  {
#if DEBUG
    return true;
#else
    return this->cls != nullptr;
#endif
  }

  template <typename T = void> T* GetMethod(const char* name, int arg_count = -1)
  {
    if (!this->cls) {
      return nullptr;
    }

    auto fn = il2cpp_class_get_method_from_name(this->cls, name, arg_count);
    if (fn != nullptr) {
      return (T*)fn->methodPointer;
    }

    return nullptr;
  }

  template <typename T = void> T* GetVirtualMethod(const char* name, int arg_count = -1)
  {
    if (!this->cls) {
      return nullptr;
    }

    auto fn = il2cpp_class_get_method_from_name(this->cls, name, arg_count);

    auto get_method_virtual = il2cpp_object_get_virtual_method((Il2CppObject*)this, fn);

    return (T*)get_method_virtual->methodPointer;
  }

  template <typename R, typename... Args> class InvokerMethod
  {

  public:
    InvokerMethod(const MethodInfo* fn)
        : fn(fn)
    {
    }

    R Invoke(void* _this, Args... args)
    {
      Il2CppException* exception = nullptr;
      void*            params    = {};
      auto             result    = il2cpp_runtime_invoke(this->fn, _this, &params, &exception);

      return *(R*)il2cpp_object_unbox(result);
    }

    const MethodInfo* fn;
  };

  template <typename R, typename... Args>
  const InvokerMethod<R, Args...> GetInvokeMethod(const char* name, int arg_count = -1)
  {
    if (!this->cls) {
      return nullptr;
    }

    auto fn = il2cpp_class_get_method_from_name(this->cls, name, arg_count);

    return InvokerMethod<R, Args...>(fn);
  }

  const MethodInfo*
  GetMethodInfoSpecial(const char*                                                    name,
                       std::function<bool(int param_count, const Il2CppType** param)> arg_filter = nullptr)
  {
    if (!this->cls) {
      return nullptr;
    }

    auto  flags = 0;
    void* iter  = NULL;
    while (const MethodInfo* method = il2cpp_class_get_methods(this->cls, &iter)) {
      if (method->name[0] == name[0] && !strcmp(name, method->name) && ((method->flags & flags) == flags)) {
        if (!arg_filter || arg_filter(method->parameters_count, method->parameters)) {
          return method;
        }
      }
    }
    return nullptr;
  }

  template <typename T = void>
  T* GetMethodSpecial(const char*                                                    name,
                      std::function<bool(int param_count, const Il2CppType** param)> arg_filter = nullptr)
  {
    if (!this->cls) {
      return nullptr;
    }
    auto info = GetMethodInfoSpecial(name, arg_filter);
    if (info) {
      return (T*)info->methodPointer;
    }
    return nullptr;
  }

  template <typename T = void> T* GetMethodSpecial2(Il2CppObject* obj, const char* name)
  {
    if (!this->cls) {
      return nullptr;
    }

    for (auto i = 0; i < obj->klass->method_count; ++i) {
      auto method = obj->klass->klass->methods[i];
      if (method->name[0] == name[0] && !strcmp(name, method->name)) {
        return (T*)method->methodPointer;
      }
    }
    return nullptr;
  }

  const MethodInfo* GetMethodInfo(const char* name, int arg_count = -1)
  {
    if (!this->cls) {
      return nullptr;
    }

    return il2cpp_class_get_method_from_name(this->cls, name, arg_count);
  }

  inline IL2CppPropertyHelper GetProperty(const char* name)
  {
    return IL2CppPropertyHelper{this->cls, il2cpp_class_get_property_from_name(this->cls, name)};
  }

  inline IL2CppFieldHelper GetField(const char* name)
  {
    return IL2CppFieldHelper{this->cls, il2cpp_class_get_field_from_name(this->cls, name)};
  }

  inline IL2CppStaticFieldHelper GetStaticField(const char* name)
  {
    return IL2CppStaticFieldHelper{this->cls, il2cpp_class_get_field_from_name(this->cls, name)};
  }

  inline IL2CppClassHelper GetParent(const char* name)
  {
    Il2CppClass* pcls = this->cls;
    if (pcls) {
      do {
        if (pcls->name[0] == name[0] && !strcmp(name, pcls->name)) {
          return IL2CppClassHelper{pcls};
        }

        pcls = il2cpp_class_get_parent(pcls);
      } while (pcls);
    }
    return IL2CppClassHelper{nullptr};
  }

  inline IL2CppClassHelper GetNestedType(const char* name)
  {
    for (int i = 0; i < this->cls->nested_type_count; ++i) {
      auto type = this->cls->nestedTypes[i];
      if (strcmp(type->name, name) == 0) {
        return type;
      }
    }

    return nullptr;
  }

  Il2CppClass* get_cls()
  {
    return this->cls;
  }

private:
  Il2CppClass* cls;
};

#define il2cpp_get_class_helper(assembly, namespacez, name) il2cpp_get_class_helper_impl(assembly, namespacez, name)

inline IL2CppClassHelper il2cpp_get_class_helper_impl(const char* assembly, const char* namespacez, const char* name)
{
  auto domain    = il2cpp_domain_get();
  auto assemblyT = domain ? il2cpp_domain_assembly_open(domain, assembly) : nullptr;
  auto image     = assemblyT ? il2cpp_assembly_get_image(assemblyT) : nullptr;

  auto cls = image ? il2cpp_class_from_name(image, namespacez, name) : nullptr;

  return IL2CppClassHelper{cls};
}

template <typename T> inline T* il2cpp_get_array_element(Il2CppArray* array, size_t index)
{
  Il2CppArraySize* n = (Il2CppArraySize*)(array);
  return (T*)n->vector[index];
}

extern eastl::unordered_map<Il2CppClass*, eastl::vector<uintptr_t>> tracked_objects;

template <typename T> class ObjectFinder
{
public:
  static T* Get()
  {
    auto& objects = tracked_objects[T::get_class_helper().get_cls()];
    if (objects.empty()) {
      // TODO: assert?
      return nullptr;
    }
    return reinterpret_cast<T*>(objects.back());
  }

  static eastl::span<T*> GetAll()
  {
    auto& objects = tracked_objects[T::get_class_helper().get_cls()];
    return {reinterpret_cast<T**>(objects.data()), reinterpret_cast<T**>(objects.data()) + objects.size()};
  }
};

template <typename T> T* il2cpp_resolve_icall_typed(const char* name)
{
  return (T*)il2cpp_resolve_icall(name);
}
