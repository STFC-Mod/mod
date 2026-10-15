//! protobuf-lite, its Abseil closure, and the generated C++ for the game messages.
//! Source manifests live in zig/deps/*_sources.zon.
const std = @import("std");
const builtin = @import("builtin");
const Ctx = @import("ctx.zig").Ctx;
const Lib = @import("ctx.zig").Lib;
const globFiles = @import("ctx.zig").globFiles;

const abseil_sources: [101][]const u8 = @import("deps/abseil_sources.zon");
const protobuf_sources: [26][]const u8 = @import("deps/protobuf_sources.zon");

/// The Abseil sources protobuf-lite reaches, not the whole library.
pub fn abseil(ctx: *const Ctx) Lib {
    const b = ctx.b;
    const src = b.dependency("abseil", .{}).path("");
    const module = ctx.createModule(true);
    module.addIncludePath(src);
    if (ctx.isMacos()) module.linkFramework("CoreFoundation", .{});
    module.addCSourceFiles(.{ .root = src, .files = &abseil_sources, .flags = ctx.cxxFlags("c++17", &.{}) });
    return .{ .artifact = ctx.staticLib("abseil", module), .include_dirs = ctx.dirs(&.{src}) };
}

/// The protobuf-lite runtime and its UTF-8 validator.
pub fn protobufLite(ctx: *const Ctx, absl: Lib) Lib {
    const b = ctx.b;
    const src = b.dependency("protobuf", .{}).path("");
    const module = ctx.createModule(true);
    module.addIncludePath(src.path(b, "src"));
    module.addIncludePath(src.path(b, "third_party/utf8_range"));
    absl.linkInto(module);
    module.addCSourceFiles(.{ .root = src, .files = &protobuf_sources, .flags = ctx.cxxFlags("c++17", &.{}) });
    module.addCSourceFiles(.{
        .root = src,
        .files = &.{"third_party/utf8_range/utf8_range.c"},
        .flags = ctx.cFlags(&.{"-std=c11"}),
    });
    return .{
        .artifact = ctx.staticLib("protobuf-lite", module),
        .include_dirs = ctx.dirs(&.{ src.path(b, "src"), src.path(b, "third_party/utf8_range"), absl.include_dirs[0] }),
    };
}

/// Generate and compile lite C++ for every game schema. null until Zig has fetched the host's protoc.
pub fn generate(ctx: *const Ctx, runtime: Lib) ?Lib {
    const b = ctx.b;
    const protoc_dep = comptime switch (builtin.os.tag) {
        .macos => if (builtin.cpu.arch == .aarch64) "protoc_osx_aarch_64" else "protoc_osx_x86_64",
        .linux => if (builtin.cpu.arch == .aarch64) "protoc_linux_aarch_64" else "protoc_linux_x86_64",
        .windows => "protoc_win64",
        else => @compileError("No pinned protoc binary for this host"),
    };
    const protoc = (b.lazyDependency(protoc_dep, .{}) orelse return null)
        .path(if (builtin.os.tag == .windows) "bin/protoc.exe" else "bin/protoc");
    const schema_dir = b.path("src/mod/game/proto");
    const well_known_dir = b.dependency("protobuf", .{}).path("src");
    var compiled: std.ArrayList([]const u8) = .empty;

    const emit = std.Build.Step.Run.create(b, "protoc");
    emit.addFileArg(protoc);
    const generated = emit.addPrefixedOutputDirectoryArg("--cpp_out=lite:", "proto");
    emit.addPrefixedDirectoryArg("-I", schema_dir);
    emit.addPrefixedDirectoryArg("-I", well_known_dir);
    for (globFiles(b, schema_dir, "", &.{".proto"}, &.{})) |schema| {
        emit.addFileArg(schema_dir.path(b, schema));
        const stem = schema[0 .. schema.len - ".proto".len];
        compiled.append(b.allocator, b.fmt("{s}.pb.cc", .{stem})) catch @panic("OOM");
    }
    inline for (.{ "timestamp", "wrappers" }) |name| {
        emit.addFileArg(well_known_dir.path(b, "google/protobuf/" ++ name ++ ".proto"));
        compiled.append(b.allocator, "google/protobuf/" ++ name ++ ".pb.cc") catch @panic("OOM");
    }

    const module = ctx.createModule(true);
    module.addIncludePath(generated);
    // Headers only: consumers link protobuf-lite next to these objects.
    runtime.includeInto(module);
    module.addCSourceFiles(.{ .root = generated, .files = compiled.items, .flags = ctx.cxxFlags("c++17", &.{}) });
    return .{
        .artifact = ctx.staticLib("stfc-proto", module),
        .include_dirs = ctx.dirs(&.{generated}),
    };
}
