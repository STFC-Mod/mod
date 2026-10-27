#include "settings/boolean_settings.h"
#include <cstdlib>
#include <iostream>

using namespace mod_settings;
#define CHECK(x)                                                                                                       \
  do {                                                                                                                 \
    if (!(x)) {                                                                                                        \
      std::cerr << "FAILED line " << __LINE__ << ": " << #x << '\n';                                                   \
      std::exit(1);                                                                                                    \
    }                                                                                                                  \
  } while (false)

struct Fake {
  bool                  value = false, available = true, reject = false, fail_readback = false, throw_write = false;
  unsigned              reads = 0, writes = 0;
  std::uint64_t         generation = 1;
  std::function<void()> on_read, on_write;
  Definition            definition(std::string id = "test.confirm")
  {
    return {std::move(id), "[MOD] Confirm test",
            [this] {
              ++reads;
              if (on_read)
                on_read();
              return available ? ReadResult::Known(value, generation) : ReadResult{};
            },
            [this](bool desired, std::uint64_t expected) {
              ++writes;
              CHECK(expected == generation);
              if (on_write)
                on_write();
              if (throw_write)
                throw std::runtime_error("write");
              if (reject)
                return ApplyResult::Rejected;
              value = desired;
              if (fail_readback)
                available = false;
              return ApplyResult::Applied;
            }};
  }
};

int main()
{
  {
    Fake            f;
    BooleanSetting  setting(f.definition());
    CHECK(f.reads == 0 && f.writes == 0); // Construction schedules no work.
    auto* s       = &setting;
    auto initial = s->Observe();
    CHECK(initial.state.known() && !*initial.state.value);
    CHECK(s->SetFromUser(false, initial).outcome == Outcome::Unchanged);
    CHECK(f.writes == 0);
    auto result = s->SetFromUser(true, initial);
    CHECK(result.outcome == Outcome::AppliedVerified && result.snapshot.state.value == true && f.writes == 1);
    CHECK(s->SetFromUser(false, initial).outcome == Outcome::Conflict);
    CHECK(f.writes == 1);
    CHECK(s->SetFromUser(false, result.snapshot).outcome == Outcome::AppliedVerified);
    CHECK(f.writes == 2);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    auto           before = s.Observe();
    {
      BooleanSetting::RenderScope outer(s);
      BooleanSetting::RenderScope inner(s);
      s.Observe();
      CHECK(s.SetFromUser(true, before).outcome == Outcome::Suppressed);
    }
    CHECK(f.writes == 0);
    CHECK(s.SetFromUser(true, before).outcome == Outcome::AppliedVerified);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    auto           before = s.Observe();
    f.on_write            = [&] { CHECK(s.SetFromUser(true, before).outcome == Outcome::Busy); };
    CHECK(s.SetFromUser(true, before).outcome == Outcome::AppliedVerified);
    CHECK(f.writes == 1);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    auto           before = s.Observe();
    f.on_read             = [&] { CHECK(s.SetFromUser(true, before).outcome == Outcome::Busy); };
    CHECK(s.SetFromUser(true, before).outcome == Outcome::AppliedVerified);
    CHECK(f.writes == 1);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    auto           before = s.Observe();
    f.reject              = true;
    auto result           = s.SetFromUser(true, before);
    CHECK(result.outcome == Outcome::Rejected && result.snapshot.state.value == false);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    auto           before = s.Observe();
    f.fail_readback       = true;
    auto result           = s.SetFromUser(true, before);
    CHECK(result.outcome == Outcome::Unverified && !result.snapshot.state.value && f.value);
    CHECK(f.writes == 1); // Never attempt an unverified reverse write.
    f.available = true;
    CHECK(s.Observe().state.value == true);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    auto           before = s.Observe();
    f.throw_write         = true;
    CHECK(s.SetFromUser(true, before).outcome == Outcome::Unverified);
    f.throw_write = false;
    CHECK(s.SetFromUser(true, s.Observe()).outcome == Outcome::AppliedVerified);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    auto           before = s.Observe();
    f.available           = false;
    CHECK(!s.Observe().state.value);
    CHECK(s.SetFromUser(true, before).outcome == Outcome::Rejected);
    CHECK(f.writes == 0);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    auto           before = s.Observe();
    ++f.generation;
    CHECK(s.SetFromUser(true, before).outcome == Outcome::Conflict);
    CHECK(f.writes == 0);
    before = s.Observe();
    s.InvalidateSession();
    CHECK(s.SetFromUser(true, before).outcome == Outcome::Conflict);
    CHECK(f.writes == 0);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    auto           before = s.Observe();
    f.on_write            = [&] { s.InvalidateSession(); };
    CHECK(s.SetFromUser(true, before).outcome == Outcome::Unverified);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    f.on_read = [&] { s.InvalidateSession(); };
    CHECK(!s.Observe().state.known());
  }
  {
    Fake           f;
    BooleanSetting s(f.definition()), other(f.definition("other"));
    CHECK(s.SetFromUser(true, other.Observe()).outcome == Outcome::Conflict);
    CHECK(f.writes == 0);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    f.on_read = [&] { CHECK(!s.Observe().state.known()); };
    CHECK(s.Observe().state.known());
  }
  {
    Definition     malformed{"invalid", "invalid", [] { return ReadResult{Availability::Known, true, 0}; },
                             [](bool, std::uint64_t) { return ApplyResult::Applied; }};
    BooleanSetting s(std::move(malformed));
    CHECK(!s.Observe().state.value);
  }
  {
    Fake           f;
    BooleanSetting s(f.definition());
    bool           rejected = false;
    std::thread    other([&] {
      try {
        s.Observe();
      } catch (const std::logic_error&) {
        rejected = true;
      }
    });
    other.join();
    CHECK(rejected && f.reads == 0);
  }
  std::cout << "PASS boolean settings: readback, failure, refresh, reentry, generation and thread contracts\n";
}
