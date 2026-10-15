// The mod's loading screens: the login background and logos, the transition screen between sections, and the
// loading tips. They share the embedded images and reset together when the game reloads.

#include "config.h"
#include "patches/patches.h"
#include "version.h"

#include <prime/ui.h>
#include <prime/unity.h>

#include <il2cpp/il2cpp.h>
#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string_view>
#include <vector>

// Generated into the Zig build cache from assets/ (or -Dbg_image).
#include "embedded_cc_logo_image.h"
#include "embedded_loading_image.h"
#include "embedded_logo_image.h"

namespace
{
struct Rect {
  float x, y, width, height;
};

struct Texture2D : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Texture2D">;
};

struct Behaviour : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Behaviour">;

  void SetEnabled(bool value)
  { Set<"enabled">(value); }
};

struct TMP_Text : il2cpp::Object {
  using Class = il2cpp::Class<"Unity.TextMeshPro", "TMPro", "TMP_Text">;

  void SetText(Il2CppString* value)
  { Set<"text">(value); }
};

struct LoginSequence : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Login", "LoginSequence">;

  Component* mainCanvas()
  { return Field<"_mainCanvas", Component*>(); }
};

struct LoadingTipViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.LoadingScreen", "LoadingTipViewController">;

  TMP_Text* textMeshPro()
  { return Field<"_textMeshPro", TMP_Text*>(); }
};

struct TransitionViewController : Component {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.LoadingScreen", "TransitionViewController">;

  Image* staticOverride()
  { return Field<"_staticOverride", Image*>(); }
  // The canvas Animator; Behaviour is enough to toggle it.
  Behaviour* animator()
  { return Field<"_animator", Behaviour*>(); }
};

struct SlideShowViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.SlideShow", "SlideShowViewController">;

  Image* image()
  { return Field<"_image", Image*>(); }
};

using ImageConversion = il2cpp::Class<"UnityEngine.ImageConversionModule", "UnityEngine", "ImageConversion">;
using Screen          = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Screen">;
using Byte            = il2cpp::Class<"mscorlib", "System", "Byte">;
// The non-generic MonoSingleton base; its static PrepareAllForReload runs before a reload.
using MonoSingletonBase = il2cpp::Class<"Assembly-CSharp", "", "MonoSingleton">;

void SetRect(RectTransform* rect, Vector2 anchor_min, Vector2 anchor_max, Vector2 pivot, Vector2 size, Vector2 position)
{
  if (!rect)
    return;
  rect->SetAnchorMin(anchor_min);
  rect->SetAnchorMax(anchor_max);
  rect->SetPivot(pivot);
  rect->SetSizeDelta(size);
  rect->SetAnchoredPosition(position);
}

void ShowSprite(Image* image, Sprite* sprite, bool preserve_aspect)
{
  image->SetSprite(sprite);
  image->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
  image->SetType(Image::Type::Simple);
  image->SetPreserveAspect(preserve_aspect);
  image->SetRaycastTarget(false);
}

void HideImage(Image* image)
{
  if (image)
    image->SetColor({0.0f, 0.0f, 0.0f, 0.0f});
}

// The width and height from a PNG's IHDR chunk, if `data` is a PNG.
bool PngSize(const uint8_t* data, size_t size, int32_t& width, int32_t& height)
{
  static constexpr uint8_t kSignature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (!data || size < 24 || std::memcmp(data, kSignature, sizeof(kSignature)) != 0
      || std::memcmp(data + 12, "IHDR", 4) != 0)
    return false;
  const auto big_endian = [](const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | uint32_t{p[3]};
  };
  const auto w = big_endian(data + 16), h = big_endian(data + 20);
  if (w == 0 || h == 0 || w > INT32_MAX || h > INT32_MAX)
    return false;
  width  = static_cast<int32_t>(w);
  height = static_cast<int32_t>(h);
  return true;
}

Texture2D* LoadTexture(const uint8_t* data, size_t size)
{
  auto* bytes = data && size ? il2cpp::Array<uint8_t>::New(Byte::Raw(), size) : nullptr;
  if (!bytes)
    return nullptr;
  std::memcpy(bytes->data(), data, size);
  // Texture2D(width, height, TextureFormat.RGBA32, mipChain); LoadImage resizes it.
  auto* texture = il2cpp::New<Texture2D>(int32_t{2}, int32_t{2}, int32_t{4}, false);
  return texture && ImageConversion::Call<"LoadImage", bool>(texture, bytes, false) ? texture : nullptr;
}

