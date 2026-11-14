//! protobuf-lite, its Abseil closure, and the generated C++ for the game messages sync reads.
//! Source manifests live in zig/deps/*_sources.zon; the message roots in zig/deps/protobuf_roots.txt.
const std = @import("std");
const builtin = @import("builtin");
const Ctx = @import("ctx.zig").Ctx;
const Lib = @import("ctx.zig").Lib;
const globFiles = @import("ctx.zig").globFiles;
const hostTool = @import("ctx.zig").hostTool;
const hostTests = @import("ctx.zig").hostTests;

const abseil_sources: []const []const u8 = @import("deps/abseil_sources.zon");
const protobuf_sources: []const []const u8 = @import("deps/protobuf_sources.zon");

/// The Abseil sources protobuf-lite reaches, not the whole library.
pub fn abseil(ctx: *const Ctx) Lib {
    const b = ctx.b;
    const src = b.dependency("abseil", .{}).path("");
    var lib: Lib = .{ .include_dirs = ctx.dirs(&.{src}) };
    const module = ctx.createModule(true);
    lib.includeInto(module);
    if (ctx.isMacos()) module.linkFramework("CoreFoundation", .{});
    addRuntimeSources(ctx, module, "abseil", src, abseil_sources);
    lib.artifact = ctx.staticLib("abseil", module);
    return lib;
}

/// The protobuf-lite runtime and its UTF-8 validator.
pub fn protobufLite(ctx: *const Ctx, absl: Lib) Lib {
    const b = ctx.b;
    const src = b.dependency("protobuf", .{}).path("");
    // protobuf's public headers include Abseil's, so consumers get both include dirs.
    var lib: Lib = .{
        .dependencies = ctx.libs(&.{absl}),
        .include_dirs = ctx.dirs(&.{ src.path(b, "src"), src.path(b, "third_party/utf8_range"), absl.include_dirs[0] }),
    };
    const module = ctx.createModule(true);
    lib.includeInto(module);
    addRuntimeSources(ctx, module, "protobuf", src, protobuf_sources);
    module.addCSourceFiles(.{
        .root = src,
        .files = &.{"third_party/utf8_range/utf8_range.c"},
        .flags = ctx.cFlags(&.{"-std=c11"}),
    });
    lib.artifact = ctx.staticLib("protobuf-lite", module);
    return lib;
}

/// Generate and compile C++ for the game messages. null until Zig has fetched the host's protoc.
///
/// Three host steps:
///   1. protoc parses every schema into one descriptor set.
///   2. select-proto keeps the root messages and the types they reach, so unused messages emit no code.
///   3. protoc writes lite C++ from the trimmed set.
/// Only the server models, the platform's store content, shared stfc types, the sync messages the game does not
/// declare (stfc.sync.proto), and two well-known types are compiled. The other schema files are catalogs no sync code
/// reads.
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
    const schemas = globFiles(b, schema_dir, "", &.{".proto"}, &.{});
    const well_known = .{ "google/protobuf/timestamp", "google/protobuf/wrappers" };
    var compiled: std.ArrayList([]const u8) = .empty;

    // 1. Describe every schema.
    const describe = std.Build.Step.Run.create(b, "protoc");
    describe.addFileArg(protoc);
    const descriptors = describe.addPrefixedOutputFileArg("--descriptor_set_out=", "game.pb");
    describe.addArg("--include_imports");
    describe.addPrefixedDirectoryArg("-I", schema_dir);
    describe.addPrefixedDirectoryArg("-I", well_known_dir);
    for (schemas) |schema| {
        describe.addFileArg(schema_dir.path(b, schema));
        const read = std.mem.eql(u8, schema, "Digit.PrimePlatform.Content.proto") or
            std.mem.eql(u8, schema, "Digit.PrimeServer.Models.proto") or std.mem.eql(u8, schema, "stfc.proto") or
            std.mem.eql(u8, schema, "stfc.sync.proto");
        if (read) {
            const stem = schema[0 .. schema.len - ".proto".len];
            compiled.append(b.allocator, b.fmt("{s}.pb.cc", .{stem})) catch @panic("OOM");
        }
    }
    inline for (well_known) |name| {
        describe.addFileArg(well_known_dir.path(b, name ++ ".proto"));
        compiled.append(b.allocator, name ++ ".pb.cc") catch @panic("OOM");
    }

    // 2. Trim to the sync roots.
    const select = b.addRunArtifact(hostTool(b, "select-proto", "zig/tools/select_proto.zig", .Debug));
    select.addFileArg(descriptors);
    select.addFileArg(b.path("zig/deps/protobuf_roots.txt"));
    const selected = select.addOutputFileArg("selected.pb");
    b.step("test-codegen", "Check protobuf descriptor selection against independent protoc fixtures")
        .dependOn(hostTests(b, "zig/tools/select_proto_tests.zig"));

    // 3. Emit lite C++ for every schema in the trimmed set.
    const emit = std.Build.Step.Run.create(b, "protoc-cpp");
    emit.addFileArg(protoc);
    emit.addPrefixedFileArg("--descriptor_set_in=", selected);
    const generated = emit.addPrefixedOutputDirectoryArg("--cpp_out=lite:", "proto");
    emit.addArgs(schemas);
    inline for (well_known) |name| emit.addArg(name ++ ".proto");

    var lib: Lib = .{ .include_dirs = ctx.dirs(&.{generated}) };
    const module = ctx.createModule(true);
    lib.includeInto(module);
    // Headers only: consumers link protobuf-lite next to these objects.
    runtime.includeInto(module);
    module.addCSourceFiles(.{ .root = generated, .files = compiled.items, .flags = ctx.cxxFlags("c++17", &.{}) });
    lib.artifact = ctx.staticLib("stfc-proto", module);
    return lib;
}

/// On macOS, compile runtime sources in batches of four. Other targets keep one unit per file: MSVC headers and
/// source-local CRT defines depend on include order.
fn addRuntimeSources(
    ctx: *const Ctx,
    module: *std.Build.Module,
    name: []const u8,
    root: std.Build.LazyPath,
    sources: []const []const u8,
) void {
    const flags = ctx.cxxFlags("c++17", &.{});
    if (ctx.isMacos()) {
        ctx.addCxxBatches(module, name, root, sources, flags);
    } else {
        module.addCSourceFiles(.{ .root = root, .files = sources, .flags = flags });
    }
}
