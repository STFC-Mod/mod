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
includes("mods")


target("il2cpp-bindings-tests")
    set_kind("binary")
    set_default(false)
    set_exceptions("cxx")
    add_files("tests/il2cpp_bindings.cc")
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

-- Pure startup gate regression tests; no game process required.
target("chat-startup-tests")
    set_kind("binary")
    set_default(false)
    add_files("tests/chat_startup.cc")
    add_includedirs("mods/src")
