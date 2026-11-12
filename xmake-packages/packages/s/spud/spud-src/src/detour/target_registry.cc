#include "target_registry.h"

#include <array>

namespace spud::detail
{

namespace
{
  std::array<uintptr_t, 2> target_keys(const target_owner &owner)
  { return {owner.requested_address, owner.canonical_address}; }
} // namespace

target_claim_result target_registry::claim(const target_owner &candidate)
{
  std::lock_guard lock(mutex_);
  bool            owner_seen = false;

  for (const auto key : target_keys(candidate)) {
    const auto entry = targets_.find(key);
    if (entry == targets_.end()) {
      continue;
    }
    if (entry->second.owner_token != candidate.owner_token) {
      return {target_claim_status::conflict, entry->second};
    }
    owner_seen = true;
  }
  if (owner_seen) {
    return {target_claim_status::already_owned, {}};
  }

  std::array<uintptr_t, 2> inserted{};
  size_t                   inserted_count = 0;
  try {
    for (const auto key : target_keys(candidate)) {
      if (targets_.contains(key)) {
        continue;
      }
      targets_.emplace(key, candidate);
      inserted[inserted_count++] = key;
    }
  } catch (...) {
    for (size_t index = 0; index < inserted_count; ++index) {
      targets_.erase(inserted[index]);
    }
    throw;
  }

  return {target_claim_status::claimed, {}};
}

void target_registry::release(const target_owner &owner)
{
  std::lock_guard lock(mutex_);
  for (const auto key : target_keys(owner)) {
    const auto entry = targets_.find(key);
    if (entry != targets_.end() && entry->second.owner_token == owner.owner_token) {
      targets_.erase(entry);
    }
  }
}

size_t target_registry::size() const
{
  std::lock_guard lock(mutex_);
  return targets_.size();
}

target_registry &detour_target_registry()
{
  // Detours can be destroyed during static teardown, so the registry must
  // intentionally outlive every static detour object.
  static auto *registry = new target_registry();
  return *registry;
}

} // namespace spud::detail
