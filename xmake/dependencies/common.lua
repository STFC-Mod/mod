-- Keep package versions and their xmake-repo revisions reproducible across
-- developer machines and CI runners.
set_policy("package.requires_lock", true)

-- Cold CI configuration is dominated by CMake-backed dependency builds.
-- Ninja gives those package builds a fast, parallel generator.
set_policy("package.cmake_generator.ninja", true)

add_repositories("stfc-community-mod-repo xmake-packages")

package("libil2cpp")
    on_fetch(function(package, opt)
        return {includedirs = path.join(os.projectdir(), "third_party/libil2cpp")}
    end)
package_end()

add_requires("eastl")
add_requires("spdlog")
add_requires("toml++")
add_requires("nlohmann_json")
add_requires("protobuf 35.1")
add_requires("cpr", {system = false})
add_requires("spud v0.2.0-4")
add_requires("libil2cpp")
add_requires("simdutf", {system = false})

add_requireconfs("cpr.libcurl.zlib", {
    system = false,
    configs = {
        shared = false
    }
})
