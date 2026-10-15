target("stfc-community-mod")
do
    set_kind("shared")
    add_files("*.cc")
    add_deps("mods")
    set_exceptions("cxx")
    set_policy("build.optimization.lto", true)
end
