#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct InventoryForPopup : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Inventories", "InventoryForPopup">;

  int64_t MaxItemsToUse()
  { return Get<"MaxItemsToUse", int64_t>(); }
  void SetMaxItemsToUse(int64_t value)
  { Set<"MaxItemsToUse">(value); }
  bool IsDonationUse()
  { return Get<"IsDonationUse", bool>(); }
  void SetIsDonationUse(bool value)
  { Set<"IsDonationUse">(value); }
  bool IsChestPurchase()
  { return Get<"IsChestPurchase", bool>(); }
};
