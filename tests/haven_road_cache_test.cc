#include "../mods/src/patches/haven_road_cache.h"

#include <cstdlib>
#include <numeric>
#include <unordered_set>

namespace
{
void Check(bool condition)
{
  if (!condition)
    std::abort();
}

struct Backend {
  using Object = int;
  struct Weak {
    static inline int handles = 0;
    int               object  = 0;
    Weak()                    = default;
    explicit Weak(int value)
        : object(value)
    {
      if (object)
        ++handles;
    }
    Weak(const Weak&)            = delete;
    Weak& operator=(const Weak&) = delete;
    Weak(Weak&& other) noexcept
        : object(std::exchange(other.object, 0))
    {
    }
    Weak& operator=(Weak&& other) noexcept
    {
      if (this != &other) {
        if (object)
          --handles;
        object = std::exchange(other.object, 0);
      }
      return *this;
    }
    ~Weak()
    {
      if (object)
        --handles;
    }
    explicit operator bool() const
    { return object != 0; }
  };
  static inline int                     owner = 0, finds = 0, scans = 0, scanned_owner = 0, fail_retain = 0;
  static inline bool                    scan_ok = true;
  static inline std::unordered_set<int> live, active;
  static inline std::vector<int>        materials;
  static int                            GetOwner(const Weak& weak)
  { return live.contains(weak.object) && active.contains(weak.object) ? weak.object : 0; }
  static int FindOwner()
  {
    ++finds;
    return live.contains(owner) && active.contains(owner) ? owner : 0;
  }
  static Weak Retain(int object)
  { return Weak(object == fail_retain ? 0 : object); }
  static bool ScanOwner(int owner, std::vector<Weak>& out)
  {
    ++scans;
    scanned_owner = owner;
    for (int object : materials)
      if (auto weak = Retain(object); weak)
        out.push_back(std::move(weak));
    return scan_ok;
  }
};
} // namespace

int main()
{
  using Cache = haven_roads::DiscoveryCache<Backend>;
  using namespace std::chrono_literals;
  const auto t = Cache::Clock::time_point{} + 100s;
  {
    Cache cache;
    // Missing owner and repeated same-timestamp callers cannot multiply finds.
    for (int i = 0; i < 10000; ++i)
      cache.Refresh(t);
    Check(Backend::finds == 1 && Backend::scans == 0);
    Backend::owner = 1;
    Backend::live = Backend::active = {1};
    Backend::materials              = {10};
    cache.Refresh(t + 4s);
    Check(Backend::finds == 1 && cache.Materials().empty());
    cache.Refresh(t + 5s);
    Check(Backend::finds == 2 && Backend::scans == 1 && Backend::scanned_owner == 1);
    Check(cache.Materials().size() == 1 && cache.Materials()[0].object == 10);
    for (int i = 0; i < 10000; ++i)
      cache.Refresh(t + 6s);
    Check(Backend::finds == 2 && Backend::scans == 1 && Backend::Weak::handles == 2);

    // Reacquire the canonical path even while the former owner stays active.
    Backend::owner = 2;
    Backend::live.insert(2);
    Backend::active.insert(2);
    Backend::materials = {20};
    cache.Refresh(t + 10s);
    Check(Backend::finds == 3 && Backend::scans == 2 && Backend::scanned_owner == 2);
    Check(cache.Materials()[0].object == 20 && Backend::Weak::handles == 2);

    // Descendant/material replacement under the same owner is rediscovered.
    Backend::materials = {21};
    cache.Refresh(t + 15s);
    Check(Backend::finds == 4 && Backend::scans == 3 && cache.Materials()[0].object == 21);

    // Failed scans retire stale membership and retry at the same bounded rate.
    Backend::scan_ok = false;
    cache.Refresh(t + 20s);
    cache.Refresh(t + 21s);
    Check(Backend::finds == 5 && Backend::scans == 4 && cache.Materials().empty());
    Check(Backend::Weak::handles == 1);
    Backend::scan_ok     = true;
    Backend::fail_retain = 2;
    cache.Refresh(t + 25s);
    Check(Backend::finds == 6 && Backend::scans == 4 && Backend::Weak::handles == 0);
    cache.Refresh(t + 26s);
    Check(Backend::finds == 6 && Backend::scans == 4);
    Backend::fail_retain = 0;

    // Preserve coverage above256 materials: the ceiling follows the legacy
    // renderer/material scan bounds, rather than arbitrarily truncating roads.
    Backend::materials.resize(1000);
    std::iota(Backend::materials.begin(), Backend::materials.end(), 10000);
    cache.Refresh(t + 30s);
    Check(cache.Materials().size() == 1000 && Backend::Weak::handles == 1001);
    Check(cache.Materials().front().object == 10000 && cache.Materials().back().object == 10999);
    Backend::materials.resize(haven_roads::kMaterialLimit + 10);
    std::iota(Backend::materials.begin(), Backend::materials.end(), 20000);
    cache.Refresh(t + 35s);
    Check(cache.Materials().size() == haven_roads::kMaterialLimit);
    Check(Backend::Weak::handles == static_cast<int>(haven_roads::kMaterialLimit + 1));
    // A failed material weak retain publishes no stale/unretained entries, and
    // does not turn the next owner callback into another full discovery scan.
    Backend::materials.assign(1000, 31);
    Backend::fail_retain = 31;
    cache.Refresh(t + 40s);
    const auto scans = Backend::scans;
    cache.Refresh(t + 41s);
    Check(cache.Materials().empty() && Backend::Weak::handles == 1 && Backend::scans == scans);
    Backend::fail_retain = 0;
    Backend::materials.resize(1000);
    std::iota(Backend::materials.begin(), Backend::materials.end(), 30000);
    cache.Refresh(t + 45s);
    Check(cache.Materials().size() == 1000 && Backend::Weak::handles == 1001);
    for (int i = 1; i < 96; ++i) {
      cache.Refresh(t + 45s + i * 5s);
      Check(Backend::Weak::handles == 1001);
    }
    // Native-inactive but managed-live owner invalidates immediately, with no
    // scene-wide retry before the next scheduled discovery.
    Backend::active.erase(2);
    cache.Refresh(t + 521s);
    Check(cache.Materials().empty() && Backend::Weak::handles == 0);
    const auto finds = Backend::finds;
    cache.Refresh(t + 522s);
    Check(Backend::finds == finds);
    Backend::owner = 0;
    cache.Refresh(t + 525s);
    Check(Backend::finds == finds + 1 && Backend::Weak::handles == 0);
  }
  Check(Backend::Weak::handles == 0);
}
