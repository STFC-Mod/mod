#include <spud/arch.h>
#include <spud/detour.h>
#include <spud/memory/protection.h>
#include <spud/utils.h>

#include "detour_impl.h"
#include "remapper.h"
#include "target_registry.h"

#if SPUD_OS_APPLE
#include <libkern/OSCacheControl.h>
#endif

#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>

#if SPUD_OS_APPLE || SPUD_OS_LINUX
#define ASM_FUNC(n, a) n a asm(#n)
#else
#define ASM_FUNC(n, a) n a
#endif

extern "C" uintptr_t ASM_FUNC(spud_read_context_value, ());

namespace spud {
namespace {
std::atomic<detour_diagnostic_handler> diagnostic_handler = nullptr;
}

void set_detour_diagnostic_handler(detour_diagnostic_handler handler) noexcept {
  diagnostic_handler.store(handler, std::memory_order_release);
}

namespace detail {
namespace {

void report_conflict(const target_owner &candidate,
                     const target_owner &incumbent) noexcept {
  std::array<char, 512> message{};
  std::snprintf(
      message.data(), message.size(),
      "spud: duplicate detour rejected (requested=0x%" PRIxPTR
      ", canonical=0x%" PRIxPTR ", replacement=0x%" PRIxPTR
      "); existing owner requested=0x%" PRIxPTR ", canonical=0x%" PRIxPTR
      ", replacement=0x%" PRIxPTR ")",
      candidate.requested_address, candidate.canonical_address,
      candidate.replacement_address, incumbent.requested_address,
      incumbent.canonical_address, incumbent.replacement_address);

  if (const auto handler = diagnostic_handler.load(std::memory_order_acquire);
      handler != nullptr) {
    try {
      handler(message.data());
      return;
    } catch (...) {
    }
  }

  std::fprintf(stderr, "%s\n", message.data());
  std::fflush(stderr);
}

} // namespace

struct DetourImpl {
  std::vector<uint8_t> (*create_absolute_jump)(uintptr_t target,
                                               uintptr_t data);
  std::tuple<relocation_info, size_t> (*collect_relocations)(uintptr_t address,
                                                             size_t jump_size);
  trampoline_buffer (*create_trampoline)(
      uintptr_t return_address, std::span<uint8_t> target,
      const relocation_info &relocation_infos);
  uintptr_t (*maybe_resolve_jump)(uintptr_t) = [](auto v) { return v; };
};

const static std::array<DetourImpl, Arch::kCount> kDetourImpls = {
    // x86_64
    DetourImpl{.create_absolute_jump = x64::create_absolute_jump,
               .collect_relocations = x64::collect_relocations,
               .create_trampoline = x64::create_trampoline,
               .maybe_resolve_jump = x64::maybe_resolve_jump},
    // x86
    DetourImpl{.create_absolute_jump = x64::create_absolute_jump},
#if SPUD_AARCH64_SUPPORT
    // Arm64
    DetourImpl{.create_absolute_jump = arm64::create_absolute_jump,
               .collect_relocations = arm64::collect_relocations,
               .create_trampoline = arm64::create_trampoline,
               .maybe_resolve_jump = arm64::maybe_resolve_jump},
#endif
};

detail::detour &detour::install(Arch arch) {
  if (installed_) {
    last_install_status_ = detour_install_status::already_installed;
    return *this;
  }
  if (context_container_ == nullptr) {
    last_install_status_ = detour_install_status::not_installed;
    return *this;
  }

  const auto &impl = kDetourImpls[arch];

  // We don't want to hook things that point to a direct jump
  // This will resolve that jump and instead we hook the underlying function
  // func_ = impl.maybe_resolve_jump(func_);
  // TODO(alex): This will break hook stacking most likely right now...
  const auto canonical_address = impl.maybe_resolve_jump(requested_address_);
  const target_owner candidate = {
      .requested_address = requested_address_,
      .canonical_address = canonical_address,
      .owner_token = reinterpret_cast<uintptr_t>(context_container_.get()),
      .replacement_address = func_,
  };
  const auto claim = detour_target_registry().claim(candidate);
  if (claim.status == target_claim_status::conflict) {
    last_install_status_ = detour_install_status::duplicate_target;
    report_conflict(candidate, claim.incumbent);
    return *this;
  }
  if (claim.status == target_claim_status::already_owned) {
    last_install_status_ = detour_install_status::already_installed;
    return *this;
  }

  address_ = canonical_address;
  wrapper_ = impl.maybe_resolve_jump(wrapper_);

  bool jit_write_protection_disabled = false;
  try {
    const auto jump = impl.create_absolute_jump(
        wrapper_, reinterpret_cast<uintptr_t>(context_container_.get()));

    auto [relocation_infos, required_trampoline_size] =
        impl.collect_relocations(address_, jump.size());

    // Required trampoline size is how many bytes we have to take up of the
    // original function This will then be used to calculate the expanded size,
    // since we do have some instruction replacement and expansion going on

    auto trampoline = impl.create_trampoline(
        address_ + required_trampoline_size,
        {reinterpret_cast<uint8_t *>(address_), required_trampoline_size},
        relocation_infos);

    // TODO(tashcan): This isn't particularly amazing, we might have to adjust
    // permissions
    disable_jit_write_protection();
    jit_write_protection_disabled = true;

    auto trampoline_address = alloc_executable_memory(trampoline.data.size());
    assert(trampoline_address != nullptr);

    std::memcpy(trampoline_address, trampoline.data.data(),
                trampoline.data.size());
    trampoline_ =
        trampoline.start + reinterpret_cast<uintptr_t>(trampoline_address);

    context_container_->func = func_;
    context_container_->trampoline = trampoline_;
#if __cpp_lib_source_location && SPUD_DETOUR_TRACING
    context_container_->location = location_;
#endif

    {
      const auto copy_size = required_trampoline_size;
      original_func_data_.resize(copy_size);
      std::memcpy(original_func_data_.data(),
                  reinterpret_cast<void *>(address_), copy_size);

      remapper remap(address_, copy_size);
      {
        SPUD_SCOPED_PROTECTION(remap, copy_size,
                               mem_protection::READ_WRITE_EXECUTE);

        // This will leave some trashed instructions
        // Which is okay for now
        // TODO(tashcan): Add some kind of NOP function to make the remaining
        // stuff "valid"
        std::memcpy(reinterpret_cast<void *>(uintptr_t(remap)), jump.data(),
                    jump.size());
#if SPUD_OS_APPLE
        sys_dcache_flush((void *)address_, copy_size);
#endif
      }
#if SPUD_OS_APPLE
      sys_icache_invalidate((void *)address_, copy_size);
#endif
    }

    enable_jit_write_protection();
    jit_write_protection_disabled = false;
  } catch (...) {
    if (jit_write_protection_disabled) {
      enable_jit_write_protection();
    }
    detour_target_registry().release(candidate);
    original_func_data_.clear();
    trampoline_ = 0;
    address_ = requested_address_;
    throw;
  }

  installed_ = true;
  last_install_status_ = detour_install_status::installed;
  return *this;
}

void detour::remove() {
  if (!installed_ || original_func_data_.empty()) {
    return;
  }
  assert(context_container_.get() != nullptr);

  disable_jit_write_protection();
  {
    remapper remap(address_, original_func_data_.size());
    {
      SPUD_SCOPED_PROTECTION(remap, original_func_data_.size(),
                             mem_protection::READ_WRITE_EXECUTE);
      std::memcpy(reinterpret_cast<void *>(uintptr_t(remap)),
                  original_func_data_.data(), original_func_data_.size());
    }
  }
  enable_jit_write_protection();

  original_func_data_.clear();
  detour_target_registry().release({
      .requested_address = requested_address_,
      .canonical_address = address_,
      .owner_token = reinterpret_cast<uintptr_t>(context_container_.get()),
      .replacement_address = func_,
  });
  installed_ = false;
  trampoline_ = 0;
  address_ = requested_address_;
  last_install_status_ = detour_install_status::not_installed;
}

detour::Self &detour::detach() {
  if (!installed_ || original_func_data_.empty()) {
    return *this;
  }

  original_func_data_.clear();
  // The target remains patched, so both the callback context and target claim
  // intentionally live until process exit.
  (void)context_container_.release();
  return *this;
}

uintptr_t detour::get_context_value() {
  return spud_read_context_value();
}

} // namespace detail
} // namespace spud
