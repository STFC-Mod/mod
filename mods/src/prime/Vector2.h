#pragma once

#include <il2cpp/il2cpp.h>

#include <compare>

struct Vector2 {
  float x, y;
};

#pragma pack(push, 1)
struct vec2 {
  float x, y;

  friend auto operator<=>(const vec2&, const vec2&) = default;
};
#pragma pack(pop)