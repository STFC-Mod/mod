//! Build graph for the mod, its native test fixtures, and the macOS launcher.
//!
//!   zig/ctx.zig         per-target compile rules (Ctx), library handles (Lib), shared options
//!   zig/toolchains.zig  pinned SDKs: fetch-toolchains and sysroot lookup
//!   zig/deps.zig        third-party C and C++ libraries
//!   zig/protobuf.zig    protobuf-lite, Abseil, and generated game messages
//!   zig/mod.zig         IL2CPP and Unity libraries, the mod, platform entry points, macos-universal
//!   zig/launcher.zig    the Swift launcher app
//!   zig/tests.zig       native fixtures and the dependency smoke test
//!   zig/tools/          host programs the build runs
const std = @import("std");
const builtin = @import("builtin");
const Ctx = @import("zig/ctx.zig").Ctx;
const Options = @import("zig/ctx.zig").Options;
const globFiles = @import("zig/ctx.zig").globFiles;
const deps = @import("zig/deps.zig");
const launcher = @import("zig/launcher.zig");
const mod = @import("zig/mod.zig");
const protobuf = @import("zig/protobuf.zig");
const tests = @import("zig/tests.zig");
const toolchains = @import("zig/toolchains.zig");

comptime {
    if (builtin.zig_version.order(.{ .major = 0, .minor = 16, .patch = 0 }) != .eq)
        @compileError("This build is pinned to Zig 0.16.0");
}

pub fn build(b: *std.Build) void {
    // Steps that do not depend on -Dtarget.
    toolchains.addFetchSteps(b);
    addFormatStep(b);

    const target = b.standardTargetOptions(.{
        .default_target = .{ .abi = if (builtin.os.tag == .windows) .msvc else null },
    });
    const optimize = b.standardOptimizeOption(.{});
    const options = Options.parse(b);
    mod.addUniversal(b, optimize, options);
    const ctx = Ctx.init(b, target, optimize, options);

    // Libraries the native fixtures share with the shipped mod. None of them need networking or a platform SDK,
    // so Linux can build and run the fixtures.
    const spud = deps.spud(ctx, deps.zydis(ctx), deps.capstone(ctx));
    const il2cpp = mod.il2cpp(ctx, spud);
    const spdlog = deps.spdlog(ctx);
    const toml = deps.tomlplusplus(ctx);
    const proto_runtime = protobuf.protobufLite(ctx, protobuf.abseil(ctx));
    // A null result means Zig still has to fetch a lazy dependency; it reruns build() once it has.
    const proto = protobuf.generate(ctx, proto_runtime) orelse return;
    tests.add(ctx, .{
        .il2cpp = il2cpp,
        .spdlog = spdlog,
        .spud = spud,
        .proto = proto,
        .proto_runtime = proto_runtime,
        .toml = toml,
    });
    if (!ctx.isWindows() and !ctx.isMacos()) return;

    // Everything below ships in, or tests, a platform build.
    const zlib = deps.zlib(ctx);
    const curl = deps.curl(ctx, zlib) orelse return;
    const cpr = deps.cpr(ctx, curl);
    const unity = mod.unity(ctx, il2cpp, spud);
    mod.add(ctx, &.{ il2cpp, unity, spdlog, spud, proto, proto_runtime, cpr, curl, toml, deps.nlohmannJson(ctx) });
    launcher.add(ctx);
    tests.addDependencySmoke(ctx, &.{ proto, proto_runtime, cpr, curl, zlib });

    // Build one dependency in isolation, to check a pin bump or a cross-compile without the mod.
    const net_step = cpr.addInstallStep(b, "dep-net", "Build the platform HTTPS dependencies");
    curl.addBuildDependencies(net_step);
    const proto_step = proto.addInstallStep(b, "dep-protobuf", "Build protobuf-lite and generated game messages");
    proto_runtime.addBuildDependencies(proto_step);
    _ = spud.addInstallStep(b, "dep-spud", "Build SPUD and its pinned dependencies");
    _ = spdlog.addInstallStep(b, "dep-spdlog", "Build only the spdlog dependency");
}

/// `fmt`: clang-format for C++ and zig fmt for the build. Objective-C++ (.mm) is left alone; the C++ rules do not
/// suit it.
fn addFormatStep(b: *std.Build) void {
    const step = b.step("fmt", "Format the project's C++ and Zig sources (needs clang-format on PATH)");
    const clang_format = b.addSystemCommand(&.{ "clang-format", "-i" });
    clang_format.has_side_effects = true;
    inline for (.{ "src", "tests" }) |dir|
        for (globFiles(b, b.path(""), dir, &.{ ".cc", ".h" }, &.{})) |file| clang_format.addFileArg(b.path(file));
    step.dependOn(&clang_format.step);
    step.dependOn(&b.addFmt(.{ .paths = &.{ "build.zig", "build.zig.zon", "zig" } }).step);
}
