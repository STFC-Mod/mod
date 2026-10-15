//! protobuf-lite, its Abseil closure, and the generated C++ for the game messages sync reads.
//! Source manifests live in zig/deps/*_sources.zon; the message roots in zig/deps/protobuf_roots.txt.
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
    addRuntimeSources(ctx, module, "abseil", src, &abseil_sources);
    return .{ .artifact = ctx.staticLib("abseil", module), .include_dirs = ctx.dirs(&.{src}) };
}

/// The protobuf-lite runtime and its UTF-8 validator.
pub fn protobufLite(ctx: *const Ctx, absl: Lib) Lib {
    const b = ctx.b;
    const src = b.dependency("protobuf", .{}).path("");
    const module = ctx.createModule(true);
    module.addIncludePath(src.path(b, "src"));
    module.addIncludePath(src.path(b, "third_party/utf8_range"));
    absl.includeInto(module);
    addRuntimeSources(ctx, module, "protobuf", src, &protobuf_sources);
    module.addCSourceFiles(.{
        .root = src,
        .files = &.{"third_party/utf8_range/utf8_range.c"},
        .flags = ctx.cFlags(&.{"-std=c11"}),
    });
    return .{
        .artifact = ctx.staticLib("protobuf-lite", module),
        .dependencies = b.allocator.dupe(Lib, &.{absl}) catch @panic("OOM"),
        .include_dirs = ctx.dirs(&.{ src.path(b, "src"), src.path(b, "third_party/utf8_range"), absl.include_dirs[0] }),
    };
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
    inline for (.{ "timestamp", "wrappers" }) |name| {
        describe.addFileArg(well_known_dir.path(b, "google/protobuf/" ++ name ++ ".proto"));
        compiled.append(b.allocator, "google/protobuf/" ++ name ++ ".pb.cc") catch @panic("OOM");
    }

    // 2. Trim to the sync roots.
    const selector = b.addExecutable(.{
        .name = "select-proto",
        .root_module = b.createModule(.{
            .root_source_file = b.path("zig/tools/select_proto.zig"),
            .target = b.graph.host,
            .optimize = .Debug,
        }),
    });
    const select = b.addRunArtifact(selector);
    select.addFileArg(descriptors);
    select.addFileArg(b.path("zig/deps/protobuf_roots.txt"));
    const selected = select.addOutputFileArg("selected.pb");

    const selector_tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("zig/tools/select_proto_tests.zig"),
            .target = b.graph.host,
        }),
    });
    b.step("test-codegen", "Check protobuf descriptor selection against independent protoc fixtures")
        .dependOn(&b.addRunArtifact(selector_tests).step);

    // 3. Emit lite C++ for every schema in the trimmed set.
    const emit = std.Build.Step.Run.create(b, "protoc-cpp");
    emit.addFileArg(protoc);
    emit.addPrefixedFileArg("--descriptor_set_in=", selected);
    const generated = emit.addPrefixedOutputDirectoryArg("--cpp_out=lite:", "proto");
    emit.addArgs(schemas);
    emit.addArgs(&.{ "google/protobuf/timestamp.proto", "google/protobuf/wrappers.proto" });

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

/// On macOS, compile runtime sources in batches of four so they share header parsing without serializing the
/// library into one unit. Other targets keep one unit per file: MSVC headers and source-local CRT defines depend
/// on include order.
fn addRuntimeSources(
    ctx: *const Ctx,
    module: *std.Build.Module,
    name: []const u8,
    root: std.Build.LazyPath,
    sources: []const []const u8,
) void {
    const flags = ctx.cxxFlags("c++17", &.{});
    if (!ctx.isMacos()) return module.addCSourceFiles(.{ .root = root, .files = sources, .flags = flags });
    var offset: usize = 0;
    while (offset < sources.len) : (offset += 4) {
        const batch = sources[offset..@min(offset + 4, sources.len)];
        ctx.addCxxBatch(module, ctx.b.fmt("{s}-{d}", .{ name, offset / 4 }), root, batch, flags);
    }
}
