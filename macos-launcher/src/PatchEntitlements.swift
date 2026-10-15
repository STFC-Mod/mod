import Foundation
import Security
import AppKit
import os

/// Logger for entitlement management operations
private let logger = Logger(subsystem: "com.stfcmod.startrekpatch", category: "entitlements")

/// Entitlements required to enable the loader for STFC.
/// The game ships with these from Scopely's Developer ID signature, so normally
/// no re-signing is needed — we just verify they're present.
let loaderEntitlements: [String: Bool] = [
    "com.apple.security.cs.allow-dyld-environment-variables": true,
    "com.apple.security.cs.allow-unsigned-executable-memory": true,
    "com.apple.security.cs.disable-library-validation": true,
]

/// Errors that can occur during entitlement management
enum EntitlementError: Error {
    case gamePathNotFound
    case entitlementVerificationFailed
    case entitlementApplicationFailed

    var localizedDescription: String {
        switch self {
        case .gamePathNotFound:
            return "Could not find the game executable path"
        case .entitlementVerificationFailed:
            return "Failed to verify game entitlements"
        case .entitlementApplicationFailed:
            return "Failed to apply entitlements to the game"
        }
    }
}

/// Ensures the game has the required loader entitlements, applying them only if missing.
/// The game's original Scopely signature already includes the entitlements we need,
/// so this typically just verifies and returns without modifying anything.
/// - Parameter signingIdentity: Optional signing identity (defaults to "-" for ad-hoc signing)
/// - Throws: EntitlementError if unable to find, verify, or apply entitlements
func ensureGameHasLoaderEntitlements(signingIdentity: String = "-") throws {
    // Get the game path
    guard let gamePath = try fetchGamePath() else {
        throw EntitlementError.gamePathNotFound
    }

    // Always verify entitlements first — even after a game update, the new binary
    // typically ships with the entitlements we need from Scopely's signature.
    if checkEntitlements(appPath: gamePath, expectedEntitlements: loaderEntitlements) {
        logger.info("Game already has the required loader entitlements, no re-signing needed")
        return
    }

    // Entitlements are missing — fall back to re-signing.
    // This should be rare; it means the game shipped without the expected entitlements.
    logger.warning("Game is missing required loader entitlements, re-signing is needed")

    // Check if the game is currently running
    let gameAppPath = (gamePath as NSString).deletingLastPathComponent
        .replacingOccurrences(of: "/Contents/MacOS", with: "")
    if isGameRunning(bundlePath: gameAppPath) {
        logger.error("Game is currently running, cannot modify signature")
        throw EntitlementError.entitlementApplicationFailed
    }

    // Request user to grant access to the game bundle via file picker
    // This serves as explicit user consent for the modification and provides security-scoped access
    guard let confirmedURL = requestGameBundleAccess(gameAppPath: gameAppPath, gamePath: gamePath) else {
        logger.error("User did not confirm access to game bundle")
        throw EntitlementError.entitlementApplicationFailed
    }

    // Verify the user selected the correct path
    if confirmedURL.path != gameAppPath {
        logger.warning("User selected different path: \(confirmedURL.path) vs expected \(gameAppPath)")
        // Continue with the user's selection
    }

    // Start accessing the security-scoped resource
    guard confirmedURL.startAccessingSecurityScopedResource() else {
        logger.error("Failed to access security-scoped resource")
        throw EntitlementError.entitlementApplicationFailed
    }
    defer {
        confirmedURL.stopAccessingSecurityScopedResource()
    }

    // Apply the entitlements using the security-scoped access
    // We only sign the main executable, preserving nested framework/plugin signatures
    logger.info("Applying loader entitlements to the game binary...")
    let applySuccess = applyEntitlementsDirectly(
        mainExecutablePath: gamePath,
        entitlements: loaderEntitlements,
        signingIdentity: signingIdentity
    )
    guard applySuccess else {
        throw EntitlementError.entitlementApplicationFailed
    }

    // Verify the entitlements were applied successfully to the main executable
    guard checkEntitlements(appPath: gamePath, expectedEntitlements: loaderEntitlements) else {
        throw EntitlementError.entitlementVerificationFailed
    }

    logger.info("Successfully applied loader entitlements to the game")
}

