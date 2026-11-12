#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace spud::detail
{

struct target_owner {
  uintptr_t requested_address;
  uintptr_t canonical_address;
  uintptr_t owner_token;
  uintptr_t replacement_address;
};

enum class target_claim_status {
  claimed,
  already_owned,
  conflict,
};

struct target_claim_result {
  target_claim_status status;
  target_owner        incumbent{};
};

class target_registry
{
public:
  target_claim_result claim(const target_owner &candidate);
  void                release(const target_owner &owner);
  size_t              size() const;

private:
  mutable std::mutex                          mutex_;
  std::unordered_map<uintptr_t, target_owner> targets_;
};

target_registry &detour_target_registry();

} // namespace spud::detail
