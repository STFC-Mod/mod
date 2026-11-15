#include <il2cpp/method_contract.h>
#include <il2cpp/runtime.h>
#include <patches/haven_road_cache.h>
#include <spdlog/spdlog.h>
#include <str_utils.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>

namespace
{
struct RoadDepthApi {
  Il2CppClass*      renderer = nullptr;
  const MethodInfo *find = nullptr, *children = nullptr, *materials = nullptr;
  const MethodInfo *name = nullptr, *shader = nullptr, *has = nullptr, *get = nullptr, *set = nullptr;
  const MethodInfo *alive = nullptr, *active = nullptr, *instance_id = nullptr;

  RoadDepthApi()
  {
    auto cls = [](const char* n) {
      return il2cpp_get_class_helper("UnityEngine.CoreModule", "UnityEngine", n).get_cls();
    };
    auto *game_object = cls("GameObject"), *material = cls("Material"), *object = cls("Object");
    renderer  = cls("Renderer");
    find      = method_contract::Resolve(game_object, "Find", true, "UnityEngine.GameObject", {"System.String"});
    children  = method_contract::Resolve(game_object, "GetComponentsInChildren", false, "UnityEngine.Component[]",
                                         {"System.Type", "System.Boolean"});
    materials = method_contract::Resolve(renderer, "get_sharedMaterials", false, "UnityEngine.Material[]", {});
    name      = method_contract::Resolve(object, "get_name", false, "System.String", {});
    shader    = method_contract::Resolve(material, "get_shader", false, "UnityEngine.Shader", {});
    has       = method_contract::Resolve(material, "HasProperty", false, "System.Boolean", {"System.String"});
    get       = method_contract::Resolve(material, "GetFloat", false, "System.Single", {"System.String"});
    set    = method_contract::Resolve(material, "SetFloat", false, "System.Void", {"System.String", "System.Single"});
    alive  = method_contract::Resolve(object, "op_Implicit", true, "System.Boolean", {"UnityEngine.Object"});
    active = method_contract::Resolve(game_object, "get_activeInHierarchy", false, "System.Boolean", {});
    instance_id = method_contract::Resolve(object, "GetInstanceID", false, "System.Int32", {});
  }

  bool Valid() const
  {
    return renderer && find && children && materials && name && shader && has && get && set && alive && active
           && instance_id;
  }
};

RoadDepthApi& Api()
{
  static RoadDepthApi api;
  return api;
}

Il2CppObject* Invoke(const MethodInfo* method, Il2CppObject* object, void** args = nullptr)
{
  Il2CppObject* result = nullptr;
  return Il2CppRuntime::TryInvoke(method, object, args, &result) ? result : nullptr;
}

// All roots are temporary and released on the owner-thread stack. Persistent
// discovery keeps weak handles, which do not retain scene objects or assets.
class StrongObject
{
public:
  explicit StrongObject(Il2CppObject* object = nullptr)
      : handle_(object ? il2cpp_gchandle_new(object, false) : nullptr)
  {
  }
  StrongObject(const StrongObject&)            = delete;
  StrongObject& operator=(const StrongObject&) = delete;
  StrongObject(StrongObject&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr))
  {
  }
  StrongObject& operator=(StrongObject&& other) noexcept
  {
    if (this != &other) {
      if (handle_)
        il2cpp_gchandle_free(handle_);
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }
  ~StrongObject()
  {
    if (handle_)
      il2cpp_gchandle_free(handle_);
  }
  explicit operator bool() const
  { return handle_ != nullptr; }
  Il2CppObject* Get() const
  { return handle_ ? il2cpp_gchandle_get_target(handle_) : nullptr; }

private:
  Il2CppGCHandle handle_ = nullptr;
};

bool Alive(const StrongObject& root)
{
  auto* object = root.Get();
  bool  alive  = false;
  void* args[]{object};
  return object && Il2CppRuntime::TryBoolean(Invoke(Api().alive, nullptr, args), alive) && alive;
}

class WeakObject
{
public:
  WeakObject() = default;
  explicit WeakObject(const StrongObject& root)
      : handle_(root.Get() ? il2cpp_gchandle_new_weakref(root.Get(), false) : nullptr)
  {
  }
  WeakObject(const WeakObject&)            = delete;
  WeakObject& operator=(const WeakObject&) = delete;
  WeakObject(WeakObject&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr))
  {
  }
  WeakObject& operator=(WeakObject&& other) noexcept
  {
    if (this != &other) {
      if (handle_)
        il2cpp_gchandle_free(handle_);
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }
  ~WeakObject()
  {
    if (handle_)
      il2cpp_gchandle_free(handle_);
  }
  explicit operator bool() const
  { return handle_ != nullptr; }
  StrongObject Lock() const
  {
    StrongObject root(handle_ ? il2cpp_gchandle_get_target(handle_) : nullptr);
    if (!Alive(root))
      return StrongObject{};
    return root;
  }

private:
  Il2CppGCHandle handle_ = nullptr;
};

std::string Name(const StrongObject& object)
{
  StrongObject value(object ? Invoke(Api().name, object.Get()) : nullptr);
  const auto*  string = reinterpret_cast<Il2CppString*>(value.Get());
  return string && string->length >= 0 && string->length <= 256 ? to_string(string) : std::string{};
}

bool IsRoad(const std::string& name)
{
  for (const char* prefix :
       {"mat_PB_3101101_asphalt_", "mat_PB_3101201_asphalt_", "mat_PB_3101301_asphalt_", "mat_PB_3101401_asphalt_",
        "mat_PB_3101501_asphalt_", "mat_PB_3101601_asphalt_", "mat_PB_3101701_asphalt_"}) {
    if (name.starts_with(prefix))
      return true;
  }
  return false;
}

struct NativeBackend {
  using Object = StrongObject;
  using Weak   = WeakObject;

