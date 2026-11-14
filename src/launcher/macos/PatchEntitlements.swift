// Makes sure the game may load the mod: its executable needs the entitlements below. Scopely's own signature
// normally has them, so this usually only checks. When they are missing, the executable is re-signed ad hoc with
// them, after the user confirms the game bundle in a file panel.

import AppKit
import Foundation
import os

private let logger = Logger(subsystem: "com.stfcmod.startrekpatch", category: "entitlements")

// What DYLD_INSERT_LIBRARIES and the mod's runtime patching need.
private let loaderEntitlements: [String: Bool] = [
  "com.apple.security.cs.allow-dyld-environment-variables": true,
  "com.apple.security.cs.allow-unsigned-executable-memory": true,
  "com.apple.security.cs.disable-library-validation": true,
]

private let gameExecutableInBundle = "Contents/MacOS/Star Trek Fleet Command"

enum EntitlementError: LocalizedError {
  case gamePathNotFound
  case gameRunning
  case accessNotGranted
  case wrongBundleSelected(String)
  case entitlementVerificationFailed
  case entitlementApplicationFailed

  var errorDescription: String? {
    switch self {
    case .gamePathNotFound:
      return "Could not find the game executable path"
    case .gameRunning:
      return "Quit the game before the launcher updates its signature"
    case .accessNotGranted:
      return "The launcher needs access to the game bundle to enable the mod"
    case .wrongBundleSelected(let path):
      return "\(path) is not the Star Trek Fleet Command app"
    case .entitlementVerificationFailed:
      return "Failed to verify game entitlements"
    case .entitlementApplicationFailed:
      return "Failed to apply entitlements to the game"
    }
  }
}

// Checks the game's entitlements and re-signs its executable only when they are missing. Even after a game update
// the new binary normally still carries them. Blocks while the file panel is open, so call it off the main thread.
func ensureGameHasLoaderEntitlements() throws(EntitlementError) {
  guard let installPath = try? XsollaUpdater(gameName: "Star Trek Fleet Command").gamePath() else {
    throw EntitlementError.gamePathNotFound
  }
  let gameAppPath = (installPath as NSString).appendingPathComponent("Star Trek Fleet Command.app")
  let gamePath = (gameAppPath as NSString).appendingPathComponent(gameExecutableInBundle)

  if checkEntitlements(executablePath: gamePath, expected: loaderEntitlements) {
    logger.info("Game already has the required loader entitlements, no re-signing needed")
    return
  }
  logger.warning("Game is missing required loader entitlements, re-signing is needed")

  if isGameRunning(bundlePath: gameAppPath) {
    logger.error("Game is currently running, cannot modify signature")
    throw EntitlementError.gameRunning
  }

  // The panel is the user's consent, and grants the security-scoped access that signing needs.
  guard let confirmedURL = requestGameBundleAccess(gameAppPath: gameAppPath, gamePath: gamePath) else {
    logger.error("User did not confirm access to game bundle")
    throw EntitlementError.accessNotGranted
  }
  // Access is granted to the selected bundle only, so that is the one signed. It must still be the game.
  let selectedExecutable = confirmedURL.appendingPathComponent(gameExecutableInBundle).path
  guard FileManager.default.isExecutableFile(atPath: selectedExecutable) else {
    logger.error("Selected bundle has no game executable: \(confirmedURL.path, privacy: .private)")
    throw EntitlementError.wrongBundleSelected(confirmedURL.path)
  }
  if confirmedURL.standardizedFileURL.path != URL(fileURLWithPath: gameAppPath).standardizedFileURL.path {
    logger.warning(
      "User selected \(confirmedURL.path, privacy: .private) instead of \(gameAppPath, privacy: .private)")
  }

  guard confirmedURL.startAccessingSecurityScopedResource() else {
    logger.error("Failed to access security-scoped resource")
    throw EntitlementError.entitlementApplicationFailed
  }
  defer {
    confirmedURL.stopAccessingSecurityScopedResource()
  }

  // Only the main executable is signed, so nested frameworks and plugins keep their own signatures.
  logger.info("Applying loader entitlements to the game binary")
  guard sign(executablePath: selectedExecutable, entitlements: loaderEntitlements) else {
    throw EntitlementError.entitlementApplicationFailed
  }
  guard checkEntitlements(executablePath: selectedExecutable, expected: loaderEntitlements) else {
    throw EntitlementError.entitlementVerificationFailed
  }
  logger.info("Successfully applied loader entitlements to the game")
}

