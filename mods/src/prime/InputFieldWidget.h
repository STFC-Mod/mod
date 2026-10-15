#pragma once

#include "TMP_InputField.h"

#include <il2cpp/il2cpp.h>

struct InputFieldWidget : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "InputFieldWidget">;

  void Focus()
  { Call<"Focus">(); }

  TMP_InputField* input()
  { return Field<"_input", TMP_InputField*>(); }
  bool isActiveAndEnabled()
  { return Get<"isActiveAndEnabled", bool>(); }
};
