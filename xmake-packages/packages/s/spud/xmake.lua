package("spud")
add_deps("cmake")
set_sourcedir(path.join(os.scriptdir(), "spud-src"))

on_install(function(package)
    local configs = {"-DSPUD_BUILD_EXAMPLES=OFF", "-DSPUD_BUILD_BENCHMARKS=OFF"}
    table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
    table.insert(configs, "-DBUILD_SHARED_LIBS=" .. (package:config("shared") and "ON" or "OFF"))
    table.insert(configs, "-DSPUD_BUILD_TESTS=OFF")
    table.insert(configs, "-DSPUD_NO_LTO=ON")
    table.insert(configs, "-DSPUD_DETOUR_TRACING=OFF")
    import("package.tools.cmake").install(package, configs)
end)