// Runs a tool and returns its exit status and output; nil when it could not start.
private func run(_ tool: String, _ arguments: [String]) -> (status: Int32, output: Data, errors: String)? {
  let process = Process()
  process.executableURL = URL(fileURLWithPath: tool)
  process.arguments = arguments
  let outputPipe = Pipe()
  let errorPipe = Pipe()
  process.standardOutput = outputPipe
  process.standardError = errorPipe
  do {
    try process.run()
  } catch {
    logger.error("Could not run \(tool, privacy: .public): \(error.localizedDescription, privacy: .public)")
    return nil
  }
  // Read before waiting: a full pipe would otherwise block the tool forever.
  let output = (try? outputPipe.fileHandleForReading.readToEnd()) ?? Data()
  let errors = (try? errorPipe.fileHandleForReading.readToEnd()).flatMap { String(data: $0, encoding: .utf8) } ?? ""
  process.waitUntilExit()
  return (process.terminationStatus, output, errors)
}

// True when the executable's signature has every expected entitlement with its expected value.
private func checkEntitlements(executablePath: String, expected: [String: Bool]) -> Bool {
  guard let result = run("/usr/bin/codesign", ["-d", "--entitlements", ":-", "--xml", executablePath]) else {
    return false
  }
  guard result.status == 0 else {
    logger.error("codesign failed with status \(result.status): \(result.errors, privacy: .public)")
    return false
  }
  guard
    !result.output.isEmpty,
    let plist = try? PropertyListSerialization.propertyList(from: result.output, format: nil),
    let entitlements = plist as? [String: Any]
  else {
    logger.error("Could not read the game's entitlements")
    return false
  }
  for (key, value) in expected where (entitlements[key] as? Bool) != value {
    logger.debug("Entitlement \(key, privacy: .public) is not \(value)")
    return false
  }
  return true
}

// Shows the file panel on the main thread and waits for the answer. Must be called off the main thread.
private func requestGameBundleAccess(gameAppPath: String, gamePath: String) -> URL? {
  let semaphore = DispatchSemaphore(value: 0)
  var result: URL?
  DispatchQueue.main.async {
    let panel = NSOpenPanel()
    panel.message = """
      The STFC Community Mod needs to re-sign the game so that it can load the mod.

      File to be modified: \(gamePath)

      Changes: allow DYLD environment variables, allow unsigned executable memory, and disable library validation.
      Only the game's signature changes. A game update or reinstall restores the original.

      The first time, macOS asks you to allow App Management for this launcher. After allowing it, quit the launcher
      and start it again.

      The selection below should only need changing if the game was not found where expected.
      Press "Authorize and Continue" to grant access to the game bundle.
      """
    panel.prompt = "Authorize and Continue"
    panel.canChooseFiles = false
    panel.canChooseDirectories = true
    panel.allowsMultipleSelection = false
    panel.canCreateDirectories = false
    if FileManager.default.fileExists(atPath: gameAppPath) {
      let gameURL = URL(fileURLWithPath: gameAppPath)
      panel.directoryURL = gameURL.deletingLastPathComponent()
      panel.nameFieldStringValue = gameURL.lastPathComponent
    }
    // begin, not runModal, so the main thread keeps serving the launcher's UI.
    panel.begin { response in
      if response == .OK, let selected = panel.url {
        logger.info("User granted access to: \(selected.path, privacy: .private)")
        result = selected
      } else {
        logger.info("User cancelled file access dialog")
      }
      semaphore.signal()
    }
  }
  semaphore.wait()
  return result
}

// Re-signs one executable ad hoc with `entitlements`. Must run while the bundle's security-scoped access is held.
private func sign(executablePath: String, entitlements: [String: Bool]) -> Bool {
  let entitlementsURL = FileManager.default.temporaryDirectory
    .appendingPathComponent("stfc_entitlements_\(UUID().uuidString).plist")
  defer {
    try? FileManager.default.removeItem(at: entitlementsURL)
  }
  do {
    try PropertyListSerialization.data(fromPropertyList: entitlements, format: .xml, options: 0)
      .write(to: entitlementsURL)
  } catch {
    logger.error("Could not write the entitlements: \(error.localizedDescription, privacy: .public)")
    return false
  }

  // Fails harmlessly when there is no signature to remove.
  if let removed = run("/usr/bin/codesign", ["--remove-signature", executablePath]), !removed.errors.isEmpty {
    logger.debug("Remove signature: \(removed.errors, privacy: .public)")
  }

  let arguments = [
    "--force", "--options", "runtime", "--sign", "-", "--entitlements", entitlementsURL.path, executablePath,
  ]
  logger.info("Running codesign \(arguments.joined(separator: " "), privacy: .private)")
  guard let signed = run("/usr/bin/codesign", arguments) else {
    return false
  }
  guard signed.status == 0 else {
    logger.error("codesign failed with exit code \(signed.status): \(signed.errors, privacy: .public)")
    return false
  }
  logger.info("Signed the game executable")
  return true
}

private func isGameRunning(bundlePath: String) -> Bool {
  let bundleURL = URL(fileURLWithPath: bundlePath).standardizedFileURL
  return NSWorkspace.shared.runningApplications.contains { $0.bundleURL?.standardizedFileURL == bundleURL }
}