/// Fetches the path to the Star Trek Fleet Command game executable
/// - Returns: The full path to the game executable, or nil if not found
/// - Throws: Can throw errors related to file reading or parsing
func fetchGamePath() throws -> String? {
    // Get the Library directory path
    guard let libraryPath = FileManager.default.urls(
        for: .libraryDirectory,
        in: .userDomainMask
    ).first else {
        logger.error("Could not find Library directory")
        return nil
    }

    // Construct path to launcher_settings.ini
    let launcherSettingsPath = libraryPath
        .appendingPathComponent("Preferences")
        .appendingPathComponent("Star Trek Fleet Command")
        .appendingPathComponent("launcher_settings.ini")
        .path

    // Check if the file exists
    guard FileManager.default.fileExists(atPath: launcherSettingsPath) else {
        logger.error("launcher_settings.ini not found at \(launcherSettingsPath)")
        return nil
    }

    // Parse the INI file
    let config = try parseConfig(launcherSettingsPath)

    // Get the game install path from the [General] section
    guard let generalSection = config["General"],
          let gameInstallPath = generalSection["152033..GAME_PATH"] else {
        logger.error("Could not find game path in launcher_settings.ini")
        return nil
    }

    // Append the app path
    let appPath = "Star Trek Fleet Command.app/Contents/MacOS/Star Trek Fleet Command"
    let fullGamePath = (gameInstallPath as NSString).appendingPathComponent(appPath)

    return fullGamePath
}

/// Checks if an application's entitlements match the provided dictionary of expected values
/// - Parameters:
///   - appPath: Path to the .app bundle or executable to check
///   - expectedEntitlements: Dictionary of entitlement keys and their expected values
/// - Returns: true if all expected entitlements match, false otherwise
func checkEntitlements(appPath: String, expectedEntitlements: [String: Bool]) -> Bool {
    // Create process to run codesign command
    let process = Process()
    process.executableURL = URL(fileURLWithPath: "/usr/bin/codesign")
    process.arguments = ["-d", "--entitlements", ":-", "--xml", appPath]

    let outputPipe = Pipe()
    let errorPipe = Pipe()
    process.standardOutput = outputPipe
    process.standardError = errorPipe

    do {
        try process.run()
        process.waitUntilExit()

        // Check if codesign succeeded
        guard process.terminationStatus == 0 else {
            logger.error("codesign failed with status \(process.terminationStatus)")
            if let errorData = try? errorPipe.fileHandleForReading.readToEnd(),
               let errorString = String(data: errorData, encoding: .utf8) {
                logger.error("codesign error output: \(errorString)")
            }
            return false
        }

        // Read the output
        let outputData = try outputPipe.fileHandleForReading.readToEnd()
        guard let data = outputData, !data.isEmpty else {
            logger.error("No entitlements data received from codesign")
            return false
        }

        // Parse the plist
        guard let plist = try? PropertyListSerialization.propertyList(from: data, format: nil),
              let entitlementsDict = plist as? [String: Any] else {
            logger.error("Failed to parse entitlements plist")
            return false
        }

        // Compare each expected entitlement
        for (key, expectedValue) in expectedEntitlements {
            guard let actualValue = entitlementsDict[key] else {
                logger.debug("Entitlement '\(key)' not found in app")
                return false
            }

            if (actualValue as? Bool) != expectedValue {
                let expected = String(describing: expectedValue)
                let actual = String(describing: actualValue)
                logger.debug("Entitlement '\(key)' mismatch: expected \(expected), got \(actual)")
                return false
            }
        }

        return true

    } catch {
        logger.error("Error running codesign: \(error.localizedDescription)")
        return false
    }
}

/// Requests user to grant access to the game bundle via file picker
/// - Parameters:
///   - gameAppPath: Path to the game .app bundle
///   - gamePath: Path to the game executable that will be modified
/// - Returns: URL with security-scoped access, or nil if user cancelled
func requestGameBundleAccess(gameAppPath: String, gamePath: String) -> URL? {
    let semaphore = DispatchSemaphore(value: 0)
    var result: URL?

    DispatchQueue.main.async {
        requestGameBundleAccessOnMainThread(gameAppPath: gameAppPath, gamePath: gamePath) { selectedURL in
            result = selectedURL
            semaphore.signal()
        }
    }

    semaphore.wait()
    return result
}

