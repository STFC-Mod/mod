target("il2cpp")
    set_kind("static")
    add_files("src/*.cc")
    add_includedirs("include", {public = true})
    add_packages("libil2cpp", {public = true})
    set_exceptions("cxx")
    if is_plat("windows") then
        add_linkdirs(".", {public = true})
    end
