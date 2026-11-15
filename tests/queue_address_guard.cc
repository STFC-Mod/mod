#include "patches/queue_address_guard_policy.h"
#include <cassert>
#include <iostream>

int main()
{
  using namespace queue_address_guard;
  const Address  base{true, 0, 71088, 71237, 710};
  const Address  system{true, 0, 925162490, 0, 710};
  const Evidence reproduced{true, true, true, true, false, 512, 1, 0, base, system, system};
  assert(UseDeployedAddress(reproduced));
  for (int state : {1, 64, 512}) {
    auto e         = reproduced;
    e.player_state = state;
    assert(UseDeployedAddress(e));
  }
  // Destruction, dock/repair, and an intentional warp must retain native clearing.
  for (int state : {-1, 0, 2, 4, 8, 16, 32, 128, 256}) {
    auto e         = reproduced;
    e.player_state = state;
    assert(!UseDeployedAddress(e));
  }
  for (int state : {-1, 2, 4, 5, 99}) {
    auto e           = reproduced;
    e.deployed_state = state;
    assert(!UseDeployedAddress(e));
  }
  for (int reason : {-1, 1, 2, 3, 4, 5}) {
    auto e           = reproduced;
    e.removal_reason = reason;
    assert(!UseDeployedAddress(e));
  }
  auto reject = [](Evidence e) { assert(!UseDeployedAddress(e)); };
  auto e      = reproduced;
  e.enabled   = false;
  reject(e);
  e                    = reproduced;
  e.player_state_event = false;
  reject(e); // manual clear/UI path
  e             = reproduced;
  e.owned_queue = false;
  reject(e);
  e                  = reproduced;
  e.owned_deployment = false;
  reject(e); // wrong fleet or unknown ownership
  e                 = reproduced;
  e.planning_recall = true;
  reject(e);
  e                = reproduced;
  e.reported.valid = false;
  reject(e);
  e                = reproduced;
  e.deployed.valid = false;
  reject(e);
  e              = reproduced;
  e.target.valid = false;
  reject(e);
  e               = reproduced;
  e.target.system = 123;
  reject(e); // real foreign target
  e                 = reproduced;
  e.target.instance = 711;
  reject(e);
  e               = reproduced;
  e.target.planet = 123;
  reject(e);
  e                 = reproduced;
  e.reported.planet = 0;
  reject(e); // scope is the observed base substitution
  e                 = reproduced;
  e.reported.system = system.system;
  reject(e);
  e                   = reproduced;
  e.reported.instance = 711;
  reject(e);
  e                 = reproduced;
  e.reported.galaxy = 1;
  reject(e);
  e          = reproduced;
  e.deployed = base;
  reject(e); // own deployment really moved to base
  e          = reproduced;
  e.reported = system;
  reject(e); // no wrong address to correct
  std::cout << "Kirshara address guard policy tests passed\n";
}
