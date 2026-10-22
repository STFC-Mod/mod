// Real reloads against an isolated filesystem boundary, including readers that outlive a settings replacement.
#include "config.h"
#include "file.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <latch>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace
{
const auto directory = std::filesystem::temp_directory_path()
                       / ("stfc-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
const std::string config_name = "settings.toml", vars_name = "runtime.vars";
bool              fail_vars = false;

void Check(bool ok, const char* message)
{
  if (!ok)
    throw std::runtime_error(message);
}
void Write(std::string_view settings)
{
  std::ofstream file(directory / config_name);
  file.exceptions(std::ios::failbit | std::ios::badbit);
  file << settings;
}
} // namespace

namespace File
{
const std::string& Config()
{ return config_name; }
const std::string& DefaultConfig()
{ return config_name; }
const std::string& Vars()
{ return vars_name; }
std::filesystem::path Path(std::string_view name, bool)
{
  if (fail_vars && name == vars_name)
    throw std::runtime_error("runtime vars directory is unavailable");
  return directory / name;
}
void MigrateLegacyFiles() {}
} // namespace File

namespace unity::Key
{
bool Pressed(KeyCode)
{ return false; }
bool Down(KeyCode)
{ return false; }
bool IsModified()
{ return false; }
} // namespace unity::Key

int main()
{
  spdlog::set_level(spdlog::level::off);
  std::filesystem::create_directories(directory);
  int result = 0;
  try {
    Write(R"(
      [patches]
      uiscalehooks = false
      [shortcuts]
      quit = "F1"
      [sync]
      resolver_cache_ttl = 123
      [sync.targets.test]
      url = "https://example.invalid"
      token = "original"
      resources = true
    )");
    Check(!Config::Get().patches.uiscalehooks && Shortcut::Describe(Action::kQuit) == "F1", "startup activation");
    auto                              first   = Config::Sync::Get();
    std::weak_ptr<const Config::Sync> retired = first;
    std::latch                        captured(1);
    bool                              reader_ok = false;
    // This reader spans every reload below and must keep its original settings throughout.
    std::jthread reader([snapshot = std::move(first), &captured, &reader_ok](std::stop_token stop) {
      std::mutex                  mutex;
      std::condition_variable_any ready;
      std::unique_lock            lock(mutex);
      captured.count_down();
      ready.wait(lock, stop, [] { return false; });
      reader_ok = snapshot->resolver_cache_ttl == 123 && snapshot->options.resources
                  && snapshot->targets.at("test").token == "original";
    });
    captured.wait();
    bool reloads_ok = true;
    for (int i = 0; i < 20; ++i) {
      std::weak_ptr<const Config::Sync> previous = Config::Sync::Get();
      Write(std::format("[patches]\nuiscalehooks = true\n[shortcuts]\nquit = \"F2\"\n[sync]\nresolver_cache_ttl = {}",
                        200 + i));
      reloads_ok &= Config::Reload();
      reloads_ok &= Config::Sync::Get()->resolver_cache_ttl == 200 + i;
      if (i != 0)
        reloads_ok &= previous.expired();
    }
    const bool reader_retained = !retired.expired();
    reader.request_stop();
    reader.join();
    Check(reloads_ok, "reloads replace snapshots and reclaim unreferenced settings");
    Check(reader_retained && reader_ok && retired.expired(), "an old snapshot lives exactly as long as its reader");
    Check(!Config::Get().patches.uiscalehooks && Shortcut::Describe(Action::kQuit) == "F2",
          "reload activates bindings but preserves startup hook groups");
    const auto current  = Config::Sync::Get();
    const auto ui_scale = Config::Get().graphics.ui_scale;
    Write("[shortcuts]\nquit = \"F3\"\n[sync]\nresolver_cache_ttl = 999\n[graphics]\nui_scale = 2.0");
    fail_vars = true;
    Check(!Config::Reload(), "a filesystem preparation failure rejects the reload");
    fail_vars = false;
    Check(Config::Sync::Get() == current && Shortcut::Describe(Action::kQuit) == "F2"
              && Config::Get().graphics.ui_scale == ui_scale,
          "a rejected reload changes neither settings, snapshots nor bindings");
    Write("[broken");
    Check(!Config::Reload() && Config::Sync::Get() == current && Shortcut::Describe(Action::kQuit) == "F2",
          "invalid TOML preserves the current settings");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    result = 1;
  }
  std::filesystem::remove_all(directory);
  if (!result)
    std::cout << "config reload tests passed\n";
  return result;
}
