
#include "loading_screen_common.h"

#include <spud/detour.h>

namespace ls = ls_common;

// Forward declarations — defined in loading_screen.cc / loading_tip.cc
void ResetLoadingScreenState();
void ResetLoadingTipState();

// Per-TVC-instance state (reset in TVC.Awake)
static bool   g_spriteApplied   = false;
static void*  g_bgImageComp     = nullptr;
static void*  g_bgRectTransform = nullptr;
static void*  g_bgOverlayGO     = nullptr;
static void*  g_logoGO          = nullptr;
static void*  g_ccLogoGO        = nullptr;
static void*  g_canvasAnimator  = nullptr;

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
static void* FindTVCBGImage(void* _this, void*& outRectTransform)
{
  static auto tv_h  = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.LoadingScreen", "TransitionViewController");
  static auto mb_h  = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "MonoBehaviour");
  static auto go_h  = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "GameObject");
  static auto tr_h  = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Transform");
  static auto img_h = il2cpp_get_class_helper("UnityEngine.UI", "UnityEngine.UI", "Image");

  // Try _staticOverride field first
  if (tv_h.isValidHelper()) {
    static auto f_ov = tv_h.GetField("_staticOverride");
    if (f_ov.isValidHelper()) {
      void* img = *reinterpret_cast<void**>((char*)_this + f_ov.offset());
      if (img) return img;
    }
  }

  // Fallback: traverse children to find "BGContainer" → first child's Image
  static auto fn_go = mb_h.GetMethod("get_gameObject");
  static auto fn_tr = go_h.GetMethod("get_transform");
  static auto fn_cc = tr_h.GetMethod("get_childCount");
  static auto fn_ch = tr_h.GetMethod("GetChild");
  static auto fn_gg = tr_h.GetMethod("get_gameObject");
  static auto fn_nm = go_h.GetMethod("get_name");
  static auto fn_gc = go_h.GetMethod("GetComponent", 1);
  if (!fn_go || !fn_tr || !fn_cc || !fn_ch || !fn_gg || !fn_nm || !fn_gc) return nullptr;

  void* go = reinterpret_cast<void* (*)(void*)>(fn_go)(_this);
  if (!go) return nullptr;
  void* root = reinterpret_cast<void* (*)(void*)>(fn_tr)(go);
  if (!root) return nullptr;

  void*   bgc = nullptr;
  int32_t n   = reinterpret_cast<int32_t (*)(void*)>(fn_cc)(root);
  for (int i = 0; i < n; ++i) {
    void* ct = reinterpret_cast<void* (*)(void*, int32_t)>(fn_ch)(root, i);
    if (!ct) continue;
    void* cg = reinterpret_cast<void* (*)(void*)>(fn_gg)(ct);
    if (!cg) continue;
    char buf[64] = {};
    ls::ReadIl2CppString(reinterpret_cast<void* (*)(void*)>(fn_nm)(cg), buf, sizeof(buf));
    if (strcmp(buf, "BGContainer") == 0) {
      bgc = ct;
      break;
    }
  }
  if (!bgc) return nullptr;
  if (reinterpret_cast<int32_t (*)(void*)>(fn_cc)(bgc) == 0) return nullptr;

  void* bgt = reinterpret_cast<void* (*)(void*, int32_t)>(fn_ch)(bgc, 0);
  if (!bgt) return nullptr;
  outRectTransform = bgt;
  void* bgg = reinterpret_cast<void* (*)(void*)>(fn_gg)(bgt);
  if (!bgg) return nullptr;
  void* it = img_h.GetType();
  if (!it) return nullptr;
  return reinterpret_cast<void* (*)(void*, void*)>(fn_gc)(bgg, it);
}

