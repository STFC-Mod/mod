// The mod's loading screens, from [graphics] loader_*: the login background and logos, the transition screen between
// sections, and the loading tips. They share the embedded images and reset together when the game reloads.

#include "config.h"
#include "features/features.h"
#include "version.h"

#include "game/loading.h"
#include "game/ui.h"

#include <il2cpp/hook.h>
#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <unity/ui.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string_view>
#include <utility>
#include <vector>

// Generated into the Zig build cache from assets/ (or -Dbg_image).
#include "embedded_cc_logo_image.h"
#include "embedded_loading_image.h"
#include "embedded_logo_image.h"

namespace
{
void ShowSprite(unity::Image* image, unity::Sprite* sprite, bool preserve_aspect)
{
  image->SetSprite(sprite);
  image->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
  image->SetType(unity::Image::Type::Simple);
  image->SetPreserveAspect(preserve_aspect);
  image->SetRaycastTarget(false);
}

void HideImage(unity::Image* image)
{
  if (image)
    image->SetColor({0.0f, 0.0f, 0.0f, 0.0f});
}

// Decodes `data` into `asset` unless it is still loaded; warns once per failed decode.
unity::ImageAsset* Load(unity::ImageAsset& asset, const uint8_t* data, size_t size, const char* name)
{
  if (asset.Load(data, size))
    return &asset;
  spdlog::warn("[LoadingScreens] could not load the {} image", name);
  return nullptr;
}

struct Assets {
  unity::ImageAsset background, logo, community_logo;

  // The configured loader_image, or the embedded one when it is unset or unreadable. Called from hooks, so it
  // never throws.
  unity::ImageAsset* Background()
  {
    if (background.Texture())
      return &background;
    const auto&     path = Config::Get().graphics.loader_image;
    std::error_code error;
    if (!path.empty() && std::filesystem::exists(path, error)) {
      std::ifstream        file(path, std::ios::binary);
      std::vector<uint8_t> data(std::istreambuf_iterator<char>(file), {});
      if (!data.empty() && Load(background, data.data(), data.size(), "custom loading background"))
        return &background;
    }
    return Load(background, g_embeddedLoadingImage, g_embeddedLoadingImage_SIZE, "embedded loading background");
  }

  unity::ImageAsset* Logo()
  { return Load(logo, g_embeddedLogoImage, g_embeddedLogoImage_SIZE, "mod logo"); }

  unity::ImageAsset* CommunityLogo()
  { return Load(community_logo, g_embeddedCCLogoImage, g_embeddedCCLogoImage_SIZE, "community logo"); }
};
// Leaked on purpose: its GC handles must not be released by static destructors after the runtime is gone.
Assets& assets = *new Assets;

unity::GameObject* CreateOverlay(const char* name, unity::ImageAsset& asset, unity::Transform* parent,
                                 unity::Vector2 anchor_min, unity::Vector2 anchor_max, bool preserve_aspect,
                                 bool behind_siblings)
{
  auto* sprite    = parent ? asset.Sprite() : nullptr;
  auto* object    = sprite ? unity::GameObject::Create(name) : nullptr;
  auto* transform = object ? object->GetComponent<unity::Transform>() : nullptr;
  if (!transform)
    return nullptr;
  // Attached to the canvas root, so the overlay covers the whole screen.
  transform->SetParent(unity::CanvasRoot(parent), false);
  if (behind_siblings)
    transform->SetAsFirstSibling();

  // Adding a Graphic turns the plain Transform into a RectTransform.
  auto* image = object->AddComponent<unity::Image>();
  auto* rect  = image ? object->GetComponent<unity::RectTransform>() : nullptr;
  if (!rect)
    return nullptr;
  rect->Place(anchor_min, anchor_max, {0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f});
  ShowSprite(image, sprite, preserve_aspect);
  return object;
}

enum class Corner { kBottomLeft, kBottomRight };

// A logo in a bottom corner, sized relative to a 1920-wide screen and scaled by loader_logo_scale.
bool CreateLogo(const char* name, unity::ImageAsset* asset, unity::Transform* parent, Corner corner)
{
  if (!asset || !parent)
    return false;

  float width  = static_cast<float>(unity::Screen::width());
  float height = static_cast<float>(unity::Screen::height());
  if (width <= 0.0f)
    width = 1334.0f;
  if (height <= 0.0f)
    height = 750.0f;

  const float scale       = width / 1920.0f * Config::Get().graphics.loader_logo_scale;
  const float logo_width  = 150.0f * scale;
  const float logo_height = logo_width * asset->AspectRatio();
  const float pad_x       = 30.0f * scale;
  const float pad_y       = 185.0f * scale;
  const float left        = corner == Corner::kBottomLeft ? pad_x : width - pad_x - logo_width;

  return CreateOverlay(name, *asset, parent, {left / width, pad_y / height},
                       {(left + logo_width) / width, (pad_y + logo_height) / height}, true, false);
}

// The mod logo on the right, the community logo on the left.
struct Logos {
  bool logo           = false;
  bool community_logo = false;

