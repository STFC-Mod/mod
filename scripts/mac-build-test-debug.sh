#!/bin/bash

# STFC Community Mod - Development Build Script
# This script provides an easy way to configure, build, run, and debug the mod during development

set -e  # Exit on error

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
BUILD_MODE="debug"
ARCH=$(uname -m)  # Use native architecture (arm64 or x86_64)
ACTION="build"
CLEAN=false
VERBOSE=false
ATTACH_DEBUGGER=false
USE_LAUNCHER=false  # Default to using loader for direct mod testing

# Project paths
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/macosx/${ARCH}"

# Print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Print usage information
print_usage() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS] [ACTION]

STFC Community Mod Development Script

ACTIONS:
    build           Build the project (default)
    run             Build and run the application (allows crash dumps)
    debug           Build and run with lldb debugger attached (captures crashes)
    crashlogs       Show recent crash logs for the application
    clean           Clean build artifacts
    rebuild         Clean and build
    config          Configure only (no build)

OPTIONS:
    -m, --mode MODE     Build mode: debug, release, releasedbg, check (default: debug)
    -a, --arch ARCH     Architecture: arm64, x86_64, universal (default: native)
    -l, --launcher      Use launcher instead of loader (default: loader for direct mod testing)
    -c, --clean         Clean before building
    -v, --verbose       Verbose output
    -h, --help          Show this help message

EXAMPLES:
    $(basename "$0")                          # Build in debug mode
    $(basename "$0") -m release build         # Build in release mode
    $(basename "$0") run                      # Build and run with loader (generates crash dumps)
    $(basename "$0") -l run                   # Build and run with launcher (full launcher flow)
    $(basename "$0") debug                    # Build and debug loader with lldb (no crash dumps)
    $(basename "$0") -l debug                 # Build and debug launcher with lldb
    $(basename "$0") crashlogs                # View recent crash logs
    $(basename "$0") -c build                 # Clean build
    $(basename "$0") rebuild                  # Clean and rebuild
    $(basename "$0") -m releasedbg debug      # Build release with debug info and debug

NOTE: Use 'run' action for crash dump generation. The 'debug' action runs under lldb which
      intercepts crashes and prevents system crash reports from being generated.

EOF
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -m|--mode)
            BUILD_MODE="$2"
            shift 2
            ;;
        -a|--arch)
            ARCH="$2"
            shift 2
            ;;
        -l|--launcher)
            USE_LAUNCHER=true
            shift
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        build|run|debug|crashlogs|clean|rebuild|config)
            ACTION="$1"
            shift
            ;;
        *)
            print_error "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

# Validate build mode
case $BUILD_MODE in
    debug|release|releasedbg|check)
        ;;
    *)
        print_error "Invalid build mode: $BUILD_MODE"
        print_error "Valid modes: debug, release, releasedbg, check"
        exit 1
        ;;
esac

# Update build directory based on architecture
BUILD_DIR="${PROJECT_ROOT}/build/macosx/${ARCH}/${BUILD_MODE}"
APP_PATH="${BUILD_DIR}/macOSLauncher.app"
LOADER_PATH="${BUILD_DIR}/stfc-community-mod-loader"
PACKAGED_APP_PATH="${BUILD_DIR}/STFC Community Mod.app"

# Configure the project
configure_project() {
    print_info "Configuring project for ${ARCH} in ${BUILD_MODE} mode..."

    cd "$PROJECT_ROOT"

    local xmake_opts="-y -p macosx -a ${ARCH} -m ${BUILD_MODE} --target_minver=14.6"
    if [[ "$VERBOSE" == true ]]; then
        xmake_opts="${xmake_opts} -v"
    fi

    xmake f ${xmake_opts}

    print_success "Configuration complete"
}

# Clean build artifacts
clean_build() {
    print_info "Cleaning build artifacts..."

    cd "$PROJECT_ROOT"
    xmake clean -a

    print_success "Clean complete"
}

# Build the project
build_project() {
    print_info "Building project for ${ARCH} in ${BUILD_MODE} mode..."

    cd "$PROJECT_ROOT"

    if [[ "$VERBOSE" == true ]]; then
        xmake -v
    else
        xmake
    fi

    print_success "Build complete"

    # Show build output location
    print_info "Build artifacts location: ${BUILD_DIR}"

    # List built files
    if [[ -d "$BUILD_DIR" ]]; then
        print_info "Built files:"
        ls -lh "${BUILD_DIR}" | grep -E '\.(app|dylib)$' || true
    fi

    package_app_bundle
}

