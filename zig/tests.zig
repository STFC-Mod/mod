//! Native test fixtures. They link the same libraries as the shipped mod, and run without the game.
const std = @import("std");
const builtin = @import("builtin");
const Ctx = @import("ctx.zig").Ctx;
const Lib = @import("ctx.zig").Lib;
const deps = @import("deps.zig");

/// The libraries the fixtures link. They are the production artifacts, built at -Doptimize.
pub const Libs = struct {
    il2cpp: Lib,
    spdlog: Lib,
    spud: Lib,
    proto: Lib,
    proto_runtime: Lib,
    toml: Lib,
};

/// Register the fixture steps. Fixture code compiles at -Dtest-optimize, so a Debug fixture keeps its assertions
/// while sharing release libraries with the shipped build.
pub fn add(build_ctx: *const Ctx, libs: Libs) void {
    const ctx = build_ctx.withOptimize(build_ctx.options.test_optimize orelse build_ctx.optimize);
    const b = ctx.b;
    const build_tests = b.step("build-tests", "Compile the tests without running them");
    const test_il2cpp = b.step("test-il2cpp", "Build and run the IL2CPP boundary tests (native targets only)");
    const test_mod = b.step("test-mod", "Build and run the settings and shortcut tests (native targets only)");
    const test_sync = b.step("test-sync", "Run sync record and delivery regression tests without the game");
    const test_launcher = b.step("test-launcher", "Run launcher delta and archive tests (Linux/macOS targets)");
    const test_spud = b.step("test-spud", "Run the x64 trampoline relocation test (native x86_64 targets only)");
    const flags = ctx.cxxFlags("c++23", &.{});
    const mod_flags = ctx.cxxFlags("c++23", &.{"-Wno-deprecated-literal-operator"});

    // The action queue decisions in src/mod, header-only.
    {
        const module = ctx.createModule(true);
        module.addIncludePath(b.path("src/mod"));
        module.addCSourceFile(.{ .file = b.path("tests/action_queue.cc"), .flags = flags });
        addFixture(ctx, build_tests, test_mod, "action-queue-tests", module);
    }

    // IL2CPP bindings and GC handles against a fake runtime API defined in each test file.
    {
        const module = ctx.createModule(true);
        libs.il2cpp.linkInto(module);
        // The fake API must precede consumers so the linker does not pull in production API storage.
        module.addCSourceFile(.{ .file = b.path("tests/il2cpp_bindings.cc"), .flags = flags });
        module.addIncludePath(b.path("src/libs/unity/include"));
        module.addCSourceFile(.{ .file = b.path("src/libs/unity/src/input.cc"), .flags = flags });
        addFixture(ctx, build_tests, test_il2cpp, "il2cpp-bindings-tests", module);
    }
    {
        const module = ctx.createModule(true);
        libs.il2cpp.linkInto(module);
        module.addCSourceFile(.{ .file = b.path("tests/il2cpp_gc.cc"), .flags = flags });
        addFixture(ctx, build_tests, test_il2cpp, "il2cpp-gc-tests", module);
    }
    {
        const module = ctx.createModule(true);
        module.addIncludePath(b.path("src/libs/unity/include"));
        libs.il2cpp.linkInto(module);
        libs.spud.linkInto(module);
        module.addCSourceFile(.{ .file = b.path("tests/unity_tracker.cc"), .flags = flags });
        addFixture(ctx, build_tests, test_il2cpp, "unity-tracker-tests", module);
    }

    // Settings, shortcuts and reload. The fixture paths are defined on mod_logic.cc alone, so the shared settings
    // objects compile with identical flags and come from Clang's cache for the second fixture.
    {
        const module = settingsModule(ctx, libs);
        module.addCSourceFile(.{
            .file = b.path("tests/mod_logic.cc"),
            .flags = ctx.cxxFlags("c++23", &.{
                "-Wno-deprecated-literal-operator",
                b.fmt("-DSTFC_SHORTCUT_DEFAULTS={s}", .{quotedPath(b, "tests/shortcut_defaults.toml")}),
                b.fmt("-DSTFC_EXAMPLE_SETTINGS={s}", .{
                    quotedPath(b, "docs/configuration/example_community_patch_settings_en.toml"),
                }),
            }),
        });
        module.addCSourceFiles(.{
            .files = &.{
                "src/mod/shortcuts.cc",
                "src/mod/config.cc",
                "src/mod/file.cc",
                "src/mod/features/notification_audio.cc",
            },
            .flags = mod_flags,
        });
        if (ctx.isWindows()) {
            // file.cc reads the command line through CommandLineToArgvW.
            module.linkSystemLibrary("shell32", .{});
            module.linkSystemLibrary("winmm", .{});
        }
        if (ctx.isMacos()) {
            module.addCSourceFiles(.{
                .files = &.{ "src/mod/file_macos.mm", "src/mod/features/notification_audio_mac.mm" },
                .flags = flags,
            });
            module.linkFramework("Cocoa", .{});
        }
        addFixture(ctx, build_tests, test_mod, "mod-logic-tests", module);
    }
    {
        const module = settingsModule(ctx, libs);
        module.addCSourceFiles(.{
            .files = &.{
                "tests/config_reload.cc",
                "src/mod/shortcuts.cc",
                "src/mod/config.cc",
                "src/mod/features/notification_audio.cc",
            },
            .flags = mod_flags,
        });
        if (ctx.isWindows()) module.linkSystemLibrary("winmm", .{});
        if (ctx.isMacos()) {
            module.addCSourceFile(.{ .file = b.path("src/mod/features/notification_audio_mac.mm"), .flags = flags });
            module.linkFramework("Cocoa", .{});
        }
        addFixture(ctx, build_tests, test_mod, "config-reload-tests", module);
    }

    // Sync records and delivery state over the generated protobuf messages.
    {
        const module = ctx.createModule(true);
        module.addIncludePath(b.path("src/mod"));
        module.addIncludePath(b.path("src/libs/unity/include"));
        libs.il2cpp.includeInto(module);
        inline for (.{ libs.proto, libs.proto_runtime, libs.spdlog, libs.toml, deps.nlohmannJson(ctx) }) |lib|
            lib.linkInto(module);
        module.addCSourceFiles(.{
            .files = &.{ "tests/sync_logic.cc", "src/mod/sync/records.cc", "src/mod/sync/state.cc" },
            .flags = mod_flags,
        });
        addFixture(ctx, build_tests, test_sync, "sync-logic-tests", module);
    }

    // Launcher deltas in C++, plus the Swift archive code when the host can run swiftc.
    if (ctx.isLinux() or ctx.isMacos()) {
        const module = ctx.createModule(true);
        deps.rsync(ctx).linkInto(module);
        module.addCSourceFiles(.{
            .files = &.{ "tests/launcher_rsync.cc", "src/launcher/macos/rsync.cc" },
            .flags = flags,
        });
        addFixture(ctx, build_tests, test_launcher, "launcher-rsync-tests", module);
        if (builtin.os.tag == .macos and ctx.isMacos()) {
            const compile = b.addSystemCommand(&.{ "xcrun", "swiftc", "-parse-as-library", "-o" });
            const exe = compile.addOutputFileArg("launcher-archive-tests");
            compile.addFileArg(b.path("src/launcher/macos/UpdaterFileOperations.swift"));
            compile.addFileArg(b.path("tests/launcher_archive.swift"));
            const run = std.Build.Step.Run.create(b, "launcher-archive-tests");
            run.addFileArg(exe);
            run.addDirectoryArg(b.path("tests/fixtures/launcher"));
            test_launcher.dependOn(&run.step);
        }
    } else {
        test_launcher.dependOn(&b.addFail("The launcher rsync tests require a Linux or macOS target").step);
    }

    // A prologue where two RIP-relative instructions expand ahead of a relative call, detoured and run. ARM64
    // relocates differently and has nothing to check here.
    if (ctx.target.result.cpu.arch == .x86_64) {
        const module = ctx.createModule(true);
        libs.spud.linkInto(module);
        module.addCSourceFile(.{ .file = b.path("tests/spud_relocation.cc"), .flags = flags });
        addFixture(ctx, build_tests, test_spud, "spud-relocation-tests", module);
    } else {
        test_spud.dependOn(&b.addFail("The SPUD relocation test requires an x86_64 target").step);
    }
}

