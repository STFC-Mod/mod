//! Downloads, verifies and extracts a pinned SDK archive into .cache/toolchain, with no external tools.
const std = @import("std");
const Io = std.Io;
const Dir = Io.Dir;
const Sha256 = std.crypto.hash.sha2.Sha256;

/// Arguments, in the order zig/toolchains.zig passes them: cache dir, version, manifest URL, URL environment
/// variable, expected SHA-256, local archive (may be empty), and `windows-msvc` or `macos-sdk`.
pub fn main(init: std.process.Init) !void {
    const allocator = init.arena.allocator();
    const args = try init.minimal.args.toSlice(allocator);
    if (args.len != 8) return error.InvalidArguments;
    const kind = args[7];
    const windows = std.mem.eql(u8, kind, "windows-msvc");
    if (!windows and !std.mem.eql(u8, kind, "macos-sdk")) return error.InvalidToolchainKind;
    const cache = args[1];
    const version = args[2];
    const manifest_url = args[3];
    const url_env = args[4];
    const expected_hash = args[5];
    const local_archive = args[6];
    const url = init.environ_map.get(url_env) orelse manifest_url;
    if (version.len == 0 or std.mem.indexOfAny(u8, version, "/\\") != null) return error.InvalidVersion;
    const io = init.io;
    const cwd = Dir.cwd();
    try cwd.createDirPath(io, cache);
    const root = try std.fs.path.join(allocator, &.{ cache, kind, version });
    const stamp = try std.fs.path.join(allocator, &.{ root, ".complete" });
    const extension = if (windows) "zip" else "tar.gz";
    const archive = if (local_archive.len != 0)
        local_archive
    else
        try std.fmt.allocPrint(allocator, "{s}/{s}-{s}.{s}", .{ cache, kind, version, extension });
    if (!try exists(io, cwd, archive)) {
        if (local_archive.len != 0) return error.FileNotFound;
        if (url.len == 0) {
            std.debug.print("Set {s} or fill in zig/toolchains.zon to download the toolchain sysroot.\n", .{url_env});
            return error.MissingToolchainUrl;
        }
        try downloadArchive(init, url, archive);
    }
    const digest = try hashFile(io, archive);
    const hex = std.fmt.bytesToHex(digest, .lower);
    if (expected_hash.len != 0 and !std.ascii.eqlIgnoreCase(expected_hash, &hex)) {
        std.debug.print("Toolchain archive SHA-256 mismatch: got {s}\n", .{hex});
        return error.HashMismatch;
    }
    if (expected_hash.len == 0) std.debug.print("Toolchain archive SHA-256: {s}\n", .{hex});
    // Include pin metadata and the actual archive digest: replacing an unpinned archive or changing the manifest
    // invalidates the completed extraction. Bump the extractor version when the extracted layout changes.
    const identity = try std.fmt.allocPrint(allocator, "extractor-v5\n{s}\n{s}\n{s}\n{s}\n{s}\n", .{
        version,
        manifest_url,
        url_env,
        expected_hash,
        hex,
    });
    const old = cwd.readFileAlloc(io, stamp, allocator, .limited(8192)) catch null;
    if (old) |data| if (std.mem.eql(u8, data, identity)) {
        std.debug.print("Toolchain sysroot already complete at {s}\n", .{root});
        return;
    };
    const staging = try std.fmt.allocPrint(allocator, "{s}.extracting", .{root});
    try cwd.deleteTree(io, staging);
    try cwd.createDirPath(io, staging);
    errdefer cwd.deleteTree(io, staging) catch {};
    {
        var dest = try cwd.openDir(io, staging, .{ .iterate = true });
        defer dest.close(io);
        if (windows) {
            try extractWindows(io, archive, dest);
            try validate(io, dest);
            try windowsAliases(allocator, io, dest);
        } else {
            try extractSdk(io, archive, dest);
            try dest.access(io, "usr/include/stdio.h", .{});
            try dest.access(io, "System/Library/Frameworks/Foundation.framework", .{});
        }
        try dest.writeFile(io, .{ .sub_path = ".complete", .data = identity });
    }
    // Preserve the previous sysroot until the new archive has been validated.
    try cwd.deleteTree(io, root);
    try cwd.rename(staging, cwd, root, io);
    std.debug.print("Toolchain sysroot ready at {s}\n", .{root});
}