  static Object GetOwner(const Weak& weak)
  {
    auto root   = weak.Lock();
    bool active = false;
    if (!root || !Il2CppRuntime::TryBoolean(Invoke(Api().active, root.Get()), active) || !active)
      return Object{};
    return root;
  }
  static Weak Retain(const Object& object)
  { return Weak(object); }
  static Object FindOwner()
  {
    StrongObject name(reinterpret_cast<Il2CppObject*>(
        il2cpp_string_new("StarbaseRoot/StarbaseManager/StarbasePlanetaryVisualsHolder")));
    if (!name)
      return Object{};
    void*  args[]{name.Get()};
    Object root(Invoke(Api().find, nullptr, args));
    if (!Alive(root))
      return Object{};
    return root;
  }
  static bool ScanOwner(const Object& owner, std::vector<Weak>& roads)
  {
    auto&        api = Api();
    StrongObject type(reinterpret_cast<Il2CppObject*>(il2cpp_type_get_object(il2cpp_class_get_type(api.renderer))));
    if (!type)
      return false;
    bool         inactive = true;
    void*        args[]{type.Get(), &inactive};
    StrongObject renderer_root(Invoke(api.children, owner.Get(), args));
    auto*        renderers = reinterpret_cast<Il2CppArraySize*>(renderer_root.Get());
    if (!renderers || renderers->max_length > haven_roads::kRendererLimit)
      return false;
    std::unordered_set<int> road_ids;
    for (uintptr_t i = 0; i < renderers->max_length; ++i) {
      StrongObject renderer(reinterpret_cast<Il2CppObject*>(renderers->vector[i]));
      if (!Alive(renderer))
        continue;
      StrongObject material_root(Invoke(api.materials, renderer.Get()));
      auto*        materials = reinterpret_cast<Il2CppArraySize*>(material_root.Get());
      if (!materials || materials->max_length > haven_roads::kMaterialsPerRenderer)
        continue;
      for (uintptr_t j = 0; j < materials->max_length; ++j) {
        StrongObject material(reinterpret_cast<Il2CppObject*>(materials->vector[j]));
        if (!Alive(material) || !IsRoad(Name(material)))
          continue;
        StrongObject shader(Invoke(api.shader, material.Get()));
        if (!Alive(shader) || Name(shader) != "Custom/LitMSAE")
          continue;
        auto* boxed_id = Invoke(api.instance_id, material.Get());
        if (!boxed_id || !method_contract::Type(il2cpp_class_get_type(boxed_id->klass), "System.Int32"))
          continue;
        const int id = *static_cast<int*>(il2cpp_object_unbox(boxed_id));
        if (road_ids.insert(id).second) {
          if (auto weak = Retain(material); weak)
            roads.push_back(std::move(weak));
          if (roads.size() == haven_roads::kMaterialLimit)
            return true;
        }
      }
    }
    return true;
  }
};

bool ReadDepth(Il2CppObject* material, Il2CppString* property, float& value)
{
  void* args[]{property};
  bool  present = false;
  if (!Il2CppRuntime::TryBoolean(Invoke(Api().has, material, args), present) || !present)
    return false;
  auto* boxed = Invoke(Api().get, material, args);
  if (!boxed || !method_contract::Type(il2cpp_class_get_type(boxed->klass), "System.Single"))
    return false;
  value = *static_cast<float*>(il2cpp_object_unbox(boxed));
  return std::isfinite(value);
}
} // namespace

