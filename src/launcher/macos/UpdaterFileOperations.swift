import Foundation
import OSLog

private let fileOperationLogger = Logger(subsystem: "com.stfcmod.startrekpatch", category: "xsolla")

func copyContentsOfDirectory(from sourceURL: URL, to targetURL: URL) throws {
  let fileManager = FileManager.default

  try fileManager.createDirectory(at: targetURL, withIntermediateDirectories: true)
  let sourceContents = try fileManager.contentsOfDirectory(
    at: sourceURL, includingPropertiesForKeys: [.isSymbolicLinkKey, .isDirectoryKey])

  for sourceItem in sourceContents {
    let destinationItem = targetURL.appendingPathComponent(sourceItem.lastPathComponent)

    do {
      let resourceValues = try sourceItem.resourceValues(
        forKeys: [.isSymbolicLinkKey, .isDirectoryKey])
      if resourceValues.isSymbolicLink ?? false {
        // Copy symlinks as links; do not follow them to their target. Check isSymbolicLink
        // before isDirectory because isDirectoryKey resolves the link and would otherwise
        // recurse into a symlinked directory instead of preserving the link.
        if fileManager.fileExists(atPath: destinationItem.path) {
          try fileManager.removeItem(at: destinationItem)
        }
        try fileManager.copyItem(at: sourceItem, to: destinationItem)
      } else if resourceValues.isDirectory ?? false {
        // The recursive call creates destinationItem itself, so don't create it here.
        try copyContentsOfDirectory(from: sourceItem, to: destinationItem)
      } else {
        if fileManager.fileExists(atPath: destinationItem.path) {
          try fileManager.removeItem(at: destinationItem)
        }
        try fileManager.copyItem(at: sourceItem, to: destinationItem)
      }
    } catch {
      fileOperationLogger.error(
        "Error copying \(sourceItem.lastPathComponent, privacy: .public) to \(destinationItem.path, privacy: .private): \(error.localizedDescription, privacy: .public)")
      throw error
    }
  }
}

func deleteGamePaths(_ relativePaths: [String], gamePath: String, preservingStagedRoot stagedRoot: URL) throws {
  let fileManager = FileManager.default
  let gameRoot = URL(fileURLWithPath: gamePath)

  for relativePath in relativePaths {
    let stagedPath = stagedRoot.appendingPathComponent(relativePath)
    if fileManager.fileExists(atPath: stagedPath.path) {
      fileOperationLogger.info(
        "Skipping delete for \(relativePath, privacy: .public) because a staged replacement exists")
      continue
    }

    let targetPath = gameRoot.appendingPathComponent(relativePath)
    if !fileManager.fileExists(atPath: targetPath.path) {
      fileOperationLogger.debug("Skipping delete for missing file \(relativePath, privacy: .public)")
      continue
    }

    do {
      try fileManager.removeItem(at: targetPath)
      fileOperationLogger.debug("Deleted obsolete game file \(relativePath, privacy: .public)")
    } catch {
      fileOperationLogger.error(
        "Error deleting obsolete game file \(relativePath, privacy: .public): \(error.localizedDescription, privacy: .public)")
      throw error
    }
  }
}

func writeInstalledGameVersion(_ version: Int, gamePath: String) throws {
  let versionPath = URL(fileURLWithPath: gamePath).appendingPathComponent(".version")
  let fileVersion = String(format: "&game=%d", version)
  try fileVersion.write(to: versionPath, atomically: true, encoding: .utf8)
}