// One image the mod shows. Each LoginSequence starts a new generation of Unity objects, so the texture and sprite
// are rooted until the next one and then released.
class ImageAsset
{
public:
  bool Load(const uint8_t* data, size_t size, int32_t width, int32_t height, const char* name)
  {
    if (Texture())
      return true;
    name_   = name;
    width_  = width;
    height_ = height;
    if (!PngSize(data, size, width_, height_))
      spdlog::warn("[LS] {} is not a supported PNG; using fallback dimensions {}x{}", name, width_, height_);
    texture_ = il2cpp::GcHandle::Strong(LoadTexture(data, size));
    if (!texture_)
      spdlog::warn("[LS] failed to load and root {} texture", name);
    return static_cast<bool>(texture_);
  }

  Texture2D* Texture() const
  { return texture_.Get<Texture2D>(); }

  ::Sprite* Sprite()
  {
    if (sprite_)
      return sprite_.Get<::Sprite>();
    auto* texture = Texture();
    if (!texture || width_ <= 0 || height_ <= 0)
      return nullptr;
    const Rect rect{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)};
    auto*      sprite = ::Sprite::Class::Call<"Create", ::Sprite*>(texture, rect, Vector2{0.5f, 0.5f});
    if (!sprite) {
      spdlog::warn("[LS] failed to create {} sprite", name_);
      return nullptr;
    }
    sprite_ = il2cpp::GcHandle::Strong(sprite);
    return sprite;
  }

  float AspectRatio() const
  { return width_ > 0 ? static_cast<float>(height_) / static_cast<float>(width_) : 1.0f; }

  void Reset()
  {
    sprite_.Reset();
    texture_.Reset();
  }

private:
  il2cpp::GcHandle texture_;
  il2cpp::GcHandle sprite_;
  int32_t          width_  = 0;
  int32_t          height_ = 0;
  const char*      name_   = "loading asset";
};

struct Assets {
  ImageAsset background, logo, community_logo;

  void Reset()
  {
    background.Reset();
    logo.Reset();
    community_logo.Reset();
  }

  // The configured loader_image, or the embedded one when it is unset or unreadable. Called from hooks, so it
  // never throws.
  ImageAsset* Background()
  {
    if (background.Texture())
      return &background;
    const auto&     path = Config::Get().loader_image;
    std::error_code error;
    if (!path.empty() && std::filesystem::exists(path, error)) {
      std::ifstream        file(path, std::ios::binary);
      std::vector<uint8_t> data(std::istreambuf_iterator<char>(file), {});
      if (!data.empty() && background.Load(data.data(), data.size(), 1334, 750, "custom loading background"))
        return &background;
    }
    return background.Load(g_embeddedLoadingImage, g_embeddedLoadingImage_SIZE, 1334, 750,
                           "embedded loading background")
               ? &background
               : nullptr;
  }

  ImageAsset* Logo()
  { return logo.Load(g_embeddedLogoImage, g_embeddedLogoImage_SIZE, 600, 828, "mod logo") ? &logo : nullptr; }

  ImageAsset* CommunityLogo()
  {
    return community_logo.Load(g_embeddedCCLogoImage, g_embeddedCCLogoImage_SIZE, 600, 815, "community logo")
               ? &community_logo
               : nullptr;
  }
} assets;

// Overlays attach to the nearest ancestor with a Canvas so they cover the whole screen.
Transform* RootCanvas(Transform* transform)
{
  for (auto* current = transform; current; current = current->parent())
    if (auto* object = current->gameObject(); object && object->GetComponent<Canvas>())
      return current;
  return transform;
}

GameObject* CreateOverlay(const char* name, ImageAsset& asset, Transform* parent, Vector2 anchor_min,
                          Vector2 anchor_max, bool preserve_aspect, bool behind_siblings)
{
  auto* sprite    = parent ? asset.Sprite() : nullptr;
  auto* object    = sprite ? il2cpp::New<GameObject>(il2cpp_string_new(name)) : nullptr;
  auto* transform = object ? object->GetComponent<Transform>() : nullptr;
  if (!transform)
    return nullptr;
  transform->SetParent(RootCanvas(parent), false);
  if (behind_siblings)
    transform->SetAsFirstSibling();

  // Adding a Graphic turns the plain Transform into a RectTransform.
  auto* image = object->AddComponent<Image>();
  auto* rect  = image ? object->GetComponent<RectTransform>() : nullptr;
  if (!rect)
    return nullptr;
  SetRect(rect, anchor_min, anchor_max, {0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f});
  ShowSprite(image, sprite, preserve_aspect);
  return object;
}