# Package the launcher bundle the same way CI does, but keep xmake's
# macOSLauncher.app in place so local incremental builds still work.
package_app_bundle() {
    print_info "Packaging launcher application bundle..."

    if [[ ! -d "$APP_PATH" ]]; then
        print_error "Launcher app not found at: $APP_PATH"
        print_info "Make sure the launcher was built successfully"
        exit 1
    fi

    if [[ ! -f "$LOADER_PATH" ]]; then
        print_error "Loader not found at: $LOADER_PATH"
        print_info "Make sure the loader was built successfully"
        exit 1
    fi

    local dylib_path="${BUILD_DIR}/libstfc-community-mod.dylib"
    if [[ ! -f "$dylib_path" ]]; then
        print_error "Mod library not found at: $dylib_path"
        print_info "Make sure the mod library was built successfully"
        exit 1
    fi

    local icon_path="${PROJECT_ROOT}/assets/launcher.icns"
    if [[ ! -f "$icon_path" ]]; then
        print_error "Launcher icon not found at: $icon_path"
        exit 1
    fi

    local info_plist_path="${PROJECT_ROOT}/macos-launcher/src/Info.plist"
    if [[ ! -f "$info_plist_path" ]]; then
        print_error "Generated Info.plist not found at: $info_plist_path"
        print_info "Run the configure/build step so xmake can generate it from Info.plist.template"
        exit 1
    fi

    rm -rf "$PACKAGED_APP_PATH"
    ditto "$APP_PATH" "$PACKAGED_APP_PATH"

    mkdir -p "${PACKAGED_APP_PATH}/Contents/Resources"
    cp "$LOADER_PATH" "${PACKAGED_APP_PATH}/Contents/stfc-community-mod-loader"
    cp "$dylib_path" "${PACKAGED_APP_PATH}/Contents/libstfc-community-mod.dylib"
    cp "$icon_path" "${PACKAGED_APP_PATH}/Contents/Resources/"
    cp "$info_plist_path" "${PACKAGED_APP_PATH}/Contents/"

    print_info "Code signing packaged application..."
    codesign --force --verify --verbose --deep --sign "-" "$PACKAGED_APP_PATH"
    codesign --verify --deep --strict --verbose=2 "$PACKAGED_APP_PATH"

    print_success "Packaged app prepared at: $PACKAGED_APP_PATH"
}

# Prepare the app bundle for running
prepare_app() {
    if [[ "$USE_LAUNCHER" == true ]]; then
        print_info "Preparing launcher application bundle..."

        # Check if app was built
        if [[ ! -d "$PACKAGED_APP_PATH" ]]; then
            print_warning "Packaged app not found at: $PACKAGED_APP_PATH"
            package_app_bundle
        fi

        if [[ ! -d "$PACKAGED_APP_PATH" ]]; then
            print_error "Launcher app not found at: $PACKAGED_APP_PATH"
            print_info "Make sure the launcher was built successfully"
            exit 1
        fi

        print_success "Launcher prepared at: $PACKAGED_APP_PATH"
    else
        print_info "Preparing loader..."

        # Check if loader was built
        if [[ ! -f "$LOADER_PATH" ]]; then
            print_error "Loader not found at: $LOADER_PATH"
            print_info "Make sure the loader was built successfully"
            exit 1
        fi

        # Check if dylib was built
        local dylib_path="${BUILD_DIR}/libstfc-community-mod.dylib"
        if [[ ! -f "$dylib_path" ]]; then
            print_warning "Mod library not found at: $dylib_path"
            print_warning "The loader may not work correctly without the mod library"
        fi

        print_success "Loader prepared at: $LOADER_PATH"
        print_info "Loader will inject: $dylib_path"
    fi
}

# Run the application
run_app() {
    prepare_app

    if [[ "$USE_LAUNCHER" == true ]]; then
        print_info "Launching application with launcher..."
        print_info "Crash dumps will be generated at: ~/Library/Logs/DiagnosticReports/"
        open "$PACKAGED_APP_PATH"

        print_success "Launcher launched"
        print_info "To view logs, use: log stream --predicate 'process == \"macOSLauncher\"' --level debug"
        print_info "To view crash logs after a crash, run: $(basename "$0") crashlogs"
    else
        print_info "Running loader directly..."
        print_info "The loader will launch the game with the mod injected"
        print_info "Crash dumps will be generated at: ~/Library/Logs/DiagnosticReports/"

        # Run the loader
        "$LOADER_PATH"

        print_success "Loader executed"
        print_info "To view game logs, use: log stream --predicate 'process == \"Star Trek Fleet Command\"' --level debug"
        print_info "To view crash logs after a crash, run: $(basename "$0") crashlogs"
    fi
}

