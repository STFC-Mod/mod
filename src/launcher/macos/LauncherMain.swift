//
//  LauncherMain.swift
//  STFC Community Mod Launcher
//
//  Created by tashcan on 3/22/24.
//

import AppKit
import Foundation
import SwiftUI

extension Scene {
  func windowResizabilityContentSize() -> some Scene {
    if #available(macOS 13.0, *) {
      return windowResizability(.contentSize)
    } else {
      return self
    }
  }
}

class AppDelegate: NSObject, NSApplicationDelegate {
  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
    NSApplication.shared.terminate(self)
    return true
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
    .windowResizabilityContentSize()
  }

}