enum class Corner { kBottomLeft, kBottomRight };

// A logo in a bottom corner, sized relative to a 1920-wide screen and scaled by loader_logo_scale. Does nothing
// if `existing` is already set.
void CreateLogo(const char* name, ImageAsset* asset, Transform* parent, Corner corner, GameObject*& existing)
{
  if (existing || !asset || !parent)
    return;

  float width  = static_cast<float>(Screen::Get<"width", int32_t>());
  float height = static_cast<float>(Screen::Get<"height", int32_t>());
  if (width <= 0.0f)
    width = 1334.0f;
  if (height <= 0.0f)
    height = 750.0f;

  const float scale       = width / 1920.0f * Config::Get().loader_logo_scale;
  const float logo_width  = 150.0f * scale;
  const float logo_height = logo_width * asset->AspectRatio();
  const float pad_x       = 30.0f * scale;
  const float pad_y       = 185.0f * scale;
  const float left        = corner == Corner::kBottomLeft ? pad_x : width - pad_x - logo_width;

  existing = CreateOverlay(name, *asset, parent, {left / width, pad_y / height},
                           {(left + logo_width) / width, (pad_y + logo_height) / height}, true, false);
}

// The mod logo on the right, the community logo on the left.
void CreateLogos(Transform* parent, GameObject*& logo, GameObject*& community_logo)
{
  CreateLogo("STFCModLogo", assets.Logo(), parent, Corner::kBottomRight, logo);
  CreateLogo("STFCCCLogo", assets.CommunityLogo(), parent, Corner::kBottomLeft, community_logo);
}

// --- Login sequence ---

struct Login {
  GameObject* logo           = nullptr;
  GameObject* community_logo = nullptr;
} login;

// The login screen's background image: the first child Image with a known background name, or else the first
// Image found at all, searching four levels deep.
Image* FindLoginBackground(Transform* transform, int depth, Image*& first)
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
    if (auto* image = object->GetComponent<Image>()) {
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
  if (!Config::Get().loader_enabled)
    return;

  login = {};
  assets.Reset();
  auto* background = assets.Background();
  auto* canvas     = background ? _this->mainCanvas() : nullptr;
  auto* root       = canvas ? canvas->transform() : nullptr;
  if (!root)
    return;

  Image* first = nullptr;
  auto*  image = FindLoginBackground(root, 0, first);
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
  CreateLogos(image->transform(), login.logo, login.community_logo);
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
  if (!Config::Get().loader_tip_enabled)
    return;
  auto* label = _this->textMeshPro();
  if (const char* tip = tips.Next(); tip && label)
    label->SetText(il2cpp_string_new(tip));
}

// --- Transition screen ---

// State for the current TransitionViewController. Its Unity objects die with it, so a new controller or a reload
// starts over.
struct Transition {
  bool        customized     = false;
  Image*      background     = nullptr;
  Transform*  background_box = nullptr;
  GameObject* overlay        = nullptr;
  GameObject* logo           = nullptr;
  GameObject* community_logo = nullptr;
  Behaviour*  animator       = nullptr;
} transition;

// The transition background: _staticOverride, or the Image on BGContainer's first child (whose transform is
// returned in `box` so it can be stretched).
Image* FindTransitionBackground(TransitionViewController* controller, Transform*& box)
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
    return inside ? inside->GetComponent<Image>() : nullptr;
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
    if (name == "LogoContainer")
      SetRect(object->GetComponent<RectTransform>(), {1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f}, {586.0f, 248.0f},
              {-20.0f, -20.0f});
    else if (name == "LoadingTipsContainer")
      SetRect(object->GetComponent<RectTransform>(), {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {1024.0f, 100.0f},
              {0.0f, -320.0f});
  }
}

