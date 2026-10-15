import Foundation
import os

/// Logger for Xsolla operations
private let logger = Logger(subsystem: "com.stfcmod.startrekpatch", category: "xsolla")

struct DownloadAction {
  var url: String
  var size: Int
  var to: String
}

struct ExtractAction {
  var file: String
  var to: String
}

struct PatchAction {
  var binaries: String
  var patch: String
}

struct VersionAction {
  var version: Int
}

enum XsollaUpdateAction {
  case Download(DownloadAction)
  case Extract(ExtractAction)
  case Patch(PatchAction)
  case Version(VersionAction)
  case WaitActions
}

private func updateActionLogName(_ action: XsollaUpdateAction) -> String {
  switch action {
  case .Download:
    return "download"
  case .Extract:
    return "extract"
  case .Patch:
    return "patch"
  case .Version:
    return "version"
  case .WaitActions:
    return "wait"
  }
}

struct PatchRule: Decodable {
  var file_size: Optional<Int>
  var relative_path: String
  var rule: String
  var sha512: Optional<String>
}

func normalizedRelativePatchPath(_ path: String) throws -> String {
  var components: [String] = []

  for component in path.trimmingCharacters(in: .whitespacesAndNewlines).split(whereSeparator: { $0 == "/" || $0 == "\\" }) {
    switch component {
    case "", ".", "..":
      throw NSError(domain: "XsollaUpdater", code: 4, userInfo: [NSLocalizedDescriptionKey: "Invalid patch path: \(path)"])
    default:
      components.append(String(component))
    }
  }

  if components.isEmpty {
    throw NSError(domain: "XsollaUpdater", code: 4, userInfo: [NSLocalizedDescriptionKey: "Invalid patch path: \(path)"])
  }

  return components.joined(separator: "/")
}

func stagedPatchURL(root: URL, relativePath: String) throws -> URL {
  let rootURL = root.standardizedFileURL
  let targetURL = rootURL.appendingPathComponent(relativePath).standardizedFileURL
  let rootPath = rootURL.path.hasSuffix("/") ? rootURL.path : rootURL.path + "/"

  if targetURL.path != rootURL.path && targetURL.path.hasPrefix(rootPath) {
    return targetURL
  }

  throw NSError(domain: "XsollaUpdater", code: 4, userInfo: [NSLocalizedDescriptionKey: "Patch path escapes staging directory: \(relativePath)"])
}

class XsollaUpdateParser: NSObject, XMLParserDelegate {
  var gameVersion = 0

  var actions: [XsollaUpdateAction] = []

  func parser(
    _ parser: XMLParser,
    didStartElement elementName: String,
    namespaceURI: String?,
    qualifiedName qName: String?,
    attributes attributeDict: [String: String] = [:]
  ) {
    if elementName == "action" {
      switch attributeDict["type"] {
      case "torrent_download":
        actions.append(
          XsollaUpdateAction.Download(
            DownloadAction(
              url: attributeDict["alt_data_link"]!,
              size: Int(attributeDict["data_size"]!)!,
              to: attributeDict["alt_to"]!)))
        break
      case "extract":
        actions.append(
          XsollaUpdateAction.Extract(
            ExtractAction(
              file: attributeDict["file"]!,
              to: attributeDict["to"]!)))
        break
      case "patch":
        actions.append(
          XsollaUpdateAction.Patch(
            PatchAction(binaries: attributeDict["binaries"]!, patch: attributeDict["patch"]!)))
        break
      case "wait_actions":
        actions.append(XsollaUpdateAction.WaitActions)
        break
      case "extracted_size":
        break
      case "version":
        gameVersion = Int(attributeDict["version"]!)!
        actions.append(XsollaUpdateAction.Version(VersionAction(version: gameVersion)))
        break
      default:
        logger.warning("Unknown action type: \(attributeDict["type"]!)")
        break
      }
    }
  }
}

enum XsollaUpdateProgress {
  case Start(totalActions: Int)
  case Progress(currentAction: Int, totalActions: Int)
  case Extracting(currentFile: String)
  case ExtractComplete(currentFile: String)
  case Downloading(url: String)
  case DownloadComplete(url: String)
  case Patching(totalFiles: Int)
  case PatchingProgress(currentBytes: Int, totalBytes: Int)
  case PatchStepComplete
  case PatchComplete
  case Waiting
  case ApplyVersion
  case VersionApplied
  case Finalizing
  case CleaningUp
  case Complete
}