fn downloadArchive(init: std.process.Init, url: []const u8, archive: []const u8) !void {
    const allocator = init.arena.allocator();
    const io = init.io;
    const cwd = Dir.cwd();
    const temporary = try std.fmt.allocPrint(allocator, "{s}.download", .{archive});
    errdefer cwd.deleteFile(io, temporary) catch {};
    {
        const file = try cwd.createFile(io, temporary, .{});
        defer file.close(io);
        var buffer: [64 * 1024]u8 = undefined;
        var writer = file.writer(io, &buffer);
        var client: std.http.Client = .{ .allocator = init.gpa, .io = io };
        defer client.deinit();
        std.debug.print("Downloading toolchain sysroot...\n", .{});
        const response = try client.fetch(.{ .location = .{ .url = url }, .response_writer = &writer.interface });
        if (response.status != .ok) return error.DownloadFailed;
        try writer.end();
    }
    try cwd.rename(temporary, cwd, archive, io);
}

fn hashFile(io: Io, path: []const u8) ![32]u8 {
    const file = try Dir.cwd().openFile(io, path, .{});
    defer file.close(io);
    var buffer: [64 * 1024]u8 = undefined;
    var reader = file.reader(io, &buffer);
    var chunk: [64 * 1024]u8 = undefined;
    var hash = Sha256.init(.{});
    while (true) {
        const n = try reader.interface.readSliceShort(&chunk);
        if (n == 0) break;
        hash.update(chunk[0..n]);
    }
    return hash.finalResult();
}

fn extractWindows(io: Io, path: []const u8, dest: Dir) !void {
    const file = try Dir.cwd().openFile(io, path, .{});
    defer file.close(io);
    var buffer: [64 * 1024]u8 = undefined;
    var reader = file.reader(io, &buffer);
    var iter = try std.zip.Iterator.init(&reader);
    var names: [std.fs.max_path_bytes]u8 = undefined;
    while (try iter.next()) |entry| {
        if (entry.filename_len > names.len) return error.InvalidArchivePath;
        const name = names[0..entry.filename_len];
        try reader.seekTo(entry.header_zip_offset + @sizeOf(std.zip.CentralDirectoryFileHeader));
        try reader.interface.readSliceAll(name);
        std.mem.replaceScalar(u8, name, '\\', '/');
        if (!selected(name)) continue;
        // std.zip.Entry.extract additionally rejects absolute paths and '..'.
        try entry.extract(&reader, .{ .allow_backslashes = true }, &names, dest);
    }
}

fn extractSdk(io: Io, path: []const u8, dest: Dir) !void {
    // SDK tarballs contain one top-level MacOSX.sdk directory. TAR preserves framework symlinks, unlike a plain ZIP
    // extraction.
    const file = try Dir.cwd().openFile(io, path, .{});
    defer file.close(io);
    var buffer: [64 * 1024]u8 = undefined;
    var reader = file.reader(io, &buffer);
    var flate_buffer: [std.compress.flate.max_window_len]u8 = undefined;
    var gzip: std.compress.flate.Decompress = .init(&reader.interface, .gzip, &flate_buffer);
    try std.tar.extract(io, dest, &gzip.reader, .{ .strip_components = 1 });
}

/// The header folders and x64 library folders the build uses; the archive also carries tools and other
/// architectures.
fn selected(name: []const u8) bool {
    inline for (.{
        "msvc/include/",
        "msvc/lib/x64/",
        "winsdk/include/",
        "winsdk/lib/ucrt/x64/",
        "winsdk/lib/um/x64/",
    }) |prefix| {
        if (std.mem.startsWith(u8, name, prefix)) return true;
    }
    return false;
}

fn validate(io: Io, dir: Dir) !void {
    inline for (.{
        "msvc/include/vector",
        "msvc/lib/x64/libcmt.lib",
        "winsdk/include/ucrt/stdio.h",
        "winsdk/include/um/Windows.h",
        "winsdk/lib/ucrt/x64/libucrt.lib",
        "winsdk/lib/um/x64/kernel32.Lib",
    }) |path| {
        dir.access(io, path, .{}) catch {
            std.debug.print("Toolchain archive is missing {s}\n", .{path});
            return error.IncompleteToolchain;
        };
    }
}

