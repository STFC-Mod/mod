//! First-party code: the IL2CPP and Unity libraries, the mod, its platform entry points, and the macOS loader.
const std = @import("std");
const Ctx = @import("ctx.zig").Ctx;
const Lib = @import("ctx.zig").Lib;
const Options = @import("ctx.zig").Options;
const globFiles = @import("ctx.zig").globFiles;
const deps = @import("deps.zig");

/// Runtime bindings: metadata lookup, typed calls, strings, GC handles and detours. Also exposes Unity's
/// libil2cpp headers.
pub fn il2cpp(ctx: *const Ctx) Lib {
    const b = ctx.b;
    const headers = Lib{
        .include_dirs = ctx.dirs(&.{ b.path("src/libs/il2cpp/include"), b.path("third_party/libil2cpp") }),
        // il2cpp/api.h asks the linker for GameAssembly.lib on Windows.
        .lib_dirs = if (ctx.isWindows()) ctx.dirs(&.{b.path("src/libs/il2cpp")}) else &.{},
    };
    const module = ctx.createModule(true);
    headers.includeInto(module);
    module.addCSourceFiles(.{
        .root = b.path("src/libs/il2cpp"),
        .files = &.{ "src/api.cc", "src/il2cpp.cc" },
        .flags = ctx.cxxFlags("c++23", &.{}),
    });
    const artifact = ctx.staticLib("il2cpp", module);
    b.step("il2cpp", "Build the IL2CPP integration static library").dependOn(&b.addInstallArtifact(artifact, .{}).step);
    return .{ .artifact = artifact, .include_dirs = headers.include_dirs, .lib_dirs = headers.lib_dirs };
}

/// Unity engine bindings over il2cpp: core types, UI, input and instance tracking. Knows nothing about the game.
pub fn unity(ctx: *const Ctx, runtime: Lib, spud: Lib) Lib {
    const b = ctx.b;
    const include = ctx.dirs(&.{b.path("src/libs/unity/include")});
    const module = ctx.createModule(true);
    for (include) |dir| module.addIncludePath(dir);
    runtime.includeInto(module);
    spud.includeInto(module);
    module.addCSourceFiles(.{
        .root = b.path("src/libs/unity"),
        .files = &.{ "src/input.cc", "src/tracker.cc" },
        .flags = ctx.cxxFlags("c++23", &.{}),
    });
    const artifact = ctx.staticLib("unity", module);
    b.step("unity", "Build the Unity bindings static library").dependOn(&b.addInstallArtifact(artifact, .{}).step);
    return .{ .artifact = artifact, .include_dirs = include };
}

/// The `mods` static library and the binary the game loads: version.dll on Windows, the injected dylib and its
/// loader on macOS. `libs` is everything the mod compiles against.
pub fn add(ctx: *const Ctx, libs: []const Lib) void {
    const b = ctx.b;
    const flags = ctx.cxxFlags("c++23", &.{
        "-fms-extensions",
        "-Wno-invalid-offsetof",
        "-Wno-deprecated-literal-operator",
        "-Wno-unused-command-line-argument",
    });

    // The mod compiles against dependency headers only. Libraries link at the final binary, so the mod's
    // translation units compile alongside its dependencies instead of after them.
    const module = ctx.createModule(true);
    module.addIncludePath(b.path("src/mod"));
    if (ctx.options.moddbg) module.addCMacro("_MODDBG", "1");
    if (ctx.options.use_original_bg) module.addCMacro("_USE_ORIGINAL_BG", "1");
    for (libs) |lib| lib.includeInto(module);
    embedImages(ctx, module);
    addModSources(ctx, module, flags);
    const mods = ctx.staticLib("mods", module);
    const mods_step = b.step("mods", "Build the core mod static library");
    mods_step.dependOn(&b.addInstallArtifact(mods, .{}).step);
    for (libs) |lib| lib.addBuildDependencies(mods_step);

    const entry = ctx.createModule(true);
    entry.addIncludePath(b.path("src/mod"));
    entry.linkLibrary(mods);
    for (libs) |lib| lib.linkInto(entry);
    const entry_root = b.path(if (ctx.isWindows()) "src/platform/windows" else "src/platform/macos/dylib");
    entry.addCSourceFiles(.{
        .root = entry_root,
        .files = globFiles(b, entry_root, "", &.{".cc"}, &.{}),
        .flags = flags,
    });
    const library = b.addLibrary(.{
        .name = if (ctx.isWindows()) "version" else "stfc-community-mod",
        .linkage = .dynamic,
        .root_module = entry,
    });
    ctx.applyCompile(library);
    if (ctx.isWindows()) {
        // version.dll proxies the system DLL: the .def forwards its exports, the .rc carries the build identity.
        library.win32_module_definition = b.path("src/platform/windows/version.def");
        const rc_flags = addBuildIdentity(ctx, entry);
        entry.addIncludePath(b.path("src/platform/windows"));
        entry.addWin32ResourceFile(.{
            .file = b.path("src/platform/windows/version.rc"),
            .flags = rc_flags,
            .include_paths = &.{ b.path("src/mod"), b.path("src/platform/windows") },
        });
        inline for (.{ "shell32", "user32", "ole32", "oleaut32" }) |lib| entry.linkSystemLibrary(lib, .{});
    }
    b.installArtifact(library);
    if (ctx.isMacos()) addLoader(ctx, flags);
}