// Applies custom background and logos to a TransitionViewController.
// Called from TVC.Awake (as fallback) and TVC.AboutToShow (primary).
static void ApplyTransitionCustomization(void* _this)
{
  try {
    const auto& cfg = Config::Get();

    // Find the BG image component
    void* rt = nullptr;
    void* imageComp = g_bgImageComp;
    if (!imageComp) {
      imageComp = FindTVCBGImage(_this, rt);
      if (!imageComp) return;
      g_bgImageComp     = imageComp;
      g_bgRectTransform = rt;
    }

    // Determine logo parent transform
    void* logoParent = g_bgRectTransform;
    if (!logoParent && g_bgImageComp) {
      static auto comp_h    = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Component");
      static auto fn_get_tr = comp_h.GetMethod("get_transform");
      if (fn_get_tr)
        logoParent = reinterpret_cast<void* (*)(void*)>(fn_get_tr)(g_bgImageComp);
    }

    if (!g_canvasAnimator) {
      static auto tv_h = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.LoadingScreen", "TransitionViewController");
      static auto fn_animField = tv_h.GetField("_animator");
      static auto behav_h      = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Behaviour");
      static auto fn_setEn     = behav_h.GetMethodInfo("set_enabled");
      if (fn_animField.isValidHelper() && fn_setEn) {
        void* anim = *reinterpret_cast<void**>((char*)_this + fn_animField.offset());
        if (anim) {
          bool  off     = false;
          void* args[1] = {&off};
          ls::InvokeVoid(fn_setEn, anim, args, "canvasAnimator.set_enabled(false)");
          g_canvasAnimator = anim;
        }
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
    {
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
        ls::SetFullRect(g_bgRectTransform, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f});
        static auto tr_h = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Transform");
        static auto fn_eu = tr_h.GetMethodInfo("set_localEulerAngles");
        if (fn_eu) {
          ls::FakeVector3 z{0, 0, 0};
          void* args[1] = {&z};
          ls::InvokeVoid(fn_eu, g_bgRectTransform, args, "Transform.set_localEulerAngles");
        }
        static auto fn_sc = tr_h.GetMethodInfo("set_localScale");
        if (fn_sc) {
          ls::FakeVector3 o{1, 1, 1};
          void* args[1] = {&o};
          ls::InvokeVoid(fn_sc, g_bgRectTransform, args, "Transform.set_localScale");
        }
      }

    }
#endif

    if (logoParent) {
      ls::CreateLogoOverlay(logoParent, g_logoGO);
      ls::CreateCCLogoOverlay(logoParent, g_ccLogoGO);
    }

    {
      static auto mb_hR  = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "MonoBehaviour");
      static auto go_hR  = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "GameObject");
      static auto tr_hR  = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Transform");
      static auto rt_hR  = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "RectTransform");
      static auto fn_goR = mb_hR.GetMethod("get_gameObject");
      static auto fn_trR = go_hR.GetMethod("get_transform");
      static auto fn_ccR = tr_hR.GetMethod("get_childCount");
      static auto fn_chR = tr_hR.GetMethod("GetChild");
      static auto fn_ggR = tr_hR.GetMethod("get_gameObject");
      static auto fn_nmR = go_hR.GetMethod("get_name");
      static auto fn_gcR = go_hR.GetMethod("GetComponent", 1);

      if (fn_goR && fn_trR && fn_ccR && fn_chR && fn_ggR && fn_nmR && fn_gcR && rt_hR.isValidHelper()) {
        void*   tvcGO  = reinterpret_cast<void* (*)(void*)>(fn_goR)(_this);
        void*   tvcTr  = tvcGO ? reinterpret_cast<void* (*)(void*)>(fn_trR)(tvcGO) : nullptr;
        int32_t n      = tvcTr ? reinterpret_cast<int32_t (*)(void*)>(fn_ccR)(tvcTr) : 0;
        void*   rtType = rt_hR.GetType();

        for (int32_t i = 0; i < n; ++i) {
          void* ct = reinterpret_cast<void* (*)(void*, int32_t)>(fn_chR)(tvcTr, i);
          if (!ct) continue;
          void* cg = reinterpret_cast<void* (*)(void*)>(fn_ggR)(ct);
          if (!cg || !rtType) continue;
          char buf[64] = {};
          ls::ReadIl2CppString(reinterpret_cast<void* (*)(void*)>(fn_nmR)(cg), buf, sizeof(buf));

          void* rt = nullptr;
          if (strcmp(buf, "LogoContainer") == 0) {
            rt = reinterpret_cast<void* (*)(void*, void*)>(fn_gcR)(cg, rtType);
            ls::SetFullRect(rt, {1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f}, {586.0f, 248.0f}, {-20.0f, -20.0f});
          } else if (strcmp(buf, "LoadingTipsContainer") == 0) {
            rt = reinterpret_cast<void* (*)(void*, void*)>(fn_gcR)(cg, rtType);
            ls::SetFullRect(rt, {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {1024.0f, 100.0f}, {0.0f, -320.0f});
          }
        }
      }
    }

    g_spriteApplied = true;
  } catch (...) {}
}

