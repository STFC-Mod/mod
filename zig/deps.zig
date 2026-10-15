//! Third-party C and C++ libraries other than protobuf, compiled from the pins in build.zig.zon.
//! Source lists are explicit where upstream ships platform files or tools we must not compile.
const std = @import("std");
const Ctx = @import("ctx.zig").Ctx;
const Lib = @import("ctx.zig").Lib;
const globFiles = @import("ctx.zig").globFiles;

// --- Logging, settings, JSON, INI -----------------------------------------------------------------------------

/// spdlog with its bundled fmt, compiled once instead of header-only in every consumer.
pub fn spdlog(ctx: *const Ctx) Lib {
    const b = ctx.b;
    const src = b.dependency("spdlog", .{}).path("");
    const module = ctx.createModule(true);
    module.addIncludePath(src.path(b, "include"));
    module.addCMacro("SPDLOG_COMPILED_LIB", "1");
    module.addCSourceFiles(.{
        .root = src,
        .files = &.{
            "src/spdlog.cpp",
            "src/stdout_sinks.cpp",
            "src/color_sinks.cpp",
            "src/file_sinks.cpp",
            "src/async.cpp",
            "src/cfg.cpp",
            "src/bundled_fmtlib_format.cpp",
        },
        .flags = ctx.cxxFlags("c++20", &.{}),
    });
    return .{
        .artifact = ctx.staticLib("spdlog", module),
        .include_dirs = ctx.dirs(&.{src.path(b, "include")}),
        .defines = &.{.{ "SPDLOG_COMPILED_LIB", "1" }},
    };
}

/// toml++, header-only.
pub fn tomlplusplus(ctx: *const Ctx) Lib {
    return .{ .include_dirs = ctx.dirs(&.{ctx.b.dependency("tomlplusplus", .{}).path("include")}) };
}

/// nlohmann/json, header-only.
pub fn nlohmannJson(ctx: *const Ctx) Lib {
    return .{ .include_dirs = ctx.dirs(&.{ctx.b.dependency("nlohmann_json", .{}).path("single_include")}) };
}

/// inifile-cpp, header-only. Only the macOS loader reads INI files.
pub fn inifileCpp(ctx: *const Ctx) Lib {
    return .{ .include_dirs = ctx.dirs(&.{ctx.b.dependency("inifile_cpp", .{}).path("include")}) };
}

// --- Detours ---------------------------------------------------------------------------------------------------

/// Zydis and Zycore, SPUD's x86 decoder.
pub fn zydis(ctx: *const Ctx) Lib {
    const b = ctx.b;
    const core = b.dependency("zycore", .{}).path("");
    const src = b.dependency("zydis", .{}).path("");
    const module = ctx.createModule(false);
    module.addIncludePath(core.path(b, "include"));
    module.addIncludePath(src.path(b, "include"));
    module.addIncludePath(src.path(b, "src"));
    module.addCMacro("ZYCORE_STATIC_BUILD", "1");
    module.addCMacro("ZYDIS_STATIC_BUILD", "1");
    // Native fixtures on Linux: glibc hides Zycore's recursive mutex attribute under strict C11.
    if (ctx.target.result.os.tag == .linux) module.addCMacro("_GNU_SOURCE", "1");
    inline for (.{ core, src }) |root| module.addCSourceFiles(.{
        .root = root,
        .files = globFiles(b, root, "src", &.{".c"}, &.{}),
        .flags = ctx.cFlags(&.{"-std=c11"}),
    });
    return .{
        .artifact = ctx.staticLib("zydis", module),
        .include_dirs = ctx.dirs(&.{ core.path(b, "include"), src.path(b, "include") }),
        .defines = &.{ .{ "ZYCORE_STATIC_BUILD", "1" }, .{ "ZYDIS_STATIC_BUILD", "1" } },
    };
}

/// Capstone's core and AArch64 decoder only, for SPUD's ARM64 relocator.
pub fn capstone(ctx: *const Ctx) Lib {
    const b = ctx.b;
    const src = b.dependency("capstone", .{}).path("");
    const module = ctx.createModule(false);
    module.addIncludePath(src.path(b, "include"));
    module.addIncludePath(src);
    module.addCMacro("CAPSTONE_HAS_AARCH64", "1");
    module.addCMacro("CAPSTONE_USE_SYS_DYN_MEM", "1");
    module.addCSourceFiles(.{
        .root = src,
        .files = &.{
            "cs.c",
            "Mapping.c",
            "MCInst.c",
            "MCInstrDesc.c",
            "MCInstPrinter.c",
            "MCRegisterInfo.c",
            "SStream.c",
            "utils.c",
            "arch/AArch64/AArch64BaseInfo.c",
            "arch/AArch64/AArch64Disassembler.c",
            "arch/AArch64/AArch64DisassemblerExtension.c",
            "arch/AArch64/AArch64InstPrinter.c",
            "arch/AArch64/AArch64Mapping.c",
            "arch/AArch64/AArch64Module.c",
        },
        .flags = ctx.cFlags(&.{"-std=c11"}),
    });
    return .{
        .artifact = ctx.staticLib("capstone", module),
        .include_dirs = ctx.dirs(&.{src.path(b, "include")}),
    };
}