fn addModSources(ctx: *const Ctx, module: *std.Build.Module, flags: []const []const u8) void {
    const b = ctx.b;
    const root = b.path("src/mod");
    module.addCSourceFiles(.{ .root = root, .files = globFiles(b, root, "", &.{".cc"}, &.{}), .flags = flags });

    if (ctx.isMacos()) {
        module.addCSourceFiles(.{ .root = root, .files = globFiles(b, root, "", &.{".mm"}, &.{}), .flags = flags });
        module.linkFramework("Cocoa", .{});
    } else if (ctx.isWindows()) {
        inline for (.{ "winmm", "rpcrt4", "runtimeobject" }) |lib| module.linkSystemLibrary(lib, .{});
    }
}

/// Define the build identity as C++ string macros and as version resource defines. Returns the resource flags.
fn addBuildIdentity(ctx: *const Ctx, module: *std.Build.Module) []const []const u8 {
    const b = ctx.b;
    var defines: [5][2][]const u8 = undefined;
    // Each value comes from -D<option>, then the environment, then the default.
    inline for (.{
        .{ "stfc_source_state_id", "STFC_SOURCE_STATE_ID", "unknown" },
        .{ "stfc_base_commit", "STFC_BASE_COMMIT", "unknown" },
        .{ "stfc_build_invocation_id", "STFC_BUILD_INVOCATION_ID", "zig-direct" },
        .{ "stfc_build_channel", "STFC_BUILD_CHANNEL", "local" },
    }, 0..) |field, i| {
        const option, const env, const default = field;
        const value = b.option([]const u8, option, "Build identity value") orelse
            b.graph.environ_map.get(env) orelse default;
        // Values land inside C and RC string literals; keep them to characters that need no escaping.
        if (value.len == 0 or value.len > 160) std.debug.panic("{s} must contain 1–160 characters", .{option});
        for (value) |char| if (!std.ascii.isAlphanumeric(char) and std.mem.indexOfScalar(u8, "._:+/-", char) == null)
            std.debug.panic("{s} contains unsupported identity characters", .{option});
        defines[i] = .{ env, value };
    }
    const mode = if (ctx.options.moddbg) "releasedbg" else if (ctx.optimize == .Debug) "debug" else "release";
    defines[4] = .{ "STFC_BUILD_MODE", mode };

    var rc_flags: std.ArrayList([]const u8) = .empty;
    for (defines) |define| {
        const name, const value = define;
        const quoted = b.fmt("\"{s}\"", .{value});
        module.addCMacro(name, quoted);
        rc_flags.appendSlice(b.allocator, &.{ "/D", b.fmt("{s}={s}", .{ name, quoted }) }) catch @panic("OOM");
    }
    return rc_flags.items;
}

