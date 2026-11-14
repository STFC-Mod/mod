//! The Swift macOS launcher: its native rsync bridge, the Swift compile and link, and the app bundle.
//! Swift needs Xcode, so this only builds on a macOS host for a macOS target.
const std = @import("std");
const builtin = @import("builtin");
const Ctx = @import("ctx.zig").Ctx;
const globFiles = @import("ctx.zig").globFiles;
const deps = @import("deps.zig");

/// Register `launcher`, which installs zig-out/macOSLauncher.app.
pub fn add(ctx: *const Ctx) void {
    const b = ctx.b;
    const step = b.step("launcher", "Build the Swift macOS launcher app (requires Xcode on macOS)");
    if (builtin.os.tag != .macos or !ctx.isMacos()) {
        step.dependOn(&b.addFail("The Swift launcher requires a macOS host and target with Xcode installed").step);
        return;
    }
    const sdk = ctx.sysroot.macos_sdk.?;
    const swift_target = if (ctx.isAarch64()) "arm64-apple-macosx14.6" else "x86_64-apple-macosx14.6";

    // The C++ side Swift imports through module.modulemap. swiftc links the system libc++, so the bridge builds
    // against the SDK's libc++ headers instead of Zig's bundled copy.
    const rsync = deps.rsync(ctx);
    const bridge_module = ctx.createModule(false);
    bridge_module.addIncludePath(.{ .cwd_relative = b.pathJoin(&.{ sdk, "usr/include/c++/v1" }) });
    rsync.includeInto(bridge_module);
    bridge_module.addCSourceFile(.{
        .file = b.path("src/launcher/macos/rsync.cc"),
        .flags = ctx.cxxFlags("c++17", &.{}),
    });
    const bridge = ctx.staticLib("launcher-bridge", bridge_module);

    // Compile all Swift sources as one module into one object. This needs only headers, so it runs while the
    // native archives build; only the link waits for them.
    const src = b.path("src/launcher/macos");
    const swift = b.addSystemCommand(&.{ "xcrun", "swiftc", "-parse-as-library" });
    swift.addArgs(&.{ "-module-name", "macOSLauncher" });
    swift.addArgs(&.{ "-D", "SWIFT_PACKAGE" });
    swift.addArgs(&.{ "-sdk", sdk });
    swift.addArgs(&.{ "-target", swift_target });
    swift.addArgs(&.{ "-Xcc", "-fmodules", "-Xcc" });
    swift.addPrefixedFileArg("-fmodule-map-file=", src.path(b, "module.modulemap"));
    swift.addArgs(&.{ if (ctx.optimize == .Debug) "-Onone" else "-O", "-g" });
    swift.addArgs(&.{ "-emit-object", "-whole-module-optimization" });
    for (globFiles(b, src, "", &.{".swift"}, &.{})) |file| swift.addFileArg(src.path(b, file));
    swift.addArg("-o");
    const object = swift.addOutputFileArg("macOSLauncher.o");

    const extractor = b.addExecutable(.{
        .name = "extract-macho-archive",
        .root_module = b.createModule(.{
            .root_source_file = b.path("zig/tools/extract_macho_archive.zig"),
            .target = b.graph.host,
            .optimize = .Debug,
        }),
    });
    const link = b.addSystemCommand(&.{ "xcrun", "swiftc", "-sdk", sdk, "-target", swift_target, "-g" });
    link.addFileArg(object);
    link.addFileArg(appleArchive(b, extractor, "liblauncher-bridge.a", bridge));
    link.addFileArg(appleArchive(b, extractor, "librsync.a", rsync.artifact.?));
    link.addArgs(&.{ "-lc++", "-o" });
    const exe = link.addOutputFileArg("macOSLauncher");
    step.dependOn(&b.addInstallFile(exe, "macOSLauncher.app/Contents/MacOS/macOSLauncher").step);

    installBundleResources(ctx, step);
}

/// Repack a Zig static library with Apple's libtool. Apple's linker rejects the archive index and unaligned
/// members Zig writes, so the archive is rebuilt from its loose objects.
fn appleArchive(
    b: *std.Build,
    extractor: *std.Build.Step.Compile,
    name: []const u8,
    library: *std.Build.Step.Compile,
) std.Build.LazyPath {
    const unpack = b.addRunArtifact(extractor);
    unpack.addArtifactArg(library);
    const objects = unpack.addOutputDirectoryArg("objects");
    const repack = b.addSystemCommand(&.{ "xcrun", "libtool", "-static", "-o" });
    const archive = repack.addOutputFileArg(name);
    repack.setCwd(objects);
    repack.addArgs(&.{ "-filelist", "objects.txt" });
    return archive;
}

/// Info.plist with the mod version, the app icon, and the compiled asset catalog.
fn installBundleResources(ctx: *const Ctx, step: *std.Build.Step) void {
    const b = ctx.b;
    const template = readSource(b, "src/launcher/macos/Info.plist.template");
    const plist = std.mem.replaceOwned(u8, b.allocator, template, "${VERSION}", modVersion(b)) catch @panic("OOM");
    const info = b.addWriteFiles().add("Info.plist", plist);
    step.dependOn(&b.addInstallFile(info, "macOSLauncher.app/Contents/Info.plist").step);
    const icon = b.addInstallFile(b.path("assets/launcher.icns"), "macOSLauncher.app/Contents/Resources/launcher.icns");
    step.dependOn(&icon.step);

    const actool = b.addSystemCommand(&.{ "xcrun", "actool", "--platform", "macosx" });
    actool.addArgs(&.{ "--minimum-deployment-target", "14.6", "--compile" });
    const compiled = actool.addOutputDirectoryArg("resources");
    actool.addDirectoryArg(b.path("src/launcher/macos/Assets.xcassets"));
    step.dependOn(&b.addInstallDirectory(.{
        .source_dir = compiled,
        .install_dir = .prefix,
        .install_subdir = "macOSLauncher.app/Contents/Resources",
    }).step);
}

/// MAJOR.MINOR.REVISION.PATCH from the `#define`s in src/mod/version.h.
fn modVersion(b: *std.Build) []const u8 {
    const header = readSource(b, "src/mod/version.h");
    var parts: [4][]const u8 = undefined;
    inline for (.{ "VERSION_MAJOR", "VERSION_MINOR", "VERSION_REVISION", "VERSION_PATCH" }, 0..) |key, i| {
        const offset = std.mem.indexOf(u8, header, key) orelse @panic("missing " ++ key ++ " in src/mod/version.h");
        const rest = std.mem.trimStart(u8, header[offset + key.len ..], " \t");
        parts[i] = rest[0 .. std.mem.indexOfAny(u8, rest, " \t\r\n") orelse rest.len];
    }
    return b.fmt("{s}.{s}.{s}.{s}", .{ parts[0], parts[1], parts[2], parts[3] });
}

fn readSource(b: *std.Build, path: []const u8) []const u8 {
    const io = b.graph.io;
    return std.Io.Dir.cwd().readFileAlloc(io, b.pathFromRoot(path), b.allocator, .limited(64 * 1024)) catch |err|
        std.debug.panic("unable to read {s}: {t}", .{ path, err });
}
