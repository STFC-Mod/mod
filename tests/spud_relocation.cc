#include <spud/detour.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#if _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

// Match the gallery constructor's sub/cmp/jne/lea/call prologue: a RIP-relative
// cmp and lea ahead of a relative call. When both expanded in the trampoline,
// older SPUD misplaced the branch targets after them. The helper increments a
// counter, so both branch paths and call-through are checked by executing the
// installed detour, without a game installation.
int main()
{
#if _WIN32
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  const size_t size = info.dwPageSize;
  auto*        code = static_cast<uint8_t*>(VirtualAlloc(nullptr, size * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
#else
  const auto page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0)
    return 1;
  const auto size = static_cast<size_t>(page_size);
  auto* code = static_cast<uint8_t*>(mmap(nullptr, size * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  if (code == MAP_FAILED)
    code = nullptr;
#endif
  if (!code)
    return 1;

  const uint8_t body[]   = {0x48, 0x83, 0xec, 0x28,                // sub rsp, 40
                            0x80, 0x3d, 0x00, 0x00, 0x00, 0x00, 0, // cmp byte [data], 0
                            0x75, 0x18,                            // jne code+37
                            0x48, 0x8d, 0x0d, 0,    0,    0,    0, // lea rcx, [data+4]
                            0xe8, 0x27, 0,    0,    0,             // call code+64
                            0x8b, 0x05, 0,    0,    0,    0,       // mov eax, [data+4]
                            0x48, 0x83, 0xc4, 0x28,                // add rsp, 40
                            0xc3, 0x90,                            // ret; nop
                            0x8b, 0x05, 0,    0,    0,    0,       // mov eax, [data+4]
                            0x48, 0x83, 0xc4, 0x28, 0xc3};         // add rsp, 40; ret
  const uint8_t helper[] = {0xff, 0x05, 0, 0, 0, 0, 0xc3};         // inc dword [data+4]; ret
  std::memcpy(code, body, sizeof(body));
  std::memcpy(code + 64, helper, sizeof(helper));
  // Rosetta publishes patched code as RX. Keep the flag and counter on their own writable page.
  auto*      data      = code + size;
  const auto reference = [&](size_t offset, size_t next, size_t field) {
    const auto displacement = static_cast<int32_t>(size + field - next);
    std::memcpy(code + offset, &displacement, sizeof(displacement));
  };
  reference(6, 11, 0);
  reference(16, 20, 4);
  reference(27, 31, 4);
  reference(39, 43, 4);
  reference(66, 70, 4);
#if _WIN32
  DWORD previous = 0;
  if (!VirtualProtect(code, size, PAGE_EXECUTE_READ, &previous)) {
    VirtualFree(code, 0, MEM_RELEASE);
    return 1;
  }
  FlushInstructionCache(GetCurrentProcess(), code, size);
#else
  if (mprotect(code, size, PROT_READ | PROT_EXEC) != 0) {
    munmap(code, size * 2);
    return 1;
  }
  __builtin___clear_cache(reinterpret_cast<char*>(code), reinterpret_cast<char*>(code + size));
#endif

  using Function = int (*)(void*);
  auto function  = reinterpret_cast<Function>(code);
  bool passed    = function(nullptr) == 1;
  {
    auto hook = spud::detour<int(Function, void*)>::create(
        function, +[](Function original, void* value) { return original(value) + 100; });
    passed  = passed && hook.try_install() == spud::detour_error::kOk;
    passed  = passed && function(nullptr) == 102;
    data[0] = 1;
    passed  = passed && function(nullptr) == 102;
    data[0] = 0;
    passed  = passed && function(nullptr) == 103;
  }
  passed = passed && function(nullptr) == 4;
#if _WIN32
  VirtualFree(code, 0, MEM_RELEASE);
#else
  munmap(code, size * 2);
#endif
  std::puts(passed ? "SPUD multi-relocation call/branch regression passed" : "SPUD relocation regression FAILED");
  return passed ? 0 : 1;
}
