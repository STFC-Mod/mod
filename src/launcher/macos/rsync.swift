// Applies an Xsolla rsync delta through the C++ bridge in rsync.cc.

import Foundation
import librsync

func rsyncApply(source: URL, patch: URL, output: URL) throws {
  let err = rsync_apply(source.path, patch.path, output.path)
  if err != 0 {
    throw NSError(
      domain: "librsync", code: Int(err),
      userInfo: [NSLocalizedDescriptionKey: "Could not apply the game patch (librsync error \(err))"])
  }
}
