// The launcher bridge must report file errors and preserve librsync's patch result.
#include <librsync.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

extern "C" int rsync_apply(const char*, const char*, const char*);

int main()
{
  namespace fs    = std::filesystem;
  const auto root = fs::temp_directory_path()
                    / ("stfc-rsync-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directory(root);
  struct Cleanup {
    fs::path root;
    ~Cleanup()
    { fs::remove_all(root); }
  } cleanup{root};
  int        failures = 0;
  const auto check    = [&](bool ok, const char* message) {
    if (!ok) {
      std::cerr << message << '\n';
      ++failures;
    }
  };
  const auto        basis = (root / "basis").string(), patch = (root / "patch").string();
  const auto        output = (root / "output").string(), missing = (root / "missing").string();
  const std::string old_text(4096, 'a');
  const auto        new_text = old_text + " changed tail\n";
  std::ofstream(basis) << old_text;

  // Generate a real delta with copy and literal data, then check the bridge against the expected file.
  using File = std::unique_ptr<FILE, decltype(&fclose)>;
  File source(fopen(basis.c_str(), "rb"), fclose), signature(tmpfile(), fclose), updated(tmpfile(), fclose);
  File delta(fopen(patch.c_str(), "wb"), fclose);
  if (!source || !signature || !updated || !delta)
    return 1;
  if (rs_sig_file(source.get(), signature.get(), 0, 0, RS_BLAKE2_SIG_MAGIC, nullptr) != RS_DONE)
    return 1;
  rewind(signature.get());
  rs_signature_t* sums = nullptr;
  if (rs_loadsig_file(signature.get(), &sums, nullptr) != RS_DONE)
    return 1;
  std::unique_ptr<rs_signature_t, decltype(&rs_free_sumset)> owned_sums(sums, rs_free_sumset);
  if (rs_build_hash_table(sums) != RS_DONE)
    return 1;
  fwrite(new_text.data(), 1, new_text.size(), updated.get());
  rewind(updated.get());
  if (rs_delta_file(sums, updated.get(), delta.get(), nullptr) != RS_DONE)
    return 1;
  delta.reset();

  check(rsync_apply(missing.c_str(), patch.c_str(), output.c_str()) == RS_IO_ERROR, "missing basis reports IO error");
  check(!fs::exists(output), "missing basis does not create output");
  check(rsync_apply(basis.c_str(), missing.c_str(), output.c_str()) == RS_IO_ERROR, "missing delta reports IO error");
  check(!fs::exists(output), "missing delta does not create output");
  check(rsync_apply(basis.c_str(), patch.c_str(), root.string().c_str()) == RS_IO_ERROR,
        "unwritable output reports IO error");
  check(rsync_apply(basis.c_str(), patch.c_str(), output.c_str()) == RS_DONE, "valid patch succeeds");
  std::ifstream result(output, std::ios::binary);
  check(std::string(std::istreambuf_iterator<char>(result), {}) == new_text, "patch reconstructs expected content");

#if __linux__
  // A small patch can fit in stdio's buffer, so the write error may surface only at fclose.
  check(rsync_apply(basis.c_str(), patch.c_str(), "/dev/full") == RS_IO_ERROR, "output flush error is propagated");
#endif
  std::ofstream(patch, std::ios::binary | std::ios::trunc) << "invalid delta";
  check(rsync_apply(basis.c_str(), patch.c_str(), output.c_str()) != RS_DONE, "invalid delta reports failure");
  if (!failures)
    std::cout << "Launcher rsync bridge tests passed\n";
  return failures ? 1 : 0;
}
