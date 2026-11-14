#pragma once

#include <cstdint>

namespace queue_address_guard
{
struct Address {
  bool         valid{};
  std::int64_t galaxy{}, system{}, planet{};
  int          instance{};
  bool         operator==(const Address&) const = default;
};

struct Evidence {
  bool    enabled{}, player_state_event{}, owned_queue{}, owned_deployment{}, planning_recall{};
  int     player_state{-1}, deployed_state{-1}, removal_reason{-1};
  Address reported, deployed, target;
};

// Restrict correction to the observed base-address substitution. The deployment
// must independently confirm that this ship and this target are still together.
inline bool UseDeployedAddress(const Evidence& e)
{
  const bool player_active = e.player_state == 1 || e.player_state == 64 || e.player_state == 512;
  const bool deployed_active =
      e.deployed_state == 0 || e.deployed_state == 1 || e.deployed_state == 3 || e.deployed_state == 6;
  return e.enabled && e.player_state_event && e.owned_queue && e.owned_deployment && !e.planning_recall
         && e.removal_reason == 0 && player_active && deployed_active && e.reported.valid && e.deployed.valid
         && e.target.valid && e.reported.planet > 0 && e.deployed.planet == 0 && e.reported.system != e.deployed.system
         && e.reported.galaxy == e.deployed.galaxy && e.reported.instance == e.deployed.instance
         && e.deployed == e.target;
}
} // namespace queue_address_guard
