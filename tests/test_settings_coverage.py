"""Local maintenance gate for the complete in-game settings contribution.

Run with: python -m unittest discover -s tests -p test_settings_coverage.py
No game, credentials, network, or installed configuration is read.
"""

import json
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def source(name):
    return (ROOT / name).read_text(encoding="utf-8")


def sync_options():
    return set(re.findall(
        r'SyncConfig::Option\{.*?"[^\"]+",\s*"([^\"]+)"',
        source("mods/src/config.h"), re.S))


def loader_keys():
    config = source("mods/src/config.cc")
    load = config[config.index("void Config::Load()") :]
    keys = {".".join(pair) for pair in re.findall(
        r'get_config_or_default(?:_with_alias)?(?:<[^>]+>)?\s*'
        r'\(\s*config\s*,\s*parsed\s*,\s*"([^\"]+)"\s*,\s*"([^\"]+)"', load)}
    keys |= {"graphics." + key for key in re.findall(
        r'get_fleet_label_(?:detail|zoom_threshold)\(\s*config,\s*parsed,\s*"([^\"]+)"', load)}
    keys |= {"ui." + key for key in re.findall(
        r'get_mission_hud_visibility\(\s*config,\s*parsed,\s*"([^\"]+)"', load)}
    keys |= {"ui." + key for key in re.findall(
        r'read_instant_warp_filter\(\s*config,\s*parsed,\s*"([^\"]+)"', load)}
    if "read_daily_bulk_claim_factions(config, parsed" in load:
        keys.add("ui.daily_bulk_claim_factions")
    if "get_auto_confirm_instant_warp(config, parsed" in load:
        keys.add("ui.auto_confirm_instant_warp")
    # The audio table is shared by startup loading and notification dispatch.
    keys |= {"audio." + key for key in set(re.findall(r'"(alert_[a-z_]+)"', config))}
    keys |= {"sync." + key for key in sync_options()}
    return keys


class SettingsCoverage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.inventory = json.loads(source("docs/settings-coverage.json"))
        cls.rows = {row["key"]: row for row in cls.inventory["settings"]}
        cls.catalog = source("mods/src/settings/upstream_settings.cc")

    def test_every_loader_key_has_an_explicit_treatment(self):
        self.assertEqual(len(self.rows), len(self.inventory["settings"]), "duplicate canonical key")
        actual = loader_keys()
        self.assertFalse(actual - set(self.rows),
                         f"new config options need coverage: {sorted(actual - set(self.rows))}")
        self.assertFalse(set(self.rows) - actual,
                         f"stale inventory entries: {sorted(set(self.rows) - actual)}")
        for row in self.rows.values():
            self.assertIn(row["treatment"], {"in_game", "toml_only"})
            self.assertIn(row["application"], {"live", "reopen_view", "restart"})
            if row["treatment"] == "toml_only":
                self.assertTrue(self.inventory["toml_only_policies"][row["policy"]])

    def test_every_in_game_key_has_a_definition(self):
        definitions = self.catalog + source("mods/src/settings/warp_mode.cc") + source("mods/src/settings/forbidden_tech.cc")
        literals = set(re.findall(r'"([a-z][a-z_0-9]+)"', definitions))
        literals |= {"hud_" + name for name in ("q_trials", "field_training", "outposts", "missions")
                     if f'"{name}"' in self.catalog}
        for row in self.rows.values():
            if row["treatment"] != "in_game":
                continue
            with self.subTest(key=row["key"]):
                self.assertIn(row["key"].split(".", 1)[1], literals)
                self.assertTrue(row["control"])
                self.assertTrue(row["page"])
        self.assertRegex(self.catalog, r"PersistedDefinition\(\s*std::move\(definition\)")
        self.assertNotIn("runtime_config::SaveSetting", self.catalog,
                         "field callbacks must use their definition's serializer")
        self.assertRegex(self.catalog, r"action->metadata\s*=\s*&setting->metadata\(\)")
        self.assertIn("action->save_status", self.catalog)

    def test_all_registered_shortcuts_are_included(self):
        keys = re.findall(
            r'parse_config_shortcut(?:_aliases)?\(config,\s*parsed,\s*"([^\"]+)"',
            source("mods/src/config.cc"))
        keys += re.findall(r'GameFunction::\w+,\s*"([^\"]+)"', source("mods/src/patches/optional_shortcuts.h"))
        expected = {"shortcuts." + key for key in keys}
        self.assertEqual(len(keys), len(expected), "duplicate canonical shortcut registration")
        self.assertEqual(expected, set(self.inventory["shortcuts"]))
        self.assertIn("DiscoverShortcuts(", source("mods/src/settings/shortcut_settings.cc"))
        self.assertEqual(self.inventory["shortcuts_policy"]["application"], "live")

    def test_dynamic_target_fields_follow_secret_safe_policy(self):
        expected = {"sync.targets.<target>." + key
                    for key in {"url", "token", "proxy", "verify_ssl"} | sync_options()}
        self.assertEqual(expected, set(self.inventory["dynamic_settings"]))
        self.assertEqual(self.inventory["dynamic_settings_policy"]["treatment"], "toml_only")
        self.assertNotRegex(self.catalog, r'\.token\b|\["token"\]')
        self.assertIn("Credentials are never displayed", self.catalog)

    def test_non_ui_actions_share_serializers_with_lazy_initialization(self):
        begin = self.catalog.index("bool PersistCurrentSetting(")
        end = self.catalog.index("void RegisterUpstreamSettings(", begin)
        saver = self.catalog[begin:end]
        self.assertIn("RegisterUpstreamSettings(ModPages())", saver)
        self.assertIn("persist = result.persist", self.catalog)
        self.assertIn("persist(read())", self.catalog)

    def test_only_upstream_feature_dependencies_are_included(self):
        for forbidden in ("claim_trace", "galaxy_controls", "ship_tech_indicators", "opc_indicators",
                          "incoming_player_attack", "audio_file_picker", "notification_desktop",
                          "keyboard_layout", "ship_shortcut_badges"):
            self.assertNotIn(f'#include "{forbidden}', self.catalog)
            self.assertNotIn(f'#include "patches/{forbidden}', self.catalog)
        self.assertEqual(self.rows["control.enable_experimental"]["policy"], "experimental_shortcuts")
        self.assertEqual(self.rows["graphics.system_pan_momentum"]["policy"], "legacy_experimental")


if __name__ == "__main__":
    unittest.main()
