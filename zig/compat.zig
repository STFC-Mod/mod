//! The std.Build differences between Zig 0.16 and 0.17. When 0.16 support ends, inline the 0.17 side and delete this.
//!
//! 0.17 runs build.zig in a separate configure process and caches the graph it produces. The cache key covers
//! `zig build` arguments and build.zig itself, so anything else the graph depends on has to be declared: files and
//! folders read while configuring through `dependOn*`, environment variables and host programs by poisoning the
//! cache for that run. 0.16 configures on every run and needs neither.
const std = @import("std");
const builtin = @import("builtin");
const Build = std.Build;

const zig_017 = builtin.zig_version.minor >= 17;

/// An absolute path under the build root. 0.17 has no `pathFromRoot` and may run the configurer with a relative root.
pub fn pathFromRoot(b: *Build, sub_path: []const u8) []const u8 {
    if (!zig_017) return b.pathFromRoot(sub_path);
    const sub_root = if (b.root.sub_path.len == 0) "." else b.root.sub_path;
    const root = b.root.root_dir.handle.realPathFileAlloc(b.graph.io, sub_root, b.allocator) catch |err|
        std.debug.panic("unable to resolve the build root: {t}", .{err});
    return b.pathJoin(&.{ root, sub_path });
}

/// The directory behind a source, package or cwd-relative path, for reading while configuring. 0.17 resolves
/// LazyPaths only in the make phase, so it is limited to paths that exist before the build runs.
pub fn sourcePath(b: *Build, path: Build.LazyPath) Build.Cache.Path {
    if (!zig_017) return path.getPath3(b, null);
    return switch (path) {
        .src_path => |src| src.owner.root.join(b.allocator, src.sub_path) catch @panic("OOM"),
        .dependency => |dep| dep.dependency.builder.root.join(b.allocator, dep.sub_path) catch @panic("OOM"),
        .cwd_relative => |sub_path| .initCwd(sub_path),
        else => std.debug.panic("{f} is not a source path", .{path}),
    };
}

/// Reads a source or cwd-relative file while configuring. 0.17 configures again when it changes.
pub fn readFile(b: *Build, path: Build.LazyPath, limit: std.Io.Limit) []u8 {
    if (zig_017) b.dependOnFileContents(path);
    const file = sourcePath(b, path);
    return file.root_dir.handle.readFileAlloc(b.graph.io, file.sub_path, b.allocator, limit) catch |err|
        std.debug.panic("unable to read {f}: {t}", .{ file, err });
}

/// Declares that the graph depends on a directory's entries, read while configuring. Not recursive.
pub fn dependOnDirectory(b: *Build, path: Build.LazyPath) void {
    if (zig_017) b.dependOnDirectoryContents(path);
}

/// An environment variable read while configuring. 0.17 cannot track these, so the read turns its configure cache
/// off for this run.
pub fn getEnv(b: *Build, name: []const u8) ?[]const u8 {
    poisonCache(b);
    return b.graph.environ_map.get(name);
}

/// For configure logic 0.17 cannot track, such as asking the host for its SDK.
pub fn poisonCache(b: *Build) void {
    if (zig_017) b.graph.poisonCache();
}

/// Forwards the arguments after `zig build ... --` to `run`.
pub fn addPassthruArgs(run: *Build.Step.Run) void {
    if (zig_017) return run.addPassthruArgs();
    if (run.step.owner.args) |args| run.addArgs(args);
}

/// `zig fmt` over source paths under the build root.
pub fn addFmt(b: *Build, paths: []const []const u8) *Build.Step.Fmt {
    if (zig_017) return b.addFmt(.{ .paths = b.pathList(paths) });
    return b.addFmt(.{ .paths = paths });
}