/// Internal function that shows the file picker (must be called on main thread)
/// - Parameters:
///   - gameAppPath: Path to the game .app bundle
///   - gamePath: Path to the game executable that will be modified
///   - completion: Completion handler called with the selected URL or nil if cancelled
private func requestGameBundleAccessOnMainThread(
    gameAppPath: String,
    gamePath: String,
    completion: @escaping (URL?) -> Void
) {
    let openPanel = NSOpenPanel()
    openPanel.message = """
    The STFC Community Mod needs to modify the game's code signature to enable mod loading.

    File to be modified: \(gamePath)

    Changes: Enable DYLD environment variables, allow unsigned code, disable library validation

    This is safe - only the game's signature is modified (reversible via Repair).
    You'll be asked for your password to authorize this change.

    If this is your first time running the launcher, macOS security will ask you to authorize the
    App Management permission for this app. After allowing this, you must quit the launcher and
    start it again for the change to take effect.

    The default selection below should not need to be changed unless we did not detect
    the game path correctly. Press "Authorize and Continue" to grant access to the game bundle.
    """
    openPanel.prompt = "Authorize and Continue"
    openPanel.canChooseFiles = false
    openPanel.canChooseDirectories = true
    openPanel.allowsMultipleSelection = false
    openPanel.canCreateDirectories = false

    // Pre-select the game path if it exists
    let gameURL = URL(fileURLWithPath: gameAppPath)
    if FileManager.default.fileExists(atPath: gameAppPath) {
        openPanel.directoryURL = gameURL.deletingLastPathComponent()
        openPanel.nameFieldStringValue = gameURL.lastPathComponent
    }

    // Use async begin() instead of blocking runModal() to avoid main thread blocking
    openPanel.begin { response in
        if response == .OK, let selectedURL = openPanel.url {
            logger.info("User granted access to: \(selectedURL.path)")
            completion(selectedURL)
        } else {
            logger.info("User cancelled file access dialog")
            completion(nil)
        }
    }
}

/// Applies entitlements to an application using codesign directly (no elevation)
/// - Parameters:
///   - mainExecutablePath: Path to the main executable within the bundle
///   - entitlements: Dictionary of entitlement keys and values to apply
///   - signingIdentity: Optional signing identity (defaults to "-" for ad-hoc signing)
/// - Returns: true if signing succeeded, false otherwise
/// - Note: Must be called while security-scoped resource access is active
func applyEntitlementsDirectly(
    mainExecutablePath: String,
    entitlements: [String: Bool],
    signingIdentity: String = "-"
) -> Bool {
    // Create temporary entitlements plist file
    let tempDir = NSTemporaryDirectory()
    let tempFileName = "temp_entitlements_\(UUID().uuidString).plist"
    let tempEntitlementsPath = (tempDir as NSString).appendingPathComponent(tempFileName)

    do {
        // Serialize the entitlements dictionary to plist format
        let plistData = try PropertyListSerialization.data(
            fromPropertyList: entitlements,
            format: .xml,
            options: 0
        )

        // Write the plist to the temporary file
        try plistData.write(to: URL(fileURLWithPath: tempEntitlementsPath))

        // Ensure cleanup happens even if signing fails
        defer {
            try? FileManager.default.removeItem(atPath: tempEntitlementsPath)
        }

        // Execute codesign directly (no elevation needed - user owns these files)
        let success = executeCodesignDirectly(
            mainExecutablePath: mainExecutablePath,
            entitlementsPath: tempEntitlementsPath,
            signingIdentity: signingIdentity
        )

        if success {
            logger.info("Successfully applied entitlements to \(mainExecutablePath)")
        } else {
            logger.error("Failed to apply entitlements")
        }

        return success

    } catch {
        logger.error("Error applying entitlements: \(error.localizedDescription)")
        // Clean up temp file if it exists
        try? FileManager.default.removeItem(atPath: tempEntitlementsPath)
        return false
    }
}

