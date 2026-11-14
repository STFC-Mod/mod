#pragma once

#include <il2cpp/il2cpp.h>

struct TMP_InputField : il2cpp::Object {
  using Class = il2cpp::Class<"Unity.TextMeshPro", "TMPro", "TMP_InputField">;

  bool isFocused()
  { return Get<"isFocused", bool>(); }

  void ActivateInputField()
  { Call<"ActivateInputField">(); }
  void DeactivateInputField(bool clearSelection = false)
  { Call<"DeactivateInputField">(clearSelection); }
  void SendOnFocusLost()
  { Call<"SendOnFocusLost">(); }
  void SendOnFocus()
  { Call<"SendOnFocus">(); }
};
