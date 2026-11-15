import Foundation

struct GitHubUpdater {
  func latestPatchVersion() async -> Int {
    let url = URL(string: "https://api.github.com/repos/STFC-Mod/mod/releases/latest")!
    do {
      let (data, _) = try await URLSession.shared.data(from: url)
      let json = try JSONSerialization.jsonObject(with: data) as! [String: Any]
      let tagName = json["tag_name"] as! String
      let versionSegments = tagName.split(separator: ".")
      return Int(versionSegments[1]) ?? 0
    } catch {
      return 0
    }
  }
}