/// Make the SDK usable on a case-sensitive filesystem. MSVC and Windows SDK file names mix case, and so do the
/// `#include` lines and `/DEFAULTLIB` directives that name them. Adds symlinks: a lowercase alias for every file,
/// then every spelling an include or library directive asks for. Never changes the extracted files.
fn windowsAliases(allocator: std.mem.Allocator, io: Io, root: Dir) !void {
    var root_buffer: [std.fs.max_path_bytes]u8 = undefined;
    const root_path = root_buffer[0..try root.realPath(io, &root_buffer)];
    // Snapshot first so the walker never visits the aliases it creates.
    var files: std.ArrayList([]const u8) = .empty;
    var walker = try root.walk(allocator);
    defer walker.deinit();
    while (try walker.next(io)) |entry| {
        if (entry.kind == .file) try files.append(allocator, try allocator.dupe(u8, entry.path));
    }
    for (files.items) |path| {
        const basename = std.fs.path.basename(path);
        const lower = try std.ascii.allocLowerString(allocator, basename);
        if (std.mem.eql(u8, basename, lower)) continue;
        const alias = try std.fs.path.join(allocator, &.{ std.fs.path.dirname(path) orelse "", lower });
        // APFS and Windows already resolve the alias on case-insensitive volumes.
        if (try exists(io, root, alias)) continue;
        try root.symLink(io, basename, alias, .{});
    }
    // Lowercase relative path to the original file.
    var index: std.StringHashMapUnmanaged([]const u8) = .empty;
    defer index.deinit(allocator);
    for (files.items) |path| try index.put(allocator, try std.ascii.allocLowerString(allocator, path), path);
    const include_dirs = [_][]const u8{
        "msvc/include",
        "winsdk/include/um",
        "winsdk/include/shared",
        "winsdk/include/winrt",
        "winsdk/include/cppwinrt",
        "winsdk/include/ucrt",
    };
    // Read each file through page_allocator and free it: the arena would keep every header and library alive.
    for (files.items) |path| {
        const header = std.mem.startsWith(u8, path, "msvc/include/") or std.mem.startsWith(u8, path, "winsdk/include/");
        if (!header) continue;
        const content = try root.readFileAlloc(io, path, std.heap.page_allocator, .limited(16 * 1024 * 1024));
        defer std.heap.page_allocator.free(content);
        var lines = std.mem.splitScalar(u8, content, '\n');
        while (lines.next()) |line| {
            var rest = std.mem.trimStart(u8, line, " \t");
            if (!std.mem.startsWith(u8, rest, "#")) continue;
            rest = std.mem.trimStart(u8, rest[1..], " \t");
            if (!std.mem.startsWith(u8, rest, "include")) continue;
            rest = std.mem.trimStart(u8, rest["include".len..], " \t");
            if (rest.len == 0 or (rest[0] != '<' and rest[0] != '"')) continue;
            const end = std.mem.indexOfScalarPos(u8, rest, 1, if (rest[0] == '<') '>' else '"') orelse continue;
            const local_dir = std.fs.path.dirname(path);
            try aliasSpelling(allocator, io, root, root_path, &index, local_dir, &include_dirs, rest[1..end]);
        }
    }
    // Static libraries embed linker directives as plain text, quoted or not.
    const lib_dirs = [_][]const u8{ "msvc/lib/x64", "winsdk/lib/um/x64", "winsdk/lib/ucrt/x64" };
    for (files.items) |path| {
        if (!std.ascii.endsWithIgnoreCase(path, ".lib")) continue;
        const content = try root.readFileAlloc(io, path, std.heap.page_allocator, .limited(64 * 1024 * 1024));
        defer std.heap.page_allocator.free(content);
        for ([_][]const u8{ "/DEFAULTLIB:", "/defaultlib:" }) |prefix| {
            var pos: usize = 0;
            while (std.mem.indexOfPos(u8, content, pos, prefix)) |start| {
                const rest = content[start + prefix.len ..];
                pos = start + prefix.len;
                if (rest.len == 0) break;
                const quoted = rest[0] == '"';
                const begin: usize = if (quoted) 1 else 0;
                const end = if (quoted)
                    std.mem.indexOfScalarPos(u8, rest, begin, '"') orelse continue
                else
                    std.mem.indexOfAny(u8, rest, " \t\r\n\x00") orelse continue;
                const name = rest[begin..end];
                const lib = if (std.ascii.endsWithIgnoreCase(name, ".lib"))
                    name
                else
                    try std.fmt.allocPrint(allocator, "{s}.lib", .{name});
                try aliasSpelling(allocator, io, root, root_path, &index, null, &lib_dirs, lib);
                pos += end + @intFromBool(quoted);
            }
        }
    }
}