// The existing qualified Haven camera path owns this call. Keep its no-argument,
// one-way material bias contract; discovery retains only weak owner/materials.
void ApplyHavenRoadDepthBias()
{
  using Clock      = std::chrono::steady_clock;
  static auto next = Clock::time_point{};
  const auto  now  = Clock::now();
  if (now < next)
    return;
  next = now + std::chrono::seconds(1);
  if (!Api().Valid()) {
    static bool warned = false;
    if (!warned) {
      const auto& api = Api();
      if (!api.renderer)
        spdlog::warn("[HavenZoom] road depth unavailable: missing UnityEngine.Renderer class");
      for (const auto& [method, signature] : std::array<std::pair<const MethodInfo*, const char*>, 11>{
               {{api.find, "GameObject.Find(String) -> GameObject [static]"},
                {api.children, "GameObject.GetComponentsInChildren(Type, Boolean) -> Component[]"},
                {api.materials, "Renderer.get_sharedMaterials() -> Material[]"},
                {api.name, "Object.get_name() -> String"},
                {api.shader, "Material.get_shader() -> Shader"},
                {api.has, "Material.HasProperty(String) -> Boolean"},
                {api.get, "Material.GetFloat(String) -> Single"},
                {api.set, "Material.SetFloat(String, Single) -> Void"},
                {api.alive, "Object.op_Implicit(Object) -> Boolean [static]"},
                {api.active, "GameObject.get_activeInHierarchy() -> Boolean"},
                {api.instance_id, "Object.GetInstanceID() -> Int32"}}}) {
        if (!method)
          spdlog::warn("[HavenZoom] keeping native road depth: missing or incompatible UnityEngine.{}", signature);
      }
      warned = true;
    }
    return;
  }
  // Never run IL2CPP handle destructors after runtime teardown. Replacement and
  // invalidation release old weak handles during normal owner-thread calls.
  static auto* cache = new haven_roads::DiscoveryCache<NativeBackend>;
  cache->Refresh(now);
  if (cache->Materials().empty())
    return;
  const std::array<StrongObject, 2> properties{
      StrongObject(reinterpret_cast<Il2CppObject*>(il2cpp_string_new("_OffsetFactor"))),
      StrongObject(reinterpret_cast<Il2CppObject*>(il2cpp_string_new("_OffsetUnits")))};
  if (!properties[0] || !properties[1])
    return;
  for (const auto& weak : cache->Materials()) {
    auto material = weak.Lock();
    if (!material)
      continue;
    std::array<float, 2> before{};
    if (!ReadDepth(material.Get(), reinterpret_cast<Il2CppString*>(properties[0].Get()), before[0])
        || !ReadDepth(material.Get(), reinterpret_cast<Il2CppString*>(properties[1].Get()), before[1]))
      continue;
    bool changed = false, verified = true;
    for (unsigned k = 0; k < 2; ++k) {
      float target = std::min(before[k], -1.0f);
      if (target == before[k])
        continue;
      void* args[]{properties[k].Get(), &target};
      if (!Il2CppRuntime::TryInvoke(Api().set, material.Get(), args)) {
        verified = false;
        continue;
      }
      changed     = true;
      float after = 0.0f;
      verified    = ReadDepth(material.Get(), reinterpret_cast<Il2CppString*>(properties[k].Get()), after)
                    && after == target && verified;
    }
    if (changed) {
      spdlog::debug("[HavenZoom] road depth bias: verified={}", verified);
      static bool reported = false;
      if (verified && !reported) {
        spdlog::info("[HavenZoom] corrected road surface depth for expanded Haven zoom");
        reported = true;
      }
    }
  }
}
