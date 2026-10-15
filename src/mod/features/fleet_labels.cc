// zoom_label_player_* and zoom_label_non_player_*: how much detail fleet labels in system view show. The game picks
// each label's level of detail from the camera's zoom; these settings override that per fleet kind. The camera's own
// hooks in camera.cc report the zoom here.

#include "config.h"
#include "features/features.h"

#include "game/navigation.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>

#include <optional>
#include <unordered_map>

namespace
{
// Fleet label detail. The game picks each fleet label's LOD from the camera's zoom level; the zoom_label_*
// settings override that per fleet kind: always expanded, always compact, or expanded below a zoom threshold.
class FleetLabels
{
public:
  bool installed = false;

  static bool Enabled()
  {
    const auto& config = Config::Get();
    return config.graphics.zoom_label_player.detail != FleetLabelDetail::kNative
           || config.graphics.zoom_label_non_player.detail != FleetLabelDetail::kNative;
  }

  // The level `lod` should show instead of `native`, or nothing when its fleet is not tracked or labels are native.
  std::optional<ZoomLevels> LevelFor(NavigationLOD* lod, ZoomLevels native) const
  {
    const auto* profile = Enabled() ? Profile(lod) : nullptr;
    if (!profile)
      return std::nullopt;
    // Expanded keeps Near where the game would show it, and lifts everything else to Middle.
    const auto expanded = native == ZoomLevels::Near ? ZoomLevels::Near : ZoomLevels::Middle;
    switch (profile->detail) {
      case FleetLabelDetail::kExpanded:
        return expanded;
      case FleetLabelDetail::kCompact:
        return ZoomLevels::Far;
      case FleetLabelDetail::kThreshold:
        if (!zoom_known_)
          return native;
        return ExpandedAt(profile->zoom_threshold) ? expanded : ZoomLevels::Far;
      default:
        return native;
    }
  }

  void Track(NavigationFleetWidget* widget)
  {
    // Fresh pooled widgets are enabled before BindDataContext assigns their context; forcing an LOD before then
    // fires OnLODChanged callbacks that dereference the unbound fleet data.
    auto* lod = widget && widget->Context() ? widget->lod() : nullptr;
    if (!lod)
      return;
    widgets_.insert_or_assign(lod, widget);
    if (zoom_known_ && Enabled())
      lod->UpdateLOD(zoom_level_);
  }

  void Untrack(NavigationFleetWidget* widget)
  {
    auto* lod = widget ? widget->lod() : nullptr;
    if (!lod)
      return;
    widgets_.erase(lod);
    // Give a reused widget back to the game at the game's own level.
    if (widget->Context() && zoom_known_ && Enabled()) {
      lod->SetTargetLevel(zoom_level_);
      lod->UpdateLOD(zoom_level_);
      lod->SetTargetLevel(zoom_level_);
    }
  }

  // The camera `zoom` is about to show `depth`. Labels only apply in a solar system, and a new camera starts over.
  void OnDepth(NavigationZoom* zoom, NodeDepth depth)
  {
    if (!installed)
      return;
    if (depth == NodeDepth::SolarSystem) {
      Follow(zoom);
    } else if (camera_ == zoom) {
      Forget();
    }
  }

  // After the camera's own update: picks up the zoom level and re-applies labels when they change.
  void OnZoomed(NavigationZoom* zoom)
  {
    if (!installed || zoom->depth() != NodeDepth::SolarSystem)
      return;
    Follow(zoom);
    const bool was_known = zoom_known_;
    zoom_level_          = zoom->zoomLevel();
    zoom_known_          = true;
    if (UsesThreshold()) {
      normalized_zoom_ = zoom->NormalizedZoom();
      UpdateThreshold();
    } else if (!was_known) {
      ApplyAll();
    }
  }

private:
  const FleetLabelProfile* Profile(NavigationLOD* lod) const
  {
    const auto it     = widgets_.find(lod);
    auto*      widget = it == widgets_.end() ? nullptr : it->second;
    auto*      fleet  = widget ? widget->Context() : nullptr;
    if (!fleet)
      return nullptr;
    // A missing FleetType property reads as Nonexistent, which counts as non-player.
    const auto& config = Config::Get();
    return fleet->FleetType() == DeployedFleetType::Player ? &config.graphics.zoom_label_player
                                                           : &config.graphics.zoom_label_non_player;
  }

  static bool UsesThreshold()
  {
    const auto& config = Config::Get();
    return config.graphics.zoom_label_player.detail == FleetLabelDetail::kThreshold
           || config.graphics.zoom_label_non_player.detail == FleetLabelDetail::kThreshold;
  }

  // Labels expand when zoomed in at least as far as the threshold. 0 never expands, 1 always does.
  bool ExpandedAt(float threshold) const
  { return threshold > 0.0f && (threshold >= 1.0f || normalized_zoom_ <= threshold); }

  void Follow(NavigationZoom* zoom)
  {
    if (camera_ != zoom) {
      Forget();
      camera_ = zoom;
    }
  }

