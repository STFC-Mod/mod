import Foundation

public final class TemporaryFolderURL {
  public let contentURL: URL
  public init() {
    contentURL = URL(fileURLWithPath: NSTemporaryDirectory())
      .appendingPathComponent(UUID().uuidString)
  }

  deinit {
    DispatchQueue.global(qos: .utility).async { [contentURL = self.contentURL] in
      try? FileManager.default.removeItem(at: contentURL)
    }
  }
}
