// A minimal INI reader for the Xsolla launcher's launcher_settings.ini. The mod's C++ loader reads the same file
// with inicpp; only the [General] paths matter to either.

import Foundation

// Keys and values by section name. Keys before the first section header land in "main".
typealias IniSections = [String: [String: String]]

private func trim<S: StringProtocol>(_ text: S) -> String {
  text.trimmingCharacters(in: CharacterSet(charactersIn: " \n\r\t"))
}

func readIni(atPath path: String) throws -> IniSections {
  let contents = try String(contentsOfFile: path)
  var sections = IniSections()
  var section = "main"
  for rawLine in contents.components(separatedBy: "\n") {
    let line = trim(rawLine)
    if line.hasPrefix("[") && line.hasSuffix("]") {
      section = String(line.dropFirst().dropLast())
      continue
    }
    // '#' starts a comment. Lines that are not key=value, with something on both sides, are ignored.
    let pair = line.prefix { $0 != "#" }.split(separator: "=", maxSplits: 1)
    guard pair.count == 2 else { continue }
    sections[section, default: [:]][trim(pair[0])] = trim(pair[1])
  }
  return sections
}
