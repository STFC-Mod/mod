// The launcher app: one fixed-size window, and quitting when it closes.

import AppKit
import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate {
  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
    true
  }
}

@main
struct STFC_Community_Patch_LauncherApp: App {
  @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate

  var body: some Scene {
    WindowGroup {
      ContentView()
        .frame(width: 600, height: 400)
        .fixedSize()
    }
    .windowResizability(.contentSize)
  }
}
