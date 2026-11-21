#pragma once

// Battle journals from POST /journals/get. Since 2026-11-19 the game server answers with protobuf
// (ServiceResponse, entity group TYPE_BATTLEREPORT, JournalResponse) instead of JSON. The converter rebuilds the
// journal object the JSON server sent as response["journal"]: same keys, nesting and JSON types, with keys the wire
// no longer carries left out rather than invented, and "journal_src":"pb1" added. It uses only the generated
// protobuf code, nlohmann::json and the standard library, so the sync tests link it without the game.

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace data_sync
{
// True when the first non-whitespace byte is '{': a server that still answers with JSON.
bool IsJsonBody(std::string_view body);

// The journal object for a protobuf /journals/get body, or nullopt with a short reason in `error`. Never throws.
std::optional<nlohmann::json> JournalFromProtobuf(std::string_view body, std::string* error = nullptr);
} // namespace data_sync