protocol XSollaUpdaterDelegate {
  func updateProgress(progress: XsollaUpdateProgress)
}

private func httpStatus(_ response: URLResponse) -> Int {
  return (response as? HTTPURLResponse)?.statusCode ?? -1
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

struct XsollaUpdater {
  var gameName: String

  public init(_ gameName: String) {
    self.gameName = gameName
  }

  func gamePath() throws -> String {
    let library = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask).first
    if let library {
      let preferences = library.appendingPathComponent("Preferences").appendingPathComponent(
        self.gameName)
      let settingsIniPath = preferences.appendingPathComponent("launcher_settings.ini")
      let settingsIni = try parseConfig(settingsIniPath.path)
      let gamePath = settingsIni["General"]?["152033..GAME_PATH"]
      if let gamePath {
        if gamePath.starts(with: "//") {
          return String(gamePath.dropFirst())
        }
        return gamePath
      }
    }
    throw NSError(domain: "XsollaUpdater", code: 1, userInfo: nil)
  }

  func gameTempPath() throws -> String {
    let library = FileManager.default.urls(for: .libraryDirectory, in: .userDomainMask).first
    if let library {
      let preferences = library.appendingPathComponent("Preferences").appendingPathComponent(
        self.gameName)
      let settingsIniPath = preferences.appendingPathComponent("launcher_settings.ini")
      let settingsIni = try parseConfig(settingsIniPath.path)
      let gameTempPath = settingsIni["General"]?["152033..GAME_TEMP_PATH"]
      if let gameTempPath {
        if gameTempPath.starts(with: "//") {
          return String(gameTempPath.dropFirst())
        }
        return gameTempPath
      }
    }
    throw NSError(domain: "XsollaUpdater", code: 1, userInfo: nil)
  }

  @MainActor
  func installedVersion() -> Int {
    do {
      var gamePath = URL(fileURLWithPath: try self.gamePath())
      gamePath.appendPathComponent(".version")
      let versionFile = try String(contentsOf: gamePath, encoding: String.Encoding.utf8)
      let versionSegments = versionFile.split(separator: "=")
      return Int(versionSegments[1]) ?? 0
    } catch {
      return 0
    }
  }

  private func latestGameVersion(installedVersion: Int) async throws -> Int {
    let url = URL(
      string: String(
        format:
          "https://gus.xsolla.com/updates?version=%d&project_id=152033&region=&platform=mac_os",
        installedVersion))
    if let url {
      logger.info("Checking latest game version from installed version \(installedVersion, privacy: .public)")
      let (data, response) = try await URLSession.shared.data(from: url)
      logger.debug(
        "Xsolla latest-version response status=\(httpStatus(response), privacy: .public) bytes=\(data.count, privacy: .public)")

      let xml = XMLParser(data: data)
      let parser = XsollaUpdateParser()
      xml.delegate = parser
      guard xml.parse() else {
        let message = xml.parserError?.localizedDescription ?? "unknown XML parser error"
        logger.error("Failed to parse Xsolla latest-version XML: \(message, privacy: .public)")
        throw NSError(domain: "XsollaUpdater", code: 5, userInfo: [NSLocalizedDescriptionKey: message])
      }
      logger.info("Latest game version parsed as \(parser.gameVersion, privacy: .public)")
      return parser.gameVersion
    }
    throw NSError(domain: "XsollaUpdater", code: 2, userInfo: nil)
  }

  @MainActor
  func checkForUpdateAvailable() async -> Bool {
    do {
      let installedVersion = self.installedVersion()
      let latestGameVersion = try await self.latestGameVersion(installedVersion: installedVersion)
      let available = installedVersion < latestGameVersion
      logger.info(
        "Game update availability installed=\(installedVersion, privacy: .public) latest=\(latestGameVersion, privacy: .public) available=\(available, privacy: .public)")
      return available
    } catch {
      logger.error("Game update availability check failed: \(error.localizedDescription, privacy: .public)")
      return false
    }
  }

  @MainActor
  func updateGame(delegate: XSollaUpdaterDelegate? = nil) async throws {
    let installedVersion = self.installedVersion()
    logger.info("Starting game update from installed version \(installedVersion, privacy: .public)")

    do {
      guard
        let url = URL(
          string: String(
            format:
              "https://gus.xsolla.com/updates?version=%d&project_id=152033&region=&platform=mac_os",
            installedVersion))
      else {
        throw NSError(domain: "XsollaUpdater", code: 3, userInfo: nil)
      }

      let (data, response) = try await URLSession.shared.data(from: url)
      logger.info(
        "Fetched Xsolla update plan status=\(httpStatus(response), privacy: .public) bytes=\(data.count, privacy: .public)")

      let xml = XMLParser(data: data)
      let parser = XsollaUpdateParser()
      xml.delegate = parser
      guard xml.parse() else {
        let message = xml.parserError?.localizedDescription ?? "unknown XML parser error"
        logger.error("Failed to parse Xsolla update XML: \(message, privacy: .public)")
        throw NSError(domain: "XsollaUpdater", code: 5, userInfo: [NSLocalizedDescriptionKey: message])
      }
      logger.info(
        "Parsed Xsolla update plan targetVersion=\(parser.gameVersion, privacy: .public) actions=\(parser.actions.count, privacy: .public)")

      var tempGamePath = try self.gameTempPath()
      if tempGamePath.hasSuffix("/") || tempGamePath.hasSuffix("\\") {
        tempGamePath = String(tempGamePath.dropLast())
      }
      var gamePath = try self.gamePath()
      if gamePath.hasSuffix("/") || gamePath.hasSuffix("\\") {
        gamePath = String(gamePath.dropLast())
      }
      let tempPath = TemporaryFolderURL()

      if FileManager.default.fileExists(atPath: tempGamePath) {
        logger.info("Removing previous Xsolla temp path \(tempGamePath, privacy: .private)")
        try FileManager.default.removeItem(atPath: tempGamePath)
      }
      try FileManager.default.createDirectory(
        atPath: tempGamePath, withIntermediateDirectories: true, attributes: nil)
      try FileManager.default.createDirectory(
        at: tempPath.contentURL, withIntermediateDirectories: true, attributes: nil)
      logger.info(
        "Prepared updater directories gamePath=\(gamePath, privacy: .private) xsollaTemp=\(tempGamePath, privacy: .private) staging=\(tempPath.contentURL.path, privacy: .private)")

      delegate?.updateProgress(
        progress: XsollaUpdateProgress.Start(totalActions: parser.actions.count))

      var currentAction = 0
      var pendingGameVersion: Int?
      var pendingDeletes: [String] = []

      for action in parser.actions {
        currentAction += 1
        let actionName = updateActionLogName(action)
        logger.info(
          "Starting Xsolla action \(currentAction, privacy: .public)/\(parser.actions.count, privacy: .public): \(actionName, privacy: .public)")
        delegate?.updateProgress(
          progress: XsollaUpdateProgress.Progress(
            currentAction: currentAction, totalActions: parser.actions.count))

        switch action {
        case .Download(let downloadAction):
          delegate?.updateProgress(
            progress: XsollaUpdateProgress.Downloading(url: downloadAction.url))
          guard let downloadURL = URL(string: downloadAction.url) else {
            throw NSError(domain: "XsollaUpdater", code: 3, userInfo: nil)
          }
          logger.info(
            "Downloading update payload expectedBytes=\(downloadAction.size, privacy: .public) url=\(downloadAction.url, privacy: .private)")
          let (localURL, response) = try await URLSession.shared.download(from: downloadURL)
          let toPath = downloadAction.to.replacingOccurrences(of: "$temp_path", with: tempGamePath)
          let downloadedBytes = fileSize(at: localURL)
          logger.info(
            "Downloaded update payload status=\(httpStatus(response), privacy: .public) bytes=\(downloadedBytes, privacy: .public) target=\(toPath, privacy: .private)")
          delegate?.updateProgress(
            progress: XsollaUpdateProgress.DownloadComplete(url: downloadAction.url))
          try FileManager.default.moveItem(at: localURL, to: URL(fileURLWithPath: toPath))

        case .Extract(let extractAction):
          let fromPath = extractAction.file.replacingOccurrences(of: "$temp_path", with: tempGamePath)
          let toPath = extractAction.to.replacingOccurrences(of: "$temp_path", with: tempGamePath)
          logger.info(
            "Extracting update archive from=\(fromPath, privacy: .private) to=\(toPath, privacy: .private)")
          delegate?.updateProgress(progress: XsollaUpdateProgress.Extracting(currentFile: fromPath))
          try extractUpdateArchive(from: fromPath, to: toPath)
          delegate?.updateProgress(
            progress: XsollaUpdateProgress.ExtractComplete(currentFile: fromPath))
          logger.info("Extracted update archive to=\(toPath, privacy: .private)")

        case .Patch(let patchAction):
          var binaries = patchAction.binaries
          binaries = binaries.replacingOccurrences(of: "$temp_path", with: tempGamePath)
          binaries = binaries.replacingOccurrences(of: "$game_path", with: gamePath)
          let patch = patchAction.patch.replacingOccurrences(of: "$temp_path", with: tempGamePath)
          let patchRulesJSON = URL(fileURLWithPath: patch).appendingPathComponent("patchRules.json")
          let patchRules = try JSONDecoder().decode(
            [PatchRule].self, from: Data(contentsOf: patchRulesJSON))
          logger.info(
            "Applying Xsolla patch rules count=\(patchRules.count, privacy: .public) binaries=\(binaries, privacy: .private) patch=\(patch, privacy: .private)")
          delegate?.updateProgress(
            progress: XsollaUpdateProgress.Patching(totalFiles: patchRules.count))

          for rule in patchRules {
            let relativePath = try normalizedRelativePatchPath(rule.relative_path)

            // Skip files in _CodeSignature directory as they will be regenerated when we re-sign.
            if relativePath.contains("_CodeSignature") {
              logger.info("Skipping _CodeSignature patch rule for \(relativePath, privacy: .public)")
              delegate?.updateProgress(progress: XsollaUpdateProgress.PatchStepComplete)
              continue
            }

            let targetPath = try stagedPatchURL(root: tempPath.contentURL, relativePath: relativePath)
            let sourcePath = URL(fileURLWithPath: gamePath).appendingPathComponent(relativePath)
            let patchPath = URL(fileURLWithPath: patch).appendingPathComponent(relativePath)

            logger.debug(
              "Patch rule type=\(rule.rule, privacy: .public) relativePath=\(relativePath, privacy: .public) sourceExists=\(FileManager.default.fileExists(atPath: sourcePath.path), privacy: .public) stagedExists=\(FileManager.default.fileExists(atPath: targetPath.path), privacy: .public) patchExists=\(FileManager.default.fileExists(atPath: patchPath.path), privacy: .public)")

            switch rule.rule {
            case "patch":
              let basisPath =
                FileManager.default.fileExists(atPath: targetPath.path) ? targetPath : sourcePath
              let patchedPath = targetPath.appendingPathExtension("patching")
              do {
                try FileManager.default.createDirectory(
                  at: targetPath.deletingLastPathComponent(), withIntermediateDirectories: true,
                  attributes: nil)
                if FileManager.default.fileExists(atPath: patchedPath.path) {
                  try FileManager.default.removeItem(at: patchedPath)
                }
                logger.debug(
                  "Applying rsync patch relativePath=\(relativePath, privacy: .public) basis=\(basisPath.path, privacy: .private) output=\(patchedPath.path, privacy: .private)")
                try rsyncApply(source: basisPath, patch: patchPath, output: patchedPath)
                let basisAttributes = try FileManager.default.attributesOfItem(atPath: basisPath.path)
                if let permissions = basisAttributes[.posixPermissions] {
                  try FileManager.default.setAttributes(
                    [.posixPermissions: permissions], ofItemAtPath: patchedPath.path)
                }
                if FileManager.default.fileExists(atPath: targetPath.path) {
                  try FileManager.default.removeItem(at: targetPath)
                }
                try FileManager.default.moveItem(at: patchedPath, to: targetPath)
              } catch {
                try? FileManager.default.removeItem(at: patchedPath)
                logger.error(
                  "Error patching \(relativePath, privacy: .public): \(error.localizedDescription, privacy: .public)")
                throw error
              }

            case "create":
              if !FileManager.default.fileExists(atPath: targetPath.path) {
                try FileManager.default.createDirectory(
                  at: targetPath.deletingLastPathComponent(), withIntermediateDirectories: true,
                  attributes: nil)
                try Data().write(to: targetPath)
              }

            case "delete":
              pendingDeletes.append(relativePath)
              logger.debug("Queued deferred delete for \(relativePath, privacy: .public)")

            case "copy":
              try FileManager.default.createDirectory(
                at: targetPath.deletingLastPathComponent(), withIntermediateDirectories: true,
                attributes: nil)
              if FileManager.default.fileExists(atPath: targetPath.path) {
                try FileManager.default.removeItem(at: targetPath)
              }
              try FileManager.default.copyItem(at: patchPath, to: targetPath)

            default:
              logger.warning("Unknown patch rule \(rule.rule, privacy: .public)")
            }
            delegate?.updateProgress(progress: XsollaUpdateProgress.PatchStepComplete)
          }
          delegate?.updateProgress(progress: XsollaUpdateProgress.PatchComplete)

        case .WaitActions:
          delegate?.updateProgress(progress: XsollaUpdateProgress.Waiting)
          logger.debug("Observed Xsolla wait action")

        case .Version(let versionAction):
          pendingGameVersion = versionAction.version
          logger.info("Deferred installed version write until finalization: \(versionAction.version, privacy: .public)")
        }

        logger.info(
          "Finished Xsolla action \(currentAction, privacy: .public)/\(parser.actions.count, privacy: .public): \(actionName, privacy: .public)")
      }

      delegate?.updateProgress(progress: XsollaUpdateProgress.Finalizing)
      // `tempPath` deletes its backing staging folder in TemporaryFolderURL.deinit. Both the
      // finalize copy and the deferred deletes read from that folder, so keep `tempPath`
      // explicitly alive across them with withExtendedLifetime rather than relying on ARC's
      // last-use lifetime, which could otherwise release (and asynchronously delete) the
      // staging folder out from under these operations.
      try withExtendedLifetime(tempPath) {
        // NOTE: copying into the live game directory is not atomic — staged files replace the
        // installed ones one at a time, so a failure partway through leaves the install in a
        // mixed old/new state. Staging keeps all download/extract/patch work out of the live
        // directory until this point, and the version file is only written after this copy
        // succeeds, but recovering from a mid-copy failure still requires re-running the update.
        logger.info("Copying staged update files into the game directory")
        try copyContentsOfDirectory(from: tempPath.contentURL, to: URL(fileURLWithPath: gamePath))
        logger.info("Copied staged update files into the game directory")

        if !pendingDeletes.isEmpty {
          logger.info("Applying deferred delete rules count=\(pendingDeletes.count, privacy: .public)")
          try deleteGamePaths(
            pendingDeletes, gamePath: gamePath, preservingStagedRoot: tempPath.contentURL)
        }
      }

      if let pendingGameVersion {
        delegate?.updateProgress(progress: XsollaUpdateProgress.ApplyVersion)
        logger.info(
          "Writing installed game version \(pendingGameVersion, privacy: .public) after final file operations")
        try writeInstalledGameVersion(pendingGameVersion, gamePath: gamePath)
        delegate?.updateProgress(progress: XsollaUpdateProgress.VersionApplied)
      } else {
        logger.warning("Update plan did not include a version action; leaving installed version unchanged")
      }

      delegate?.updateProgress(progress: XsollaUpdateProgress.CleaningUp)
      do {
        logger.info("Removing Xsolla temp path \(tempGamePath, privacy: .private)")
        try FileManager.default.removeItem(atPath: tempGamePath)
      } catch {
        logger.warning("Failed to remove Xsolla temp path: \(error.localizedDescription, privacy: .public)")
      }

      delegate?.updateProgress(progress: XsollaUpdateProgress.Complete)
      logger.info(
        "Game update completed from \(installedVersion, privacy: .public) to \(pendingGameVersion ?? parser.gameVersion, privacy: .public)")
    } catch {
      logger.error(
        "Game update failed from installed version \(installedVersion, privacy: .public): \(error.localizedDescription, privacy: .public)")
      throw error
    }
  }
}
