add_rules("mode.debug", "mode.release")

-- Function to extract version from version.h
function get_version_from_header()
    local version_file = path.join(os.scriptdir(), "../mods/src/version.h")
    if not os.isfile(version_file) then
        return "1.0.0.0"
    end
    
    local content = io.readfile(version_file)
    local major = content:match("#define VERSION_MAJOR%s+(%d+)")
    local minor = content:match("#define VERSION_MINOR%s+(%d+)")
    local revision = content:match("#define VERSION_REVISION%s+(%d+)")
    local patch = content:match("#define VERSION_PATCH%s+(%d+)")
    
    if major and minor and revision and patch then
        return major .. "." .. minor .. "." .. revision .. "." .. patch
    end
    return "1.0.0.0"
end

target("macOSLauncher")
    add_rules("xcode.application")
    set_kind("binary")
    set_basename("macOSLauncher")

    add_files("src/**/*.swift")
    add_files("src/*.swift", "src/*.xcassets")
    add_files("deps/PlzmaSDK/swift/*.swift")
    add_files("src/*.cc")
    add_includedirs("deps/PLzmaSDK")
    add_packages("librsync", "PLzmaSDK")
    add_ldflags("-lc++")

    add_values("xcode.bundle_identifier", "com.stfcmod.startrekpatch")
    add_values("xcode.codesign_identity", "-")
    add_values("xcode.bundle_display_name", "Star Trek Fleet Command Community Mod")
    add_values("xcode.product_name", "STFC Community Mod")
    add_values("xcode.bundle_info_plist", "src/Info.plist")

    -- Keep imported Swift/Clang modules in a stable path that CI can persist.
    local swift_module_cache = path.join(os.projectdir(), "build", ".swift-module-cache")
    add_scflags(
        "-module-cache-path", swift_module_cache,
        "-Xcc -fmodules",
        "-Xcc -fmodule-map-file=macos-launcher/src/module.modulemap",
        "-D SWIFT_PACKAGE",
        {force = true})

    -- Generate Info.plist from template during configuration
    on_config(function (target)
        -- xmake invokes swift-frontend directly, bypassing the swiftc driver that
        -- normally injects macro plugin search paths. Xcode 26+ SDKs implement
        -- SwiftUI property wrappers (@State etc.) as external macros, so point the
        -- frontend at the platform's macro plugins explicitly.
        local sdk_platform = os.iorun("xcrun --show-sdk-platform-path"):trim()
        target:add("scflags", "-external-plugin-path",
            sdk_platform .. "/Developer/usr/lib/swift/host/plugins#" .. sdk_platform .. "/Developer/usr/bin/swift-plugin-server")

        local version_file = path.join(os.scriptdir(), "../mods/src/version.h")
        if not os.isfile(version_file) then
            -- GitHub Actions warning
            print("::warning file=" .. version_file .. "::version.h not found, using default version 1.0.0.0")
            version_file = nil
        end

        local version = "1.0.0.0"
        if version_file then
            local content = io.readfile(version_file)
            local major = content:match("#define VERSION_MAJOR%s+(%d+)")
            local minor = content:match("#define VERSION_MINOR%s+(%d+)")
            local revision = content:match("#define VERSION_REVISION%s+(%d+)")
            local patch = content:match("#define VERSION_PATCH%s+(%d+)")
            if major and minor and revision and patch then
                version = major .. "." .. minor .. "." .. revision .. "." .. patch
            elseif major and minor and revision then
                version = major .. "." .. minor .. "." .. revision .. ".0"
            else
                print("::warning file=" .. version_file .. "::unable to parse, using default version 1.0.0.0")
            end
        end

        -- Generate Info.plist
        local info_plist_template = path.join(os.scriptdir(), "src/Info.plist.template")
        local info_plist_output = path.join(os.scriptdir(), "src/Info.plist")

        if os.isfile(info_plist_template) then
            -- Read the template
            local content = io.readfile(info_plist_template)

            -- Replace ${VERSION} placeholder with actual version
            content = content:gsub("${VERSION}", version)

            -- Write the processed Info.plist
            io.writefile(info_plist_output, content)
            print("Generated Info.plist with version: " .. version)
        else
            print("::warning file=" .. info_plist_template .. "::Info.plist.template not found, skipping generation")
        end
    end)

    -- Clean up generated file after build (optional)
    after_clean(function (target)
        local info_plist_output = path.join(os.scriptdir(), "src/Info.plist")
        if os.isfile(info_plist_output) then
            os.rm(info_plist_output)
            print("Cleaned generated Info.plist")
        end
    end)
