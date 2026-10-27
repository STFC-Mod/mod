#pragma once
#include <future>
#include <string>

// Absolute UTF-8 path, empty on cancel, exception on failure. Never blocks the game thread.
std::future<std::string> OpenImageFilePicker();
