// Updates the game the way the Xsolla launcher does: reads its settings, fetches the update plan for the installed
// version, then downloads, extracts and patches into a staging folder before copying over the install.

import Foundation
import os

private let logger = Logger(subsystem: "com.stfcmod.startrekpatch", category: "xsolla")

// Xsolla's id for Star Trek Fleet Command, in its settings keys and update URLs.
private let projectID = "152033"

enum XsollaError: LocalizedError {
  case missingLauncherSetting(String)
  case invalidUpdateURL(String)
  case invalidUpdatePlan(String)
  case invalidPatchPath(String)

  var errorDescription: String? {
    switch self {
    case .missingLauncherSetting(let key):
      return "The Xsolla launcher settings have no \(key). Install the game with the Xsolla launcher first."
    case .invalidUpdateURL(let url):
      return "The update service returned an invalid URL: \(url)"
    case .invalidUpdatePlan(let reason):
      return "The update plan could not be read: \(reason)"
    case .invalidPatchPath(let path):
      return "The update contains an unsafe file path: \(path)"
    }
  }
}

// What an update is doing, for the launcher's status line.
enum XsollaUpdateProgress {
  case start
  case progress(currentAction: Int, totalActions: Int)
  case downloading
  case extracting
  case patching
  case waiting
  case applyVersion
  case versionApplied
  case finalizing
  case cleaningUp
  case complete
}

// --- Update plan ---

// One step of an update plan. Paths may hold $temp_path (the Xsolla temp folder) and $game_path.
private enum UpdateAction {
  case download(url: String, size: Int, to: String)
  case extract(file: String, to: String)
  case patch(binaries: String, patch: String)
  case version(Int)
  case waitActions

  var logName: String {
    switch self {
    case .download: return "download"
    case .extract: return "extract"
    case .patch: return "patch"
    case .version: return "version"
    case .waitActions: return "wait"
    }
  }
}

private struct UpdatePlan {
  // The version the plan ends at; 0 when it has no version action.
  var gameVersion = 0
  var actions: [UpdateAction] = []
}

// Reads the update plan. An action missing an attribute it needs fails the whole plan: applying part of a plan
// could leave the install between versions.
private final class UpdatePlanReader: NSObject, XMLParserDelegate {
  private var plan = UpdatePlan()
  private var error: XsollaError?

  static func read(_ data: Data) throws -> UpdatePlan {
    let xml = XMLParser(data: data)
    let reader = UpdatePlanReader()
    xml.delegate = reader
    let parsed = xml.parse()
    if let error = reader.error {
      logger.error("Rejected the Xsolla update plan: \(error.localizedDescription, privacy: .public)")
      throw error
    }
    guard parsed else {
      let message = xml.parserError?.localizedDescription ?? "unknown XML parser error"
      logger.error("Failed to parse the Xsolla update plan: \(message, privacy: .public)")
      throw XsollaError.invalidUpdatePlan(message)
    }
    return reader.plan
  }

  func parser(
    _ parser: XMLParser,
    didStartElement elementName: String,
    namespaceURI: String?,
    qualifiedName qName: String?,
    attributes attributeDict: [String: String] = [:]
  ) {
    guard elementName == "action", error == nil else { return }
    let type = attributeDict["type"] ?? ""

    func fail(_ reason: String) {
      error = .invalidUpdatePlan(reason)
      parser.abortParsing()
    }
    func text(_ key: String) -> String? {
      guard let value = attributeDict[key] else {
        fail("\(type) action has no \(key)")
        return nil
      }
      return value
    }
    func number(_ key: String) -> Int? {
      guard let value = text(key) else { return nil }
      guard let number = Int(value) else {
        fail("\(type) action has a non-numeric \(key): \(value)")
        return nil
      }
      return number
    }

    switch type {
    case "torrent_download":
      if let url = text("alt_data_link"), let size = number("data_size"), let to = text("alt_to") {
        plan.actions.append(.download(url: url, size: size, to: to))
      }
    case "extract":
      if let file = text("file"), let to = text("to") {
        plan.actions.append(.extract(file: file, to: to))
      }
    case "patch":
      if let binaries = text("binaries"), let patch = text("patch") {
        plan.actions.append(.patch(binaries: binaries, patch: patch))
      }
    case "wait_actions":
      plan.actions.append(.waitActions)
    case "extracted_size":
      break
    case "version":
      if let version = number("version") {
        plan.gameVersion = version
        plan.actions.append(.version(version))
      }
    default:
      logger.warning("Unknown action type: \(type, privacy: .public)")
    }
  }
}

// --- Patch rules ---

// One entry of a patch folder's patchRules.json.
private struct PatchRule: Decodable {
  var relativePath: String
  var rule: String

  enum CodingKeys: String, CodingKey {
    case relativePath = "relative_path"
    case rule
  }
}

