// The updater's file work. It depends on nothing else in the launcher, so the archive tests compile it alone.

import Foundation
import OSLog

private let logger = Logger(subsystem: "com.stfcmod.startrekpatch", category: "xsolla")

extension FileManager {
  func removeItemIfPresent(at url: URL) throws {
    if fileExists(atPath: url.path) {
      try removeItem(at: url)
    }
  }

  func createParentDirectory(of url: URL) throws {
    try createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
  }
}

// Merges sourceURL into targetURL, replacing files that exist in both.
func copyContentsOfDirectory(from sourceURL: URL, to targetURL: URL) throws {
  let files = FileManager.default
  try files.createDirectory(at: targetURL, withIntermediateDirectories: true)
  let sourceContents = try files.contentsOfDirectory(
    at: sourceURL, includingPropertiesForKeys: [.isSymbolicLinkKey, .isDirectoryKey])

  for sourceItem in sourceContents {
    let destinationItem = targetURL.appendingPathComponent(sourceItem.lastPathComponent)
    do {
      let resourceValues = try sourceItem.resourceValues(forKeys: [.isSymbolicLinkKey, .isDirectoryKey])
      // isDirectoryKey resolves symlinks, so a symlinked directory must be ruled out before recursing. copyItem
      // copies a symlink as a link rather than following it.
      if resourceValues.isDirectory == true && resourceValues.isSymbolicLink != true {
        // The recursive call creates destinationItem itself.
        try copyContentsOfDirectory(from: sourceItem, to: destinationItem)
      } else {
        try files.removeItemIfPresent(at: destinationItem)
        try files.copyItem(at: sourceItem, to: destinationItem)
      }
    } catch {
      logger.error(
        "Error copying \(sourceItem.lastPathComponent, privacy: .public) to \(destinationItem.path, privacy: .private): \(error.localizedDescription, privacy: .public)"
      )
      throw error
    }
  }
}

// Deletes game files the update removed, except those it also staged a replacement for.
func deleteGamePaths(_ relativePaths: [String], gamePath: String, preservingStagedRoot stagedRoot: URL) throws {
  let files = FileManager.default
  let gameRoot = URL(fileURLWithPath: gamePath)

  for relativePath in relativePaths {
    if files.fileExists(atPath: stagedRoot.appendingPathComponent(relativePath).path) {
      logger.info("Skipping delete for \(relativePath, privacy: .public) because a staged replacement exists")
      continue
    }

    let targetPath = gameRoot.appendingPathComponent(relativePath)
    guard files.fileExists(atPath: targetPath.path) else {
      logger.debug("Skipping delete for missing file \(relativePath, privacy: .public)")
      continue
    }

    do {
      try files.removeItem(at: targetPath)
      logger.debug("Deleted obsolete game file \(relativePath, privacy: .public)")
    } catch {
      logger.error(
        "Error deleting obsolete game file \(relativePath, privacy: .public): \(error.localizedDescription, privacy: .public)"
      )
      throw error
    }
  }
}

// The same "&game=<n>" form the Xsolla launcher writes.
func writeInstalledGameVersion(_ version: Int, gamePath: String) throws {
  let versionPath = URL(fileURLWithPath: gamePath).appendingPathComponent(".version")
  try "&game=\(version)".write(to: versionPath, atomically: true, encoding: .utf8)
}

// Apple's bsdtar reads the update service's 7z payloads without a bundled archive runtime.
func extractUpdateArchive(from archive: String, to directory: String) throws {
  try FileManager.default.createDirectory(atPath: directory, withIntermediateDirectories: true)
  let extractor = Process()
  extractor.executableURL = URL(fileURLWithPath: "/usr/bin/tar")
  extractor.arguments = ["-xf", archive, "-C", directory, "--no-same-owner"]
  try extractor.run()
  extractor.waitUntilExit()
  guard extractor.terminationReason == .exit && extractor.terminationStatus == 0 else {
    throw NSError(
      domain: "XsollaUpdater", code: Int(extractor.terminationStatus),
      userInfo: [NSLocalizedDescriptionKey: "Could not extract update archive \(archive)."])
  }
}