/// `test-deps` and `build-dependency-test`: exercise protobuf, zlib and the HTTPS backend as the platform links
/// them. Arguments after `--` pass an optional test URL and CA file.
pub fn addDependencySmoke(ctx: *const Ctx, libs: []const Lib) void {
    const b = ctx.b;
    const module = ctx.createModule(true);
    for (libs) |lib| lib.linkInto(module);
    module.addCSourceFile(.{ .file = b.path("tests/dependency_smoke.cc"), .flags = ctx.cxxFlags("c++23", &.{}) });
    const exe = ctx.executable("dependency-smoke", module);
    const run = b.addRunArtifact(exe);
    if (b.args) |args| run.addArgs(args);
    b.step("test-deps", "Run protobuf, zlib and HTTPS backend checks; optionally pass a test URL and CA file")
        .dependOn(&run.step);
    const build_step = b.step("build-dependency-test", "Cross-compile the dependency smoke executable");
    build_step.dependOn(&b.addInstallArtifact(exe, .{}).step);
}

/// Mod headers, logging and TOML for the settings fixtures. il2cpp contributes headers only; settings code never
/// calls into the runtime.
fn settingsModule(ctx: *const Ctx, libs: Libs) *std.Build.Module {
    const b = ctx.b;
    const module = ctx.createModule(true);
    module.addIncludePath(b.path("src/mod"));
    module.addIncludePath(b.path("src/libs/unity/include"));
    libs.spdlog.linkInto(module);
    libs.toml.linkInto(module);
    libs.il2cpp.includeInto(module);
    return module;
}

/// Install `module` as an executable under `build-tests`, and run it under `run_step`.
fn addFixture(
    ctx: *const Ctx,
    build_step: *std.Build.Step,
    run_step: *std.Build.Step,
    name: []const u8,
    module: *std.Build.Module,
) void {
    const b = ctx.b;
    const exe = ctx.executable(name, module);
    build_step.dependOn(&b.addInstallArtifact(exe, .{}).step);
    run_step.dependOn(&b.addRunArtifact(exe).step);
}

/// `path` from the repository root as a C string literal.
fn quotedPath(b: *std.Build, path: []const u8) []const u8 {
    return std.json.Stringify.valueAlloc(b.allocator, b.pathFromRoot(path), .{}) catch @panic("OOM");
}
