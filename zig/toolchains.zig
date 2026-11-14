//! Pinned Windows and macOS SDKs: the fetch step that provisions them and the lookup that finds a sysroot.
const std = @import("std");
const builtin = @import("builtin");
const compat = @import("compat.zig");
const Options = @import("ctx.zig").Options;
const hostTool = @import("ctx.zig").hostTool;
const hostTests = @import("ctx.zig").hostTests;

/// One entry of zig/toolchains.zon.
const Pin = struct {
    version: []const u8,
    url: []const u8,
    url_env: []const u8,
    sha256: []const u8,
};

const pins: struct { windows_msvc: Pin, macos_sdk: Pin } = @import("toolchains.zon");

/// SDK roots for one target. All null means "let Zig find the host SDK".
pub const Sysroot = struct {
    /// Extracted toolchain with msvc/ and winsdk/ folders.
    msvc_root: ?[]const u8 = null,
    /// Zig libc file pointing into `msvc_root`.
    msvc_libc_file: ?std.Build.LazyPath = null,
    /// MacOSX.sdk folder.
    macos_sdk: ?[]const u8 = null,
};

/// Register `fetch-toolchains` and the fetcher's own tests. Both run on the build host.
pub fn addFetchSteps(b: *std.Build) void {
    const test_step = b.step("test-toolchains", "Run toolchain archive selection and Mach-O archive tests");
    test_step.dependOn(hostTests(b, "zig/tools/fetch_toolchain.zig"));
    // The Mach-O archive extractor repacks the launcher's native archives; its tests run with the fetcher's.
    test_step.dependOn(hostTests(b, "zig/tools/extract_macho_archive.zig"));

    const tool = hostTool(b, "fetch-toolchain", "zig/tools/fetch_toolchain.zig", .ReleaseSafe);
    const step = b.step("fetch-toolchains", "Download, verify and extract cross-compilation sysroots");
    const selected = b.option([]const u8, "toolchains", "Comma-separated sysroots: windows_msvc,macos_sdk") orelse
        "windows_msvc";
    const local_archive = b.option([]const u8, "toolchain-archive", "Use a local archive instead of downloading") orelse
        "";
    var names = std.mem.splitScalar(u8, selected, ',');
    while (names.next()) |name| {
        const windows = std.mem.eql(u8, name, "windows_msvc");
        if (!windows and !std.mem.eql(u8, name, "macos_sdk")) std.debug.panic("unknown toolchain: {s}", .{name});
        const pin = if (windows) pins.windows_msvc else pins.macos_sdk;
        const fetch = b.addRunArtifact(tool);
        fetch.addArgs(&.{
            compat.pathFromRoot(b, ".cache/toolchain"),
            pin.version,
            pin.url,
            pin.url_env,
            pin.sha256,
            local_archive,
            if (windows) "windows-msvc" else "macos-sdk",
        });
        step.dependOn(&fetch.step);
    }
}

/// Find the sysroot for a Windows or macOS target. Precedence: the -D override, the pinned archive under
/// .cache/toolchain, then the host's own SDK. Panics with the fix when none applies.
pub fn find(b: *std.Build, target: std.Build.ResolvedTarget, options: Options) Sysroot {
    switch (target.result.os.tag) {
        .windows => {
            if (target.result.abi != .msvc)
                std.debug.panic("Windows builds must use the MSVC ABI, e.g. -Dtarget=x86_64-windows-msvc", .{});
            const cached = b.pathJoin(&.{ ".cache/toolchain/windows-msvc", pins.windows_msvc.version });
            const root = options.msvc_sysroot orelse fetched(b, cached);
            if (root) |r| return .{ .msvc_root = r, .msvc_libc_file = msvcLibcFile(b, r) };
            // A Windows host can fall back to an installed Visual Studio; see Ctx.createModule.
            if (builtin.os.tag == .windows) return .{};
            std.debug.panic(
                "MSVC toolchain not found at {s}. " ++
                    "Run `zig build fetch-toolchains` (set {s} if zig/toolchains.zon has no url).",
                .{ cached, pins.windows_msvc.url_env },
            );
        },
        .macos => {
            if (options.macos_sdk) |sdk| return .{ .macos_sdk = sdk };
            const cached = b.pathJoin(&.{ ".cache/toolchain/macos-sdk", pins.macos_sdk.version });
            if (pins.macos_sdk.version.len != 0) if (fetched(b, cached)) |sdk| return .{ .macos_sdk = sdk };
            // Resolve the host SDK explicitly, rather than leaving it to Zig, so the other architecture's build
            // (x86_64 on arm64) finds frameworks too.
            if (builtin.os.tag == .macos) {
                compat.poisonCache(b);
                if (std.zig.system.darwin.getSdk(b.allocator, b.graph.io, &target.result)) |sdk|
                    return .{ .macos_sdk = sdk };
                std.debug.panic("unable to locate the macOS SDK via xcrun; install the Xcode command line tools", .{});
            }
            std.debug.panic(
                "macOS SDK not found at {s}. Run `zig build fetch-toolchains` (set {s}) or pass -Dmacos-sdk=<path>.",
                .{ cached, pins.macos_sdk.url_env },
            );
        },
        else => return .{},
    }
}

/// A Zig libc file for the extracted toolchain, so Zig takes the CRT and STL from it instead of searching the host.
/// Zig 0.17 requires `darwin_sdk_dir`; 0.16 ignores keys it does not know.
fn msvcLibcFile(b: *std.Build, root: []const u8) std.Build.LazyPath {
    const contents = b.fmt(
        \\include_dir={0s}/winsdk/include/ucrt
        \\sys_include_dir={0s}/msvc/include
        \\crt_dir={0s}/winsdk/lib/ucrt/x64
        \\msvc_lib_dir={0s}/msvc/lib/x64
        \\kernel32_lib_dir={0s}/winsdk/lib/um/x64
        \\gcc_dir=
        \\darwin_sdk_dir=
        \\
    , .{root});
    return b.addWriteFiles().add("msvc-libc.txt", contents);
}

/// The absolute path of a fetched toolchain folder under the build root, or null if it is missing. A found one is a
/// configure dependency, so deleting it configures again; a missing one ends in a panic or a poisoned cache.
fn fetched(b: *std.Build, sub_path: []const u8) ?[]const u8 {
    const path = compat.pathFromRoot(b, sub_path);
    var dir = std.Io.Dir.cwd().openDir(b.graph.io, path, .{}) catch return null;
    dir.close(b.graph.io);
    compat.dependOnDirectory(b, b.path(sub_path));
    return path;
}
