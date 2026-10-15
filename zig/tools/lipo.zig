//! Merge two thin Mach-O binaries into a deterministic universal binary.
const std = @import("std");

/// Merge ARM64 and x86_64 Mach-O executables.
pub fn main(init: std.process.Init) !void {
    const allocator = init.arena.allocator();
    const args = try init.minimal.args.toSlice(allocator);
    if (args.len != 4) return error.InvalidArguments;
    const io = init.io;
    const cwd = std.Io.Dir.cwd();
    const inputs = [2][]const u8{
        try cwd.readFileAlloc(io, args[1], allocator, .limited(1024 * 1024 * 1024)),
        try cwd.readFileAlloc(io, args[2], allocator, .limited(1024 * 1024 * 1024)),
    };
    var header: [48]u8 = @splat(0);
    std.mem.writeInt(u32, header[0..4], 0xcafebabe, .big);
    std.mem.writeInt(u32, header[4..8], 2, .big);
    var offsets: [2]usize = undefined;
    var offset: usize = header.len;
    for (inputs, 0..) |input, i| {
        if (input.len < 32 or std.mem.readInt(u32, input[0..4], .little) != 0xfeedfacf) return error.ExpectedThinMachO;
        const cpu = std.mem.readInt(u32, input[4..8], .little);
        if (cpu != (if (i == 0) @as(u32, 0x0100000c) else @as(u32, 0x01000007))) return error.UnexpectedArchitecture;
        offset = std.mem.alignForward(usize, offset, 16384);
        offsets[i] = offset;
        const record = header[8 + i * 20 ..][0..20];
        std.mem.writeInt(u32, record[0..4], cpu, .big);
        std.mem.writeInt(u32, record[4..8], std.mem.readInt(u32, input[8..12], .little), .big);
        std.mem.writeInt(u32, record[8..12], std.math.cast(u32, offset) orelse return error.BinaryTooLarge, .big);
        std.mem.writeInt(u32, record[12..16], std.math.cast(u32, input.len) orelse return error.BinaryTooLarge, .big);
        std.mem.writeInt(u32, record[16..20], 14, .big);
        offset += input.len;
    }
    const file = try cwd.createFile(io, args[3], .{ .permissions = .executable_file });
    defer file.close(io);
    var buffer: [64 * 1024]u8 = undefined;
    var writer = file.writer(io, &buffer);
    try writer.interface.writeAll(&header);
    var written: usize = header.len;
    for (inputs, offsets) |input, start| {
        try writer.interface.splatByteAll(0, start - written);
        try writer.interface.writeAll(input);
        written = start + input.len;
    }
    try writer.end();
}