  void Forget()
  {
    camera_          = nullptr;
    zoom_known_      = false;
    zoom_level_      = ZoomLevels::None;
    normalized_zoom_ = 0.0f;
    threshold_known_ = false;
  }

  // UpdateLOD goes through the hook, which picks each label's level.
  void ApplyAll()
  {
    if (!Enabled())
      return;
    for (const auto& [lod, widget] : widgets_)
      if (widget->Context())
        lod->UpdateLOD(zoom_level_);
  }

  // Re-applies labels only when a threshold profile crosses its threshold, not on every zoom step.
  void UpdateThreshold()
  {
    const auto& config     = Config::Get();
    const bool  player     = config.graphics.zoom_label_player.detail == FleetLabelDetail::kThreshold
                             && ExpandedAt(config.graphics.zoom_label_player.zoom_threshold);
    const bool  non_player = config.graphics.zoom_label_non_player.detail == FleetLabelDetail::kThreshold
                             && ExpandedAt(config.graphics.zoom_label_non_player.zoom_threshold);
    if (threshold_known_ && player == player_expanded_ && non_player == non_player_expanded_)
      return;
    threshold_known_     = true;
    player_expanded_     = player;
    non_player_expanded_ = non_player;
    ApplyAll();
#ifdef _MODDBG
    spdlog::info("Fleet label detail threshold state updated at normalized zoom {:.4f}", normalized_zoom_);
#endif
  }

  std::unordered_map<NavigationLOD*, NavigationFleetWidget*> widgets_;
  // The system camera the zoom state below belongs to.
  NavigationZoom* camera_              = nullptr;
  bool            zoom_known_          = false;
  ZoomLevels      zoom_level_          = ZoomLevels::None;
  float           normalized_zoom_     = 0.0f;
  bool            threshold_known_     = false;
  bool            player_expanded_     = false;
  bool            non_player_expanded_ = false;
} labels;

void NavigationLOD_UpdateLOD_Hook(auto original, NavigationLOD* _this, ZoomLevels level)
{
  const auto effective = labels.LevelFor(_this, level);
  if (!effective)
    return original(_this, level);
  // The target level is set on both sides of the update so the game's own bookkeeping agrees with ours.
  _this->SetTargetLevel(*effective);
  original(_this, *effective);
  _this->SetTargetLevel(*effective);
}

void NavigationFleetWidget_OnEnable_Hook(auto original, NavigationFleetWidget* _this)
{
  original(_this);
  labels.Track(_this);
}

void NavigationFleetWidget_OnDidBindContext_Hook(auto original, NavigationFleetWidget* _this)
{
  original(_this);
  labels.Track(_this);
}

void NavigationFleetWidget_OnDisable_Hook(auto original, NavigationFleetWidget* _this)
{
  labels.Untrack(_this);
  original(_this);
}

void NavigationFleetWidget_OnAboutToReleaseContext_Hook(auto original, NavigationFleetWidget* _this)
{
  labels.Untrack(_this);
  original(_this);
}

void InstallFleetLabels()
{
  // Installed as a set, whether or not zoom_label_* is set, so labels can be switched at runtime. Missing fields
  // and properties (_lod, Context, FleetType) read as null or zero, which the hooks treat as native labels, so
  // only the methods gate installation. The zoom reports come from camera.cc's NavigationZoom.Update hook.
  auto update     = NavigationZoom::Class::Method("Update");
  auto update_lod = NavigationLOD::Class::Method("UpdateLOD");
  auto on_enable  = NavigationFleetWidget::Class::Method("OnEnable");
  auto on_disable = NavigationFleetWidget::Class::Method("OnDisable");
  auto on_bind    = NavigationFleetWidget::Class::Method("OnDidBindContext");
  auto on_release = NavigationFleetWidget::Class::Method("OnAboutToReleaseContext");
  if (!update || !update_lod || !on_enable || !on_disable || !on_bind || !on_release) {
    spdlog::error("Fleet label detail hooks were not installed; using native fleet labels");
    return;
  }
  labels.installed = true;
  IL2CPP_HOOK(update_lod, NavigationLOD_UpdateLOD_Hook);
  IL2CPP_HOOK(on_enable, NavigationFleetWidget_OnEnable_Hook);
  IL2CPP_HOOK(on_disable, NavigationFleetWidget_OnDisable_Hook);
  IL2CPP_HOOK(on_bind, NavigationFleetWidget_OnDidBindContext_Hook);
  IL2CPP_HOOK(on_release, NavigationFleetWidget_OnAboutToReleaseContext_Hook);
}
} // namespace

void FleetLabelsFollowDepth(NavigationZoom* zoom, NodeDepth depth)
{ labels.OnDepth(zoom, depth); }

void FleetLabelsAfterZoom(NavigationZoom* zoom)
{ labels.OnZoomed(zoom); }

const Feature kFleetLabels{"FleetLabels", &Config::Patches::zoomhooks, InstallFleetLabels};