/// Link `spelling` to the file it names case-insensitively, in `local_dir` first (the including file's folder)
/// and then in each of `search_dirs`. Does nothing when no file matches or the spelling already exists.
fn aliasSpelling(
    allocator: std.mem.Allocator,
    io: Io,
    root: Dir,
    root_path: []const u8,
    index: *const std.StringHashMapUnmanaged([]const u8),
    local_dir: ?[]const u8,
    search_dirs: []const []const u8,
    spelling: []const u8,
) !void {
    const name = try allocator.dupe(u8, spelling);
    std.mem.replaceScalar(u8, name, '\\', '/');
    // Only alias files inside the extracted search directories.
    if (name.len == 0 or std.fs.path.isAbsolute(name)) return;
    var components = std.mem.splitScalar(u8, name, '/');
    while (components.next()) |part| if (std.mem.eql(u8, part, "..")) return;
    const dirs = if (local_dir) |dir|
        try std.mem.concat(allocator, []const u8, &.{ &.{dir}, search_dirs })
    else
        search_dirs;
    for (dirs) |dir| {
        const alias = try std.fs.path.join(allocator, &.{ dir, name });
        const source = index.get(try std.ascii.allocLowerString(allocator, alias)) orelse continue;
        if (try exists(io, root, alias)) return;
        const parent = std.fs.path.dirname(alias).?;
        try root.createDirPath(io, parent);
        const target = try std.fs.path.relative(allocator, root_path, null, parent, source);
        try root.symLink(io, target, alias, .{});
        return;
    }
}

/// Access check for `path` under `dir`. FileNotFound reads as false; other errors propagate.
fn exists(io: Io, dir: Dir, path: []const u8) !bool {
    dir.access(io, path, .{}) catch |err| return if (err == error.FileNotFound) false else err;
    return true;
}

test "only compiler headers and x64 static libraries are extracted" {
    try std.testing.expect(selected("msvc/include/vector"));
    try std.testing.expect(selected("winsdk/lib/um/x64/kernel32.Lib"));
    try std.testing.expect(!selected("msvc/bin/Hostx64/x64/cl.exe"));
    try std.testing.expect(!selected("msvc/lib/arm64/libcmt.lib"));
    try std.testing.expect(selected("msvc/lib/x64/libcmt.amd64.pdb"));
}

test "SDK include and linker spellings resolve on case-sensitive filesystems" {
    const io = std.testing.io;
    var tmp = std.testing.tmpDir(.{ .iterate = true });
    defer tmp.cleanup();
    var arena = std.heap.ArenaAllocator.init(std.testing.allocator);
    defer arena.deinit();
    const allocator = arena.allocator();
    try tmp.dir.createDirPath(io, "winsdk/include/shared");
    try tmp.dir.createDirPath(io, "winsdk/include/um");
    try tmp.dir.createDirPath(io, "msvc/lib/x64");
    try tmp.dir.createDirPath(io, "winsdk/lib/um/x64");
    try tmp.dir.writeFile(io, .{ .sub_path = "winsdk/include/shared/driverspecs.h", .data = "driver specs" });
    try tmp.dir.writeFile(io, .{ .sub_path = "winsdk/include/shared/specstrings.h", .data = "spec strings" });
    try tmp.dir.writeFile(io, .{
        .sub_path = "winsdk/include/um/Windows.h",
        .data = "#include <DriverSpecs.h>\n# include \"SpecStrings.h\"\n",
    });
    try tmp.dir.writeFile(io, .{
        .sub_path = "msvc/lib/x64/libcpmt.lib",
        .data = "\x00 /DEFAULTLIB:\"Advapi32\" /defaultlib:Kernel32.lib \x00",
    });
    try tmp.dir.writeFile(io, .{ .sub_path = "winsdk/lib/um/x64/advapi32.lib", .data = "advapi import" });
    try tmp.dir.writeFile(io, .{ .sub_path = "winsdk/lib/um/x64/kernel32.lib", .data = "kernel import" });
    try windowsAliases(allocator, io, tmp.dir);
    const driver = try tmp.dir.readFileAlloc(io, "winsdk/include/shared/DriverSpecs.h", allocator, .limited(100));
    try std.testing.expectEqualStrings("driver specs", driver);
    const specs = try tmp.dir.readFileAlloc(io, "winsdk/include/shared/SpecStrings.h", allocator, .limited(100));
    try std.testing.expectEqualStrings("spec strings", specs);
    try tmp.dir.access(io, "winsdk/include/um/windows.h", .{});
    try tmp.dir.access(io, "winsdk/lib/um/x64/Advapi32.lib", .{});
    try tmp.dir.access(io, "winsdk/lib/um/x64/Kernel32.lib", .{});
    // A second run over existing aliases must succeed.
    try windowsAliases(allocator, io, tmp.dir);
}
