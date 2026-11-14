//! Extract Mach-O objects without trusting the input archive's index or member alignment.
const std = @import("std");

/// The Mach-O members of an ar archive, in order. Index members are skipped.
const Objects = struct {
    bytes: []const u8,
    /// Past the "!<arch>\n" magic.
    offset: usize = 8,

    fn init(bytes: []const u8) !Objects {
        if (!std.mem.startsWith(u8, bytes, "!<arch>\n")) return error.InvalidArchive;
        return .{ .bytes = bytes };
    }

    // Each member has a 60-byte text header: name in bytes 0..16, decimal size in 48..58, "`\n" in 58..60. Members
    // are padded to an even length. A BSD name "#1/<n>" stores the real name in the first n bytes of the data.
    fn next(self: *Objects) !?[]const u8 {
        while (self.offset < self.bytes.len) {
            if (self.bytes.len - self.offset < 60) return error.TruncatedHeader;
            const header = self.bytes[self.offset..][0..60];
            if (!std.mem.eql(u8, header[58..60], "`\n")) return error.InvalidHeader;
            const size = try std.fmt.parseInt(usize, std.mem.trim(u8, header[48..58], " "), 10);
            const start = self.offset + 60;
            if (size > self.bytes.len - start) return error.TruncatedMember;
            var member = self.bytes[start..][0..size];
            self.offset = start + size;
            if (size % 2 != 0) {
                if (self.offset == self.bytes.len) return error.MissingPadding;
                self.offset += 1;
            }
            var name = std.mem.trimEnd(u8, header[0..16], " ");
            if (std.mem.startsWith(u8, name, "#1/")) {
                const name_size = try std.fmt.parseInt(usize, name[3..], 10);
                if (name_size > member.len) return error.InvalidName;
                name = std.mem.trimEnd(u8, member[0..name_size], "\x00");
                member = member[name_size..];
            }
            // BSD and GNU symbol tables and the GNU long-name table.
            if (std.mem.startsWith(u8, name, "__.SYMDEF") or
                std.mem.eql(u8, name, "/") or std.mem.eql(u8, name, "//") or
                std.mem.eql(u8, name, "/SYM64/")) continue;
            // MH_MAGIC_64 in either byte order.
            if (!std.mem.startsWith(u8, member, "\xcf\xfa\xed\xfe") and
                !std.mem.startsWith(u8, member, "\xfe\xed\xfa\xcf")) return error.NotMachO;
            return member;
        }
        return null;
    }
};

/// Usage: extract-macho-archive <archive> <output dir>. Writes the objects as 0.o, 1.o, ... and lists them in
/// objects.txt for `libtool -filelist`.
pub fn main(init: std.process.Init) !void {
    const allocator = init.arena.allocator();
    const args = try init.minimal.args.toSlice(allocator);
    if (args.len != 3) return error.InvalidArguments;
    const io = init.io;
    const cwd = std.Io.Dir.cwd();
    const bytes = try cwd.readFileAlloc(io, args[1], allocator, .unlimited);
    var objects = try Objects.init(bytes);
    try cwd.createDirPath(io, args[2]);
    var dest = try cwd.openDir(io, args[2], .{});
    defer dest.close(io);
    var list: std.ArrayList(u8) = .empty;
    var count: usize = 0;
    while (try objects.next()) |object| : (count += 1) {
        // Numbered names keep distinct objects that share an original basename.
        const name = try std.fmt.allocPrint(allocator, "{d}.o", .{count});
        try dest.writeFile(io, .{ .sub_path = name, .data = object });
        try list.print(allocator, "{s}\n", .{name});
    }
    if (count == 0) return error.EmptyArchive;
    try dest.writeFile(io, .{ .sub_path = "objects.txt", .data = list.items });
}

fn appendMember(allocator: std.mem.Allocator, list: *std.ArrayList(u8), name: []const u8, bytes: []const u8) !void {
    var header: [60]u8 = @splat(' ');
    @memcpy(header[0..name.len], name);
    _ = try std.fmt.bufPrint(header[48..58], "{d}", .{bytes.len});
    @memcpy(header[58..60], "`\n");
    try list.appendSlice(allocator, &header);
    try list.appendSlice(allocator, bytes);
    if (bytes.len % 2 != 0) try list.append(allocator, '\n');
}

test "preserve duplicate object names and BSD inline names while skipping indices" {
    const allocator = std.testing.allocator;
    var bytes: std.ArrayList(u8) = .empty;
    defer bytes.deinit(allocator);
    try bytes.appendSlice(allocator, "!<arch>\n");
    try appendMember(allocator, &bytes, "/", "obsolete index");
    try appendMember(allocator, &bytes, "same.o/", "\xcf\xfa\xed\xfefirst");
    try appendMember(allocator, &bytes, "#1/6", "same.o\xcf\xfa\xed\xfesecond");
    var objects = try Objects.init(bytes.items);
    try std.testing.expectEqualStrings("\xcf\xfa\xed\xfefirst", (try objects.next()).?);
    try std.testing.expectEqualStrings("\xcf\xfa\xed\xfesecond", (try objects.next()).?);
    try std.testing.expectEqual(null, try objects.next());
}

test "reject incomplete archive members" {
    var objects = try Objects.init("!<arch>\nshort");
    try std.testing.expectError(error.TruncatedHeader, objects.next());
}
