// GcHandle ownership against a fake handle table: null and failed roots, moves, resets and weak targets.

#include "il2cpp_test_api.h"

#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace
{
Il2CppObject                                      object{}, other{};
std::unordered_map<Il2CppGCHandle, Il2CppObject*> targets;
uintptr_t                                         next_handle     = 1;
bool                                              fail_allocation = false;
int                                               frees           = 0;
bool                                              weak            = false;

void Require(bool condition)
{
  if (!condition)
    throw std::runtime_error("GC ownership regression");
}
} // namespace

API(Il2CppGCHandle, il2cpp_gchandle_new, (Il2CppObject * object, bool pinned))
{
  if (!object || pinned)
    throw std::runtime_error("invalid root");
  if (fail_allocation)
    return nullptr;
  auto handle     = reinterpret_cast<Il2CppGCHandle>(next_handle++);
  targets[handle] = object;
  return handle;
}
END_API
API(Il2CppGCHandle, il2cpp_gchandle_new_weakref, (Il2CppObject * object, bool resurrect))
{
  if (resurrect)
    throw std::runtime_error("unexpected resurrection");
  weak = true;
  return il2cpp_gchandle_new(object, false);
}
END_API
API(Il2CppObject*, il2cpp_gchandle_get_target, (Il2CppGCHandle handle))
{ return targets.at(handle); }
END_API
API(void, il2cpp_gchandle_free, (Il2CppGCHandle handle))
{
  if (targets.erase(handle) != 1)
    throw std::runtime_error("handle freed twice");
  ++frees;
}
END_API

int main()
{
  Require(!il2cpp::GcHandle::Strong(nullptr));
  Require(!il2cpp::GcHandle::Weak(nullptr));
  fail_allocation = true;
  Require(!il2cpp::GcHandle::Strong(&object) && targets.empty());
  fail_allocation = false;
  {
    auto root = il2cpp::GcHandle::Strong(&object);
    Require(root.Get() == &object);
    auto moved = std::move(root);
    Require(!root && !root.Get() && moved.Get() == &object);
    root  = il2cpp::GcHandle::Strong(&other);
    moved = std::move(root);
    Require(frees == 1 && !root && moved.Get() == &other);
    auto& same = moved;
    moved      = std::move(same);
    Require(moved.Get() == &other && frees == 1);
    moved.Reset();
    moved.Reset();
    Require(!moved && frees == 2);
    root = il2cpp::GcHandle::Weak(&object);
    Require(weak && root.Get() == &object);
    targets.begin()->second = nullptr; // Managed collection clears a weak target.
    Require(root && !root.Get());
  }
  Require(frees == 3 && targets.empty());
  std::cout << "IL2CPP GC ownership tests passed\n";
}
