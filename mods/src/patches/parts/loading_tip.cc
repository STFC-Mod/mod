#include "config.h"
#include "version.h"

#include <il2cpp/il2cpp.h>
#include <spud/detour.h>

#include <cstring>
#include <random>

namespace
{
static bool g_isLoadingScreen = true;
static std::mt19937 g_rng{std::random_device{}()};

struct TMP_Text : il2cpp::Object {
  using Class = il2cpp::Class<"Unity.TextMeshPro", "TMPro", "TMP_Text">;

  void SetText(Il2CppString* value)
  { Set<"text">(value); }
};

struct LoadingTipViewController : il2cpp::Object {
  using Class = il2cpp::Class<"Assembly-CSharp", "Prime.LoadingScreen", "LoadingTipViewController">;

  TMP_Text* textMeshPro()
  { return Field<"_textMeshPro", TMP_Text*>(); }
};

struct TipEntry
{
  const char* text;
  int         weight;
};
const TipEntry kCustomTips[] = {
  {"Did you know? You can customize zoom presets, hotkeys, and UI scale in the community mod settings file to suit your playstyle.", 15},
  {"The community mod supports custom loading screens, transition backgrounds, and borderless fullscreen mode for a better experience.", 10},
  {"Press F11 at any time to toggle between borderless fullscreen and windowed mode without restarting the game or losing your session.", 15},
  {"Visit https://stfc.pro to track player rankings and stats across over 236,000 players and 114 servers in Star Trek Fleet Command.", 50},
  {"Join the community Discord server to report bugs, request new features, and stay up to date with the latest mod releases and updates.", 10},
};

constexpr size_t kNumCustomTips = std::size(kCustomTips);

constexpr int kCustomTipChance    = 50;
constexpr int kPassThroughChance  = 50;

std::string GetLoadingScreenTip()
{
  return "Welcome to Star Trek Fleet Command, Supported by the Community Mod v"
         VER_FILE_VERSION_STR VERSION_PATCH_STR "! Please check our discord for the latest information!";
}

void SetTMPText(TMP_Text* tmp, const std::string& text)
{
  if (tmp) tmp->SetText(il2cpp_string_new(text.c_str()));
}

static size_t g_lastCustomTipIdx = SIZE_MAX;

std::string PickTransitionTip()
{
  std::uniform_int_distribution<int> rollDist(1, kCustomTipChance + kPassThroughChance);
  int roll = rollDist(g_rng);

  if (roll > kCustomTipChance)
    return "";

  if (kNumCustomTips == 0) return "";

  int  totalWeight = 0;
  int  indices[kNumCustomTips];
  int  numCandidates = 0;

  for (size_t i = 0; i < kNumCustomTips; ++i) {
    if (i == g_lastCustomTipIdx && kNumCustomTips > 1) continue;
    totalWeight += kCustomTips[i].weight;
    indices[numCandidates++] = (int)i;
  }

  if (totalWeight <= 0 || numCandidates == 0) {
    g_lastCustomTipIdx = 0;
    return kCustomTips[0].text;
  }

  std::uniform_int_distribution<int> dist(1, totalWeight);
  int tipRoll = dist(g_rng);

  for (int j = 0; j < numCandidates; ++j) {
    int i = indices[j];
    tipRoll -= kCustomTips[i].weight;
    if (tipRoll <= 0) {
      g_lastCustomTipIdx = (size_t)i;
      return kCustomTips[i].text;
    }
  }

  g_lastCustomTipIdx = (size_t)indices[numCandidates - 1];
  return kCustomTips[indices[numCandidates - 1]].text;
}

void LTVC_SetRandomTipLocalisedText_Hook(auto original, LoadingTipViewController* _this)
{
  original(_this);

  const auto& cfg = Config::Get();
  if (!cfg.loader_tip_enabled) return;

  std::string chosenTip;

  if (g_isLoadingScreen) {
    chosenTip = GetLoadingScreenTip();
    g_isLoadingScreen = false;
  } else {
    chosenTip = PickTransitionTip();
  }

  if (chosenTip.empty())
    return;

  SetTMPText(_this->textMeshPro(), chosenTip);
}

} // namespace

void ResetLoadingTipState()
{
  g_isLoadingScreen  = true;
  g_lastCustomTipIdx = SIZE_MAX;
}

void InstallLoadingTipHooks()
{
  if (auto m = LoadingTipViewController::Class::Method("SetRandomTipLocalisedText"))
    SPUD_STATIC_DETOUR(m.Pointer(), LTVC_SetRandomTipLocalisedText_Hook);
}
