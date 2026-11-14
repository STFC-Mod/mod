#pragma once

#include "CanvasController.h"
#include "CanvasScaler.h"
#include "MonoSingleton.h"

struct UIFrameManager : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "UIFrameManager">;

  // The game clamps the width to the available space and the side frame's minimum. False when the call failed.
  bool ResizeSideFrame(float width)
  { return TryCall<"ResizeSideFrame", void(float)>(width); }
};

struct ScreenManager : MonoSingleton<ScreenManager> {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Client.UI", "ScreenManager">;

  static CanvasController* GetTopCanvas(bool visibleOnly = false)
  { return Class::Call<"GetTopCanvas", CanvasController*>(visibleOnly); }

  // Every registered canvas, visible or not.
  il2cpp::List<CanvasController*>* screens()
  { return Field<"_screens", il2cpp::List<CanvasController*>*>(); }
  CanvasScaler* canvasRootScaler()
  { return Field<"m_canvasRootScaler", CanvasScaler*>(); }
  float minimumCanvasScaleFactor()
  { return Field<"_minimumCanvasScaleFactor", float>(); }
  float maximumCanvasScaleFactor()
  { return Field<"_maximumCanvasScaleFactor", float>(); }
  UIFrameManager* frameManager()
  { return Field<"_frameManager", UIFrameManager*>(); }
};
