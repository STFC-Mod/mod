target("stfc-community-mod")
do
    set_kind("shared")
    add_rules("stfc.identity")
    add_files("*.cc")
    add_files("*.rc")
    add_files("version.def")
    add_shflags("/DEF:src/version.def")
    add_deps("mods")
    set_exceptions("cxx")
    if is_plat("windows") then
        add_cxflags("/bigobj")
        add_links("Shell32.lib")
    end
    set_policy("build.optimization.lto", true)
    add_links("User32.lib", "Ole32.lib", "OleAut32.lib")
end
