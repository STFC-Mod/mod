#include "ship_tile_click.h"

#include "config.h"

#include "double_click_assign_ship.h"
#include "game/fleet.h"
#include "pinned_ship_click.h"

#include <il2cpp/il2cpp.h>

#include <il2cpp/hook.h>
#include <spdlog/spdlog.h>
#include <unity/input.h>

namespace
{

void ShipTileWidget_HandleOnClick_Hook(auto original, ShipTileWidget* _this)
{
  original(_this);
  unity::Key::Poll();

  auto& cfg = Config::Get();
  if (!cfg.ui.double_click_to_assign_ship && !cfg.patches.pinnedshipclicktoggle)
    return;

  auto* ship = _this ? _this->Context() : nullptr;
  if (!ship)
    return;

  PinShipClickOnTileClicked(ship);
  DoubleClickOnShipTileClicked(ship);
}

} // namespace

namespace ship_tile_click
{

bool TryInstallHook()
{
  static bool installed = false;
  if (installed) {
    return true;
  }

  auto method = ShipTileWidget::Class::Method("HandleOnClick", 0);
  if (!method) {
    return false;
  }
  installed = IL2CPP_HOOK(method, ShipTileWidget_HandleOnClick_Hook);
  if (installed) {
    spdlog::info("Ship tile click dispatcher: hooks installed");
  }
  return installed;
}

} // namespace ship_tile_click