/// Executes codesign directly without elevation
/// - Parameters:
///   - mainExecutablePath: Path to the main executable within the bundle
///   - entitlementsPath: Path to temporary entitlements plist
///   - signingIdentity: Code signing identity
/// - Returns: true if successful, false otherwise
/// - Note: Must be called while security-scoped resource access is active
func executeCodesignDirectly(
    mainExecutablePath: String,
    entitlementsPath: String,
    signingIdentity: String
) -> Bool {
    // Strategy: Sign just the main executable with entitlements
    // We preserve existing signatures on nested frameworks/plugins

    // First, try to remove the old signature from the main executable only
    logger.info("Removing old signature from: \(mainExecutablePath)")
    let removeProcess = Process()
    removeProcess.executableURL = URL(fileURLWithPath: "/usr/bin/codesign")
    removeProcess.arguments = ["--remove-signature", mainExecutablePath]

    let removeOutputPipe = Pipe()
    let removeErrorPipe = Pipe()
    removeProcess.standardOutput = removeOutputPipe
    removeProcess.standardError = removeErrorPipe

    try? removeProcess.run()
    removeProcess.waitUntilExit()

    // Log output from removal (informational, errors are expected if no signature)
    if let outputData = try? removeOutputPipe.fileHandleForReading.readToEnd(),
       !outputData.isEmpty,
       let outputString = String(data: outputData, encoding: .utf8), !outputString.isEmpty {
        logger.info("Remove signature stdout: \(outputString)")
    }
    if let errorData = try? removeErrorPipe.fileHandleForReading.readToEnd(),
       !errorData.isEmpty,
       let errorString = String(data: errorData, encoding: .utf8), !errorString.isEmpty {
        logger.debug("Remove signature stderr: \(errorString)")
    }

    // Now sign just the main executable with the entitlements
    // This preserves nested code signatures and only modifies what we need
    logger.info("Signing executable with entitlements...")
    logger.info("""
        Command: codesign --force --sign \(signingIdentity) \
        --options runtime \
        --entitlements \(entitlementsPath) \(mainExecutablePath)
        """)
    let signProcess = Process()
    signProcess.executableURL = URL(fileURLWithPath: "/usr/bin/codesign")
    signProcess.arguments = [
        "--force",
        "--options", "runtime",
        "--sign", signingIdentity,
        "--entitlements", entitlementsPath,
        mainExecutablePath
    ]

    let outputPipe = Pipe()
    let errorPipe = Pipe()
    signProcess.standardOutput = outputPipe
    signProcess.standardError = errorPipe

    do {
        try signProcess.run()
        signProcess.waitUntilExit()

        // Always log stdout if present
        if let outputData = try? outputPipe.fileHandleForReading.readToEnd(),
           !outputData.isEmpty,
           let outputString = String(data: outputData, encoding: .utf8), !outputString.isEmpty {
            logger.info("codesign stdout: \(outputString)")
        }

        // Always log stderr if present
        if let errorData = try? errorPipe.fileHandleForReading.readToEnd(),
           !errorData.isEmpty,
           let errorString = String(data: errorData, encoding: .utf8), !errorString.isEmpty {
            if signProcess.terminationStatus != 0 {
                logger.error("codesign stderr: \(errorString)")
            } else {
                logger.info("codesign stderr: \(errorString)")
            }
        }

        if signProcess.terminationStatus != 0 {
            logger.error("codesign failed with exit code: \(signProcess.terminationStatus)")
            return false
        }

        logger.info("Successfully signed main executable")
        return true

    } catch {
        logger.error("Error running codesign: \(error.localizedDescription)")
        return false
    }
}

/// Checks if the game is currently running
/// - Parameter bundlePath: Path to the .app bundle
/// - Returns: true if the game is running, false otherwise
func isGameRunning(bundlePath: String) -> Bool {
    let runningApps = NSWorkspace.shared.runningApplications
    let bundleURL = URL(fileURLWithPath: bundlePath)

    for app in runningApps {
        if let appBundleURL = app.bundleURL, appBundleURL == bundleURL {
            logger.info("Game is currently running at \(bundlePath)")
            return true
        }
    }

    return false
}