// A patch rule's path, normalized to forward slashes. Rejects anything that could climb out of the staging folder.
// Empty components are dropped, so "a//b" is "a/b".
private func normalizedRelativePatchPath(_ path: String) throws -> String {
  let components = path.trimmingCharacters(in: .whitespacesAndNewlines).split { $0 == "/" || $0 == "\\" }
  guard !components.isEmpty, !components.contains(where: { $0 == "." || $0 == ".." }) else {
    throw XsollaError.invalidPatchPath(path)
  }
  return components.joined(separator: "/")
}

// The rule's file in the staging folder. A second guard after normalization: the result must lie strictly inside root.
private func stagedPatchURL(root: URL, relativePath: String) throws -> URL {
  let rootURL = root.standardizedFileURL
  let targetURL = rootURL.appendingPathComponent(relativePath).standardizedFileURL
  let rootPath = rootURL.path.hasSuffix("/") ? rootURL.path : rootURL.path + "/"
  guard targetURL.path != rootURL.path, targetURL.path.hasPrefix(rootPath) else {
    throw XsollaError.invalidPatchPath(relativePath)
  }
  return targetURL
}

// --- Updater ---

private func httpStatus(_ response: URLResponse) -> Int {
  (response as? HTTPURLResponse)?.statusCode ?? -1
}

private func fileSize(at url: URL) -> Int {
  guard
    let attributes = try? FileManager.default.attributesOfItem(atPath: url.path),
    let size = attributes[.size] as? NSNumber
  else {
    return -1
  }
  return size.intValue
}

private func withoutTrailingSeparator(_ path: String) -> String {
  path.hasSuffix("/") || path.hasSuffix("\\") ? String(path.dropLast()) : path
}

struct XsollaUpdater {
  // The game's folder under ~/Library/Preferences, where the Xsolla launcher keeps launcher_settings.ini.
  let gameName: String

