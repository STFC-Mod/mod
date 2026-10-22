//! Per-target compile rules shared by every library and executable in the build.
//! `Ctx` turns project options and a located sysroot into modules and flags; `Lib` carries what a consumer needs
//! to compile against and link one library.
const std = @import("std");
const Build = std.Build;
const toolchains = @import("toolchains.zig");

/// The oldest macOS the mod, loader and launcher support. Scripts and CI pass `-Dtarget=<arch>-macos.14.6` and the
/// launcher's Info.plist gets it from here; change them together.
pub const macos_min = "14.6";

/// Project options that apply to every target. Options that configure a single step stay with that step.
pub const Options = struct {
    /// null keeps the per-target default: on for Windows release builds, off elsewhere.
    lto: ?bool,
    moddbg: bool,
    use_original_bg: bool,
    /// A custom loading screen, relative to the working directory; empty for the built-in one.
    bg_image: []const u8,
    /// Fixture optimization; null follows -Doptimize.
    test_optimize: ?std.builtin.OptimizeMode,
    msvc_sysroot: ?[]const u8,
    macos_sdk: ?[]const u8,

    /// Declare the shared options. Call once from `build`.
    pub fn parse(b: *Build) Options {
        return .{
            .lto = b.option(bool, "lto", "Enable LTO (Windows release default; unsupported for macOS in Zig 0.16)"),
            .moddbg = b.option(bool, "moddbg", "Enable diagnostic patch toggles (_MODDBG)") orelse false,
            .use_original_bg = b.option(
                bool,
                "use_original_bg",
                "Use the original game loading background",
            ) orelse false,
            .bg_image = b.option([]const u8, "bg_image", "Custom loading screen image") orelse "",
            .test_optimize = b.option(
                std.builtin.OptimizeMode,
                "test-optimize",
                "Optimization mode for test fixtures (dependencies use -Doptimize)",
            ),
            .msvc_sysroot = b.option([]const u8, "msvc-sysroot", "Path to an extracted MSVC + Windows SDK toolchain"),
            .macos_sdk = b.option([]const u8, "macos-sdk", "Path to a MacOSX.sdk directory"),
        };
    }
};

