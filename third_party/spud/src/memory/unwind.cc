// Registration state shared by every platform: source snapshots and
// dependencies. Platforms and formats live in unwind/.
#include "unwind.h"

#include "detour/detour_impl.h"
#include "readable.h"

#include <spud/arch.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace spud::detail {

size_t source_view::read(uintptr_t at, std::span<uint8_t> out) const {
  size_t read = read_code_prefix(at, out);
  // The overlap of [at, at + out.size()) with the view.
  const uintptr_t begin = std::max(at, address);
  const uintptr_t end = std::min(at + out.size(), address + bytes.size());
  if (bytes.empty() || begin >= end)
    return read;
  std::memcpy(out.data() + (begin - at), bytes.data() + (begin - address),
              end - begin);
  // The view can cover what memory could not provide, but only from where
  // the readable bytes end.
  if (begin <= at + read)
    read = std::max<size_t>(read, end - at);
  return read;
}

// Commit rereads every snapshot inside the stop window. Join overlapping and
// adjacent ranges so that costs one read per region rather than per field,
// and close a short gap with known bytes: a syscall costs more than
// comparing a few hundred extra bytes. Ranges whose overlap disagrees stay
// separate, so the mismatch still fails.
std::vector<unwind_registration::snapshot> unwind_registration::merge_snapshots(
    std::vector<snapshot> snapshots,
    std::span<const std::pair<uintptr_t, std::span<const uint8_t>>> known) {
  constexpr size_t kMaxGap = 256;
  std::sort(snapshots.begin(), snapshots.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  std::vector<snapshot> merged;
  for (auto &[address, bytes] : snapshots) {
    if (!merged.empty()) {
      auto &[begin, joined] = merged.back();
      const uintptr_t end = begin + joined.size();
      if (address <= end) {
        const size_t overlap = std::min<size_t>(end - address, bytes.size());
        if (std::equal(bytes.begin(), bytes.begin() + overlap,
                       joined.begin() + (address - begin))) {
          joined.insert(joined.end(), bytes.begin() + overlap, bytes.end());
          continue;
        }
      } else if (address - end <= kMaxGap) {
        const auto gap =
            std::find_if(known.begin(), known.end(), [&](const auto &range) {
              return end >= range.first &&
                     address <= range.first + range.second.size();
            });
        if (gap != known.end()) {
          const auto fill =
              gap->second.subspan(end - gap->first, address - end);
          joined.insert(joined.end(), fill.begin(), fill.end());
          joined.insert(joined.end(), bytes.begin(), bytes.end());
          continue;
        }
      }
    }
    merged.emplace_back(address, std::move(bytes));
  }
  return merged;
}

#if !SPUD_UNWIND_SUPPORT
// Without unwind support nothing is described; the pool still retains the
// source module through the platform file.
struct unwind_registration::platform {};

unwind_registration::unwind_registration() = default;

unwind_registration::~unwind_registration() {
  if (source_module_ != nullptr)
    close_source_module(source_module_);
}

bool unwind_registration::prepare(uintptr_t, size_t, uintptr_t, uintptr_t,
                                  size_t, const trampoline_buffer &,
                                  std::span<const size_t>, size_t,
                                  source_view) {
  return true;
}

bool unwind_registration::publish() {
  return true;
}

std::optional<std::pair<uintptr_t, uintptr_t>> function_extent(uintptr_t) {
  return std::nullopt;
}
#endif

bool unwind_registration::prepare_leaf(uintptr_t allocation, size_t size) {
  return prepare(allocation, size, 0, 0, 0, trampoline_buffer{0, {}, {}});
}

bool unwind_registration::source_matches() const {
  // No allocation here: a stopped thread can hold the allocator lock.
  std::array<uint8_t, 4096> bytes{};
  for (const auto &[address, original] : source_bytes_) {
    for (size_t off = 0; off < original.size();) {
      const size_t count = std::min(bytes.size(), original.size() - off);
      if (!read_code_bytes(address + off, {bytes.data(), count}) ||
          std::memcmp(bytes.data(), original.data() + off, count) != 0)
        return false;
      off += count;
    }
  }
  return true;
}

void unwind_registration::add_dependency(
    const unwind_registration *registration) {
  // A relay shares this trampoline's source, so its FDE, LSDA and landing
  // snapshots mostly repeat ours. Merged here, each region is read once.
  if (registration == nullptr || registration->source_bytes_.empty())
    return;
  auto snapshots = std::move(source_bytes_);
  snapshots.insert(snapshots.end(), registration->source_bytes_.begin(),
                   registration->source_bytes_.end());
  source_bytes_ = merge_snapshots(std::move(snapshots));
}

void *unwind_registration::release_source_module() {
  return std::exchange(source_module_, nullptr);
}

} // namespace spud::detail
