set_project("stfc-community-mod")

includes("xmake/options.lua")
includes("xmake/dependencies/common.lua")

set_languages("c++23")
set_runtimes("MT")

includes("xmake/rules/stfc_identity.lua")

if is_plat("windows") then
    includes("xmake/dependencies/windows.lua")
    includes("win-proxy-dll")
    add_links("rpcrt4", "runtimeobject")
elseif is_plat("macosx") then
    includes("xmake/dependencies/macos.lua")
    includes("macos-dylib")
    includes("macos-loader")
    includes("macos-launcher")
end

add_rules("mode.debug")
add_rules("mode.release")
add_rules("mode.releasedbg")

includes("xmake/rules/protobuf_sccache.lua")
includes("xmake/rules/cxx_sccache.lua")
includes("libs/il2cpp")
includes("libs/unity")
includes("mods")


target("il2cpp-bindings-tests")
    set_kind("binary")
    set_default(false)
    set_exceptions("cxx")
    add_files("tests/il2cpp_bindings.cc", "libs/unity/src/input.cc")
    add_includedirs("libs/unity/include")
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
    add_files("tests/mod_logic.cc", "mods/src/patches/shortcuts.cc", "mods/src/patches/parts/notification_audio.cc")
    add_includedirs("mods/src", "libs/il2cpp/include", "third_party/libil2cpp", "libs/unity/include")
    add_packages("spdlog", "toml++")
    if is_plat("macosx") then
        add_frameworks("Cocoa")
        add_files("mods/src/patches/parts/notification_audio_mac.mm")
    elseif is_plat("windows") then
        add_syslinks("shell32", "winmm")
    end

target("sync-logic-tests")
    set_kind("binary")
    set_default(false)
    set_exceptions("cxx")
    add_files("tests/sync_logic.cc", "mods/src/patches/parts/sync/records.cc", "mods/src/patches/parts/sync/state.cc")
    add_includedirs("mods/src", "libs/il2cpp/include", "third_party/libil2cpp", "libs/unity/include")
    add_packages("protobuf", "spdlog", "toml++", "nlohmann_json")
    add_rules("protobuf.cpp")
    add_files("mods/src/prime/proto/*.proto")