/// One target's build settings. Every module and compile step in the graph goes through this.
pub const Ctx = struct {
    b: *Build,
    target: Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    options: Options,
    lto: bool,
    sysroot: toolchains.Sysroot,

    /// Resolve LTO and locate the platform SDK. Linux targets only build native fixtures and need no sysroot.
    pub fn init(
        b: *Build,
        target: Build.ResolvedTarget,
        optimize: std.builtin.OptimizeMode,
        options: Options,
    ) *const Ctx {
        const os = target.result.os.tag;
        const lto = options.lto orelse (os == .windows and optimize != .Debug);
        if (lto and os == .macos)
            std.debug.panic("Zig 0.16 does not support Mach-O LTO; use -Dlto=false for macOS", .{});
        const ctx = b.allocator.create(Ctx) catch @panic("OOM");
        ctx.* = .{
            .b = b,
            .target = target,
            .optimize = optimize,
            .options = options,
            .lto = lto,
            .sysroot = if (os == .windows or os == .macos) toolchains.find(b, target, options) else .{},
        };
        return ctx;
    }

    /// A copy of this context compiling at `optimize`. Debug fixtures never use LTO.
    pub fn withOptimize(ctx: *const Ctx, optimize: std.builtin.OptimizeMode) *const Ctx {
        const copy = ctx.b.allocator.create(Ctx) catch @panic("OOM");
        copy.* = ctx.*;
        copy.optimize = optimize;
        if (optimize == .Debug) copy.lto = false;
        return copy;
    }

    // Target checks. The build host is `builtin.os.tag`; host-only steps such as swiftc and protoc check that.
    pub fn isWindows(ctx: *const Ctx) bool {
        return ctx.target.result.os.tag == .windows;
    }

    pub fn isMacos(ctx: *const Ctx) bool {
        return ctx.target.result.os.tag == .macos;
    }

    pub fn isLinux(ctx: *const Ctx) bool {
        return ctx.target.result.os.tag == .linux;
    }

    pub fn isAarch64(ctx: *const Ctx) bool {
        return ctx.target.result.cpu.arch == .aarch64;
    }

    /// A C or C++ module for this target with the sysroot attached. C++ uses libc++ everywhere except Windows,
    /// which links the static MSVC STL and CRT from the sysroot.
    pub fn createModule(ctx: *const Ctx, cpp: bool) *Build.Module {
        const b = ctx.b;
        const module = b.createModule(.{
            .target = ctx.target,
            .optimize = ctx.optimize,
            .link_libc = true,
            .link_libcpp = if (cpp and !ctx.isWindows()) true else null,
            .pic = true,
            // Zig turns UBSan on in Debug, but a trap inside a game detour takes the client down.
            .sanitize_c = .off,
        });

        if (ctx.isWindows()) {
            if (ctx.sysroot.msvc_root) |root| {
                inline for (.{ "um", "shared", "winrt", "cppwinrt" }) |dir|
                    module.addSystemIncludePath(.{ .cwd_relative = b.pathJoin(&.{ root, "winsdk/include", dir }) });
                inline for (.{ "winsdk/lib/um/x64", "winsdk/lib/ucrt/x64", "msvc/lib/x64" }) |dir|
                    module.addLibraryPath(.{ .cwd_relative = b.pathJoin(&.{ root, dir }) });
            } else if (b.graph.environ_map.get("INCLUDE")) |includes| {
                // No cached SDK on a Windows host: a Visual Studio developer shell supplies the paths, including
                // C++/WinRT, which Zig's own MSVC detection leaves out. The shell sets both; LIB is read only with
                // INCLUDE.
                var include_dirs = std.mem.tokenizeScalar(u8, includes, ';');
                while (include_dirs.next()) |dir| module.addSystemIncludePath(.{ .cwd_relative = dir });
                var lib_dirs = std.mem.tokenizeScalar(u8, b.graph.environ_map.get("LIB") orelse "", ';');
                while (lib_dirs.next()) |dir| module.addLibraryPath(.{ .cwd_relative = dir });
            }
            // The static release CRT and STL (/MT), so version.dll needs no Visual C++ redistributable.
            inline for (.{ "libcmt", "libcpmt", "libucrt", "libvcruntime" }) |lib|
                module.linkSystemLibrary(lib, .{});
        } else if (ctx.sysroot.macos_sdk) |sdk| {
            module.addSystemIncludePath(.{ .cwd_relative = b.pathJoin(&.{ sdk, "usr/include" }) });
            module.addSystemFrameworkPath(.{ .cwd_relative = b.pathJoin(&.{ sdk, "System/Library/Frameworks" }) });
            module.addLibraryPath(.{ .cwd_relative = b.pathJoin(&.{ sdk, "usr/lib" }) });
        }
        return module;
    }

    /// Apply the libc file and LTO. Every library and executable built for the target goes through here.
    pub fn applyCompile(ctx: *const Ctx, compile: *Build.Step.Compile) void {
        if (ctx.sysroot.msvc_libc_file) |libc| compile.setLibCFile(libc);
        if (ctx.lto) {
            compile.lto = .full;
            compile.use_lld = true;
        }
    }

    /// A static library from `module`, through `applyCompile`.
    pub fn staticLib(ctx: *const Ctx, name: []const u8, module: *Build.Module) *Build.Step.Compile {
        const lib = ctx.b.addLibrary(.{ .name = name, .linkage = .static, .root_module = module });
        ctx.applyCompile(lib);
        return lib;
    }

    /// An executable from `module`, through `applyCompile`.
    pub fn executable(ctx: *const Ctx, name: []const u8, module: *Build.Module) *Build.Step.Compile {
        const exe = ctx.b.addExecutable(.{ .name = name, .root_module = module });
        ctx.applyCompile(exe);
        return exe;
    }

    /// An owned copy of `list`. `Lib` fields outlive the caller's stack, and `&.{...}` of runtime values points
    /// into it, so those literals must be copied.
    pub fn dirs(ctx: *const Ctx, list: []const Build.LazyPath) []const Build.LazyPath {
        return ctx.b.allocator.dupe(Build.LazyPath, list) catch @panic("OOM");
    }

    /// An owned copy of `list`, for `Lib.dependencies`. See `dirs`.
    pub fn libs(ctx: *const Ctx, list: []const Lib) []const Lib {
        return ctx.b.allocator.dupe(Lib, list) catch @panic("OOM");
    }

    /// Compile `files` as one translation unit of `#include`s, so headers they share are parsed once. Only use it
    /// for sources that tolerate each other's macros and statics.
    pub fn addCxxBatch(
        ctx: *const Ctx,
        module: *Build.Module,
        name: []const u8,
        root: Build.LazyPath,
        files: []const []const u8,
        flags: []const []const u8,
    ) void {
        const b = ctx.b;
        var contents: std.Io.Writer.Allocating = .init(b.allocator);
        const generated = b.addWriteFiles();
        for (files, 0..) |file, index| {
            // Changing an included source must also change the generated batch's cache key.
            _ = generated.addCopyFile(root.path(b, file), b.fmt("inputs/{d}.cc", .{index}));
            const path = b.pathJoin(&.{ root.getPath(b), file });
            std.mem.replaceScalar(u8, path, '\\', '/');
            const quoted = std.json.Stringify.valueAlloc(b.allocator, path, .{}) catch @panic("OOM");
            contents.writer.print("#include {s}\n", .{quoted}) catch @panic("OOM");
        }
        const source = generated.add(b.fmt("{s}-sources.cc", .{name}), contents.written());
        module.addCSourceFile(.{ .file = source, .flags = flags });
    }

    /// `addCxxBatch` over groups of four files, named `<name>-0`, `<name>-1` and so on. Each group shares header
    /// parsing without serializing the whole module into one unit.
    pub fn addCxxBatches(
        ctx: *const Ctx,
        module: *Build.Module,
        name: []const u8,
        root: Build.LazyPath,
        files: []const []const u8,
        flags: []const []const u8,
    ) void {
        var start: usize = 0;
        while (start < files.len) : (start += 4) {
            const batch = files[start..@min(start + 4, files.len)];
            ctx.addCxxBatch(module, ctx.b.fmt("{s}-{d}", .{ name, start / 4 }), root, batch, flags);
        }
    }

    /// C flags for this target, then `extra`. On Windows, -fms-runtime-lib=static selects the static CRT that
    /// `createModule` links, so objects and libraries agree on /MT.
    pub fn cFlags(ctx: *const Ctx, extra: []const []const u8) []const []const u8 {
        const allocator = ctx.b.allocator;
        var list: std.ArrayList([]const u8) = .empty;
        if (ctx.isWindows()) list.append(allocator, "-fms-runtime-lib=static") catch @panic("OOM");
        list.appendSlice(allocator, extra) catch @panic("OOM");
        return list.items;
    }

    /// C++ flags at `std_ver` with exceptions enabled, then `extra`.
    pub fn cxxFlags(ctx: *const Ctx, comptime std_ver: []const u8, extra: []const []const u8) []const []const u8 {
        const allocator = ctx.b.allocator;
        var list: std.ArrayList([]const u8) = .empty;
        list.appendSlice(allocator, &.{ "-std=" ++ std_ver, "-fexceptions", "-fcxx-exceptions" }) catch @panic("OOM");
        // NOMINMAX keeps windows.h from defining min and max macros over std::min and std::max. Zig passes
        // -nostdinc++ when it uses the MSVC STL; Clang's MSVC driver reports it unused, and Zig turns that warning
        // into an error.
        if (ctx.isWindows()) list.appendSlice(allocator, &.{
            "-DNOMINMAX",
            "-fms-runtime-lib=static",
            "-Wno-unused-command-line-argument",
        }) catch @panic("OOM");
        list.appendSlice(allocator, extra) catch @panic("OOM");
        return list.items;
    }
};

