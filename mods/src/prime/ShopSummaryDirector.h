#pragma once

#include <il2cpp/il2cpp.h>

#include <cstdint>

struct ShopSummaryDirector : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Shop", "ShopSummaryDirector">;

  // SectionID[] declared on SectionDirectorBase; elements are the SectionID enum (Hub.h).
  il2cpp::Array<int32_t>* backLogicSkipSectionIds()
  { return Field<"_backLogicSkipSectionIds", il2cpp::Array<int32_t>*>(); }
};
