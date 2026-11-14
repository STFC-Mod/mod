cmake_minimum_required(VERSION 3.23 FATAL_ERROR)

if(WIN32)
    project(spud VERSION 0.1.0 LANGUAGES C CXX ASM_MASM)
else()
    project(spud VERSION 0.1.0 LANGUAGES C CXX ASM)
endif(WIN32)

# Only the top-level project owns BUILD_TESTING and the CTest dashboard targets.
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()
include(FetchContent)
# Timestamps of extracted dependency tarballs are the extraction time, so a
# changed pin rebuilds.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
endif()

option(SPUD_BUILD_TESTS "Build Catch2 unit tests" ${PROJECT_IS_TOP_LEVEL})
option(SPUD_BUILD_BENCHMARKS "Build Catch2 benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(SPUD_BUILD_EXAMPLES "Build example programs" ${PROJECT_IS_TOP_LEVEL})

if(
    CMAKE_SYSTEM_PROCESSOR MATCHES "arm"
    OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch"
    OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64"
)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH "arm64")
    else()
        set(ARCH "arm")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^mips.*")
    message(FATAL_ERROR "MIPS is not a supported Spud architecture")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^ppc.*")
    message(FATAL_ERROR "PowerPC is not a supported Spud architecture")
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(ARCH "x86_64")
    else()
        message(FATAL_ERROR "32-bit x86 is not a supported Spud architecture")
    endif()
endif()

if(CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES _spud_osx_arch_count)
    if(_spud_osx_arch_count GREATER 1)
        message(
            FATAL_ERROR
            "Spud does not support universal binaries. Specify one of arm64, x86_64."
        )
    endif()
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        set(ARCH "arm64")
    elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        set(ARCH "x86_64")
    else()
        message(
            FATAL_ERROR
            "Architecture ${CMAKE_OSX_ARCHITECTURES} is not supported. "
            "Use arm64 or x86_64."
        )
    endif()
endif()

if(NOT ARCH STREQUAL "x86_64" AND NOT ARCH STREQUAL "arm64")
    message(FATAL_ERROR "Architecture ${ARCH} is not supported")
endif()
if(WIN32 AND ARCH STREQUAL "arm64")
    message(FATAL_ERROR "Windows ARM64 is not a supported Spud target")
endif()

# ARM64 hosts need the Capstone-backed backend. x86-64 builds only compile it
# when asked, or for the unit tests, which check ARM64 encodings on every host.
if(ARCH STREQUAL "arm64" OR SPUD_BUILD_TESTS)
    set(SPUD_AARCH64_DEFAULT ON)
else()
    set(SPUD_AARCH64_DEFAULT OFF)
endif()

# Generated from cmake/spud.manifest.json by `zig build gen-cmake`.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/generated/options.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/generated/deps.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/generated/sources.cmake")

# Deprecated spelling, kept so existing -DSPUD_NO_INSTALL=ON builds still work.
if(SPUD_NO_INSTALL)
    message(DEPRECATION "SPUD_NO_INSTALL is deprecated; use SPUD_INSTALL=OFF")
    set(SPUD_INSTALL OFF)
endif()


spud_provide_zydis()
spud_provide_capstone()

add_library("spud" STATIC)
add_library(spud::spud ALIAS spud)
target_compile_features("spud" PUBLIC cxx_std_20)
target_link_libraries("spud" PRIVATE "Zydis")
if(WIN32)
    target_link_libraries("spud" PRIVATE "psapi")
endif()
if(SPUD_AARCH64_SUPPORT)
    target_link_libraries("spud" PRIVATE "capstone")
endif()

# Warnings for spud's own targets, never for vendored code. Errors are for
# CI only, so a newer compiler's new warning cannot break a consumer build.
function(spud_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE "/permissive-" "/W4")
        if(SPUD_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE "/WX")
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
        if(SPUD_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
spud_warnings(spud)
if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT MSVC)
    target_compile_options(spud PUBLIC $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-fms-extensions>)
endif()

if(SPUD_DETOUR_TRACING)
    target_compile_definitions(spud PUBLIC SPUD_DETOUR_TRACING=1)
endif()

target_compile_definitions(spud PUBLIC
    SPUD_UNWIND_SUPPORT=$<BOOL:${SPUD_UNWIND_SUPPORT}>)

# No public header reads it; the tests that include src/ add it themselves.
if(SPUD_AARCH64_SUPPORT)
    target_compile_definitions(spud PRIVATE SPUD_AARCH64_SUPPORT=1)
endif()

