// Extracts the 7z fixtures with the launcher's extractor, and checks that it rejects corrupt and missing archives.
// Run with the fixtures folder as the only argument.

import Foundation

// The fixtures were generated independently with 7zz a -t7z -m0=METHOD -ms=on, so no archive encoder is needed here.
@main struct ArchiveTests {
  static func check(_ ok: Bool, _ message: String) throws {
    guard ok else { throw NSError(domain: "ArchiveTests", code: 1, userInfo: [NSLocalizedDescriptionKey: message]) }
  }

  static func main() throws {
    let files = FileManager.default
    let fixtures = URL(fileURLWithPath: CommandLine.arguments[1])
    let temporary = files.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try files.createDirectory(at: temporary, withIntermediateDirectories: true)
    defer { try? files.removeItem(at: temporary) }

    for method in ["LZMA", "LZMA2", "PPMd"] {
      let destination = temporary.appendingPathComponent("extracted \(method)")
      try extractUpdateArchive(from: fixtures.appendingPathComponent("\(method).7z").path, to: destination.path)
      for (name, expected) in [
        "dir/hello.txt": String(repeating: "Archive fixture\n", count: 1000),
        "empty": "", "héllo.txt": "unicode\n", "run.sh": "#!/bin/sh\necho archive\n",
      ] {
        let data = try Data(contentsOf: destination.appendingPathComponent(name))
        try check(data == Data(expected.utf8), "\(method): incorrect contents for \(name)")
      }
    }

    let corrupt = temporary.appendingPathComponent("corrupt.7z")
    try Data([0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c]).write(to: corrupt)
    for archive in [corrupt, temporary.appendingPathComponent("missing.7z")] {
      var failed = false
      do {
        try extractUpdateArchive(from: archive.path, to: temporary.appendingPathComponent("bad output").path)
      } catch {
        failed = true
      }
      try check(failed, "Extraction must reject \(archive.lastPathComponent)")
    }
    print("Launcher archive fixtures passed")
  }
}
