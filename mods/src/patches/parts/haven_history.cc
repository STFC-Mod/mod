#include "config.h"
#include "errormsg.h"

#include <il2cpp/il2cpp-functions.h>
#include <il2cpp/il2cpp_helper.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <cstdint>
#include <vector>

namespace
{

void ReverseRecords(void* records)
{
  if (!records)
    return;

  auto* listClass = il2cpp_object_get_class(reinterpret_cast<Il2CppObject*>(records));
  if (!listClass)
    return;

  auto* getCount = il2cpp_class_get_method_from_name(listClass, "get_Count", 0);
  auto* getItem  = il2cpp_class_get_method_from_name(listClass, "get_Item", 1);
  auto* setItem  = il2cpp_class_get_method_from_name(listClass, "set_Item", 2);
  if (!getCount || !getItem || !setItem) {
    spdlog::warn("[HavenHistory] Record list is missing expected IList<T> methods");
    return;
  }

  Il2CppException* exception = nullptr;
  auto*            countObj  = il2cpp_runtime_invoke(getCount, records, nullptr, &exception);
  if (exception || !countObj)
    return;

  const auto count = *reinterpret_cast<int32_t*>(il2cpp_object_unbox(countObj));
  if (count <= 1)
    return;
  if (count > 2000) {
    spdlog::warn("[HavenHistory] Record list reported an implausible count ({}); skipping", count);
    return;
  }

  std::vector<void*> items;
  items.reserve(count);
  for (int32_t index = 0; index < count; ++index) {
    void* getArgs[1] = {&index};
    exception        = nullptr;
    auto* item       = il2cpp_runtime_invoke(getItem, records, getArgs, &exception);
    if (exception)
      return;
    items.push_back(item);
  }

  for (int32_t index = 0; index < count; ++index) {
    void* setArgs[2] = {&index, items[count - index - 1]};
    exception        = nullptr;
    il2cpp_runtime_invoke(setItem, records, setArgs, &exception);
    if (exception)
      return;
  }
}

void PlanetaryHelpHistoryContext_ApplyRecords_Hook(auto original, void* _this, void* records)
{
  if (Config::Get().reverse_haven_history)
    ReverseRecords(records);
  original(_this, records);
}

} // namespace

void InstallHavenHistoryHooks()
{
  auto context = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.PlanetaryBase", "PlanetaryHelpHistoryContext");
  auto applyRecords = context.GetMethod("ApplyRecords", 1);
  if (!applyRecords) {
    ErrorMsg::MissingMethod("PlanetaryHelpHistoryContext", "ApplyRecords");
    return;
  }

  SPUD_STATIC_DETOUR(applyRecords, PlanetaryHelpHistoryContext_ApplyRecords_Hook);
  spdlog::info("Haven history: hooks installed");
}