/// The vendored SPUD detour library. third_party/spud is SPUD's unmodified include/, src/ and LICENSE; the pinned
/// commit is in docs/THIRD_PARTY.md.
pub fn spud(ctx: *const Ctx, zy: Lib, cs: Lib) Lib {
    const b = ctx.b;
    const src = b.path("third_party/spud");
    const aarch64 = ctx.isAarch64();
    const module = ctx.createModule(true);
    module.addIncludePath(src.path(b, "include"));
    module.addIncludePath(src.path(b, "src"));
    zy.linkInto(module);
    // Only the ARM64 relocator uses Capstone. Like upstream, x86-64 builds leave both out.
    if (aarch64) {
        module.addCMacro("SPUD_AARCH64_SUPPORT", "1");
        cs.linkInto(module);
    }
    // Every SIMD kernel for the architecture is compiled. Each opts in per function with SPUD_TARGET and is picked
    // at run time, so no translation unit raises the CPU baseline.
    const skip: []const []const u8 = if (aarch64)
        &.{ "_sse.cc", "_avx2.cc" }
    else
        &.{ "_neon.cc", "src/detour/aarch64" };
    module.addCSourceFiles(.{
        .root = src,
        .files = globFiles(b, src, "src", &.{".cc"}, skip),
        .flags = ctx.cxxFlags("c++23", &.{}),
    });
    return .{
        .artifact = ctx.staticLib("spud", module),
        .dependencies = b.allocator.dupe(Lib, if (aarch64) &.{ zy, cs } else &.{zy}) catch @panic("OOM"),
        .include_dirs = ctx.dirs(&.{src.path(b, "include")}),
    };
}

// --- Networking ------------------------------------------------------------------------------------------------

/// zlib, for libcurl on Windows.
pub fn zlib(ctx: *const Ctx) Lib {
    const b = ctx.b;
    const src = b.dependency("zlib", .{}).path("");
    const module = ctx.createModule(false);
    module.addIncludePath(src);
    if (!ctx.isWindows()) module.addCMacro("HAVE_UNISTD_H", "1");
    module.addCSourceFiles(.{
        .root = src,
        .files = &.{
            "adler32.c",
            "compress.c",
            "crc32.c",
            "deflate.c",
            "gzclose.c",
            "gzlib.c",
            "gzread.c",
            "gzwrite.c",
            "inflate.c",
            "infback.c",
            "inftrees.c",
            "inffast.c",
            "trees.c",
            "uncompr.c",
            "zutil.c",
        },
        .flags = ctx.cFlags(&.{"-std=c11"}),
    });
    return .{ .artifact = ctx.staticLib("zlib", module), .include_dirs = ctx.dirs(&.{src}) };
}

/// libcurl. macOS links Apple's system copy. Windows compiles the pin with Schannel TLS and the unused protocols
/// off, so the DLL needs no OpenSSL. null until Zig has fetched the lazy dependency.
pub fn curl(ctx: *const Ctx, z: Lib) ?Lib {
    const b = ctx.b;
    // The SDK's curl headers match the system library, so CPR never picks newer APIs than Apple ships.
    if (ctx.isMacos()) return .{ .system_libs = &.{"curl"} };

    const src = (b.lazyDependency("curl", .{}) orelse return null).path("");
    const module = ctx.createModule(false);
    module.addIncludePath(src.path(b, "include"));
    module.addIncludePath(src.path(b, "lib"));
    module.addCMacro("BUILDING_LIBCURL", "1");
    module.addCMacro("CURL_STATICLIB", "1");
    z.linkInto(module);
    module.addCMacro("USE_SCHANNEL", "1");
    module.addCMacro("USE_WINDOWS_SSPI", "1");
    module.addCMacro("HAVE_LIBZ", "1");
    inline for (.{
        "FTP",  "FILE", "LDAP", "LDAPS", "RTSP", "DICT",   "TELNET",
        "TFTP", "POP3", "IMAP", "SMB",   "SMTP", "GOPHER", "MQTT",
    }) |protocol| module.addCMacro("CURL_DISABLE_" ++ protocol, "1");
    inline for (.{ "ws2_32", "crypt32", "bcrypt", "secur32", "advapi32", "iphlpapi", "user32" }) |lib|
        module.linkSystemLibrary(lib, .{});
    module.addCSourceFiles(.{
        .root = src,
        .files = globFiles(b, src, "lib", &.{".c"}, &.{}),
        .flags = ctx.cFlags(&.{"-std=c11"}),
    });
    return .{
        .artifact = ctx.staticLib("curl", module),
        .dependencies = b.allocator.dupe(Lib, &.{z}) catch @panic("OOM"),
        .include_dirs = ctx.dirs(&.{src.path(b, "include")}),
        .defines = &.{.{ "CURL_STATICLIB", "1" }},
    };
}

/// CPR, the C++ HTTP client over libcurl.
pub fn cpr(ctx: *const Ctx, curl_lib: Lib) Lib {
    const b = ctx.b;
    const src = b.dependency("cpr", .{}).path("");
    // CPR's CMake generates this header. Keep it in step with the cpr pin in build.zig.zon.
    const version = b.addWriteFiles().add("cpr/cprver.h",
        \\#pragma once
        \\#define CPR_VERSION "1.14.2"
        \\#define CPR_VERSION_MAJOR 1
        \\#define CPR_VERSION_MINOR 14
        \\#define CPR_VERSION_PATCH 2
        \\#define CPR_VERSION_NUM 0x010e02
        \\
    );
    const generated = version.dirname().dirname();
    const module = ctx.createModule(true);
    module.addIncludePath(src.path(b, "include"));
    module.addIncludePath(generated);
    curl_lib.linkInto(module);
    module.addCSourceFiles(.{
        .root = src,
        .files = globFiles(b, src, "cpr", &.{".cpp"}, &.{}),
        .flags = ctx.cxxFlags("c++17", &.{}),
    });
    return .{
        .artifact = ctx.staticLib("cpr", module),
        .include_dirs = ctx.dirs(&.{ src.path(b, "include"), generated }),
    };
}
