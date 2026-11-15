#include "ship_tile_click.h"

#include "config.h"
#include "errormsg.h"

#include "double_click_assign_ship.h"
#include "pinned_ship_click.h"
#include "prime/ShipTileWidget.h"

#include <il2cpp/il2cpp_helper.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

namespace {

void ShipTileWidget_HandleOnClick_Hook(auto original, ShipTileWidget* _this)
{
  original(_this);

  auto& cfg = Config::Get();
  if (!cfg.double_click_to_assign_ship && !cfg.installPinnedShipClickToggle) return;

  auto* ship = _this ? _this->Context : nullptr;
  if (!ship) return;

  PinShipClickOnTileClicked(ship);
  DoubleClickOnShipTileClicked(ship);
}

} // namespace

namespace ship_tile_click {

bool TryInstallHook()
{
  static bool installed = false;
  if (installed) {
    return true;
  }

  auto helper = ShipTileWidget::get_class_helper();
  if (!helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Prime.Ships", "ShipTileWidget");
    return false;
  }

  auto method = helper.GetMethod("HandleOnClick", 0);
  if (!method) {
    ErrorMsg::MissingMethod("ShipTileWidget", "HandleOnClick");
    return false;
  }

  installed = SPUD_STATIC_DETOUR(method, ShipTileWidget_HandleOnClick_Hook);
  if (installed) {
    spdlog::info("Ship tile click dispatcher: hooks installed");
  }
  return installed;
}

} // namespace ship_tile_click