target_compile_definitions(spud PRIVATE NOMINMAX=1)

# No forced LTO. GCC's IPO objects are LTO-only (-fno-fat-lto-objects), so an
# installed libspud.a would only link with the same GCC and plugin. Set
# CMAKE_INTERPROCEDURAL_OPTIMIZATION in the consuming build to opt in.

target_sources(
    "spud"
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS "include"
        FILES ${SPUD_PUBLIC_HDRS}
)
target_include_directories(
    "spud"
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE "src"
)

if(ARCH STREQUAL "arm64" AND NOT SPUD_AARCH64_SUPPORT)
    message(FATAL_ERROR "ARM64 hosts require SPUD_AARCH64_SUPPORT=ON")
endif()

# GCC and Clang read the hook context register with inline assembly; MSVC
# has no x64 inline assembly and calls this helper instead.
# CMake's MASM language only claims .asm files, so the .masm helper needs an
# explicit language or it is silently left out of the library. clang-cl
# takes the inline path like Clang everywhere else.
set(ASM_SRCS "")
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(ASM_SRCS ${SPUD_ASM_X86_64_MASM})
    set_source_files_properties(${ASM_SRCS} PROPERTIES LANGUAGE ASM_MASM)
endif()

if(ARCH STREQUAL "arm64")
    set(SPUD_NEON_SRCS ${SPUD_SIMD_NEON_SRCS})
    set(SPUD_SSE_SRCS "")
    set(SPUD_AVX2_SRCS "")
else()
    set(SPUD_SSE_SRCS ${SPUD_SIMD_SSE_SRCS})
    set(SPUD_AVX2_SRCS ${SPUD_SIMD_AVX2_SRCS})
    set(SPUD_NEON_SRCS "")
endif()

set(SPUD_AARCH64_FILES "")
if(SPUD_AARCH64_SUPPORT)
    set(SPUD_AARCH64_FILES ${SPUD_AARCH64_BACKEND_SRCS})
endif()

target_sources(
    "spud"
    PRIVATE
        ${SPUD_COMMON_SRCS}
        "${SPUD_AARCH64_FILES}"
        "${ASM_SRCS}"
        "${SPUD_SSE_SRCS}"
        "${SPUD_AVX2_SRCS}"
        "${SPUD_NEON_SRCS}"
)

