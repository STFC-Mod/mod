#pragma once

// Detours on managed methods and native functions, installed through SPUD transactions. Hooks take the original
// first:
//   void ScreenManager_Update_Hook(auto original, ScreenManager* _this) { ...; original(_this); }
//
//   il2cpp::HookSet hooks("Toasts");
//   IL2CPP_STAGE(hooks, ToastObserver::Class::Method("EnqueueToast"), ToastObserver_EnqueueToast_Hook);
//   return hooks.Commit();

#include "il2cpp.h"

#include <spud/detour.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace il2cpp
{
// The address SPUD patches for a detour on `method`: its entry, or the body a lone jump there leads to, as an arm64
// entry that is only `b body` often is. Methods with identical code share one body; compare these to find them.
void* DetourTarget(const Method& method);

// The hooks one installer puts in, through one SPUD transaction: every one of them, or none. Stage each hook with
// IL2CPP_STAGE or IL2CPP_STAGE_AT, then Commit(). A missing method, already reported by its lookup, or a null address
// makes Commit install nothing. The transaction applies the set under one suspend window and undoes it if any step
// fails, so a hook never runs without the rest of its set. SPUD measures each target from the image's unwind or
// function-start tables and refuses one too short for its patch. Every set that does not go in is reported as
// "[IL2CPP] <set> not installed: <why>", so features never log hook failures themselves.
//
// SPUD stages only removable hooks, which count calls in and out. After a successful commit they are detached and
// stay for the process. A set is committed once; a hook several features share goes in its own set, installed once.
//
// SPUD chains detours on one function safely, so two hooks may share a target. Identical methods can share
// generated code, though, and a hook on one of them then also runs for the others; DetourTarget finds them.
class HookSet
{
public:
  // Names the set in reports; the feature's descriptor name reads best.
  explicit HookSet(std::string_view name)
      : name_(name)
  {
  }

  template <typename Detour> void Stage(Detour detour)
  {
    // The transaction holds a reference, so the handle lives on the heap until the commit detaches it.
    auto handle = std::make_shared<Detour>(std::move(detour));
    transaction_.install(*handle);
    detach_.push_back([handle] { handle->detach(); });
  }
  // Marks a member of the set whose method or address is missing.
  void Missing()
  { missing_ = true; }

  // True when every staged hook went in. A set with nothing staged installs nothing and is false.
  bool Commit();

private:
  std::string                        name_;
  bool                               missing_ = false;
  spud::transaction                  transaction_;
  std::vector<std::function<void()>> detach_;
};
} // namespace il2cpp

// Macros because SPUD_AUTO_HOOK names the hook template itself to instantiate it for the target's signature.

// Stages a managed method's hook in an il2cpp::HookSet. A missing method marks the set incomplete.
#define IL2CPP_STAGE(set, method, hook)                                                                                \
  ([&](const ::il2cpp::Method& method_) { IL2CPP_STAGE_AT(set, method_.Pointer(), hook); }(method))

// Stages a hook on a native function, such as a runtime export. A null address marks the set incomplete.
#define IL2CPP_STAGE_AT(set, address, hook)                                                                            \
  ([&](void* target_) {                                                                                                \
    if (target_)                                                                                                       \
      (set).Stage(SPUD_AUTO_HOOK(target_, hook));                                                                      \
    else                                                                                                               \
      (set).Missing();                                                                                                 \
  }(reinterpret_cast<void*>(address)))
