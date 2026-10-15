//! Extract Mach-O objects without trusting the input archive's index or member alignment.
const std = @import("std");

const Objects = struct {
    bytes: []const u8,
    offset: usize = 8,

    fn init(bytes: []const u8) !Objects {
        if (!std.mem.startsWith(u8, bytes, "!<arch>\n")) return error.InvalidArchive;
        return .{ .bytes = bytes };
    }

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
            if (std.mem.startsWith(u8, name, "__.SYMDEF") or
                std.mem.eql(u8, name, "/") or std.mem.eql(u8, name, "//") or
                std.mem.eql(u8, name, "/SYM64/")) continue;
            if (!std.mem.startsWith(u8, member, "\xcf\xfa\xed\xfe") and
                !std.mem.startsWith(u8, member, "\xfe\xed\xfa\xcf")) return error.NotMachO;
            return member;
        }
        return null;
    }
};

/// Extract objects and a file list for Apple libtool.
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
        // Unique names preserve distinct objects with the same original basename.
        const name = try std.fmt.allocPrint(allocator, "{d}.o", .{count});
        try dest.writeFile(io, .{ .sub_path = name, .data = object });
        try list.appendSlice(allocator, try std.fmt.allocPrint(allocator, "{s}\n", .{name}));
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
