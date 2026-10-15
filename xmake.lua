set_project("stfc-community-mod")

includes("xmake/options.lua")
includes("xmake/dependencies/common.lua")

set_languages("c++23")
set_runtimes("MT")

includes("xmake/rules/stfc_identity.lua")

if is_plat("windows") then
    includes("xmake/dependencies/windows.lua")
    includes("src/platform/windows")
    add_links("rpcrt4", "runtimeobject")
elseif is_plat("macosx") then
    includes("xmake/dependencies/macos.lua")
    includes("src/platform/macos/dylib")
    includes("src/platform/macos/loader")
    includes("src/launcher/macos")
end

add_rules("mode.debug")
add_rules("mode.release")
add_rules("mode.releasedbg")

includes("xmake/rules/protobuf_sccache.lua")
includes("xmake/rules/cxx_sccache.lua")
includes("src/libs/il2cpp")
includes("src/libs/unity")
includes("src/mod")


target("il2cpp-bindings-tests")
    set_kind("binary")
    set_default(false)
    set_exceptions("cxx")
    add_files("tests/il2cpp_bindings.cc", "src/libs/unity/src/input.cc")
    add_includedirs("src/libs/unity/include")
    add_deps("il2cpp")

target("il2cpp-gc-tests")
    set_kind("binary")
    set_default(false)
    set_exceptions("cxx")
    add_files("tests/il2cpp_gc.cc")
    add_deps("il2cpp")

if is_arch("x64", "x86_64") then
    target("spud-relocation-tests")
        set_kind("binary")
        set_default(false)
        add_files("tests/spud_relocation.cc")
        add_packages("spud")
end

target("mod-logic-tests")
    set_kind("binary")
    set_default(false)
    set_exceptions("cxx")
    add_files("tests/mod_logic.cc", "src/mod/shortcuts.cc", "src/mod/config.cc", "src/mod/file.cc", "src/mod/features/notification_audio.cc")
    add_includedirs("src/mod", "src/libs/il2cpp/include", "third_party/libil2cpp", "src/libs/unity/include")
    add_packages("spdlog", "toml++")
    if is_plat("macosx") then
        add_files("mods/src/file_macos.mm")
    end
    if is_plat("macosx") then
        add_frameworks("Cocoa")
        add_files("src/mod/features/notification_audio_mac.mm")
    elseif is_plat("windows") then
        add_syslinks("shell32", "winmm")
    end
    add_defines('STFC_EXAMPLE_SETTINGS="' .. path.translate(path.absolute("docs/configuration/example_community_patch_settings_en.toml"), "/") .. '"')
    add_defines('STFC_SHORTCUT_DEFAULTS="' .. path.translate(path.absolute("tests/shortcut_defaults.toml"), "/") .. '"')

target("sync-logic-tests")
    set_kind("binary")
    set_default(false)
    set_exceptions("cxx")
    add_files("tests/sync_logic.cc", "src/mod/sync/records.cc", "src/mod/sync/state.cc")
    add_includedirs("src/mod", "src/libs/il2cpp/include", "third_party/libil2cpp", "src/libs/unity/include")
    add_packages("protobuf", "spdlog", "toml++", "nlohmann_json")
    add_rules("protobuf.cpp")
    add_files("src/mod/game/proto/*.proto")

target("unity-tracker-tests")
    set_kind("binary")
    set_default(false)
    set_exceptions("cxx")
    add_files("tests/unity_tracker.cc")
    add_includedirs("src/libs/unity/include")
    add_packages("spud")
    add_deps("il2cpp")

target("config-reload-tests")
    set_kind("binary")
    set_default(false)
    set_exceptions("cxx")
    add_files("tests/config_reload.cc", "src/mod/shortcuts.cc", "src/mod/config.cc", "src/mod/features/notification_audio.cc")
    add_includedirs("src/mod", "src/libs/il2cpp/include", "third_party/libil2cpp", "src/libs/unity/include")
    add_packages("spdlog", "toml++")
    if is_plat("macosx") then
        add_frameworks("Cocoa")
        add_files("src/mod/features/notification_audio_mac.mm")
    elseif is_plat("windows") then
        add_syslinks("shell32", "winmm")
    end

target("launcher-rsync-tests")
    set_kind("binary")
    set_default(false)
    set_exceptions("cxx")
    add_files("tests/launcher_rsync.cc", "src/launcher/macos/rsync.cc")
    add_includedirs("src/launcher/macos")
    add_packages("librsync")
