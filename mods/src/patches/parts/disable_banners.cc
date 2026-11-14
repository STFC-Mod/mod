#include "config.h"
#include "patches/notification_service.h"

#include <il2cpp/il2cpp.h>
#include <prime/Toast.h>

#include <spud/detour.h>

struct ToastObserver : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.HUD", "ToastObserver">;
};

void ToastObserver_EnqueueToast_Hook(auto original, ToastObserver *_this, Toast *toast)
{
  notification_handle_toast(toast);

  if (std::ranges::find(Config::Get().disabled_banner_types, toast->get_State())
      != Config::Get().disabled_banner_types.end()) {
    return;
  }

  original(_this, toast);
}

void ToastObserver_EnqueueOrCombineToast_Hook(auto original, ToastObserver *_this, Toast *toast, uintptr_t cmpAction)
{
  notification_handle_toast(toast);

  if (std::ranges::find(Config::Get().disabled_banner_types, toast->get_State())
      != Config::Get().disabled_banner_types.end()) {
    return;
  }

  original(_this, toast, cmpAction);
}

void InstallToastBannerHooks()
{
  notification_init();

  if (auto m = ToastObserver::Class::Method("EnqueueToast"))
    SPUD_STATIC_DETOUR(m.Pointer(), ToastObserver_EnqueueToast_Hook);
  if (auto m = ToastObserver::Class::Method("EnqueueOrCombineToast"))
    SPUD_STATIC_DETOUR(m.Pointer(), ToastObserver_EnqueueOrCombineToast_Hook);
}