if(SPUD_INSTALL)
    if(SPUD_UNWIND_SUPPORT)
        set(SPUD_UNWIND_SUPPORT_VALUE 1)
    else()
        set(SPUD_UNWIND_SUPPORT_VALUE 0)
    endif()
    if(SPUD_DETOUR_TRACING)
        set(SPUD_DETOUR_TRACING_VALUE 1)
    else()
        set(SPUD_DETOUR_TRACING_VALUE 0)
    endif()
    set(SPUD_STATIC_LIB_FILENAME
        "${CMAKE_STATIC_LIBRARY_PREFIX}spud${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )
    set(SPUD_ZYDIS_FILENAME
        "${CMAKE_STATIC_LIBRARY_PREFIX}Zydis${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )
    set(SPUD_ZYCORE_FILENAME
        "${CMAKE_STATIC_LIBRARY_PREFIX}Zycore${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )

    install(
        TARGETS ${PROJECT_NAME}
        FILE_SET public_headers
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
    install(
        FILES
            "$<TARGET_FILE:Zydis>"
            "$<TARGET_FILE:Zycore>"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/spudConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/spudConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/spud"
        PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/spudConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/spudConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/spudConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/spud"
    )
endif()

if(SPUD_BUILD_TESTS OR SPUD_BUILD_BENCHMARKS)
    spud_provide_catch2()
    # The signature scan corpus, compiled once for both executables.
    add_library(spud_scan_corpus OBJECT "tests/test_bin.cc")
    target_link_libraries(spud_scan_corpus PRIVATE spud)
endif()

if(SPUD_BUILD_BENCHMARKS)
    add_executable("spud.benchmark" benchmark/test.cc)
    target_link_libraries(
        "spud.benchmark"
        PRIVATE "Catch2" "Catch2::Catch2WithMain" "spud" spud_scan_corpus
    )
    target_include_directories("spud.benchmark" PRIVATE "benchmark" "tests")
    spud_warnings(spud.benchmark)
endif()

if(SPUD_BUILD_TESTS)
    set(TEST_SRCS "tests/signature.cc")

    file(
        GLOB_RECURSE TEST_DETOUR_SHARED_SRCS CONFIGURE_DEPENDS
        tests/detour/shared/*.cc
        tests/detour/shared/*.h
    )
    list(APPEND TEST_SRCS ${TEST_DETOUR_SHARED_SRCS})
    if(NOT MSVC)
        set_source_files_properties(
            "tests/detour/shared/instrumented.cc"
            PROPERTIES COMPILE_OPTIONS "-finstrument-functions")
    endif()

    if(ARCH MATCHES "arm64")
        file(
            GLOB_RECURSE TEST_DETOUR_SRCS CONFIGURE_DEPENDS
            tests/detour/arm64/*.cc
            tests/detour/arm64/*.h
        )
    else()
        file(
            GLOB_RECURSE TEST_DETOUR_SRCS CONFIGURE_DEPENDS
            tests/detour/x86_64/*.cc
            tests/detour/x86_64/*.h
        )
    endif()

    # clang-cl assembles the GAS form; MSVC needs the MASM twin.
    if(WIN32 AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        file(GLOB_RECURSE TEST_ASM_SRCS CONFIGURE_DEPENDS tests/detour/x86_64/*.asm)
    else()
        if(ARCH MATCHES "arm64")
            file(GLOB_RECURSE TEST_ASM_SRCS CONFIGURE_DEPENDS tests/detour/arm64/*.S)
        else()
            file(GLOB_RECURSE TEST_ASM_SRCS CONFIGURE_DEPENDS tests/detour/x86_64/*.S)
        endif()
    endif()

    list(APPEND TEST_SRCS ${TEST_DETOUR_SRCS})
    list(APPEND TEST_SRCS ${TEST_ASM_SRCS})

    add_executable(
        "spud.test"
        ${TEST_SRCS}
        "tests/test_util.h"
        "tests/test_util.cc"
    )
    add_library(spud_import_probe SHARED "tests/detour/shared/import_probe.c")
    set_target_properties(
        spud_import_probe
        PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:spud.test>"
            RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:spud.test>"
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_library(spud_import_lazy SHARED "tests/detour/shared/import_lazy.c")
        target_link_libraries(spud_import_lazy PRIVATE spud_import_probe)
        target_link_options(spud_import_lazy PRIVATE "LINKER:-z,lazy")
        set_target_properties(
            spud_import_lazy
            PROPERTIES LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:spud.test>"
        )
        target_link_libraries("spud.test" PRIVATE spud_import_lazy)
        target_compile_definitions("spud.test" PRIVATE SPUD_TEST_LAZY_IMPORT=1)
    endif()
    target_include_directories("spud.test" PRIVATE "tests" "src")
    spud_warnings(spud.test)
    target_link_libraries(
        "spud.test"
        PRIVATE
            "Catch2"
            "Catch2::Catch2WithMain"
            "spud"
            "Zydis"
            spud_import_probe
            spud_scan_corpus
    )
    if(SPUD_AARCH64_SUPPORT)
        target_link_libraries("spud.test" PRIVATE "capstone")
    endif()

    add_test(NAME spud.test COMMAND spud.test)
    # A hung patch should fail the run, not stall CI until its global limit.
    # The suite takes well under a minute on a desktop; 300 s leaves room
    # for slow shared runners.
    set_tests_properties(spud.test PROPERTIES TIMEOUT 300)
    # Inline overflow checks that trap without a runtime; -O2 keeps clang's
    # trap block after the return, the shape the prologue scan must end on.
    if(NOT ARCH MATCHES "arm64" AND NOT WIN32 AND
       CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set_source_files_properties(tests/detour/x86_64/sanitized/sanitized.cc
            PROPERTIES COMPILE_OPTIONS
            "-O2;-fsanitize=signed-integer-overflow;-fsanitize-trap=signed-integer-overflow")
    endif()
    # A dense switch becomes a register-dispatched jump table at -O2. MSVC
    # debug builds cannot mix /O2 with /RTC1, so they test the plain form.
    if(NOT ARCH MATCHES "arm64" AND NOT MSVC)
        set_source_files_properties(tests/detour/x86_64/optimized/switch.cc
            PROPERTIES COMPILE_OPTIONS "-O2")
    endif()
    # GCC moves catch handlers into .cold parts at -O2.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_source_files_properties(tests/detour/shared/cold_landing.cc
            PROPERTIES COMPILE_OPTIONS "-O2")
    endif()
    if(NOT WIN32)
        add_executable(spud.window_allocations tests/detour/audit/window_allocations.cc)
        target_include_directories(spud.window_allocations PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(spud.window_allocations PRIVATE spud)
        add_test(NAME spud.window_allocations COMMAND spud.window_allocations)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_library(spud.source_module_library MODULE tests/detour/audit/source_module_library.c)
        add_executable(spud.source_module tests/detour/audit/source_module.cc)
        target_link_libraries(spud.source_module PRIVATE spud ${CMAKE_DL_LIBS})
        add_test(NAME spud.source_module COMMAND spud.source_module $<TARGET_FILE:spud.source_module_library>)
        add_test(NAME spud.source_module_disabled COMMAND spud.source_module $<TARGET_FILE:spud.source_module_library> disabled)
        # A non-PIE executable takes a function's address as its own PLT
        # entry, and -z lazy leaves the GOT slot behind it unbound.
        add_executable(spud.lazy_import tests/detour/audit/lazy_import.cc)
        target_include_directories(spud.lazy_import PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
        target_compile_options(spud.lazy_import PRIVATE -fno-pie)
        target_link_options(spud.lazy_import PRIVATE -no-pie "LINKER:-z,lazy")
        target_link_libraries(spud.lazy_import PRIVATE spud spud_import_probe)
        add_test(NAME spud.lazy_import COMMAND spud.lazy_import)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ARCH STREQUAL "x86_64")
        # Hand-assembled relocation hazards from the relocation audit, one
        # process per case and mode because a bad trampoline faults. `near`
        # emits a trampoline next to the code, `hook` installs a detour.
        # Accepted cases must return the original result; refused ones must
        # be refused (limits.md lists why), not run.
        add_executable(spud.relocation_probe tests/detour/audit/probe.cc)
        target_include_directories(spud.relocation_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(spud.relocation_probe PRIVATE spud Zydis)
        if(SPUD_AARCH64_SUPPORT)
            target_link_libraries(spud.relocation_probe PRIVATE capstone)
        endif()
        # `near` writes trampoline dumps to the working directory.
        set(_spud_probe_dir "${CMAKE_CURRENT_BINARY_DIR}/relocation-probe")
        file(MAKE_DIRECTORY "${_spud_probe_dir}")
        set(_spud_probe_accepted r11_indirect gs_push gs_call ah_load push16
            rsp_store eip_load late_self_read backedge_1100 backedge_padding
            pop_slot gs_self)
        # PC discovery (call_pc, x87_pc), a segmented indirect jump, branches
        # into the middle of a copied instruction, an indirect jump back into
        # the copy, and register-indexed RIP bit operations.
        set(_spud_probe_refused call_pc gs_jmp overlap_branch overlap_rewrite
            overlap_movabs backedge_indirect bt_dynamic_self bts_dynamic_self
            x87_pc)
        foreach(_case ${_spud_probe_accepted} ${_spud_probe_refused})
            foreach(_mode near hook)
                # gs_self reads its own first byte through a nonzero GS base.
                # Aliases into the patch through a segment base are outside
                # the detour contract, so hooked it reads the jump stub.
                if(_case STREQUAL "gs_self" AND _mode STREQUAL "hook")
                    continue()
                endif()
                set(_test spud.relocation_probe.${_case}.${_mode})
                add_test(NAME ${_test}
                    COMMAND spud.relocation_probe ${_case} ${_mode}
                    WORKING_DIRECTORY "${_spud_probe_dir}")
                set_tests_properties(${_test} PROPERTIES TIMEOUT 30)
                if(_case IN_LIST _spud_probe_refused)
                    # Exit code 3 with REFUSED; a run that gets as far as
                    # printing a result did not refuse.
                    set_tests_properties(${_test} PROPERTIES
                        PASS_REGULAR_EXPRESSION "REFUSED"
                        FAIL_REGULAR_EXPRESSION "actual=")
                endif()
            endforeach()
        endforeach()
    endif()
    if(SPUD_UNWIND_SUPPORT AND
       ((CMAKE_SYSTEM_NAME STREQUAL "Linux" AND (ARCH STREQUAL "x86_64" OR ARCH STREQUAL "arm64")) OR
        (APPLE AND ARCH STREQUAL "arm64")))
        add_executable(spud.spill_unwind tests/detour/audit/spill_unwind.cc)
        target_include_directories(spud.spill_unwind PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(spud.spill_unwind PRIVATE spud Zydis)
        if(SPUD_AARCH64_SUPPORT)
            target_link_libraries(spud.spill_unwind PRIVATE capstone)
        endif()
        add_test(NAME spud.spill_unwind COMMAND spud.spill_unwind)
        set_tests_properties(spud.spill_unwind PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    if(WIN32)
        add_executable(spud.windows_unwind tests/detour/audit/windows_unwind.cc)
        if(MSVC)
            target_sources(spud.windows_unwind PRIVATE tests/detour/audit/windows_msvc.cc)
            target_compile_definitions(spud.windows_unwind PRIVATE SPUD_MSVC_FIXTURE=1 SPUD_MSVC_GS_FIXTURE=1)
            # The fixture deliberately throws through extern "C" entries.
            set_source_files_properties(tests/detour/audit/windows_msvc.cc
                PROPERTIES COMPILE_OPTIONS "/EHsc-")
        endif()
        target_include_directories(spud.windows_unwind PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(spud.windows_unwind PRIVATE spud Zydis)
        if(SPUD_AARCH64_SUPPORT)
            target_link_libraries(spud.windows_unwind PRIVATE capstone)
        endif()
        add_test(NAME spud.windows_unwind COMMAND spud.windows_unwind)
    endif()
    if(APPLE AND SPUD_UNWIND_SUPPORT)
        add_executable(spud.macos_unwind tests/detour/audit/macos_unwind.cc)
        target_include_directories(spud.macos_unwind PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(spud.macos_unwind PRIVATE spud)
        if(SPUD_AARCH64_SUPPORT)
            target_link_libraries(spud.macos_unwind PRIVATE capstone)
        endif()
        add_test(NAME spud.macos_unwind COMMAND spud.macos_unwind)
        if(ARCH STREQUAL "x86_64")
            add_executable(spud.macos_unwind_fde
                tests/detour/audit/macos_unwind.cc
                tests/detour/audit/macos_fde.cc)
            target_include_directories(spud.macos_unwind_fde PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
            target_link_libraries(spud.macos_unwind_fde PRIVATE spud)
            if(SPUD_AARCH64_SUPPORT)
                target_link_libraries(spud.macos_unwind_fde PRIVATE capstone)
            endif()
            add_test(NAME spud.macos_unwind_fde COMMAND spud.macos_unwind_fde)
        endif()
        add_executable(spud.macos_compact tests/detour/audit/macos_compact.cc)
        target_include_directories(spud.macos_compact PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(spud.macos_compact PRIVATE spud Zydis)
        if(SPUD_AARCH64_SUPPORT)
            target_link_libraries(spud.macos_compact PRIVATE capstone)
        endif()
        add_test(NAME spud.macos_compact COMMAND spud.macos_compact)
        if(ARCH STREQUAL "x86_64")
            add_executable(spud.macos_handler_backedge
                tests/detour/audit/macos_unwind.cc)
            target_compile_definitions(spud.macos_handler_backedge PRIVATE
                SPUD_UNWIND_HANDLER_BACKEDGE=1)
            target_include_directories(spud.macos_handler_backedge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
            target_link_libraries(spud.macos_handler_backedge PRIVATE spud)
            if(SPUD_AARCH64_SUPPORT)
                target_link_libraries(spud.macos_handler_backedge PRIVATE capstone)
            endif()
            add_test(NAME spud.macos_handler_backedge
                COMMAND spud.macos_handler_backedge)
        endif()
    endif()

    # SPUD_AARCH64_SUPPORT is private to the library, but these programs
    # include src/ headers whose layouts depend on it.
    if(SPUD_AARCH64_SUPPORT)
        foreach(_target spud.test spud.window_allocations spud.relocation_probe
                spud.spill_unwind spud.windows_unwind spud.macos_unwind
                spud.macos_unwind_fde spud.macos_compact
                spud.macos_handler_backedge)
            if(TARGET ${_target})
                target_compile_definitions(${_target} PRIVATE SPUD_AARCH64_SUPPORT=1)
            endif()
        endforeach()
    endif()

    if(MSVC)
        set(_spud_syntax_only /nologo /std:c++20 /Zs)
        set(_spud_header_warnings "")
        set(_spud_include_flag /I)
        set(_spud_define_flag /D)
    else()
        set(_spud_syntax_only -std=c++20 -fsyntax-only)
        # Consumers may build with -Wundef; the headers test macros with
        # defined() where arch.h only defines them when true.
        set(_spud_header_warnings -Wall -Wextra -Wundef -Werror)
        set(_spud_include_flag -I)
        set(_spud_define_flag -D)
    endif()

    add_test(
        NAME spud.tracing_header
        COMMAND
            ${CMAKE_CXX_COMPILER} ${_spud_syntax_only}
            "${_spud_define_flag}SPUD_DETOUR_TRACING=1"
            "${_spud_include_flag}${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/install/tracing_consumer.cc"
    )
    add_test(
        NAME spud.tracing_header_order
        COMMAND
            ${CMAKE_CXX_COMPILER} ${_spud_syntax_only}
            "${_spud_define_flag}SPUD_DETOUR_TRACING=1"
            "${_spud_include_flag}${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/install/tracing_consumer_order.cc"
    )

    add_test(
        NAME spud.signature_literal
        COMMAND
            ${CMAKE_CXX_COMPILER} ${_spud_syntax_only}
            "${_spud_include_flag}${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/install/signature_literal.cc"
    )
    add_test(
        NAME spud.signature_literal_rejects
        COMMAND
            ${CMAKE_CXX_COMPILER} ${_spud_syntax_only}
            "${_spud_define_flag}SPUD_BAD_PATTERN=1"
            "${_spud_include_flag}${CMAKE_CURRENT_SOURCE_DIR}/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/install/signature_literal.cc"
    )
    # Rejected for the right reason: sig() names the bad token in its error.
set_tests_properties(spud.signature_literal_rejects PROPERTIES
    PASS_REGULAR_EXPRESSION "signature_has_an_invalid_token")

    if(SPUD_INSTALL)
        set(_spud_install_check "${CMAKE_CURRENT_BINARY_DIR}/install-check")
        add_test(
            NAME spud.install
            COMMAND
                ${CMAKE_COMMAND} --install ${CMAKE_CURRENT_BINARY_DIR} --prefix
                ${_spud_install_check}
        )
        add_test(
            NAME spud.installed_header
            COMMAND
                ${CMAKE_CXX_COMPILER} ${_spud_syntax_only} ${_spud_header_warnings}
                "${_spud_include_flag}${_spud_install_check}/include"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/install/consumer.cc"
        )
        add_test(
            NAME spud.installed_tracing_header
            COMMAND
                ${CMAKE_CXX_COMPILER} ${_spud_syntax_only}
                "${_spud_define_flag}SPUD_DETOUR_TRACING=1"
                "${_spud_include_flag}${_spud_install_check}/include"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/install/tracing_consumer.cc"
        )
        set_tests_properties(
            spud.installed_header spud.installed_tracing_header
            PROPERTIES DEPENDS spud.install
        )
        add_test(
            NAME spud.installed_link
            COMMAND
                ${CMAKE_COMMAND}
                -DCMAKE_COMMAND=${CMAKE_COMMAND}
                -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
                "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
                -DSPUD_PREFIX=${_spud_install_check}
                -DCONSUMER_SRC=${CMAKE_CURRENT_SOURCE_DIR}/tests/install/link_consumer
                -DCONSUMER_BIN=${CMAKE_CURRENT_BINARY_DIR}/link-consumer-build
                -DBUILD_TYPE=$<CONFIG>
                -DGENERATOR=${CMAKE_GENERATOR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/install/run_link_consumer.cmake
        )
        set_tests_properties(
            spud.installed_link
            PROPERTIES DEPENDS spud.install
        )
    endif()

    # A consumer built with the other SPUD_DETOUR_TRACING setting must fail
    # to link against this libspud, on the handle types the setting moves
    # into an inline namespace, rather than run with the wrong layout.
    if(NOT MSVC)
        if(SPUD_DETOUR_TRACING)
            set(_spud_other_tracing 0)
        else()
            set(_spud_other_tracing 1)
        endif()
        set(_spud_mismatch_libs $<TARGET_FILE:spud> $<TARGET_FILE:Zydis> $<TARGET_FILE:Zycore>)
        if(NOT APPLE)
            list(APPEND _spud_mismatch_libs -pthread)
        endif()
        add_test(
            NAME spud.tracing_mismatch
            COMMAND
                ${CMAKE_CXX_COMPILER} -std=c++20
                -DSPUD_DETOUR_TRACING=${_spud_other_tracing}
                "-I${CMAKE_CURRENT_SOURCE_DIR}/include"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/install/link_consumer/main.cc"
                ${_spud_mismatch_libs}
                -o "${CMAKE_CURRENT_BINARY_DIR}/tracing-mismatch"
        )
        # GNU ld and lld print demangled names, ld64 may print mangled ones.
        set_tests_properties(spud.tracing_mismatch PROPERTIES
            PASS_REGULAR_EXPRESSION "detail::(tracing::)?detour::|detail(7tracing)?6detour")
    endif()
endif()

if(SPUD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
