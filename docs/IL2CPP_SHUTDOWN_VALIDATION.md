# macOS runtime shutdown hook validation

Client: `1.000.53284` (`100053284`), inspected 14 November 2026.
GameAssembly.dylib SHA-256: `a17be3929bc7545b2537093586d971ad800fe9ef4fe15a22063936b731491108`.

Quitting reached `exit -> __cxa_finalize_ranges -> il2cpp::GcHandle::~GcHandle ->
il2cpp::gc::GCHandle::Free`, after the runtime had already torn down its handle table.
The shutdown hook disables native handle operations before runtime teardown. This is terminal
process shutdown; in-process reload guards do not disable handles.

| Slice | Export | Export extent | Runtime::Shutdown body | Body extent |
| --- | --- | --- | --- | --- |
| arm64 | `0x493b6c` | 4 bytes | `0x500230` | 620 bytes |
| x86_64 | `0x472d00` | 16 bytes | `0x4f1ee0` | 608 bytes |

Both exports are too small for SPUD's architecture-specific absolute patch (up to 32 bytes on
ARM64; 24 bytes rounded to instruction boundaries on Intel). The code validates and follows
the ARM64 `b` instruction or Intel `push rbp; mov rbp, rsp; pop rbp; jmp rel32` wrapper,
then stages the hook directly on the substantive body. No client addresses are hardcoded.
SPUD verifies the resolved target against the loaded image's function boundaries at runtime.

The ARM64 body starts with stack allocation, register saves, and an ADRP load; the Intel body
starts with register saves, stack allocation, and a RIP-relative load. Both have sufficient
extent for relocation. A changed wrapper or a rejected body causes mod startup to fail with
a diagnostic rather than leaving GC handle teardown unprotected.

This validates static hook fit for both macOS slices, not in-game shutdown behavior or Windows.