/// A compiled or header-only library, as its consumers see it.
pub const Lib = struct {
    /// null for header-only libraries and for system libraries named in `system_libs`.
    artifact: ?*Build.Step.Compile = null,
    /// Libraries this one links against. Consumers link them too, instead of this artifact linking them, so
    /// independent static libraries compile in parallel.
    dependencies: []const Lib = &.{},
    system_libs: []const []const u8 = &.{},
    include_dirs: []const Build.LazyPath = &.{},
    /// Preprocessor defines consumers must match, such as SPDLOG_COMPILED_LIB.
    defines: []const [2][]const u8 = &.{},
    /// Import library folders the library's headers ask the linker for, such as GameAssembly.lib.
    lib_dirs: []const Build.LazyPath = &.{},

    /// Compile against this library's headers without linking it. Static libraries use this so they never wait
    /// for each other; the final binary links everything.
    pub fn includeInto(lib: Lib, module: *Build.Module) void {
        for (lib.include_dirs) |dir| module.addIncludePath(dir);
        for (lib.defines) |d| module.addCMacro(d[0], d[1]);
        for (lib.lib_dirs) |dir| module.addLibraryPath(dir);
    }

    /// Compile against the headers and link the library with its dependencies.
    pub fn linkInto(lib: Lib, module: *Build.Module) void {
        lib.includeInto(module);
        if (lib.artifact) |artifact| module.linkLibrary(artifact);
        for (lib.dependencies) |dependency| dependency.linkInto(module);
        for (lib.system_libs) |name| module.linkSystemLibrary(name, .{});
    }

    /// Make `step` build this library and its whole dependency closure.
    pub fn addBuildDependencies(lib: Lib, step: *Build.Step) void {
        if (lib.artifact) |artifact| step.dependOn(&artifact.step);
        for (lib.dependencies) |dependency| dependency.addBuildDependencies(step);
    }

    /// A named step that installs this library and builds its dependencies, for checking one library alone.
    pub fn addInstallStep(lib: Lib, b: *Build, name: []const u8, description: []const u8) *Build.Step {
        const step = b.step(name, description);
        step.dependOn(&b.addInstallArtifact(lib.artifact.?, .{}).step);
        lib.addBuildDependencies(step);
        return step;
    }
};

