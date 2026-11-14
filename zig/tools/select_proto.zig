//! Select complete message descriptors before C++ generation; source schemas and enum catalogs stay intact.
const std = @import("std");
const Allocator = std.mem.Allocator;

/// Filter a FileDescriptorSet to the named messages and their transitive field types.
/// Descriptor field numbers follow the pinned protobuf's descriptor.proto. Retained messages are copied verbatim.
pub fn select(output_allocator: Allocator, input: []const u8, roots: []const []const u8) ![]const u8 {
    // Everything but the returned copy lives in the arena.
    var arena = std.heap.ArenaAllocator.init(output_allocator);
    defer arena.deinit();
    const allocator = arena.allocator();
    // Top-level messages in file order, and every qualified type name mapped to its top-level message.
    var messages: std.ArrayList(Message) = .empty;
    var names: std.StringHashMapUnmanaged(usize) = .empty;
    var files: Fields = .{ .bytes = input };
    while (try files.next()) |file| {
        if (file.number != 1) continue; // FileDescriptorSet.file
        const package = try stringField(file.data, 2) orelse ""; // FileDescriptorProto.package
        const prefix = if (package.len == 0) "" else try std.fmt.allocPrint(allocator, ".{s}", .{package});
        var fields: Fields = .{ .bytes = file.data };
        while (try fields.next()) |field| {
            switch (field.number) {
                // FileDescriptorProto.message_type
                4 => {
                    const index = messages.items.len;
                    try messages.append(allocator, .{ .bytes = field.data });
                    try indexNames(allocator, &names, prefix, field.data, index);
                },
                // FileDescriptorProto.service and .extension can introduce additional message roots. These game
                // schemas use neither.
                6, 7 => return error.UnsupportedServiceOrExtension,
                else => {},
            }
        }
    }
    var pending: std.ArrayList(usize) = .empty;
    for (roots) |root| {
        const qualified = try std.fmt.allocPrint(allocator, ".{s}", .{root});
        const index = names.get(qualified) orelse return error.UnknownMessage;
        try pending.append(allocator, index);
    }
    while (pending.pop()) |index| {
        const message = &messages.items[index];
        if (message.selected) continue;
        message.selected = true;
        try addReferences(allocator, &pending, &names, message.bytes);
    }
    // Copy the set again, dropping unselected messages. `index` walks `messages` in the same file order.
    var output: std.ArrayList(u8) = .empty;
    var contents: std.ArrayList(u8) = .empty;
    files = .{ .bytes = input };
    var index: usize = 0;
    while (try files.next()) |file| {
        if (file.number != 1) {
            try output.appendSlice(allocator, file.encoded);
            continue;
        }
        contents.clearRetainingCapacity();
        var fields: Fields = .{ .bytes = file.data };
        while (try fields.next()) |field| {
            if (field.number == 4) {
                const keep = messages.items[index].selected;
                index += 1;
                if (!keep) continue;
            }
            // FileDescriptorProto.source_code_info paths refer to descriptor indexes that filtering changes; protoc
            // does not need them.
            if (field.number == 9) continue;
            try contents.appendSlice(allocator, field.encoded);
        }
        try output.append(allocator, 0x0a); // FileDescriptorSet.file, length-delimited.
        try appendVarint(allocator, &output, contents.items.len);
        try output.appendSlice(allocator, contents.items);
    }
    return output_allocator.dupe(u8, output.items);
}

const Message = struct {
    bytes: []const u8,
    selected: bool = false,
};

fn indexNames(
    allocator: Allocator,
    names: *std.StringHashMapUnmanaged(usize),
    prefix: []const u8,
    bytes: []const u8,
    owner: usize,
) !void {
    const name = try stringField(bytes, 1) orelse return error.MissingMessageName; // DescriptorProto.name
    const qualified = try std.fmt.allocPrint(allocator, "{s}.{s}", .{ prefix, name });
    try names.put(allocator, qualified, owner);
    var fields: Fields = .{ .bytes = bytes };
    while (try fields.next()) |field| {
        // DescriptorProto.nested_type stays with its enclosing message, including synthetic map entries.
        if (field.number == 3) try indexNames(allocator, names, qualified, field.data, owner);
        // DescriptorProto.enum_type
        if (field.number == 4) {
            // An enum field may reference a nested enum in an otherwise unused enclosing message.
            const enum_name = try stringField(field.data, 1) orelse return error.MissingEnumName;
            const enum_qualified = try std.fmt.allocPrint(allocator, "{s}.{s}", .{ qualified, enum_name });
            try names.put(allocator, enum_qualified, owner);
        }
    }
}

