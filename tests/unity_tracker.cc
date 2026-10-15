// Exercise the private GC callbacks without installing detours in the test process.
#include "il2cpp_test_api.h"

#include <cstdio>
#include <cstdlib>

// No fixture path may enter the live runtime. These traps also replace Windows' GameAssembly imports.
#define DO_API(ret, name, params)                                                                                      \
  API(ret, name, params)                                                                                               \
  {                                                                                                                    \
    std::fputs("Unexpected runtime call: " #name "\n", stderr);                                                        \
    std::abort();                                                                                                      \
  }                                                                                                                    \
  END_API
#define DO_API_NO_RETURN(ret, name, params) DO_API(ret, name, params)
#include <il2cpp-api-functions.h>
#undef DO_API
#undef DO_API_NO_RETURN

#include "../src/libs/unity/src/tracker.cc"

#include <array>
#include <iostream>
#include <stdexcept>

namespace
{
// Namespace storage is zero-initialized; MSVC rejects {} for this trailing-array SDK type.
Il2CppClass base_class, derived_class, leaf_class;

struct Base : il2cpp::Object {
  struct Class {
    static Il2CppClass* Raw()
    { return &base_class; }
  };
};

void Require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}

void Mark(Il2CppObject* object)
{ object->klass = reinterpret_cast<Il2CppClass*>(reinterpret_cast<uintptr_t>(object->klass) | 1); }
} // namespace

int main()
{
  using namespace unity::detail;
  derived_class.parent = &base_class;
  leaf_class.parent    = &derived_class;
  Base first{}, middle{}, last{};
  first.klass = last.klass = &base_class;
  middle.klass             = &leaf_class;

  Require(unity::Instances<Base>().empty() && !unity::Newest<Base>(), "empty registry");
  Constructed(nullptr);
  Constructed(&first);
  Constructed(&middle);
  Constructed(&middle); // A hooked base constructor can see the same instance again.
  Constructed(&last);
  const std::array<Il2CppObject*, 3> expected{&first, &middle, &last};
  auto                               snapshot = unity::Instances<Base>();
  Require(std::ranges::equal(snapshot, expected), "inherited instances keep construction order without duplicates");
  Require(Instances(&derived_class) == std::vector<Il2CppObject*>{&middle}, "all ancestor classes are indexed");
  Require(unity::Newest<Base>() == &last, "newest instance");

  Destroyed(&middle);
  Destroyed(&middle);
  Require(std::ranges::equal(snapshot, expected), "a snapshot owns its list after registry changes");
  Require(Instances(&leaf_class).empty() && Instances(&derived_class).empty(), "destroy removes all ancestors");
  Require(Instances(&base_class) == std::vector<Il2CppObject*>{&first, &last}, "destroy preserves survivor order");
  Constructed(&middle);

  int state = 0;
  LivenessFinalize_Hook(
      [&](void* received) {
        Require(received == &state, "liveness state is forwarded");
        Mark(&first);
        Mark(&middle);
      },
      &state);
  Require(Instances(&base_class) == std::vector<Il2CppObject*>{&last}, "liveness removes marked objects");
  Require(Instances(&leaf_class).empty() && Instances(&derived_class).empty(), "liveness removes ancestor entries");
  Require(unity::Newest<Base>() == &last, "newest survivor");
  LivenessFinalize_Hook([](void*) {}, nullptr);
  Destroyed(&middle); // OnDestroy/finalization may follow the liveness sweep, with the mark bit still set.

  bool finalized = false;
  OnFinalize(&last, new ChainedFinalizer{[](void* object, void* data) {
                                           Require(object != nullptr && Instances(&base_class).empty(),
                                                   "untrack before calling the prior finalizer");
                                           *static_cast<bool*>(data) = true;
                                         },
                                         &finalized});
  Require(finalized && !unity::Newest<Base>(), "prior finalizer runs and the last instance is removed");
  Destroyed(nullptr);
  std::cout << "Unity tracker tests passed\n";
}
