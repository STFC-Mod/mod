#include "loading_screen_common.h"

#include <spud/detour.h>

namespace ls = ls_common;

// Forward declarations — defined in loading_screen.cc / loading_tip.cc
void ResetLoadingScreenState();
void ResetLoadingTipState();

namespace
{
struct Behaviour : il2cpp::Object {
  using Class = il2cpp::Class<"UnityEngine.CoreModule", "UnityEngine", "Behaviour">;

  void SetEnabled(bool value)
  { Set<"enabled">(value); }
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

// The non-generic MonoSingleton base; its static PrepareAllForReload runs before a reload.
using MonoSingletonBase = il2cpp::Class<"Assembly-CSharp", "", "MonoSingleton">;
} // namespace

// Per-TVC-instance state (reset in TVC.Awake)
static bool        g_spriteApplied   = false;
static Image*      g_bgImageComp     = nullptr;
static Transform*  g_bgRectTransform = nullptr;
static GameObject* g_bgOverlayGO     = nullptr;
static GameObject* g_logoGO          = nullptr;
static GameObject* g_ccLogoGO        = nullptr;
static Behaviour*  g_canvasAnimator  = nullptr;

// Called from PrepareAllForReload to null stale pointers before reload.
// The Canvas/TVC hierarchy holding our overlays is replaced during
// GoToLoginSection, which destroys the old overlays along with it.
// Nulling here prevents TVC.Awake from touching stale/destroyed pointers.
void ResetTransitionScreenState()
{
  g_spriteApplied   = false;
  g_bgImageComp     = nullptr;
  g_bgRectTransform = nullptr;
  g_bgOverlayGO     = nullptr;
  g_logoGO          = nullptr;
  g_ccLogoGO        = nullptr;
  g_canvasAnimator  = nullptr;
  ResetLoadingScreenState();
  ResetLoadingTipState();
}

// Finds the BG Image component on a TransitionViewController instance.
// Tries TVC._staticOverride field first, then traverses children for "BGContainer".
static Image* FindTVCBGImage(TransitionViewController* _this, Transform*& outRectTransform)
{
  if (auto* img = _this->staticOverride())
    return img;

  // Fallback: traverse children to find "BGContainer" → first child's Image
  auto* root = _this->transform();
  if (!root) return nullptr;

  Transform* bgc = nullptr;
  int32_t    n   = root->childCount();
  for (int32_t i = 0; i < n; ++i) {
    auto* ct = root->GetChild(i);
    if (!ct) continue;
    auto* cg = ct->gameObject();
    if (cg && cg->Name() == "BGContainer") {
      bgc = ct;
      break;
    }
  }
  if (!bgc || bgc->childCount() == 0) return nullptr;

  auto* bgt = bgc->GetChild(0);
  if (!bgt) return nullptr;
  outRectTransform = bgt;
  auto* bgg = bgt->gameObject();
  return bgg ? bgg->GetComponent<Image>() : nullptr;
}

// Applies custom background and logos to a TransitionViewController.
// Called from TVC.Awake (as fallback) and TVC.AboutToShow (primary).
static void ApplyTransitionCustomization(TransitionViewController* _this)
{
  const auto& cfg = Config::Get();

  // Find the BG image component
  if (!g_bgImageComp) {
    Transform* rt        = nullptr;
    auto*      imageComp = FindTVCBGImage(_this, rt);
    if (!imageComp) return;
    g_bgImageComp     = imageComp;
    g_bgRectTransform = rt;
  }
  auto* imageComp = g_bgImageComp;

  // Determine logo parent transform
  auto* logoParent = g_bgRectTransform ? g_bgRectTransform : imageComp->transform();

  if (!g_canvasAnimator) {
    if (auto* anim = _this->animator()) {
      anim->SetEnabled(false);
      g_canvasAnimator = anim;
    }
  }

  if (cfg.loader_transition_black) {
    if (logoParent) {
      ls::CreateLogoOverlay(logoParent, g_logoGO);
      ls::CreateCCLogoOverlay(logoParent, g_ccLogoGO);
    }
    g_spriteApplied = true;
    return;
  }

#ifndef _USE_ORIGINAL_BG
  if (logoParent && !g_bgOverlayGO) {
    auto* asset = ls::GetLoadingAsset();
    if (asset) g_bgOverlayGO = ls::CreateBGOverlay(logoParent, *asset);
  }

  // Preserve the game's background unless the custom replacement is fully
  // constructed and attached. This keeps failures visible but harmless.
  if (!g_bgOverlayGO) {
    spdlog::warn("[LS] custom transition background unavailable; preserving game background");
    return;
  }

  ls::HideImage(imageComp);

  if (g_bgRectTransform) {
    ls::SetFullRect(g_bgRectTransform->As<RectTransform>(), {0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.5f},
                    {0.0f, 0.0f}, {0.0f, 0.0f});
    g_bgRectTransform->SetLocalEulerAngles({0, 0, 0});
    g_bgRectTransform->SetLocalScale({1, 1, 1});
  }
#endif

  if (logoParent) {
    ls::CreateLogoOverlay(logoParent, g_logoGO);
    ls::CreateCCLogoOverlay(logoParent, g_ccLogoGO);
  }

  if (auto* tvcTr = _this->transform()) {
    int32_t n = tvcTr->childCount();
    for (int32_t i = 0; i < n; ++i) {
      auto* ct = tvcTr->GetChild(i);
      if (!ct) continue;
      auto* cg = ct->gameObject();
      if (!cg) continue;
      const auto name = cg->Name();
      if (name == "LogoContainer") {
        ls::SetFullRect(cg->GetComponent<RectTransform>(), {1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f},
                        {586.0f, 248.0f}, {-20.0f, -20.0f});
      } else if (name == "LoadingTipsContainer") {
        ls::SetFullRect(cg->GetComponent<RectTransform>(), {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f},
                        {1024.0f, 100.0f}, {0.0f, -320.0f});
      }
    }
  }

  g_spriteApplied = true;
}

// --- Hooks ---

static void TVC_Awake_Hook(auto original, TransitionViewController* _this)
{
  original(_this);

  if (!Config::Get().loader_transition) return;

  // A TransitionViewController can outlive the LoginSequence generation
  // that created the cached Unity assets. Recreate them for this controller
  // before touching its background hierarchy.
  ls::ResetLoadingAssetsForScene();

  g_spriteApplied   = false;
  g_bgImageComp     = nullptr;
  g_bgRectTransform = nullptr;
  g_bgOverlayGO     = nullptr;
  g_logoGO          = nullptr;
  g_ccLogoGO        = nullptr;
  g_canvasAnimator  = nullptr;

  ApplyTransitionCustomization(_this);
}

static void TVC_AboutToShow_Hook(auto original, TransitionViewController* _this)
{
  original(_this);

  if (!Config::Get().loader_transition || g_spriteApplied) return;
  ApplyTransitionCustomization(_this);
}

static void TVC_AboutToHide_Hook(auto original, TransitionViewController* _this)
{
  if (Config::Get().loader_transition && g_canvasAnimator) {
    // Re-enable canvas animator so the hide animation plays
    g_canvasAnimator->SetEnabled(true);
  }

  original(_this);
}

static void SlideShowViewer_ShowCurrentSlide_Hook(auto original, SlideShowViewController* _this)
{
  original(_this);

  const auto& cfg = Config::Get();
  if (!cfg.loader_transition || cfg.loader_transition_black) return;

  ls::HideImage(_this->image());
}

static void TS_MonoSingleton_PrepareAllForReload_Hook(auto original)
{
  ResetTransitionScreenState();
  original();
}

void InstallTransitionScreenHooks()
{
  if (auto m = TransitionViewController::Class::Method("Awake"))
    SPUD_STATIC_DETOUR(m.Pointer(), TVC_Awake_Hook);
  if (auto m = TransitionViewController::Class::Method("AboutToShow"))
    SPUD_STATIC_DETOUR(m.Pointer(), TVC_AboutToShow_Hook);
  if (auto m = TransitionViewController::Class::Method("AboutToHide"))
    SPUD_STATIC_DETOUR(m.Pointer(), TVC_AboutToHide_Hook);
  if (auto m = SlideShowViewController::Class::Method("ShowCurrentSlide"))
    SPUD_STATIC_DETOUR(m.Pointer(), SlideShowViewer_ShowCurrentSlide_Hook);
  if (auto m = MonoSingletonBase::Method("PrepareAllForReload"))
    SPUD_STATIC_DETOUR(m.Pointer(), TS_MonoSingleton_PrepareAllForReload_Hook);
}
