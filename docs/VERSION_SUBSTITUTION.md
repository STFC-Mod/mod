# Version Substitution in macOS Launcher

## Overview

The macOS launcher's `Info.plist` file now automatically uses the version defined in `src/mod/version.h` during the build process. This ensures consistency between the application version and the mod version.

## Implementation

### Files Involved

- **Template**: `src/launcher/macos/Info.plist.template` - Contains version placeholders
- **Generated**: `src/launcher/macos/Info.plist` - Generated during build (not committed to git)
- **Source**: `src/mod/version.h` - Contains version definitions
- **Build**: `src/launcher/macos/xmake.lua` - Contains version extraction and substitution logic

### Version Extraction

The build process extracts version components from `src/mod/version.h`:

```c
#define VERSION_MAJOR     0
#define VERSION_MINOR     6  
#define VERSION_REVISION  1
#define VERSION_PATCH     7
```

These are combined into a version string like `0.6.1.7`.

### Template Processing

The `Info.plist.template` uses `${VERSION}` placeholders:

```xml
<key>CFBundleShortVersionString</key>
<string>${VERSION}</string>
<key>CFBundleVersion</key>
<string>${VERSION}</string>
```

During build, these placeholders are replaced with the actual version.

## Build Process

1. **Configuration Phase**: `on_config` function in xmake.lua executes
2. **Version Extraction**: Reads and parses `src/mod/version.h`
3. **Template Processing**: Replaces `${VERSION}` in template with extracted version
4. **File Generation**: Writes processed `Info.plist` to source directory
5. **Build**: Xcode uses the generated `Info.plist` for app bundle creation
6. **Cleanup**: Generated file is removed after clean operation

## Verification

To test the implementation without a full build:

```bash
python3 scripts/verify_version_substitution.py
```

This script simulates the build process and verifies:
- Version extraction from header file
- Template processing
- XML validity of generated Info.plist
- Correct version substitution

## Maintenance

### Updating Version

Version updates are made in `src/mod/version.h` only. The macOS launcher will automatically pick up changes during the next build.

### Modifying Info.plist

Changes to the Info.plist should be made to `Info.plist.template`, not the generated `Info.plist` file.

### Troubleshooting

If version substitution fails:

1. Check that `src/mod/version.h` exists and contains expected `VERSION_*` definitions
2. Verify `Info.plist.template` contains `${VERSION}` placeholders
3. Run the verification script to identify specific issues
4. Check xmake build output for error messages

### File Patterns

- `Info.plist.template` - **COMMIT** to git
- `Info.plist` - **DO NOT COMMIT** (generated file, in .gitignore)