  // Creates whichever is still missing.
  void Create(unity::Transform* parent)
  {
    logo           = logo || CreateLogo("STFCModLogo", assets.Logo(), parent, Corner::kBottomRight);
    community_logo = community_logo || CreateLogo("STFCCCLogo", assets.CommunityLogo(), parent, Corner::kBottomLeft);
  }
};

// --- Login sequence ---

// The login screen's background image: the first child Image with a known background name, or else the first
// Image found at all, searching four levels deep.
unity::Image* FindLoginBackground(unity::Transform* transform, int depth, unity::Image*& first)
{
  static constexpr std::string_view kNames[] = {"Background", "BG",       "bg_image", "LoadingBackground",
                                                "LoginBG",    "SplashBG", "Image"};
  if (!transform || depth > 4)
    return nullptr;
  for (int32_t i = 0; i < transform->childCount(); ++i) {
    auto* child  = transform->GetChild(i);
    auto* object = child ? child->gameObject() : nullptr;
    if (!object)
      continue;
    if (auto* image = object->GetComponent<unity::Image>()) {
      if (!first)
        first = image;
      if (std::ranges::find(kNames, object->Name()) != std::end(kNames))
        return image;
    }
    if (auto* found = FindLoginBackground(child, depth + 1, first))
      return found;
  }
  return nullptr;
}

void LoginSequence_Awake_Hook(auto original, LoginSequence* _this)
{
  original(_this);
  if (!Config::Get().graphics.loader_enabled)
    return;

  auto* background = assets.Background();
  auto* canvas     = background ? _this->mainCanvas() : nullptr;
  auto* root       = canvas ? canvas->transform() : nullptr;
  if (!root)
    return;

  unity::Image* first = nullptr;
  auto*         image = FindLoginBackground(root, 0, first);
  if (!image)
    image = first;
  if (!image)
    return;

#ifndef _USE_ORIGINAL_BG
  if (auto* sprite = background->Sprite()) {
    image->SetOverrideSprite(sprite);
    ShowSprite(image, sprite, false);
    image->SetVerticesDirty();
  }
#endif
  Logos{}.Create(image->transform());
}

// --- Loading tips ---

struct Tip {
  const char* text;
  int         weight;
};

constexpr Tip kTips[] = {
    {"Did you know? You can customize zoom presets, hotkeys, and UI scale in the community mod settings file to suit "
     "your playstyle.",
     15},
    {"The community mod supports custom loading screens, transition backgrounds, and borderless fullscreen mode for a "
     "better experience.",
     10},
    {"Press F11 at any time to toggle between borderless fullscreen and windowed mode without restarting the game or "
     "losing your session.",
     15},
    {"Visit https://stfc.pro to track player rankings and stats across over 236,000 players and 114 servers in Star "
     "Trek Fleet Command.",
     50},
    {"Join the community Discord server to report bugs, request new features, and stay up to date with the latest mod "
     "releases and updates.",
     10},
};

// The first loading screen of a session gets the welcome line. After that, half the transition screens keep the
// game's own tip and the rest get one of ours by weight, never the same one twice in a row.
struct Tips {
  bool         first_screen = true;
  size_t       last         = std::size(kTips);
  std::mt19937 random{std::random_device{}()};

