#include "loading_screen_common.h"

#include <spud/detour.h>

namespace ls = ls_common;

namespace
{
struct LoginSequence : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Digit.Prime.Login", "LoginSequence">;

  Component* mainCanvas()
  { return Field<"_mainCanvas", Component*>(); }
};
} // namespace

static GameObject* g_loginLogoGO    = nullptr;
static GameObject* g_loginCCLogoGO  = nullptr;

void ResetLoadingScreenState()
{
  g_loginLogoGO   = nullptr;
  g_loginCCLogoGO = nullptr;
}

static Image* FindLoginBGImage(Transform* transform, int depth, int maxDepth, Image** outFirst)
{
  if (!transform || depth > maxDepth) return nullptr;
  int32_t n = transform->childCount();
  for (int32_t i = 0; i < n; ++i) {
    auto* child = transform->GetChild(i);
    if (!child) continue;
    auto* cgo = child->gameObject();
    if (!cgo) continue;
    if (auto* img = cgo->GetComponent<Image>()) {
      if (!*outFirst) *outFirst = img;
      const auto name = cgo->Name();
      if (name == "Background" || name == "BG" || name == "bg_image" || name == "LoadingBackground"
          || name == "LoginBG" || name == "SplashBG" || name == "Image")
        return img;
    }
    if (auto* found = FindLoginBGImage(child, depth + 1, maxDepth, outFirst)) return found;
  }
  return nullptr;
}

static void LS_LoginSequence_Awake_Hook(auto original, LoginSequence* _this)
{
  original(_this);

  if (!Config::Get().loader_enabled) return;

  ResetLoadingScreenState();
  ls::ResetLoadingAssetsForScene();

  auto* asset = ls::GetLoadingAsset();
  if (!asset) return;

  auto* canvas = _this->mainCanvas();
  if (!canvas) return;
  auto* canvasTrans = canvas->transform();
  if (!canvasTrans) return;

  Image* first = nullptr;
  auto*  bgImg = FindLoginBGImage(canvasTrans, 0, 4, &first);
  if (!bgImg) bgImg = first;
  if (!bgImg) return;

#ifndef _USE_ORIGINAL_BG
  ls::ApplySpriteToImage(bgImg, *asset);
#endif

  if (auto* bgImgTr = bgImg->transform()) {
    ls::CreateLogoOverlay(bgImgTr, g_loginLogoGO);
    ls::CreateCCLogoOverlay(bgImgTr, g_loginCCLogoGO);
  }
}

void InstallLoadingScreenHooks()
{
  if (auto m = LoginSequence::Class::Method("Awake"))
    SPUD_STATIC_DETOUR(m.Pointer(), LS_LoginSequence_Awake_Hook);
}
