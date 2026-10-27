#include "config_save.h"
#include "runtime_config_writer.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

int main(int argc, char** argv)
{
  assert(argc == 2);
  const auto path = std::filesystem::path(argv[1]) / "community_patch_settings.toml";
  std::filesystem::create_directories(path.parent_path());
  const std::string original = "# keep this comment\n[graphics]\ngalaxy_multi_select = false\n"
                               "galaxy_label_major_detail = \"native\"\ngalaxy_label_major_threshold = 0.5\n"
                               "[unrelated]\nvalue = 42\n";
  ReplaceConfigText(path, original);
  {
    config_edit::RuntimeConfigWriter writer(path);
    assert(writer.Register("ui", "keep_base_card_open", std::nullopt));
    assert(writer.Register("control", "primary_requires_visible_target", std::nullopt));
    assert(writer.Register("control", "kirshara_fixes", std::nullopt));
    assert(writer.Register("graphics", "galaxy_multi_select", false));
    assert(writer.Register("graphics", "galaxy_label_major_detail", std::string("native")));
    assert(writer.Register("graphics", "galaxy_label_major_threshold", 0.5));
    assert(writer.Submit("control", "kirshara_fixes", false));
    assert(writer.Submit("ui", "keep_base_card_open", true));
    assert(writer.Submit("control", "primary_requires_visible_target", false));
    assert(writer.Submit("graphics", "galaxy_multi_select", true));
    assert(writer.Submit("graphics", "galaxy_label_major_detail", std::string("threshold")));
    // Quit immediately with a slider write still debounced: orderly stop must flush it.
    assert(writer.Submit("graphics", "galaxy_label_major_threshold", 0.95, std::chrono::seconds(30)));
    writer.Stop(false);
  }
  // Fresh parse models a new process, with no access to live Config or writer state.
  const auto bytes  = ReadConfigText(path);
  const auto loaded = toml::parse(bytes);
  assert(loaded["graphics"]["galaxy_multi_select"].value<bool>() == true);
  assert(loaded["graphics"]["galaxy_label_major_detail"].value<std::string>() == "threshold");
  assert(loaded["graphics"]["galaxy_label_major_threshold"].value<double>() == 0.95);
  assert(loaded["ui"]["keep_base_card_open"].value<bool>() == true);
  const auto kirshara = loaded["control"]["kirshara_fixes"].value<bool>();
  assert(kirshara && !*kirshara);
  const auto primary = loaded["control"]["primary_requires_visible_target"].value<bool>();
  assert(primary && !*primary);
  assert(loaded["unrelated"]["value"].value<int>() == 42);
  assert(bytes.starts_with("# keep this comment"));
  {
    config_edit::RuntimeConfigWriter writer(path);
    assert(writer.Register("control", "kirshara_fixes", config_edit::Value{*kirshara}));
    assert(writer.Register("control", "primary_requires_visible_target", config_edit::Value{*primary}));
    assert(writer.Submit("control", "kirshara_fixes", true));
    assert(writer.Submit("control", "primary_requires_visible_target", true));
    writer.Stop(false);
  }
  const auto restored = toml::parse(ReadConfigText(path));
  assert(restored["control"]["kirshara_fixes"].value<bool>() == true);
  assert(restored["control"]["primary_requires_visible_target"].value<bool>() == true);
  assert(restored["unrelated"]["value"].value<int>() == 42);

  // A lazily opened page must retain its startup comparison value, even when
  // another page started the worker and the file was subsequently edited.
  {
    config_edit::RuntimeConfigWriter writer(path);
    assert(writer.Register("ui", "keep_base_card_open", true));
    const auto first    = writer.Submit("ui", "keep_base_card_open", false);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (writer.LastCompletion().revision < first || writer.HasWork()) {
      assert(std::chrono::steady_clock::now() < deadline);
      std::this_thread::yield();
    }
    auto       external = ReadConfigText(path);
    const auto position = external.find("galaxy_label_major_threshold");
    assert(position != std::string::npos);
    const auto value_begin = external.find('=', position) + 1;
    const auto value_end   = external.find_first_of("\r\n#", value_begin);
    assert(value_begin && value_end != std::string::npos);
    external.replace(value_begin, value_end - value_begin, " 0.85");
    ReplaceConfigText(path, external);
    assert(writer.Register("graphics", "galaxy_label_major_threshold", 0.95));
    // A repeated definition is not permission to accept the new disk base.
    assert(writer.Register("graphics", "galaxy_label_major_threshold", 0.85));
    assert(writer.Submit("graphics", "galaxy_label_major_threshold", 0.9));
    writer.Stop(false);
    while (!writer.PollStopped()) {
      assert(std::chrono::steady_clock::now() < deadline);
      std::this_thread::yield();
    }
    assert(writer.GetStatus("graphics", "galaxy_label_major_threshold").outcome == config_edit::Outcome::Conflict);
    assert(writer.HasFailures());
    assert(toml::parse(ReadConfigText(path))["graphics"]["galaxy_label_major_threshold"].value<double>() == 0.85);
  }
  std::cout << "Runtime settings disk/reload regression passed\n";
}
