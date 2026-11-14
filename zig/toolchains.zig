//! Pinned Windows and macOS SDKs: the fetch step that provisions them and the lookup that finds a sysroot.
const std = @import("std");
const builtin = @import("builtin");
const Options = @import("ctx.zig").Options;

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
    const tool = b.addExecutable(.{
        .name = "fetch-toolchain",
        .root_module = b.createModule(.{
            .root_source_file = b.path("zig/tools/fetch_toolchain.zig"),
            .target = b.graph.host,
            .optimize = .ReleaseSafe,
        }),
    });
    const tool_tests = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("zig/tools/fetch_toolchain.zig"),
            .target = b.graph.host,
        }),
    });
    b.step("test-toolchains", "Run toolchain archive selection tests").dependOn(&b.addRunArtifact(tool_tests).step);

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
            b.pathFromRoot(".cache/toolchain"),
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
            const cached = b.pathFromRoot(b.pathJoin(&.{ ".cache/toolchain/windows-msvc", pins.windows_msvc.version }));
            const root = options.msvc_sysroot orelse if (dirExists(b, cached)) cached else null;
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
            const cached = b.pathFromRoot(b.pathJoin(&.{ ".cache/toolchain/macos-sdk", pins.macos_sdk.version }));
            if (pins.macos_sdk.version.len != 0 and dirExists(b, cached)) return .{ .macos_sdk = cached };
            // Resolve the host SDK explicitly, rather than leaving it to Zig, so the other architecture's build
            // (x86_64 on arm64) finds frameworks too.
            if (builtin.os.tag == .macos) {
                const sdk = std.zig.system.darwin.getSdk(b.allocator, b.graph.io, &target.result) orelse
                    std.debug.panic("unable to locate the macOS SDK via xcrun; install the Xcode command line tools", .{});
                return .{ .macos_sdk = sdk };
            }
            std.debug.panic(
                "macOS SDK not found at {s}. Run `zig build fetch-toolchains` (set {s}) or pass -Dmacos-sdk=<path>.",
                .{ cached, pins.macos_sdk.url_env },
            );
        },
        else => return .{},
    }
}

fn msvcLibcFile(b: *std.Build, root: []const u8) std.Build.LazyPath {
    const contents = b.fmt(
        \\include_dir={0s}/winsdk/include/ucrt
        \\sys_include_dir={0s}/msvc/include
        \\crt_dir={0s}/winsdk/lib/ucrt/x64
        \\msvc_lib_dir={0s}/msvc/lib/x64
        \\kernel32_lib_dir={0s}/winsdk/lib/um/x64
        \\gcc_dir=
        \\
    , .{root});
    return b.addWriteFiles().add("msvc-libc.txt", contents);
}

fn dirExists(b: *std.Build, path: []const u8) bool {
    var dir = std.Io.Dir.cwd().openDir(b.graph.io, path, .{}) catch return false;
    dir.close(b.graph.io);
    return true;
}
