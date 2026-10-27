#include "config_save.h"
#include "toml_editor.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>

using namespace config_edit;
static Request Mode(std::optional<Value> expected, std::string desired)
{ return {"ui", "auto_confirm_instant_warp", std::move(expected), std::move(desired)}; }
int main(int argc, char** argv)
{
  assert(argc == 2);
  TomlEditor editor;
  const auto request = Mode(Value{std::string("none")}, "warp");
  for (const std::string original :
       {"# header\n[ui] # section\nauto_confirm_instant_warp = 'none' # comment\nother = 9\n",
        "\xef\xbb\xbf# BOM\r\n[ui]\r\nauto_confirm_instant_warp = 'none' # comment\r\n",
        "ui = { other = '\xc3\xa9', auto_confirm_instant_warp = 'none' } # tail\n",
        "ui.auto_confirm_instant_warp = '''none'''\n[elsewhere]\nx = nan\n",
        "[\"ui\"]\n\"auto_confirm_instant_warp\" = \"\"\"none\"\"\""}) {
    auto edit = editor.Prepare(original, request);
    assert(edit.outcome == Outcome::Prepared);
    const auto parsed = toml::parse(edit.text);
    assert(parsed["ui"]["auto_confirm_instant_warp"].value<std::string>() == "warp");
    // Only the value token changes; comments, surrounding bytes and line endings remain.
    const auto old      = original.find("'''none'''") != std::string::npos         ? "'''none'''"
                          : original.find("\"\"\"none\"\"\"") != std::string::npos ? "\"\"\"none\"\"\""
                                                                                   : "'none'";
    auto       expected = original;
    expected.replace(expected.find(old), std::string(old).size(), "\"warp\"");
    assert(edit.text == expected);
  }
  for (const std::string original :
       {"", "# comments only", "[ui]", "[ui]\nother = 4\n[other]\nx=3\n", "ui = {} # inline\n", "ui = {other = 4}\n",
        "ui.other = 4\n", "[ui.child]\nx = 4\n"}) {
    const auto edit = editor.Prepare(original, Mode(std::nullopt, "jump"));
    assert(edit.outcome == Outcome::Prepared);
    assert(toml::parse(edit.text)["ui"]["auto_confirm_instant_warp"].value<std::string>() == "jump");
  }
  const std::string special = "quotes \"'\\\n[ui]\nauto_confirm_instant_warp = 'jump'\n\xc3\xa9";
  for (const std::string malformed : {std::string("\xff", 1), std::string("\xc0\xaf", 2), std::string("\xe2\x82", 2),
                                      std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4)}) {
    const auto refused = editor.Prepare("[ui]\nauto_confirm_instant_warp='none'\n", Mode(request.expected, malformed));
    assert(refused.outcome == Outcome::Unsupported && refused.text.empty());
  }
  const auto escaped = editor.Prepare("ui = {auto_confirm_instant_warp='none'}", Mode(request.expected, special));
  assert(escaped.outcome == Outcome::Prepared);
  assert(toml::parse(escaped.text)["ui"]["auto_confirm_instant_warp"].value<std::string>() == special);
  assert(editor.Prepare("[ui]\nauto_confirm_instant_warp='jump'", request).outcome == Outcome::Conflict);
  assert(editor.Prepare("[ui]\nauto_confirm_instant_warp='warp'", request).outcome == Outcome::AlreadySaved);
  assert(editor.Prepare("ui = [", request).outcome == Outcome::InvalidDocument);
  assert(editor.Prepare("ui = 3", request).outcome == Outcome::Unsupported);
  assert(editor.Prepare("[ui]\nother=true", request).outcome == Outcome::Conflict);
  // Decoded key identities may contain dots, Unicode and combining codepoints.
  // Columns are parser coordinates, not UTF-8 byte counts or display widths.
  const std::string unicode_section = "ui.\xc3\xa9";
  const std::string unicode_key     = "e\xcc\x81.mode";
  const std::string unicode_document =
      "[\"" + unicode_section + "\"]\r\n\"" + unicode_key + "\" = 'none' # untouched\r\n";
  const auto unicode_edit =
      editor.Prepare(unicode_document, {unicode_section, unicode_key, Value{std::string("none")}, std::string("jump")});
  assert(unicode_edit.outcome == Outcome::Prepared);
  auto unicode_expected = unicode_document;
  unicode_expected.replace(unicode_expected.find("'none'"), 6, "\"jump\"");
  assert(unicode_edit.text == unicode_expected);
  const std::string combining          = "ui = { other = 'e\xcc\x81', auto_confirm_instant_warp = 'none' }\n";
  auto              combining_expected = combining;
  combining_expected.replace(combining_expected.find("'none'"), 6, "\"warp\"");
  assert(editor.Prepare(combining, request).text == combining_expected);
  const Request boolean{"ui", "enabled", Value{false}, true};
  const auto    numeric =
      editor.Prepare("[graphics]\nthreshold = 0.5 # keep\n", {"graphics", "threshold", Value{0.5}, 0.75});
  assert(numeric.outcome == Outcome::Prepared && numeric.text == "[graphics]\nthreshold = 0.75 # keep\n");
  assert(editor.Prepare("[graphics]\nthreshold = 0.6\n", {"graphics", "threshold", Value{0.5}, 0.75}).outcome
         == Outcome::Conflict);
  const auto integer =
      editor.Prepare("[graphics]\nthreshold = 0\n", {"graphics", "threshold", Value{std::int64_t{0}}, 0.5});
  assert(integer.outcome == Outcome::Prepared
         && toml::parse(integer.text)["graphics"]["threshold"].value<double>() == 0.5);
  for (const auto desired : {std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()}) {
    const auto whole =
        editor.Prepare("[graphics]\ncount = 0 # keep\n", {"graphics", "count", Value{std::int64_t{0}}, desired});
    assert(whole.outcome == Outcome::Prepared && whole.text.ends_with(" # keep\n"));
    assert(toml::parse(whole.text)["graphics"]["count"].value<std::int64_t>() == desired);
  }
  for (const auto desired : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
    const auto invalid =
        editor.Prepare("[graphics]\nthreshold = 0.5 # keep\n", {"graphics", "threshold", Value{0.5}, desired});
    assert(invalid.outcome == Outcome::Unsupported && invalid.text.empty());
  }
  assert(editor.Prepare("[ui]\nenabled = false # keep\n", boolean).text == "[ui]\nenabled = true # keep\n");
  assert(editor.Prepare("[ui]\nenabled = true", boolean).outcome == Outcome::AlreadySaved);
  assert(editor.Prepare("[ui]\nenabled = 'false'", boolean).outcome == Outcome::Conflict);
  const std::string escaped_key = "a.\"b\\c";
  for (const std::string document : {"[ui]\n", "ui = {}\n"}) {
    const auto inserted = editor.Prepare(document, {"ui", escaped_key, std::nullopt, true});
    assert(inserted.outcome == Outcome::Prepared);
    assert(toml::parse(inserted.text)["ui"][escaped_key].value<bool>() == true);
  }

  const std::filesystem::path root(argv[1]);
  std::filesystem::create_directories(root);
  const auto        path    = root / "settings.toml";
  const std::string initial = "[ui]\nauto_confirm_instant_warp='none'\nother=true # preserve\n";
  ReplaceConfigText(path, initial);
  const auto external = initial + "# external comment\n";
  ReplaceConfigText(path, external);
  assert(editor.Save(path, request) == Outcome::Saved);
  assert(ReadConfigText(path).ends_with("# external comment\n"));
  ReplaceConfigText(path, "[ui]\nauto_confirm_instant_warp='jump'\n");
  assert(editor.Save(path, request) == Outcome::Conflict);
  assert(!ReplaceConfigText(path, initial, external));
  assert(toml::parse(ReadConfigText(path))["ui"]["auto_confirm_instant_warp"].value<std::string>() == "jump");
  const auto absent = root / "absent.toml";
  assert(!std::filesystem::exists(absent));
  assert(editor.Save(absent, request) == Outcome::IoError);
  assert(!std::filesystem::exists(absent));
  std::cout << "TOML editor preservation/conflict fixtures passed\n";
}
