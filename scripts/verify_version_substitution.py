#!/usr/bin/env python3
"""
Verification script for Info.plist version substitution implementation.
This script simulates the xmake build process and verifies that version
substitution works correctly.
"""

import os
import sys
import re
import xml.etree.ElementTree as ET

def get_version_from_header():
    """Extract version from version.h (mimics the Lua function in xmake.lua)"""
    version_file = "src/mod/version.h"
    
    if not os.path.isfile(version_file):
        return "1.0.0.0"
    
    with open(version_file, 'r') as f:
        content = f.read()
    
    major = re.search(r'#define VERSION_MAJOR\s+(\d+)', content)
    minor = re.search(r'#define VERSION_MINOR\s+(\d+)', content)
    revision = re.search(r'#define VERSION_REVISION\s+(\d+)', content)
    patch = re.search(r'#define VERSION_PATCH\s+(\d+)', content)
    
    if all([major, minor, revision, patch]):
        return f"{major.group(1)}.{minor.group(1)}.{revision.group(1)}.{patch.group(1)}"
    
    return "1.0.0.0"

def simulate_build_process():
    """Simulate the xmake on_config process"""
    print("=== Simulating xmake build process ===")
    
    # Step 1: Extract version from version.h
    version = get_version_from_header()
    print(f"1. Extracted version: {version}")
    
    # Step 2: Check template file exists
    template_file = "src/launcher/macos/Info.plist.template"
    if not os.path.isfile(template_file):
        print(f"❌ ERROR: Template file not found: {template_file}")
        return False
    
    print(f"2. Found template file: {template_file}")
    
    # Step 3: Process template
    with open(template_file, 'r') as f:
        template_content = f.read()
    
    placeholders = template_content.count("${VERSION}")
    print(f"3. Found {placeholders} version placeholders in template")
    
    processed_content = template_content.replace("${VERSION}", version)
    
    # Step 4: Write generated file
    output_file = "src/launcher/macos/Info.plist"
    with open(output_file, 'w') as f:
        f.write(processed_content)
    
    print(f"4. Generated Info.plist with version {version}")
    
    # Step 5: Verify result
    remaining = processed_content.count("${VERSION}")
    if remaining > 0:
        print(f"❌ ERROR: {remaining} placeholders remain unsubstituted")
        return False
    
    # Step 6: Validate XML
    try:
        tree = ET.parse(output_file)
        print("5. ✅ Generated Info.plist is valid XML")
    except ET.ParseError as e:
        print(f"❌ ERROR: Invalid XML: {e}")
        return False
    
    # Step 7: Verify version in final output
    root = tree.getroot()
    dict_elem = root.find('dict')
    
    keys = []
    values = []
    for child in dict_elem:
        if child.tag == 'key':
            keys.append(child.text)
        elif child.tag == 'string':
            values.append(child.text)
    
    key_value_pairs = dict(zip(keys, values))
    
    version_fields = ['CFBundleShortVersionString', 'CFBundleVersion']
    for field in version_fields:
        if field in key_value_pairs:
            if key_value_pairs[field] == version:
                print(f"6. ✅ {field}: {key_value_pairs[field]}")
            else:
                print(f"❌ ERROR: {field} has wrong value: {key_value_pairs[field]} (expected {version})")
                return False
        else:
            print(f"❌ ERROR: {field} not found in Info.plist")
            return False
    
    print("✅ SUCCESS: All checks passed!")
    return True

def cleanup():
    """Clean up generated files"""
    output_file = "src/launcher/macos/Info.plist"
    if os.path.isfile(output_file):
        os.remove(output_file)
        print(f"Cleaned up generated file: {output_file}")

def main():
    """Main function"""
    print("Info.plist Version Substitution Verification")
    print("=" * 50)
    
    # Change to repository root if not already there
    if os.path.basename(os.getcwd()) != "stfc-mod":
        # Try to find the repository root
        for parent in ['.', '..', '../..']:
            if os.path.isfile(os.path.join(parent, 'xmake.lua')) and os.path.isdir(os.path.join(parent, 'mods')):
                os.chdir(parent)
                break
        else:
            print("❌ ERROR: Cannot find repository root. Please run from stfc-mod directory.")
            return False
    
    print(f"Working directory: {os.getcwd()}")
    
    try:
        success = simulate_build_process()
        return success
    finally:
        cleanup()

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)