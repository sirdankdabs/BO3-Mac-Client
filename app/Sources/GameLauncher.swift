import AppKit
import Foundation

/// Finds the Steam copy of Black Ops III, starts it with the client engine
/// and talks to the engine through files in the data directory.
final class GameLauncher: ObservableObject {
    struct EngineStatus: Equatable {
        var state = "offline"
        var message = ""
        var server = ""
        var mode = ""
    }

    @Published var gameAppURL: URL?
    @Published private(set) var status = EngineStatus()
    @Published var alert: String?

    static let steamAppID = "311210"
    static let dataDirectory: URL = {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        let url = base.appendingPathComponent("BO3 Mac Client", isDirectory: true)
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }()

    private var timer: Timer?
    private var gameProcess: Process?
    private var pendingAfterSteam: (() -> Void)?

    init() {
        if let saved = UserDefaults.standard.string(forKey: "gameAppPath"),
           FileManager.default.fileExists(atPath: saved) {
            gameAppURL = URL(fileURLWithPath: saved)
        } else {
            gameAppURL = Self.detectGame()
        }
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in self?.readStatus() }
        readStatus()
    }

    /// The game is running with the engine loaded (or is being started by us).
    var isEngineRunning: Bool { !["offline", "steam"].contains(status.state) }

    // MARK: - Game location

    static func detectGame() -> URL? {
        let steam = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Steam", isDirectory: true)
        var libraries = [steam.path]
        if let vdf = try? String(contentsOf: steam.appendingPathComponent("steamapps/libraryfolders.vdf"), encoding: .utf8) {
            for line in vdf.split(separator: "\n") where line.contains("\"path\"") {
                let parts = line.split(separator: "\"")
                if parts.count >= 4 { libraries.append(String(parts[3])) }
            }
        }
        for library in libraries {
            let app = URL(fileURLWithPath: library)
                .appendingPathComponent("steamapps/common/Call of Duty Black Ops III/CoDBlkOps3.app")
            if FileManager.default.fileExists(atPath: app.appendingPathComponent("Contents/MacOS/CoDBlkOps3_Exe").path) {
                return app
            }
        }
        return nil
    }

    func chooseGame() {
        let panel = NSOpenPanel()
        panel.title = "Locate Call of Duty: Black Ops III"
        panel.message = "Select CoDBlkOps3.app inside your Steam library."
        panel.allowedContentTypes = [.application]
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url {
            guard FileManager.default.fileExists(atPath: url.appendingPathComponent("Contents/MacOS/CoDBlkOps3_Exe").path) else {
                alert = "That doesn't look like the Steam version of Black Ops III (CoDBlkOps3.app)."
                return
            }
            gameAppURL = url
            UserDefaults.standard.set(url.path, forKey: "gameAppPath")
        }
    }

    // MARK: - Engine status

    private func readStatus() {
        let url = Self.dataDirectory.appendingPathComponent("status")
        guard let text = try? String(contentsOf: url, encoding: .utf8) else {
            var next = EngineStatus()
            if gameProcess?.isRunning == true {
                next.state = "starting"
                next.message = "Starting Black Ops III…"
            } else if status.state == "steam" {
                next = status
            }
            if next != status { status = next }
            return
        }
        var values: [String: String] = [:]
        for line in text.split(separator: "\n") {
            if let eq = line.firstIndex(of: "=") { values[String(line[..<eq])] = String(line[line.index(after: eq)...]) }
        }
        let pid = Int32(values["pid"] ?? "") ?? 0
        let time = TimeInterval(values["time"] ?? "") ?? 0
        let alive = pid > 0 && kill(pid, 0) == 0 && Date().timeIntervalSince1970 - time < 8
        var next = EngineStatus()
        if !alive, gameProcess?.isRunning == true {
            next.state = "starting"
            next.message = "Starting Black Ops III…"
        } else if !alive, status.state == "steam" {
            next = status
        } else if alive {
            next.state = values["state"] ?? "idle"
            next.message = values["message"] ?? ""
            next.server = values["server"] ?? ""
            next.mode = values["mode"] ?? ""
        }
        if next != status { status = next }
    }

    // MARK: - Joining

    static func validName(_ name: String) -> Bool {
        let trimmed = name.trimmingCharacters(in: .whitespaces)
        return !trimmed.isEmpty && trimmed.count <= 16 && trimmed.rangeOfCharacter(from: CharacterSet(charactersIn: "\\\";%")) == nil
    }

    func join(_ server: Server, name: String) {
        guard gameAppURL != nil else { alert = "Black Ops III wasn't found. Choose its location in Settings."; return }
        let request = "server=\(server.address)\nmode=\(server.mode.rawValue)\nname=\(name.trimmingCharacters(in: .whitespaces))\n"
        do {
            try request.write(to: Self.dataDirectory.appendingPathComponent("join.request"), atomically: true, encoding: .utf8)
        } catch {
            alert = "Couldn't write the join request: \(error.localizedDescription)"
            return
        }
        if isEngineRunning { return }               // the running game picks it up
        launch(name: name)
    }

    func launch(name: String? = nil) {
        guard let app = gameAppURL else { alert = "Black Ops III wasn't found. Choose its location in Settings."; return }
        if isEngineRunning { activateGame(); return }
        if NSRunningApplication.runningApplications(withBundleIdentifier: "com.aspyr.codblkops3.steam").count > 0 ||
            Self.processRunning("CoDBlkOps3_Exe") {
            alert = "Black Ops III is already running without the client. Quit the game and try again."
            return
        }
        guard Self.steamRunning else {
            Self.startSteamInBackground()
            status.state = "steam"
            status.message = "Starting Steam…"
            waitForSteam { [weak self] in self?.launch(name: name) }
            return
        }
        guard let engine = Bundle.main.url(forResource: "BO3MacFix", withExtension: "dylib") else {
            alert = "The client engine is missing from the app bundle."
            return
        }
        let macOS = app.appendingPathComponent("Contents/MacOS", isDirectory: true)
        let process = Process()
        process.executableURL = macOS.appendingPathComponent("CoDBlkOps3_Exe")
        process.currentDirectoryURL = macOS
        var environment = ProcessInfo.processInfo.environment
        environment["DYLD_INSERT_LIBRARIES"] = engine.path
        environment["SteamAppId"] = Self.steamAppID
        environment["SteamGameId"] = Self.steamAppID
        environment["BO3MACFIX_DATA_DIR"] = Self.dataDirectory.path
        environment["BO3MACFIX_BOIII"] = "1"
        if let name, Self.validName(name) { environment["BO3MACFIX_PLAYERNAME"] = name.trimmingCharacters(in: .whitespaces) }
        process.environment = environment
        do {
            try? FileManager.default.removeItem(at: Self.dataDirectory.appendingPathComponent("status"))
            try process.run()
            gameProcess = process
            status.state = "starting"
            status.message = "Starting Black Ops III…"
        } catch {
            alert = "Couldn't start Black Ops III: \(error.localizedDescription)"
        }
    }

    func activateGame() {
        for app in NSWorkspace.shared.runningApplications where app.executableURL?.lastPathComponent == "CoDBlkOps3_Exe" {
            app.activate()
        }
    }

    /// Starts Steam without bringing its window up (-silent keeps it in the
    /// menu bar; the app isn't activated).
    static func startSteamInBackground() {
        guard let steam = NSWorkspace.shared.urlForApplication(withBundleIdentifier: "com.valvesoftware.steam") else {
            if let url = URL(string: "steam://") { NSWorkspace.shared.open(url) }
            return
        }
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.activates = false
        configuration.hides = true
        configuration.arguments = ["-silent"]
        NSWorkspace.shared.openApplication(at: steam, configuration: configuration)
    }

    static var steamRunning: Bool {
        !NSRunningApplication.runningApplications(withBundleIdentifier: "com.valvesoftware.steam").isEmpty
            || processRunning("steam_osx")
    }

    static func processRunning(_ name: String) -> Bool {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/pgrep")
        task.arguments = ["-x", name]
        task.standardOutput = FileHandle.nullDevice
        do { try task.run(); task.waitUntilExit(); return task.terminationStatus == 0 } catch { return false }
    }

    private func waitForSteam(_ then: @escaping () -> Void, attempts: Int = 60) {
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in
            guard let self else { return }
            if Self.steamRunning {
                // Give Steam a moment to sign in before the game asks for it.
                DispatchQueue.main.asyncAfter(deadline: .now() + 6, execute: then)
            } else if attempts > 0 {
                self.waitForSteam(then, attempts: attempts - 1)
            } else {
                self.status = EngineStatus()
                self.alert = "Steam didn't start. Open Steam, sign in, and try again."
            }
        }
    }

    func revealDataFolder() { NSWorkspace.shared.open(Self.dataDirectory) }
}
