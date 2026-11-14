// The launcher's buttons: configure the mod, update the game, and launch it through the mod's loader.

import SwiftUI
import os

private let logger = Logger(subsystem: "com.stfcmod.startrekpatch", category: "launcher")

struct ActionView: View {
  private let gameUpdater = XsollaUpdater(gameName: "Star Trek Fleet Command")
  @State private var gameVersion = 0
  @State private var gameUpdateAvailable = false
  @State private var updating = false
  @State private var updateAction = ""
  @State private var updateSubAction = ""
  @State private var gameInstalled = false
  @State private var gameRunning = false
  @State private var showErrorAlert = false
  @State private var errorMessage = ""

  var body: some View {
    GeometryReader { geo in
      Grid {
        GridRow {
          lcarsButton("Configure Mod", color: .lcarViolet, action: openConfigSite)

          if gameInstalled {
            Group {
              lcarsButton(gameUpdateAvailable ? "Update Game!" : "", color: .lcarTan) {
                guard gameUpdateAvailable, !updating else { return }
                // Set before the task starts, so a second click cannot start a second update.
                updating = true
                Task { await updateGame() }
              }
              lcarsButton("Engage!", color: .lcarOrange) {
                withAnimation { launchGame() }
              }
            }
            .opacity(updating || gameRunning ? 0.5 : 1.0)
            .allowsHitTesting(!updating && !gameRunning)
          } else {
            Text("Game not installed")
              .font(.lcars(size: 40))
              .foregroundStyle(Color.lcarTan)
              .offset(x: -45)
          }
        }
      }
      .frame(width: geo.size.width, height: 150)
      .offset(x: 85, y: 20)
      .task {
        // The update service is polled once a minute while the launcher is open.
        repeat {
          gameVersion = gameUpdater.installedVersion()
          gameInstalled = gameVersion > 0
          gameUpdateAvailable = await gameUpdater.checkForUpdateAvailable()
          try? await Task.sleep(for: .seconds(60))
        } while !Task.isCancelled
      }
      .overlay(alignment: .bottomTrailing) {
        Text("Game Version: \(String(format: "%02d", gameVersion))")
          .font(.lcars(size: 17))
          .foregroundStyle(Color.lcarTan)
          .offset(x: -10)
      }
      .overlay(alignment: .bottomLeading) {
        if updating {
          HStack {
            PulsatingView()
            Text("\(updateAction) (\(updateSubAction))")
              .font(.lcars(size: 17))
              .foregroundStyle(Color.lcarTan)
              .offset(x: -10)
          }
          .offset(x: 115, y: 10)
        }
      }
      .alert("Error", isPresented: $showErrorAlert) {
        Button("OK", role: .cancel) {}
      } message: {
        Text(errorMessage)
      }
    }
  }

  // A rounded LCARS button in `color`. Without text it shows random LCARS-style digits.
  private func lcarsButton(_ text: String, color: Color, action: @escaping @MainActor () -> Void) -> some View {
    Button(action: action) {
      RoundedRectangle(cornerRadius: 20)
        .frame(width: 125, height: 50)
        .overlay(alignment: .bottomTrailing) {
          HStack {
            Spacer()
            Text(text.isEmpty ? "\(randomDigits(4))-\(randomDigits(3))" : text)
              .font(.lcars(size: 17))
              .foregroundStyle(.black)
          }
          .scaleEffect(x: 0.7, anchor: .trailing)
          .padding(.bottom, 5)
          .padding(.trailing, 20)
        }
        .foregroundStyle(color)
    }
    .buttonStyle(.plain)
  }

  private func openConfigSite() {
    guard let configSiteURL = URL(string: "https://modconfig.pages.dev") else { return }
    NSWorkspace.shared.open(configSiteURL)
  }

