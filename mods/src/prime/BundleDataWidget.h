#pragma once

#include "Bundle.h"
#include "Widget.h"

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct BundleDataWidget : Widget<Bundle> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "BundleDataWidget">;

  enum ItemState : int32_t {
    None               = 0,
    Package            = 1,
    Highlighted        = 2,
    LegendOn           = 4,
    ExpiryTimerOn      = 8,
    DiscountOn         = 16,     // 0x00000010
    RemoteImage        = 32,     // 0x00000020
    Showcase           = 256,    // 0x00000100
    HasBanner          = 512,    // 0x00000200
    ShowViewButton     = 1024,   // 0x00000400
    ShowMaxPurchases   = 2048,   // 0x00000800
    ShowItemRarity     = 4096,   // 0x00001000
    ShowItemType       = 8192,   // 0x00002000
    CooldownTimerOn    = 16384,  // 0x00004000
    ShowSynthetic      = 32768,  // 0x00008000
    ShowPurchaseLimits = 65536,  // 0x00010000
    ShowFreeTag        = 131072, // 0x00020000
  };

  ItemState CurrentState()
  { return Field<"_currentState", ItemState>(); }

  void AuxViewButtonPressedHandler()
  { Call<"AuxViewButtonPressedHandler">(); }
};
