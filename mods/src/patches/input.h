#pragma once

// Keyboard and mouse state from Unity's legacy Input API.

#include <prime/KeyCode.h>

namespace Key
{
// Starts a frame's reads. Every hook that reads keys calls it first, since the frame tick and the camera hooks
// run in either order; reads before it see the previous frame. Cheap after the first call in a frame.
void Poll();

// Held during this frame.
bool Pressed(KeyCode key);
// Went down this frame.
bool Down(KeyCode key);
// Any of Ctrl, Alt, AltGr, Shift, Cmd, or Win is held.
bool IsModified();

// A TextMeshPro input field has keyboard focus, so typing must not trigger shortcuts.
bool IsInputFocused();
// The game only clears a focused field when its back button is clicked; Escape needs this.
void ClearInputFocus();
} // namespace Key