# Show crash logs
show_crash_logs() {
    local crash_dir="${HOME}/Library/Logs/DiagnosticReports"

    print_info "Searching for recent crash logs..."
    echo

    # Look for crash logs from our apps
    local found_crashes=false

    # Search for macOSLauncher crashes
    if compgen -G "${crash_dir}/macOSLauncher*.crash" > /dev/null 2>&1; then
        print_info "Recent macOSLauncher crashes:"
        ls -lht "${crash_dir}"/macOSLauncher*.crash 2>/dev/null | head -5 | while read -r line; do
            echo "  $line"
        done
        echo
        found_crashes=true
    fi

    # Search for loader crashes
    if compgen -G "${crash_dir}/stfc-community-mod-loader*.crash" > /dev/null 2>&1; then
        print_info "Recent stfc-community-mod-loader crashes:"
        ls -lht "${crash_dir}"/stfc-community-mod-loader*.crash 2>/dev/null | head -5 | while read -r line; do
            echo "  $line"
        done
        echo
        found_crashes=true
    fi

    # Search for game crashes
    if compgen -G "${crash_dir}/*Star Trek Fleet Command*.crash" > /dev/null 2>&1; then
        print_info "Recent Star Trek Fleet Command crashes:"
        ls -lht "${crash_dir}"/*"Star Trek Fleet Command"*.crash 2>/dev/null | head -5 | while read -r line; do
            echo "  $line"
        done
        echo
        found_crashes=true
    fi

    if [[ "$found_crashes" == false ]]; then
        print_warning "No crash logs found in ${crash_dir}"
        echo
    else
        # Get the most recent crash file
        local most_recent=$(ls -t "${crash_dir}"/{macOSLauncher,stfc-community-mod-loader,*"Star Trek Fleet Command"}*.crash 2>/dev/null | head -1)

        if [[ -f "$most_recent" ]]; then
            print_info "Most recent crash log: ${most_recent}"
            echo
            read -p "View most recent crash log? [y/N] " -n 1 -r
            echo
            if [[ $REPLY =~ ^[Yy]$ ]]; then
                less "$most_recent"
            fi
        fi
    fi

    print_info "Crash logs location: ${crash_dir}"
    print_info "To view a specific crash: less '${crash_dir}/<crash_file>'"
}

# Debug the application with lldb
debug_app() {
    prepare_app

    local executable
    local exec_name

    if [[ "$USE_LAUNCHER" == true ]]; then
        executable="${PACKAGED_APP_PATH}/Contents/MacOS/macOSLauncher"
        exec_name="Launcher"
    else
        executable="$LOADER_PATH"
        exec_name="Loader"
    fi

    if [[ ! -f "$executable" ]]; then
        print_error "Executable not found at: $executable"
        exit 1
    fi

    print_warning "Running under lldb - system crash reports will NOT be generated"
    print_warning "Use 'run' action instead of 'debug' if you need crash dumps"
    echo
    print_info "Starting debugger for ${exec_name}..."
    print_info "The ${exec_name} will launch under lldb"
    print_info "Use 'continue' or 'c' to start execution"
    print_info "Use 'breakpoint set -n <function_name>' to set breakpoints"

    # Create lldb command file for better debugging experience
    local lldb_script=$(mktemp)
    cat > "$lldb_script" << 'LLDB_EOF'
# Set up better debugging environment
settings set target.process.stop-on-exec false
settings set target.disable-aslr false

# Display more information on crash
settings set stop-disassembly-display never

# Print colored output
settings set use-color true

# Show source when stopping
settings set stop-line-count-before 3
settings set stop-line-count-after 3

# Load the executable
target create "%EXECUTABLE%"

# Set any initial breakpoints here if needed
# breakpoint set -n main
LLDB_EOF

    # Replace placeholders with actual values
    sed -i '' "s|%EXECUTABLE%|${executable}|g" "$lldb_script"
    sed -i '' "s|%EXEC_NAME%|${exec_name}|g" "$lldb_script"

    lldb -s "$lldb_script"

    rm "$lldb_script"
}

# Main execution flow
main() {
    print_info "STFC Community Mod Development Script"

    local exec_type="Loader"
    if [[ "$USE_LAUNCHER" == true ]]; then
        exec_type="Launcher"
    fi

    print_info "Action: ${ACTION}, Mode: ${BUILD_MODE}, Arch: ${ARCH}, Using: ${exec_type}"
    echo

    case $ACTION in
        config)
            configure_project
            ;;
        clean)
            clean_build
            ;;
        rebuild)
            clean_build
            configure_project
            build_project
            ;;
        build)
            if [[ "$CLEAN" == true ]]; then
                clean_build
            fi
            configure_project
            build_project
            ;;
        run)
            if [[ "$CLEAN" == true ]]; then
                clean_build
            fi
            configure_project
            build_project
            run_app
            ;;
        debug)
            if [[ "$CLEAN" == true ]]; then
                clean_build
            fi
            configure_project
            build_project
            debug_app
            ;;
        crashlogs)
            show_crash_logs
            ;;
        *)
            print_error "Unknown action: $ACTION"
            print_usage
            exit 1
            ;;
    esac

    echo
    print_success "All operations completed successfully!"
}

# Run main function
main
