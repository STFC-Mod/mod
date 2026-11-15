target("mods")
do
    add_ldflags("-v")
    set_kind("static")

    -- Regenerate embedded image headers when their source image changes.
    before_build(function(target)
        local function embed_image(input_file, output_file, symbol)
            if not os.isfile(input_file) then
                raise("[error] missing file: " .. input_file)
                return
            end

            if os.isfile(output_file) and os.mtime(output_file) >= os.mtime(input_file) then
                return
            end

            local fh = io.open(input_file, "rb")
            if not fh then
                raise("[error] cannot open: " .. input_file)
                return
            end

            local data = fh:read("*all")
            fh:close()

            local size = #data

            os.mkdir(path.directory(output_file))

            local out = io.open(output_file, "w")
            if not out then
                print("[error] cannot write: " .. output_file)
                return
            end

            out:write("#pragma once\n\n")
            out:write(string.format("static const unsigned char %s[] = {\n", symbol))

            for i = 1, size do
                out:write(string.format("0x%02X", data:byte(i)))
                if i < size then out:write(", ") end
                if i % 16 == 0 then out:write("\n") end
            end

            out:write("\n};\n\n")
            out:write(string.format("static const size_t %s_SIZE = %d;\n", symbol, size))

            out:close()

            cprint("${green}[embed] %s -> %s (%d bytes)${clear}",
                input_file,
                path.filename(output_file),
                size
            )
        end

        local assets = path.join(target:scriptdir(), "../assets")
        local outdir  = path.join(target:scriptdir(), "src/patches/parts")

        local loading = get_config("bg_image")
        if not loading or loading == "" then
            loading = path.join(assets, "loadingscreen.png")
        end

        embed_image(
            loading,
            path.join(outdir, "embedded_loading_image.h"),
            "g_embeddedLoadingImage"
        )

        embed_image(
            path.join(assets, "stfc-mod-logo.png"),
            path.join(outdir, "embedded_logo_image.h"),
            "g_embeddedLogoImage"
        )

        embed_image(
            path.join(assets, "official-cc-logo.png"),
            path.join(outdir, "embedded_cc_logo_image.h"),
            "g_embeddedCCLogoImage"
        )
    end)

    -- C++ sources
    -- Override only the cxx file rule on Windows CI. This leaves protobuf's
    -- separate .proto rule and generated-object build path untouched.
    if is_plat("windows") and os.getenv("STFC_MSVC_SCCACHE") == "1" then
        add_rules("stfc.cxx.sccache", {override = true})
    end
    add_files("src/**.cc")
    add_headerfiles("src/**.h")
    add_includedirs("src", { public = true })

    -- Packages
    add_packages("spud", "nlohmann_json", "protobuf", "libil2cpp", "eastl", "toml++", "spdlog", "simdutf", "libcurl", "capstone", "cpr")
    if os.getenv("STFC_PROTOBUF_SCCACHE") == "1" then
        add_rules("stfc.protobuf.cpp.sccache")
    else
        add_rules("protobuf.cpp")
    end
    add_files("src/prime/proto/*.proto")

    set_exceptions("cxx")
    add_defines("NOMINMAX")

    if is_mode("releasedbg") then
        add_defines("_MODDBG")  -- enable your debug flag
    end

    if get_config("use_original_bg") then
        add_defines("_USE_ORIGINAL_BG")
    end

    -- Platform-specific settings
    if is_plat("windows") then
        add_cxflags("/bigobj")
        add_linkdirs("src/il2cpp")
        add_syslinks("winmm")
    elseif is_plat("macosx") then
        add_cxflags("-fms-extensions")
        -- Add Objective-C++ source
        add_files("src/*.mm")
        -- Link Cocoa framework
        add_frameworks("Cocoa")
    end

    set_policy("build.optimization.lto", true)
end