  const char* Next()
  {
    if (std::exchange(first_screen, false))
      return "Welcome to Star Trek Fleet Command, Supported by the Community Mod "
             "v" VER_FILE_VERSION_STR VERSION_PATCH_STR "! Please check our discord for the latest information!";
    if (std::bernoulli_distribution(0.5)(random))
      return nullptr;

    std::array<int, std::size(kTips)> weights{};
    for (size_t i = 0; i < weights.size(); ++i)
      weights[i] = i == last ? 0 : kTips[i].weight;
    last = std::discrete_distribution<size_t>(weights.begin(), weights.end())(random);
    return kTips[last].text;
  }

  void Reset()
  {
    first_screen = true;
    last         = std::size(kTips);
  }
} tips;

void LoadingTipViewController_SetRandomTipLocalisedText_Hook(auto original, LoadingTipViewController* _this)
{
  original(_this);
  if (!Config::Get().graphics.loader_tip_enabled)
    return;
  auto* label = _this->textMeshPro();
  if (const char* tip = tips.Next(); tip && label)
    label->SetText(tip);
}

// --- Transition screen ---

// What has been done to the current TransitionViewController. Its Unity objects are looked up again on each
// attempt rather than kept, since nothing roots them between hooks. A new controller or a reload starts over.
struct Transition {
  bool  customized = false;
  bool  overlay    = false;
  Logos logos;
} transition;

// The transition background: _staticOverride, or the Image on BGContainer's first child (whose transform is
// returned in `box` so it can be stretched).
unity::Image* FindTransitionBackground(TransitionViewController* controller, unity::Transform*& box)
{
  if (auto* image = controller->staticOverride())
    return image;
  auto* root = controller->transform();
  for (int32_t i = 0; root && i < root->childCount(); ++i) {
    auto* child  = root->GetChild(i);
    auto* object = child ? child->gameObject() : nullptr;
    if (!object || object->Name() != "BGContainer")
      continue;
    box          = child->childCount() > 0 ? child->GetChild(0) : nullptr;
    auto* inside = box ? box->gameObject() : nullptr;
    return inside ? inside->GetComponent<unity::Image>() : nullptr;
  }
  return nullptr;
}

// Moves the game logo to the top-right corner and the tip text below centre, out of the way of the background.
void ArrangeTransitionLayout(TransitionViewController* controller)
{
  auto* root = controller->transform();
  for (int32_t i = 0; root && i < root->childCount(); ++i) {
    auto* child  = root->GetChild(i);
    auto* object = child ? child->gameObject() : nullptr;
    if (!object)
      continue;
    const auto name = object->Name();
    auto*      rect = object->GetComponent<unity::RectTransform>();
    if (rect && name == "LogoContainer")
      rect->Place({1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f}, {586.0f, 248.0f}, {-20.0f, -20.0f});
    else if (rect && name == "LoadingTipsContainer")
      rect->Place({0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {1024.0f, 100.0f}, {0.0f, -320.0f});
  }
}

// Runs from Awake and again from AboutToShow until it succeeds.
void CustomizeTransition(TransitionViewController* controller)
{
  unity::Transform* box        = nullptr;
  auto*             background = FindTransitionBackground(controller, box);
  if (!background)
    return;
  auto* parent = box ? box : background->transform();

  // The canvas animation would fade our overlay; AboutToHide switches it back on for the hide animation.
  if (auto* animator = controller->animator())
    animator->SetEnabled(false);

  if (!Config::Get().graphics.loader_transition_black) {
#ifndef _USE_ORIGINAL_BG
    if (parent && !transition.overlay)
      if (auto* image = assets.Background())
        transition.overlay =
            CreateOverlay("STFCModBG", *image, parent, {0.0f, 0.0f}, {1.0f, 1.0f}, false, true) != nullptr;
    // Keep the game's background unless the replacement is fully attached, so a failure is visible but harmless.
    if (!transition.overlay) {
      spdlog::warn("[TransitionScreen] the custom background is unavailable; keeping the game's");
      return;
    }
    HideImage(background);
    if (box) {
      box->As<unity::RectTransform>()->Place({0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f});
      box->SetLocalEulerAngles({0, 0, 0});
      box->SetLocalScale({1, 1, 1});
    }
#endif
  }

  if (parent)
    transition.logos.Create(parent);
  if (!Config::Get().graphics.loader_transition_black)
    ArrangeTransitionLayout(controller);
  transition.customized = true;
}

void TransitionViewController_Awake_Hook(auto original, TransitionViewController* _this)
{
  original(_this);
  if (!Config::Get().graphics.loader_transition)
    return;
  transition = {};
  CustomizeTransition(_this);
}

void TransitionViewController_AboutToShow_Hook(auto original, TransitionViewController* _this)
{
  original(_this);
  if (Config::Get().graphics.loader_transition && !transition.customized)
    CustomizeTransition(_this);
}

void TransitionViewController_AboutToHide_Hook(auto original, TransitionViewController* _this)
{
  if (auto* animator = _this->animator(); animator && Config::Get().graphics.loader_transition)
    animator->SetEnabled(true);
  original(_this);
}

void SlideShowViewController_ShowCurrentSlide_Hook(auto original, SlideShowViewController* _this)
{
  original(_this);
  const auto& config = Config::Get();
  if (config.graphics.loader_transition && !config.graphics.loader_transition_black)
    HideImage(_this->image());
}

// The hierarchy holding our overlays is destroyed during a reload; forget it so Awake does not touch dead objects.
void MonoSingleton_PrepareAllForReload_Hook(auto original)
{
  transition = {};
  tips.Reset();
  original();
}

// Shared by the login, tip and transition installs, whichever of them run, so it is its own set, installed on the
// first call.
void InstallReloadReset()
{
  static const bool installed = [] {
    il2cpp::HookSet hooks("LoadingScreens");
    IL2CPP_STAGE(hooks, MonoSingletonBase::Method("PrepareAllForReload"), MonoSingleton_PrepareAllForReload_Hook);
    return hooks.Commit();
  }();
  (void)installed;
}

bool InstallLoginScreen()
{
  InstallReloadReset();
  il2cpp::HookSet hooks("LoginScreen");
  IL2CPP_STAGE(hooks, LoginSequence::Class::Method("Awake"), LoginSequence_Awake_Hook);
  return hooks.Commit();
}

bool InstallTransitionScreen()
{
  InstallReloadReset();
  il2cpp::HookSet hooks("TransitionScreen");
  IL2CPP_STAGE(hooks, TransitionViewController::Class::Method("Awake"), TransitionViewController_Awake_Hook);
  IL2CPP_STAGE(hooks, TransitionViewController::Class::Method("AboutToShow"),
               TransitionViewController_AboutToShow_Hook);
  IL2CPP_STAGE(hooks, TransitionViewController::Class::Method("AboutToHide"),
               TransitionViewController_AboutToHide_Hook);
  IL2CPP_STAGE(hooks, SlideShowViewController::Class::Method("ShowCurrentSlide"),
               SlideShowViewController_ShowCurrentSlide_Hook);
  return hooks.Commit();
}

bool InstallLoadingTips()
{
  InstallReloadReset();
  il2cpp::HookSet hooks("LoadingTips");
  IL2CPP_STAGE(hooks, LoadingTipViewController::Class::Method("SetRandomTipLocalisedText"),
               LoadingTipViewController_SetRandomTipLocalisedText_Hook);
  return hooks.Commit();
}
} // namespace

const Feature kLoginScreen{"LoginScreen", &Config::Patches::loadingscreenhooks, InstallLoginScreen};
const Feature kTransitionScreen{"TransitionScreen", &Config::Patches::transitionscreenhooks, InstallTransitionScreen};
const Feature kLoadingTips{"LoadingTips", &Config::Patches::loadingtiphooks, InstallLoadingTips};
