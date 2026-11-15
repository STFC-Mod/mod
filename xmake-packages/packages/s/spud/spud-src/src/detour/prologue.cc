#include <algorithm>
#include <cstring>
#include <spud/detour.h>
#if SPUD_ARCH_X86_64
#include <Zydis/Zydis.h>
#elif SPUD_ARCH_ARM64 && SPUD_AARCH64_SUPPORT
#include <capstone/capstone.h>
#endif

bool spud::has_detour_prologue(const void* address, size_t extent)
{
  if (!address)
    return false;
#if SPUD_ARCH_X86_64
  // mov r11, imm64; jmp [rip]; destination address.
  constexpr size_t overwrite = 24;
  if (extent < overwrite)
    return false;
  ZydisDecoder decoder;
  if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64)))
    return false;
  size_t     bytes = 0;
  const auto limit = std::min(extent, size_t{64});
  while (bytes < overwrite) {
    ZydisDecodedInstruction instruction;
    ZydisDecodedOperand     operands[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, static_cast<const uint8_t*>(address) + bytes, limit - bytes,
                                              &instruction, operands)))
      return false;
    if (instruction.meta.category == ZYDIS_CATEGORY_RET || instruction.meta.category == ZYDIS_CATEGORY_UNCOND_BR
        || instruction.meta.category == ZYDIS_CATEGORY_INTERRUPT)
      return false;
    bytes += instruction.length;
  }
  return true;
#elif SPUD_ARCH_ARM64 && SPUD_AARCH64_SUPPORT
  // ldr; up to four mov instructions; br; destination address.
  constexpr size_t overwrite = 32;
  if (extent < overwrite)
    return false;
  csh handle{};
  if (cs_open(CS_ARCH_AARCH64, CS_MODE_LITTLE_ENDIAN, &handle) != CS_ERR_OK)
    return false;
  cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
  cs_insn*   instructions = nullptr;
  const auto count        = cs_disasm(handle, static_cast<const uint8_t*>(address), std::min(extent, size_t{64}),
                                      reinterpret_cast<uintptr_t>(address), 0, &instructions);
  size_t     bytes        = 0;
  for (size_t i = 0; i < count && bytes < overwrite; ++i) {
    const auto& instruction = instructions[i];
    if (cs_insn_group(handle, &instruction, CS_GRP_RET) || cs_insn_group(handle, &instruction, CS_GRP_INT)
        || std::strcmp(instruction.mnemonic, "b") == 0 || std::strcmp(instruction.mnemonic, "br") == 0)
      break;
    bytes += instruction.size;
  }
  cs_free(instructions, count);
  cs_close(&handle);
  return bytes >= overwrite;
#else
  (void)extent;
  return false;
#endif
}