fn addReferences(
    allocator: Allocator,
    pending: *std.ArrayList(usize),
    names: *const std.StringHashMapUnmanaged(usize),
    bytes: []const u8,
) !void {
    var fields: Fields = .{ .bytes = bytes };
    while (try fields.next()) |field| {
        switch (field.number) {
            // DescriptorProto.field and .extension: FieldDescriptorProto.type_name (6) and .extendee (2).
            2, 6 => {
                if (try stringField(field.data, 6)) |type_name| {
                    // Enums remain in the descriptor even when no message refers to them.
                    if (names.get(type_name)) |index| try pending.append(allocator, index);
                }
                if (try stringField(field.data, 2)) |extendee| {
                    if (names.get(extendee)) |index| try pending.append(allocator, index);
                }
            },
            3 => try addReferences(allocator, pending, names, field.data), // DescriptorProto.nested_type
            else => {},
        }
    }
}

/// One protobuf field: `encoded` is the whole tag, length and payload; `data` is the payload alone.
const Field = struct { number: u32, encoded: []const u8, data: []const u8 };

/// Reads the fields of one encoded message in order.
const Fields = struct {
    bytes: []const u8,
    offset: usize = 0,

    fn varint(self: *Fields) !u64 {
        var value: u64 = 0;
        for (0..10) |i| {
            if (self.offset == self.bytes.len) return error.TruncatedDescriptor;
            const byte = self.bytes[self.offset];
            self.offset += 1;
            if (i == 9 and byte > 1) return error.InvalidVarint;
            value |= @as(u64, byte & 0x7f) << @intCast(i * 7);
            if (byte & 0x80 == 0) return value;
        }
        return error.InvalidVarint;
    }

    fn next(self: *Fields) !?Field {
        if (self.offset == self.bytes.len) return null;
        const start = self.offset;
        const tag = try self.varint();
        const number = std.math.cast(u32, tag >> 3) orelse return error.InvalidField;
        if (number == 0) return error.InvalidField;
        const size: usize = switch (tag & 7) {
            0 => blk: {
                _ = try self.varint();
                break :blk 0;
            },
            1 => 8,
            2 => std.math.cast(usize, try self.varint()) orelse return error.InvalidField,
            5 => 4,
            else => return error.UnsupportedWireType,
        };
        if (size > self.bytes.len - self.offset) return error.TruncatedDescriptor;
        const data = self.bytes[self.offset..][0..size];
        self.offset += size;
        return .{ .number = number, .encoded = self.bytes[start..self.offset], .data = data };
    }
};

/// The payload of the first field `number` in `bytes`, for string and nested-message fields.
fn stringField(bytes: []const u8, number: u32) !?[]const u8 {
    var fields: Fields = .{ .bytes = bytes };
    while (try fields.next()) |field| {
        if (field.number == number) return field.data;
    }
    return null;
}

fn appendVarint(allocator: Allocator, bytes: *std.ArrayList(u8), value: usize) !void {
    var remaining = value;
    while (remaining >= 0x80) : (remaining >>= 7) try bytes.append(allocator, @as(u8, @truncate(remaining)) | 0x80);
    try bytes.append(allocator, @intCast(remaining));
}

/// Usage: select-proto <descriptor set> <roots file> <output>. The roots file names one message per line; blank
/// lines and lines starting with # are ignored.
pub fn main(init: std.process.Init) !void {
    const allocator = init.arena.allocator();
    const args = try init.minimal.args.toSlice(allocator);
    if (args.len != 4) return error.InvalidArguments;
    const cwd = std.Io.Dir.cwd();
    const input = try cwd.readFileAlloc(init.io, args[1], allocator, .limited(16 * 1024 * 1024));
    const manifest = try cwd.readFileAlloc(init.io, args[2], allocator, .limited(64 * 1024));
    var roots: std.ArrayList([]const u8) = .empty;
    var lines = std.mem.splitScalar(u8, manifest, '\n');
    while (lines.next()) |line| {
        const name = std.mem.trim(u8, line, " \t\r");
        if (name.len != 0 and name[0] != '#') try roots.append(allocator, name);
    }
    const selected = try select(allocator, input, roots.items);
    try cwd.writeFile(init.io, .{ .sub_path = args[3], .data = selected });
}