/// Files under `root/sub_path` ending in one of `exts`, sorted, relative to `root` and with forward slashes.
/// A file is skipped when its path contains any of the `exclude` substrings.
pub fn globFiles(
    b: *Build,
    root: Build.LazyPath,
    sub_path: []const u8,
    exts: []const []const u8,
    exclude: []const []const u8,
) []const []const u8 {
    const io = b.graph.io;
    const root_path = root.getPath3(b, null);
    var dir = root_path.root_dir.handle.openDir(io, b.pathJoin(&.{ root_path.sub_path, sub_path }), .{
        .iterate = true,
    }) catch |err| std.debug.panic("unable to open {s}/{s}: {t}", .{ root_path.sub_path, sub_path, err });
    defer dir.close(io);
    var walker = dir.walk(b.allocator) catch @panic("OOM");
    defer walker.deinit();

    var out: std.ArrayList([]const u8) = .empty;
    next: while (walker.next(io) catch |err| std.debug.panic("walk failed: {t}", .{err})) |entry| {
        if (entry.kind != .file) continue;
        const ext = std.fs.path.extension(entry.basename);
        for (exts) |want| {
            if (std.mem.eql(u8, ext, want)) break;
        } else continue;
        const rel = b.pathJoin(&.{ sub_path, entry.path });
        // Windows hosts join with backslashes; exclusions and callers expect logical paths.
        std.mem.replaceScalar(u8, rel, '\\', '/');
        for (exclude) |skip| if (std.mem.indexOf(u8, rel, skip) != null) continue :next;
        out.append(b.allocator, rel) catch @panic("OOM");
    }
    std.mem.sort([]const u8, out.items, {}, struct {
        fn lessThan(_: void, lhs: []const u8, rhs: []const u8) bool {
            return std.mem.lessThan(u8, lhs, rhs);
        }
    }.lessThan);
    return out.items;
}

/// A program the build runs on the host, from one Zig source file under zig/tools.
pub fn hostTool(
    b: *Build,
    name: []const u8,
    source: []const u8,
    optimize: std.builtin.OptimizeMode,
) *Build.Step.Compile {
    return b.addExecutable(.{
        .name = name,
        .root_module = b.createModule(.{
            .root_source_file = b.path(source),
            .target = b.graph.host,
            .optimize = optimize,
        }),
    });
}

/// The step that builds and runs the `test` blocks of a Zig source file on the host.
pub fn hostTests(b: *Build, source: []const u8) *Build.Step {
    const tests = b.addTest(.{
        .root_module = b.createModule(.{ .root_source_file = b.path(source), .target = b.graph.host }),
    });
    return &b.addRunArtifact(tests).step;
}
