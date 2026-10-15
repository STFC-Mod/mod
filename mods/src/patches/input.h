#pragma once

// Keyboard and mouse state from Unity's legacy Input API, read once per frame and cached.

#include <prime/KeyCode.h>

namespace Key
{
// Held during this frame.
bool Pressed(KeyCode key);
// Went down this frame.
bool Down(KeyCode key);

// Any of Ctrl, Alt, AltGr, Shift, Cmd, or Win is held.
bool IsModified();
bool HasShift();
bool HasAlt();
bool HasCtrl();

// A TextMeshPro input field has keyboard focus, so typing must not trigger shortcuts.
bool IsInputFocused();
// The game only clears a focused field when its back button is clicked; Escape needs this.
void ClearInputFocus();

// A modified shortcut on an arrow key owns that arrow until it is released, so CTRL-DOWN does not
// also pan the camera on the following frames.
void ClaimDirectionalInput(KeyCode key);
bool IsDirectionalInputClaimed();
} // namespace Key