/// Generate one header per image, holding the PNG bytes the mod draws at run time.
fn embedImages(ctx: *const Ctx, module: *std.Build.Module) void {
    const b = ctx.b;
    const images = b.addWriteFiles();
    // Header, symbol, and source image.
    const table = [_][3][]const u8{
        .{ "embedded_loading_image.h", "g_embeddedLoadingImage", ctx.options.bg_image },
        .{ "embedded_logo_image.h", "g_embeddedLogoImage", "assets/stfc-mod-logo.png" },
        .{ "embedded_cc_logo_image.h", "g_embeddedCCLogoImage", "assets/official-cc-logo.png" },
    };
    for (table) |entry| {
        const header, const symbol, const path = entry;
        const limit: std.Io.Limit = .limited(64 * 1024 * 1024);
        const data = std.Io.Dir.cwd().readFileAlloc(b.graph.io, path, b.allocator, limit) catch |err|
            std.debug.panic("unable to read embedded image {s}: {t}", .{ path, err });
        var out: std.Io.Writer.Allocating = .init(b.allocator);
        // Adjacent string literals of \x escapes. An initializer list of one expression per byte costs Clang far
        // more time and memory. The literal adds a null byte, so _SIZE carries the real length.
        out.writer.print("#pragma once\n#include <cstddef>\nstatic const unsigned char {s}[] =\n", .{symbol}) catch
            @panic("OOM");
        const hex = "0123456789abcdef";
        var offset: usize = 0;
        while (offset < data.len) {
            const chunk = data[offset..@min(offset + 64, data.len)];
            var escaped: [64 * 4]u8 = undefined;
            for (chunk, 0..) |byte, i| escaped[i * 4 ..][0..4].* = .{ '\\', 'x', hex[byte >> 4], hex[byte & 15] };
            out.writer.print("\"{s}\"\n", .{escaped[0 .. chunk.len * 4]}) catch @panic("OOM");
            offset += chunk.len;
        }
        if (data.len == 0) out.writer.writeAll("\"\"\n") catch @panic("OOM");
        out.writer.print(";\nstatic const size_t {s}_SIZE = {d};\n", .{ symbol, data.len }) catch @panic("OOM");
        _ = images.add(header, out.written());
    }
    module.addIncludePath(images.getDirectory());
}

/// The macOS loader executable, which launches the game with the dylib injected.
fn addLoader(ctx: *const Ctx, flags: []const []const u8) void {
    const b = ctx.b;
    const root = b.path("src/platform/macos/loader");
    const module = ctx.createModule(true);
    deps.inifileCpp(ctx).linkInto(module);
    module.addCSourceFiles(.{
        .root = root,
        .files = globFiles(b, root, "", &.{ ".cc", ".mm" }, &.{}),
        .flags = flags,
    });
    module.linkFramework("Foundation", .{});
    b.installArtifact(ctx.executable("stfc-community-mod-loader", module));
}

/// `macos-universal`: run this build for both macOS architectures and lipo the dylib and loader together.
/// Each architecture is a child `zig build` because one graph resolves a single -Dtarget.
pub fn addUniversal(b: *std.Build, optimize: std.builtin.OptimizeMode, options: Options) void {
    const lipo = b.addExecutable(.{
        .name = "lipo",
        .root_module = b.createModule(.{
            .root_source_file = b.path("zig/tools/lipo.zig"),
            .target = b.graph.host,
            .optimize = .ReleaseSafe,
        }),
    });
    const step = b.step("macos-universal", "Build and merge ARM64 and Intel macOS dylib and loader");
    var prefixes: [2]std.Build.LazyPath = undefined;
    inline for (.{ "aarch64", "x86_64" }, 0..) |arch, i| {
        const child = b.addSystemCommand(&.{
            b.graph.zig_exe,
            "build",
            "-Dtarget=" ++ arch ++ "-macos.14.6",
            b.fmt("-Doptimize={s}", .{@tagName(optimize)}),
            b.fmt("-Dlto={}", .{options.lto orelse false}),
            b.fmt("-Dmoddbg={}", .{options.moddbg}),
            b.fmt("-Duse_original_bg={}", .{options.use_original_bg}),
            b.fmt("-Dbg_image={s}", .{options.bg_image}),
        });
        child.setCwd(b.path(""));
        child.has_side_effects = true;
        if (options.macos_sdk) |sdk| child.addArg(b.fmt("-Dmacos-sdk={s}", .{sdk}));
        child.addArg("--prefix");
        prefixes[i] = child.addOutputDirectoryArg(arch);
    }
    inline for (.{ "lib/libstfc-community-mod.dylib", "bin/stfc-community-mod-loader" }) |binary| {
        const merge = b.addRunArtifact(lipo);
        merge.addFileArg(prefixes[0].path(b, binary));
        merge.addFileArg(prefixes[1].path(b, binary));
        const merged = merge.addOutputFileArg(std.fs.path.basename(binary));
        step.dependOn(&b.addInstallFile(merged, binary).step);
    }
}
