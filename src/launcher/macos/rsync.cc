// Applies one librsync delta for the Swift launcher; see rsync.h. The tests link it without Swift.

#include <librsync.h>

#include <cstdio>
#include <memory>

extern "C" {
int rsync_apply(const char* source_file, const char* patch_file, const char* target_file)
{
  using File = std::unique_ptr<FILE, decltype(&fclose)>;
  File basis_file(fopen(source_file, "rb"), fclose);
  if (!basis_file)
    return RS_IO_ERROR;
  File delta_file(fopen(patch_file, "rb"), fclose);
  if (!delta_file)
    return RS_IO_ERROR;
  File new_file(fopen(target_file, "wb"), fclose);
  if (!new_file)
    return RS_IO_ERROR;

  rs_stats_t stats{};
  auto       result = rs_patch_file(basis_file.get(), delta_file.get(), new_file.get(), &stats);
  if (result != RS_DONE)
    return result;
  // Buffered writes can fail only when the output is closed.
  return fclose(new_file.release()) == 0 ? RS_DONE : RS_IO_ERROR;
}
}