// Runs from Awake and again from AboutToShow until it succeeds.
void CustomizeTransition(TransitionViewController* controller)
{
  if (!transition.background) {
    transition.background = FindTransitionBackground(controller, transition.background_box);
    if (!transition.background)
      return;
  }
  auto* parent = transition.background_box ? transition.background_box : transition.background->transform();

  // The canvas animation would fade our overlay; it is switched back on for the hide animation.
  if (!transition.animator) {
    if (auto* animator = controller->animator()) {
      animator->SetEnabled(false);
      transition.animator = animator;
    }
  }

  if (!Config::Get().loader_transition_black) {
#ifndef _USE_ORIGINAL_BG
    if (parent && !transition.overlay)
      if (auto* background = assets.Background())
        transition.overlay = CreateOverlay("STFCModBG", *background, parent, {0.0f, 0.0f}, {1.0f, 1.0f}, false, true);
    // Keep the game's background unless the replacement is fully attached, so a failure is visible but harmless.
    if (!transition.overlay) {
      spdlog::warn("[LS] custom transition background unavailable; preserving game background");
      return;
    }
    HideImage(transition.background);
    if (auto* box = transition.background_box) {
      SetRect(box->As<RectTransform>(), {0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f});
      box->SetLocalEulerAngles({0, 0, 0});
      box->SetLocalScale({1, 1, 1});
    }
#endif
  }

  if (parent)
    CreateLogos(parent, transition.logo, transition.community_logo);
  if (!Config::Get().loader_transition_black)
    ArrangeTransitionLayout(controller);
  transition.customized = true;
}

void TransitionViewController_Awake_Hook(auto original, TransitionViewController* _this)
{
  original(_this);
  if (!Config::Get().loader_transition)
    return;
  // A controller can outlive the LoginSequence generation that created the cached images; recreate them before
  // touching its hierarchy.
  assets.Reset();
  transition = {};
  CustomizeTransition(_this);
}

void TransitionViewController_AboutToShow_Hook(auto original, TransitionViewController* _this)
{
  original(_this);
  if (Config::Get().loader_transition && !transition.customized)
    CustomizeTransition(_this);
}

void TransitionViewController_AboutToHide_Hook(auto original, TransitionViewController* _this)
{
  if (Config::Get().loader_transition && transition.animator)
    transition.animator->SetEnabled(true);
  original(_this);
}

void SlideShowViewController_ShowCurrentSlide_Hook(auto original, SlideShowViewController* _this)
{
  original(_this);
  const auto& config = Config::Get();
  if (config.loader_transition && !config.loader_transition_black)
    HideImage(_this->image());
}

// The hierarchy holding our overlays is destroyed during a reload; forget it so Awake does not touch dead objects.
void MonoSingleton_PrepareAllForReload_Hook(auto original)
{
  transition = {};
  login      = {};
  tips.Reset();
  original();
}
} // namespace

void InstallLoadingScreenHooks()
{
  if (auto m = LoginSequence::Class::Method("Awake"))
    SPUD_STATIC_DETOUR(m.Pointer(), LoginSequence_Awake_Hook);
}

void InstallLoadingTipHooks()
{
  if (auto m = LoadingTipViewController::Class::Method("SetRandomTipLocalisedText"))
    SPUD_STATIC_DETOUR(m.Pointer(), LoadingTipViewController_SetRandomTipLocalisedText_Hook);
}

void InstallTransitionScreenHooks()
{
  if (auto m = TransitionViewController::Class::Method("Awake"))
    SPUD_STATIC_DETOUR(m.Pointer(), TransitionViewController_Awake_Hook);
  if (auto m = TransitionViewController::Class::Method("AboutToShow"))
    SPUD_STATIC_DETOUR(m.Pointer(), TransitionViewController_AboutToShow_Hook);
  if (auto m = TransitionViewController::Class::Method("AboutToHide"))
    SPUD_STATIC_DETOUR(m.Pointer(), TransitionViewController_AboutToHide_Hook);
  if (auto m = SlideShowViewController::Class::Method("ShowCurrentSlide"))
    SPUD_STATIC_DETOUR(m.Pointer(), SlideShowViewController_ShowCurrentSlide_Hook);
  // TODO: the reload reset is only installed with the transition hooks, so with transitionscreenhooks off the
  // welcome tip and login logos are not reset by a reload.
  if (auto m = MonoSingletonBase::Method("PrepareAllForReload"))
    SPUD_STATIC_DETOUR(m.Pointer(), MonoSingleton_PrepareAllForReload_Hook);
}
