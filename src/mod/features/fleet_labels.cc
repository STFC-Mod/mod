// [graphics] zoom_label_player_* and zoom_label_non_player_*: how much detail fleet labels in system view show. The
// game picks each label's level of detail from the camera's zoom; these settings override that per fleet kind. The
// camera's own hooks in camera.cc report the zoom here.

#include "config.h"
#include "features/features.h"

#include "game/navigation.h"

#include <il2cpp/hook.h>
#include <spdlog/spdlog.h>

#include <optional>
#include <unordered_map>

namespace
{
// The tracked fleet labels and the zoom state they follow. Each fleet kind is always expanded, always compact, or
// expanded below a zoom threshold.
class FleetLabels
{
public:
  // Set once every hook is in: an untracked OnDisable would leave a freed widget in the map.
  void MarkInstalled()
  { installed_ = true; }

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
        if (!camera_.zoom_known)
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
    auto* lod = widget->Context() ? widget->lod() : nullptr;
    if (!lod)
      return;
    widgets_.insert_or_assign(lod, widget);
    if (camera_.zoom_known && Enabled())
      lod->UpdateLOD(camera_.zoom_level);
  }

  void Untrack(NavigationFleetWidget* widget)
  {
    auto* lod = widget->lod();
    if (!lod)
      return;
    widgets_.erase(lod);
    // Give a reused widget back to the game at the game's own level.
    if (widget->Context() && camera_.zoom_known && Enabled()) {
      lod->SetTargetLevel(camera_.zoom_level);
      lod->UpdateLOD(camera_.zoom_level);
      lod->SetTargetLevel(camera_.zoom_level);
    }
  }

  // The camera `zoom` is about to show `depth`. Labels only apply in a solar system, and a new camera starts over.
  void OnDepth(NavigationZoom* zoom, NodeDepth depth)
  {
    if (!installed_)
      return;
    if (depth == NodeDepth::SolarSystem)
      Follow(zoom);
    else if (camera_.zoom == zoom)
      camera_ = {};
  }

  // After the camera's own update: picks up the zoom level and re-applies labels when they change.
  void OnZoomed(NavigationZoom* zoom)
  {
    if (!installed_ || zoom->depth() != NodeDepth::SolarSystem)
      return;
    Follow(zoom);
    const bool was_known = camera_.zoom_known;
    camera_.zoom_level   = zoom->zoomLevel();
    camera_.zoom_known   = true;
    if (UsesThreshold()) {
      camera_.normalized_zoom = zoom->NormalizedZoom();
      UpdateThreshold();
    } else if (!was_known) {
      ApplyAll();
    }
  }

private:
  const FleetLabelProfile* Profile(NavigationLOD* lod) const
  {
    const auto it    = widgets_.find(lod);
    auto*      fleet = it == widgets_.end() ? nullptr : it->second->Context();
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
  { return threshold > 0.0f && (threshold >= 1.0f || camera_.normalized_zoom <= threshold); }

  void Follow(NavigationZoom* zoom)
  {
    if (camera_.zoom != zoom)
      camera_ = {.zoom = zoom};
  }

  // UpdateLOD goes through the hook, which picks each label's level.
  void ApplyAll()
  {
    if (!Enabled())
      return;
    for (const auto& [lod, widget] : widgets_)
      if (widget->Context())
        lod->UpdateLOD(camera_.zoom_level);
  }

  // Re-applies labels only when a threshold profile crosses its threshold, not on every zoom step.
  void UpdateThreshold()
  {
    const auto& graphics = Config::Get().graphics;
    const auto  expanded = [this](const FleetLabelProfile& profile) {
      return profile.detail == FleetLabelDetail::kThreshold && ExpandedAt(profile.zoom_threshold);
    };
    const bool player     = expanded(graphics.zoom_label_player);
    const bool non_player = expanded(graphics.zoom_label_non_player);
    if (camera_.threshold_known && player == camera_.player_expanded && non_player == camera_.non_player_expanded)
      return;
    camera_.threshold_known     = true;
    camera_.player_expanded     = player;
    camera_.non_player_expanded = non_player;
    ApplyAll();
    spdlog::debug("[FleetLabels] threshold crossed at normalized zoom {:.4f}", camera_.normalized_zoom);
  }

  // What is known of the system camera's zoom. A new camera starts over.
  struct Camera {
    NavigationZoom* zoom            = nullptr;
    bool            zoom_known      = false;
    ZoomLevels      zoom_level      = ZoomLevels::None;
    float           normalized_zoom = 0.0f;
    // Whether player and non-player threshold labels were last expanded, once threshold_known.
    bool threshold_known     = false;
    bool player_expanded     = false;
    bool non_player_expanded = false;
  };

  bool                                                       installed_ = false;
  std::unordered_map<NavigationLOD*, NavigationFleetWidget*> widgets_;
  Camera                                                     camera_;
} fleet_labels;

// --- Hooks ---

void NavigationLOD_UpdateLOD_Hook(auto original, NavigationLOD* _this, ZoomLevels level)
{
  const auto effective = fleet_labels.LevelFor(_this, level);
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
  fleet_labels.Track(_this);
}

void NavigationFleetWidget_OnDidBindContext_Hook(auto original, NavigationFleetWidget* _this)
{
  original(_this);
  fleet_labels.Track(_this);
}

void NavigationFleetWidget_OnDisable_Hook(auto original, NavigationFleetWidget* _this)
{
  fleet_labels.Untrack(_this);
  original(_this);
}

void NavigationFleetWidget_OnAboutToReleaseContext_Hook(auto original, NavigationFleetWidget* _this)
{
  fleet_labels.Untrack(_this);
  original(_this);
}

bool InstallFleetLabels()
{
  // Installed whether or not zoom_label_* is set, so labels can be switched at runtime. Missing fields and
  // properties (_lod, Context, FleetType) read as null or zero, which the hooks treat as native labels. The zoom
  // reports come from camera.cc's NavigationZoom.Update hook, so that method has to exist too.
  if (!NavigationZoom::Class::Method("Update"))
    return false;
  il2cpp::HookSet hooks("FleetLabels");
  IL2CPP_STAGE(hooks, NavigationLOD::Class::Method("UpdateLOD"), NavigationLOD_UpdateLOD_Hook);
  IL2CPP_STAGE(hooks, NavigationFleetWidget::Class::Method("OnEnable"), NavigationFleetWidget_OnEnable_Hook);
  IL2CPP_STAGE(hooks, NavigationFleetWidget::Class::Method("OnDisable"), NavigationFleetWidget_OnDisable_Hook);
  IL2CPP_STAGE(hooks, NavigationFleetWidget::Class::Method("OnDidBindContext"),
               NavigationFleetWidget_OnDidBindContext_Hook);
  IL2CPP_STAGE(hooks, NavigationFleetWidget::Class::Method("OnAboutToReleaseContext"),
               NavigationFleetWidget_OnAboutToReleaseContext_Hook);
  // camera.cc's zoom hooks report here whether or not this installed; they only count once it has.
  if (!hooks.Commit())
    return false;
  fleet_labels.MarkInstalled();
  return true;
}
} // namespace

void FleetLabelsFollowDepth(NavigationZoom* zoom, NodeDepth depth)
{ fleet_labels.OnDepth(zoom, depth); }

void FleetLabelsAfterZoom(NavigationZoom* zoom)
{ fleet_labels.OnZoomed(zoom); }

const Feature kFleetLabels{"FleetLabels", &Config::Patches::zoomhooks, InstallFleetLabels};