// --- Hooks ---

static void TVC_Awake_Hook(auto original, void* _this)
{
  original(_this);

  try {
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
  } catch (...) {}
}

static void TVC_AboutToShow_Hook(auto original, void* _this)
{
  original(_this);

  try {
    if (!Config::Get().loader_transition || g_spriteApplied) return;
    ApplyTransitionCustomization(_this);
  } catch (...) {}
}

static void TVC_AboutToHide_Hook(auto original, void* _this)
{
  try {
    if (Config::Get().loader_transition && g_canvasAnimator) {
      // Re-enable canvas animator so the hide animation plays
      static auto behav_h  = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Behaviour");
      static auto fn_setEn = behav_h.GetMethodInfo("set_enabled");
      if (fn_setEn) {
        bool  on      = true;
        void* args[1] = {&on};
        ls::InvokeVoid(fn_setEn, g_canvasAnimator, args, "canvasAnimator.set_enabled(true)");
      }
    }
  } catch (...) {}

  original(_this);
}

static void SlideShowViewer_ShowCurrentSlide_Hook(auto original, void* _this)
{
  original(_this);

  try {
    const auto& cfg = Config::Get();
    if (!cfg.loader_transition || cfg.loader_transition_black) return;

    static auto h  = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.SlideShow", "SlideShowViewController");
    static auto fi = h.GetField("_image");
    if (!fi.isValidHelper()) return;
    void* img = *reinterpret_cast<void**>((char*)_this + fi.offset());
    if (img) ls::HideImage(img);
  } catch (...) {}
}

static void TS_MonoSingleton_PrepareAllForReload_Hook(auto original)
{
  ResetTransitionScreenState();
  original();
}

void InstallTransitionScreenHooks()
{
  auto tv_h = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.LoadingScreen", "TransitionViewController");
  if (!tv_h.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Prime.LoadingScreen", "TransitionViewController");
    return;
  }

  bool ok = true;

  if (auto m = tv_h.GetMethod("Awake")) {
    SPUD_STATIC_DETOUR(m, TVC_Awake_Hook);
  } else {
    ErrorMsg::MissingMethod("TransitionViewController", "Awake");
    ok = false;
  }

  if (auto m = tv_h.GetMethod("AboutToShow")) {
    SPUD_STATIC_DETOUR(m, TVC_AboutToShow_Hook);
  } else {
    ErrorMsg::MissingMethod("TransitionViewController", "AboutToShow");
    ok = false;
  }

  if (auto m = tv_h.GetMethod("AboutToHide")) {
    SPUD_STATIC_DETOUR(m, TVC_AboutToHide_Hook);
  } else {
    ErrorMsg::MissingMethod("TransitionViewController", "AboutToHide");
    ok = false;
  }

  auto ss_h = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.SlideShow", "SlideShowViewController");
  if (ss_h.isValidHelper()) {
    if (auto m = ss_h.GetMethod("ShowCurrentSlide")) {
      SPUD_STATIC_DETOUR(m, SlideShowViewer_ShowCurrentSlide_Hook);
    }
  }

  auto ms_h = il2cpp_get_class_helper("Assembly-CSharp", "", "MonoSingleton");
  if (ms_h.isValidHelper()) {
    if (auto m = ms_h.GetMethod("PrepareAllForReload")) {
      SPUD_STATIC_DETOUR(m, TS_MonoSingleton_PrepareAllForReload_Hook);
    }
  }

  if (!ok)
    spdlog::error("[TransitionScreen] Some hooks failed to install");
}