  private func showProgress(_ progress: XsollaUpdateProgress) {
    switch progress {
    case .start:
      updateAction = "Updating"
      updateSubAction = "Planning"
    case .progress(let currentAction, let totalActions):
      updateAction = "Updating \(currentAction) of \(totalActions)"
    case .extracting:
      updateSubAction = "Extracting"
    case .downloading:
      updateSubAction = "Downloading"
    case .patching:
      updateSubAction = "Patching"
    case .waiting:
      updateSubAction = "Waiting"
    case .applyVersion:
      updateSubAction = "Applying Version"
    case .versionApplied:
      updateSubAction = "Version Applied"
    case .finalizing:
      updateSubAction = "Finalizing"
    case .cleaningUp:
      updateSubAction = "Cleaning Up"
    case .complete:
      updateSubAction = "Complete"
    }
  }

  @MainActor
  private func updateGame() async {
    logger.info("User requested game update")
    updateAction = "Starting"
    updateSubAction = "Planning"
    do {
      try await gameUpdater.updateGame(onProgress: showProgress)
      logger.info("Game update finished successfully")
    } catch {
      logger.error("Error updating game: \(error.localizedDescription, privacy: .public)")
      updateAction = "Update Failed"
      updateSubAction = "Failed"
      showError("Game update failed: \(error.localizedDescription)")
    }
    updating = false
    gameVersion = gameUpdater.installedVersion()
    gameUpdateAvailable = await gameUpdater.checkForUpdateAvailable()
  }

  private func showError(_ message: String) {
    errorMessage = message
    showErrorAlert = true
  }

  // A GCD thread rather than a Task: runGame blocks until the game exits, which must not tie up a Swift concurrency
  // thread, and it waits for the entitlements panel, which needs the main thread free.
  private func launchGame() {
    gameRunning = true
    DispatchQueue.global().async {
      let result = Result { try runGame() }
      DispatchQueue.main.async {
        gameRunning = false
        if case .failure(let error) = result {
          showError(error.localizedDescription)
        }
      }
    }
  }
}

private enum LaunchError: LocalizedError {
  case loaderMissing
  case loaderFailed(Error)

  var errorDescription: String? {
    switch self {
    case .loaderMissing:
      return "The mod loader is missing from the launcher. Reinstall the community mod."
    case .loaderFailed(let error):
      return "Could not start the game: \(error.localizedDescription)"
    }
  }
}

// Runs the game through the mod's loader until it exits. A free function, so it can run off the main actor.
private func runGame() throws {
  do {
    try ensureGameHasLoaderEntitlements()
  } catch {
    logger.error("Error ensuring game entitlements: \(error.localizedDescription, privacy: .public)")
    throw error
  }

  guard let loader = Bundle.main.path(forAuxiliaryExecutable: "stfc-community-mod-loader") else {
    logger.error("The mod loader is missing from the launcher bundle")
    throw LaunchError.loaderMissing
  }
  let process = Process()
  process.executableURL = URL(fileURLWithPath: loader)
  do {
    try process.run()
  } catch {
    logger.error("Could not start the mod loader: \(error.localizedDescription, privacy: .public)")
    throw LaunchError.loaderFailed(error)
  }
  process.waitUntilExit()
}

// The pulsing dot shown while an update runs.
struct PulsatingView: View {
  @State private var animate = false

  var body: some View {
    ZStack {
      Circle().fill(Color.lcarTan.opacity(0.25)).frame(width: 40, height: 40).scaleEffect(animate ? 1 : 0)
      Circle().fill(Color.lcarTan.opacity(0.35)).frame(width: 30, height: 30).scaleEffect(animate ? 1 : 0)
      Circle().fill(Color.lcarTan.opacity(0.45)).frame(width: 15, height: 15).scaleEffect(animate ? 1 : 0)
      Circle().fill(Color.lcarTan).frame(width: 16.25, height: 16.25)
    }
    .onAppear { animate = true }
    .animation(
      animate ? Animation.easeInOut(duration: 1.5).repeatForever(autoreverses: true) : .default,
      value: animate)
  }
}