  // A path from the Xsolla launcher's settings. The launcher sometimes writes them with a doubled leading slash;
  // the mod's loader strips it the same way.
  private func launcherSetting(_ name: String) throws -> String {
    let key = "\(projectID)..\(name)"
    guard let library = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask).first else {
      throw XsollaError.missingLauncherSetting(key)
    }
    let settings = library.appendingPathComponent("Preferences").appendingPathComponent(gameName)
      .appendingPathComponent("launcher_settings.ini")
    guard let value = try readIni(atPath: settings.path)["General"]?[key] else {
      throw XsollaError.missingLauncherSetting(key)
    }
    return value.hasPrefix("//") ? String(value.dropFirst()) : value
  }

  func gamePath() throws -> String {
    try launcherSetting("GAME_PATH")
  }

  func gameTempPath() throws -> String {
    try launcherSetting("GAME_TEMP_PATH")
  }

  // The installed version from the game's .version file ("&game=<n>"); 0 when it cannot be read.
  func installedVersion() -> Int {
    guard
      let gamePath = try? self.gamePath(),
      let versionFile = try? String(
        contentsOf: URL(fileURLWithPath: gamePath).appendingPathComponent(".version"), encoding: .utf8)
    else {
      return 0
    }
    let segments = versionFile.split(separator: "=")
    guard segments.count == 2 else { return 0 }
    return Int(segments[1].trimmingCharacters(in: .whitespacesAndNewlines)) ?? 0
  }

  // The plan that takes `installedVersion` to the latest version.
  private func fetchUpdatePlan(from installedVersion: Int) async throws -> UpdatePlan {
    let address =
      "https://gus.xsolla.com/updates?version=\(installedVersion)&project_id=\(projectID)&region=&platform=mac_os"
    guard let url = URL(string: address) else {
      throw XsollaError.invalidUpdateURL(address)
    }
    let (data, response) = try await URLSession.shared.data(from: url)
    logger.info(
      "Fetched Xsolla update plan status=\(httpStatus(response), privacy: .public) bytes=\(data.count, privacy: .public)"
    )
    let plan = try UpdatePlanReader.read(data)
    logger.info(
      "Parsed Xsolla update plan targetVersion=\(plan.gameVersion, privacy: .public) actions=\(plan.actions.count, privacy: .public)"
    )
    return plan
  }

  @MainActor
  func checkForUpdateAvailable() async -> Bool {
    do {
      let installedVersion = self.installedVersion()
      let latestGameVersion = try await fetchUpdatePlan(from: installedVersion).gameVersion
      let available = installedVersion < latestGameVersion
      logger.info(
        "Game update availability installed=\(installedVersion, privacy: .public) latest=\(latestGameVersion, privacy: .public) available=\(available, privacy: .public)"
      )
      return available
    } catch {
      logger.error("Game update availability check failed: \(error.localizedDescription, privacy: .public)")
      return false
    }
  }

  // The update runs on the main actor, so `progress` can update the UI directly. Its file work blocks the UI while
  // it runs; only the network waits suspend.
  @MainActor
  func updateGame(onProgress progress: @MainActor (XsollaUpdateProgress) -> Void) async throws {
    let installedVersion = self.installedVersion()
    logger.info("Starting game update from installed version \(installedVersion, privacy: .public)")
    do {
      try await runUpdate(from: installedVersion, onProgress: progress)
    } catch {
      logger.error(
        "Game update failed from installed version \(installedVersion, privacy: .public): \(error.localizedDescription, privacy: .public)"
      )
      throw error
    }
  }

  @MainActor
  private func runUpdate(from installedVersion: Int, onProgress progress: @MainActor (XsollaUpdateProgress) -> Void)
    async throws
  {
    let files = FileManager.default
    let plan = try await fetchUpdatePlan(from: installedVersion)
    let tempGamePath = withoutTrailingSeparator(try gameTempPath())
    let gamePath = withoutTrailingSeparator(try self.gamePath())
    // Patched and copied files wait here until every action has succeeded. It can hold much of the game, so it is
    // removed in the background once the update ends, whether or not it succeeded.
    let staging = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent(UUID().uuidString)
    defer {
      DispatchQueue.global(qos: .utility).async {
        try? FileManager.default.removeItem(at: staging)
      }
    }

    if files.fileExists(atPath: tempGamePath) {
      logger.info("Removing previous Xsolla temp path \(tempGamePath, privacy: .private)")
      try files.removeItem(atPath: tempGamePath)
    }
    try files.createDirectory(atPath: tempGamePath, withIntermediateDirectories: true)
    try files.createDirectory(at: staging, withIntermediateDirectories: true)
    logger.info(
      "Prepared updater directories gamePath=\(gamePath, privacy: .private) xsollaTemp=\(tempGamePath, privacy: .private) staging=\(staging.path, privacy: .private)"
    )

    progress(.start)

    func inTemp(_ path: String) -> String {
      path.replacingOccurrences(of: "$temp_path", with: tempGamePath)
    }

    var pendingGameVersion: Int?
    var pendingDeletes: [String] = []
    for (index, action) in plan.actions.enumerated() {
      let currentAction = index + 1
      logger.info(
        "Starting Xsolla action \(currentAction, privacy: .public)/\(plan.actions.count, privacy: .public): \(action.logName, privacy: .public)"
      )
      progress(.progress(currentAction: currentAction, totalActions: plan.actions.count))

      switch action {
      case .download(let url, let size, let to):
        progress(.downloading)
        guard let downloadURL = URL(string: url) else {
          throw XsollaError.invalidUpdateURL(url)
        }
        logger.info(
          "Downloading update payload expectedBytes=\(size, privacy: .public) url=\(url, privacy: .private)")
        let (localURL, response) = try await URLSession.shared.download(from: downloadURL)
        let toPath = inTemp(to)
        logger.info(
          "Downloaded update payload status=\(httpStatus(response), privacy: .public) bytes=\(fileSize(at: localURL), privacy: .public) target=\(toPath, privacy: .private)"
        )
        try files.moveItem(at: localURL, to: URL(fileURLWithPath: toPath))

      case .extract(let file, let to):
        let fromPath = inTemp(file)
        let toPath = inTemp(to)
        logger.info("Extracting update archive from=\(fromPath, privacy: .private) to=\(toPath, privacy: .private)")
        progress(.extracting)
        try extractUpdateArchive(from: fromPath, to: toPath)
        logger.info("Extracted update archive to=\(toPath, privacy: .private)")

      case .patch(let binaries, let patch):
        // binaries only shows up in the log: rules patch from the installed game, or from an earlier staged copy.
        let binariesPath = inTemp(binaries).replacingOccurrences(of: "$game_path", with: gamePath)
        pendingDeletes += try applyPatch(
          folder: inTemp(patch), binaries: binariesPath, gamePath: gamePath, staging: staging, onProgress: progress)

      case .waitActions:
        progress(.waiting)
        logger.debug("Observed Xsolla wait action")

      case .version(let version):
        pendingGameVersion = version
        logger.info("Deferred installed version write until finalization: \(version, privacy: .public)")
      }

      logger.info(
        "Finished Xsolla action \(currentAction, privacy: .public)/\(plan.actions.count, privacy: .public): \(action.logName, privacy: .public)"
      )
    }

    progress(.finalizing)
    // Not atomic: staged files replace installed ones one at a time, so a failure partway leaves the install mixed.
    // The version file is written only after the copy, so the next run repeats the update.
    logger.info("Copying staged update files into the game directory")
    try copyContentsOfDirectory(from: staging, to: URL(fileURLWithPath: gamePath))
    logger.info("Copied staged update files into the game directory")

    if !pendingDeletes.isEmpty {
      logger.info("Applying deferred delete rules count=\(pendingDeletes.count, privacy: .public)")
      try deleteGamePaths(pendingDeletes, gamePath: gamePath, preservingStagedRoot: staging)
    }

    if let pendingGameVersion {
      progress(.applyVersion)
      logger.info("Writing installed game version \(pendingGameVersion, privacy: .public) after final file operations")
      try writeInstalledGameVersion(pendingGameVersion, gamePath: gamePath)
      progress(.versionApplied)
    } else {
      logger.warning("Update plan did not include a version action; leaving installed version unchanged")
    }

    progress(.cleaningUp)
    do {
      logger.info("Removing Xsolla temp path \(tempGamePath, privacy: .private)")
      try files.removeItem(atPath: tempGamePath)
    } catch {
      logger.warning("Failed to remove Xsolla temp path: \(error.localizedDescription, privacy: .public)")
    }

    progress(.complete)
    logger.info(
      "Game update completed from \(installedVersion, privacy: .public) to \(pendingGameVersion ?? plan.gameVersion, privacy: .public)"
    )
  }

  // Stages the results of one patch folder's rules and returns the paths its delete rules name. Those are deleted
  // only after the staged copy, so a failed update never loses files it did not replace.
  @MainActor
  private func applyPatch(
    folder patch: String, binaries: String, gamePath: String, staging: URL,
    onProgress progress: @MainActor (XsollaUpdateProgress) -> Void
  ) throws -> [String] {
    let files = FileManager.default
    let patchRulesJSON = URL(fileURLWithPath: patch).appendingPathComponent("patchRules.json")
    let patchRules = try JSONDecoder().decode([PatchRule].self, from: Data(contentsOf: patchRulesJSON))
    logger.info(
      "Applying Xsolla patch rules count=\(patchRules.count, privacy: .public) binaries=\(binaries, privacy: .private) patch=\(patch, privacy: .private)"
    )
    progress(.patching)

    var deletes: [String] = []
    for rule in patchRules {
      let relativePath = try normalizedRelativePatchPath(rule.relativePath)

      // Signatures are regenerated when the launcher re-signs the game.
      if relativePath.contains("_CodeSignature") {
        logger.info("Skipping _CodeSignature patch rule for \(relativePath, privacy: .public)")
        continue
      }

      let targetPath = try stagedPatchURL(root: staging, relativePath: relativePath)
      let sourcePath = URL(fileURLWithPath: gamePath).appendingPathComponent(relativePath)
      let patchPath = URL(fileURLWithPath: patch).appendingPathComponent(relativePath)

      logger.debug(
        "Patch rule type=\(rule.rule, privacy: .public) relativePath=\(relativePath, privacy: .public) sourceExists=\(files.fileExists(atPath: sourcePath.path), privacy: .public) stagedExists=\(files.fileExists(atPath: targetPath.path), privacy: .public) patchExists=\(files.fileExists(atPath: patchPath.path), privacy: .public)"
      )

      switch rule.rule {
      case "patch":
        // A file patched earlier in this update is patched again from its staged copy.
        let basisPath = files.fileExists(atPath: targetPath.path) ? targetPath : sourcePath
        let patchedPath = targetPath.appendingPathExtension("patching")
        do {
          try files.createParentDirectory(of: targetPath)
          try files.removeItemIfPresent(at: patchedPath)
          logger.debug(
            "Applying rsync patch relativePath=\(relativePath, privacy: .public) basis=\(basisPath.path, privacy: .private) output=\(patchedPath.path, privacy: .private)"
          )
          try rsyncApply(source: basisPath, patch: patchPath, output: patchedPath)
          // librsync writes a fresh file; give it the basis's mode so executables stay executable.
          if let permissions = try files.attributesOfItem(atPath: basisPath.path)[.posixPermissions] {
            try files.setAttributes([.posixPermissions: permissions], ofItemAtPath: patchedPath.path)
          }
          try files.removeItemIfPresent(at: targetPath)
          try files.moveItem(at: patchedPath, to: targetPath)
        } catch {
          try? files.removeItem(at: patchedPath)
          logger.error("Error patching \(relativePath, privacy: .public): \(error.localizedDescription, privacy: .public)")
          throw error
        }

      case "create":
        if !files.fileExists(atPath: targetPath.path) {
          try files.createParentDirectory(of: targetPath)
          try Data().write(to: targetPath)
        }

      case "delete":
        deletes.append(relativePath)
        logger.debug("Queued deferred delete for \(relativePath, privacy: .public)")

      case "copy":
        try files.createParentDirectory(of: targetPath)
        try files.removeItemIfPresent(at: targetPath)
        try files.copyItem(at: patchPath, to: targetPath)

      default:
        logger.warning("Unknown patch rule \(rule.rule, privacy: .public)")
      }
    }
    return deletes
  }
}
