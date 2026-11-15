#include "loading_screen_common.h"

#include <spud/detour.h>

namespace ls = ls_common;

static void* g_loginLogoGO    = nullptr;
static void* g_loginCCLogoGO  = nullptr;

void ResetLoadingScreenState()
{
  g_loginLogoGO   = nullptr;
  g_loginCCLogoGO = nullptr;
}

static void* FindLoginBGImage(void* transform, int depth, int maxDepth,
                              void* fn_cc, void* fn_ch, void* fn_gg,
                              void* fn_nm, void* fn_gc, void* imageType, void** outFirst)
{
  if (!transform || depth > maxDepth) return nullptr;
  int32_t n = reinterpret_cast<int32_t (*)(void*)>(fn_cc)(transform);
  for (int i = 0; i < n; ++i) {
    void* child = reinterpret_cast<void* (*)(void*, int32_t)>(fn_ch)(transform, i);
    if (!child) continue;
    void* cgo = reinterpret_cast<void* (*)(void*)>(fn_gg)(child);
    if (!cgo) continue;
    void* img = reinterpret_cast<void* (*)(void*, void*)>(fn_gc)(cgo, imageType);
    if (img) {
      if (!*outFirst) *outFirst = img;
      char buf[64] = {};
      ls::ReadIl2CppString(reinterpret_cast<void* (*)(void*)>(fn_nm)(cgo), buf, sizeof(buf));
      if (strcmp(buf, "Background") == 0 || strcmp(buf, "BG") == 0 || strcmp(buf, "bg_image") == 0
          || strcmp(buf, "LoadingBackground") == 0 || strcmp(buf, "LoginBG") == 0 || strcmp(buf, "SplashBG") == 0
          || strcmp(buf, "Image") == 0)
        return img;
    }
    void* found = FindLoginBGImage(child, depth + 1, maxDepth, fn_cc, fn_ch, fn_gg, fn_nm, fn_gc, imageType, outFirst);
    if (found) return found;
  }
  return nullptr;
}

static void LS_LoginSequence_Awake_Hook(auto original, void* _this)
{
  original(_this);

  try {
    if (!Config::Get().loader_enabled) return;

    ResetLoadingScreenState();
    ls::ResetLoadingAssetsForScene();

    auto* asset = ls::GetLoadingAsset();
    if (!asset) return;

    static auto ls_h = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Login", "LoginSequence");
    if (!ls_h.isValidHelper()) return;
    static auto f_mc = ls_h.GetField("_mainCanvas");
    if (!f_mc.isValidHelper()) return;
    void* canvas = *reinterpret_cast<void**>((char*)_this + f_mc.offset());
    if (!canvas) return;

    static auto co_h    = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Component");
    static auto fn_ct   = co_h.GetMethod("get_transform");
    if (!fn_ct) return;
    void* canvasTrans = reinterpret_cast<void* (*)(void*)>(fn_ct)(canvas);
    if (!canvasTrans) return;

    static auto tr_h = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "Transform");
    static auto go_h = il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", "GameObject");
    static auto im_h = il2cpp_get_class_helper("UnityEngine.UI", "UnityEngine.UI", "Image");
    static auto fn_cc = tr_h.GetMethod("get_childCount");
    static auto fn_ch = tr_h.GetMethod("GetChild");
    static auto fn_gg = tr_h.GetMethod("get_gameObject");
    static auto fn_nm = go_h.GetMethod("get_name");
    static auto fn_gc = go_h.GetMethod("GetComponent", 1);
    if (!fn_cc || !fn_ch || !fn_gg || !fn_nm || !fn_gc) return;

    void* imgType = im_h.GetType();
    if (!imgType) return;

    void* first  = nullptr;
    void* bgImg  = FindLoginBGImage(canvasTrans, 0, 4, fn_cc, fn_ch, fn_gg, fn_nm, fn_gc, imgType, &first);
    if (!bgImg) bgImg = first;
    if (!bgImg) return;

#ifndef _USE_ORIGINAL_BG
    ls::ApplySpriteToImage(bgImg, *asset);
#endif

    void* bgImgTr = reinterpret_cast<void* (*)(void*)>(fn_ct)(bgImg);
    if (bgImgTr) {
      ls::CreateLogoOverlay(bgImgTr, g_loginLogoGO);
      ls::CreateCCLogoOverlay(bgImgTr, g_loginCCLogoGO);
    }
  } catch (...) {}
}

void InstallLoadingScreenHooks()
{
  auto ls_h = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Login", "LoginSequence");
  if (!ls_h.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Prime.Login", "LoginSequence");
    return;
  }

  if (auto m = ls_h.GetMethod("Awake")) {
    SPUD_STATIC_DETOUR(m, LS_LoginSequence_Awake_Hook);
  } else {
    ErrorMsg::MissingMethod("LoginSequence", "Awake");
    spdlog::error("[LoadingScreen] LoginSequence.Awake not found");
  }
}
