import SwiftUI

enum ModeFilter: String, CaseIterable, Identifiable {
    case all, zombies, multiplayer
    var id: String { rawValue }
    var title: String { self == .all ? "All" : self == .zombies ? "Zombies" : "Multiplayer" }
}

struct ContentView: View {
    @EnvironmentObject var browser: ServerBrowser
    @EnvironmentObject var launcher: GameLauncher
    @AppStorage("playerName") private var playerName = ""
    @AppStorage("modeFilter") private var modeFilter = ModeFilter.all
    @AppStorage("hideFull") private var hideFull = false
    @AppStorage("hideEmpty") private var hideEmpty = false
    @AppStorage("hideUnjoinable") private var hideUnjoinable = false
    @State private var search = ""
    @State private var selection: Server.ID?
    @State private var sortOrder = [KeyPathComparator(\Server.humans, order: .reverse)]

    private var visible: [Server] {
        browser.servers.filter { server in
            switch modeFilter {
            case .all: break
            case .zombies: if server.mode != .zombies { return false }
            case .multiplayer: if server.mode != .multiplayer { return false }
            }
            if hideFull && server.isFull { return false }
            if hideEmpty && server.humans == 0 { return false }
            if hideUnjoinable && server.blocker != nil && !server.isFull { return false }
            if !search.isEmpty {
                let haystack = "\(server.plainName) \(Names.map(server.map)) \(server.map) \(Names.gametype(server.gametype)) \(server.address)"
                if !haystack.localizedCaseInsensitiveContains(search) { return false }
            }
            return true
        }
        .sorted(using: sortOrder)
    }

    private var selected: Server? { browser.servers.first { $0.id == selection } }
    private var totalPlayers: Int { browser.servers.reduce(0) { $0 + $1.humans } }

    var body: some View {
        VStack(spacing: 0) {
            if launcher.gameAppURL == nil {
                GameMissingBanner()
            }
            serverTable
        }
        .safeAreaInset(edge: .bottom, spacing: 0) { bottomBar }
        .navigationTitle("BO3 Mac Client")
        .navigationSubtitle(browser.servers.isEmpty ? "Community servers" : "\(visible.count) servers · \(totalPlayers) players online")
        .searchable(text: $search, placement: .toolbar, prompt: "Search servers, maps, modes")
        .toolbar {
            ToolbarItem(placement: .principal) {
                Picker("Mode", selection: $modeFilter) {
                    ForEach(ModeFilter.allCases) { Text($0.title).tag($0) }
                }
                .pickerStyle(.segmented)
                .frame(width: 280)
            }
            ToolbarItemGroup(placement: .primaryAction) {
                Menu {
                    Toggle("Hide full servers", isOn: $hideFull)
                    Toggle("Hide empty servers", isOn: $hideEmpty)
                    Toggle("Hide servers that need mods or a password", isOn: $hideUnjoinable)
                } label: {
                    Label("Filters", systemImage: "line.3.horizontal.decrease.circle")
                }
                Button { browser.refresh() } label: {
                    if browser.isRefreshing {
                        ProgressView().controlSize(.small)
                    } else {
                        Label("Refresh", systemImage: "arrow.clockwise")
                    }
                }
                .keyboardShortcut("r", modifiers: .command)
                .help("Refresh the server list (⌘R)")
            }
        }
        .onAppear {
            browser.zoneDirectory = launcher.gameAppURL?.appendingPathComponent("Contents/Assets/zone", isDirectory: true)
            if browser.servers.isEmpty { browser.refresh() }
        }
        .onChange(of: launcher.gameAppURL) { _, url in
            browser.zoneDirectory = url?.appendingPathComponent("Contents/Assets/zone", isDirectory: true)
        }
        .alert("BO3 Mac Client", isPresented: Binding(get: { launcher.alert != nil }, set: { if !$0 { launcher.alert = nil } })) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(launcher.alert ?? "")
        }
    }

    @ViewBuilder private var serverTable: some View {
        if browser.servers.isEmpty {
            Group {
                if browser.isRefreshing {
                    ProgressView("Finding servers…")
                } else if let error = browser.lastError {
                    ContentUnavailableView {
                        Label("No connection", systemImage: "wifi.exclamationmark")
                    } description: {
                        Text(error)
                    } actions: {
                        Button("Try Again") { browser.refresh() }
                    }
                } else {
                    ContentUnavailableView("No servers", systemImage: "server.rack")
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        } else {
            Table(visible, selection: $selection, sortOrder: $sortOrder) {
                TableColumn("Server", value: \.plainName) { server in
                    VStack(alignment: .leading, spacing: 2) {
                        Text(ColorText.attributed(server.hostname))
                            .lineLimit(1)
                        if !server.modName.isEmpty {
                            Text(server.modName).font(.caption).foregroundStyle(.secondary).lineLimit(1)
                        }
                    }
                    .padding(.vertical, 3)
                }
                .width(min: 260, ideal: 420)

                TableColumn("Map", value: \.map) { server in
                    VStack(alignment: .leading, spacing: 2) {
                        Text(Names.map(server.map)).lineLimit(1)
                        Text("\(server.mode.title) · \(Names.gametype(server.gametype))")
                            .font(.caption).foregroundStyle(.secondary).lineLimit(1)
                    }
                }
                .width(min: 150, ideal: 190)

                TableColumn("Players", value: \.humans) { server in
                    PlayersView(server: server)
                }
                .width(min: 90, ideal: 110)

                TableColumn("Ping", value: \.ping) { server in
                    HStack(spacing: 5) {
                        Circle().fill(pingColor(server.ping)).frame(width: 7, height: 7)
                        Text("\(server.ping) ms").monospacedDigit().foregroundStyle(.secondary)
                    }
                }
                .width(min: 70, ideal: 80)

                TableColumn("") { server in
                    StatusTag(server: server)
                }
                .width(min: 90, ideal: 110)
            }
            .contextMenu(forSelectionType: Server.ID.self) { ids in
                if let id = ids.first, let server = browser.servers.first(where: { $0.id == id }) {
                    Button("Join") { join(server) }.disabled(server.blocker != nil)
                    Button("Copy Address") {
                        NSPasteboard.general.clearContents()
                        NSPasteboard.general.setString(server.address, forType: .string)
                    }
                }
            } primaryAction: { ids in
                if let id = ids.first, let server = browser.servers.first(where: { $0.id == id }) { join(server) }
            }
        }
    }

    private var bottomBar: some View {
        HStack(spacing: 14) {
            HStack(spacing: 8) {
                Image(systemName: "person.crop.circle.fill")
                    .font(.title2)
                    .foregroundStyle(.secondary)
                TextField("Player name", text: $playerName)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 170)
                    .help("The name other players will see (up to 16 characters). Leave empty to use your Steam name.")
                if !playerName.isEmpty && !GameLauncher.validName(playerName) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundStyle(.orange)
                        .help("Use 1–16 characters, without \\ \" ; or %")
                }
            }

            EngineStatusView(status: launcher.status)

            Spacer()

            Button {
                launcher.launch(name: playerName.isEmpty ? nil : playerName)
            } label: {
                Label(launcher.isEngineRunning ? "Show Game" : "Launch Game", systemImage: "gamecontroller")
            }
            .controlSize(.large)
            .disabled(launcher.gameAppURL == nil)

            Button {
                if let server = selected { join(server) }
            } label: {
                Label("Join", systemImage: "play.fill")
                    .frame(minWidth: 70)
            }
            .controlSize(.large)
            .buttonStyle(.borderedProminent)
            .keyboardShortcut(.defaultAction)
            .disabled(selected == nil || selected?.blocker != nil || launcher.gameAppURL == nil)
            .help(selected?.blocker ?? "Join the selected server")
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
        .background(.bar)
        .overlay(alignment: .top) { Divider() }
    }

    private func join(_ server: Server) {
        if let blocker = server.blocker { launcher.alert = blocker; return }
        if !playerName.isEmpty && !GameLauncher.validName(playerName) {
            launcher.alert = "Player names can have 1–16 characters, without \\ \" ; or %."
            return
        }
        launcher.join(server, name: playerName)
    }

    private func pingColor(_ ping: Int) -> Color {
        ping < 80 ? .green : ping < 150 ? .yellow : .red
    }
}

struct PlayersView: View {
    let server: Server
    var body: some View {
        HStack(spacing: 6) {
            Text("\(server.humans)/\(server.maxClients)")
                .monospacedDigit()
                .fontWeight(server.humans > 0 ? .semibold : .regular)
                .foregroundStyle(server.humans > 0 ? .primary : .secondary)
            if server.bots > 0 {
                Text("+\(server.bots) bots").font(.caption).foregroundStyle(.secondary)
            }
        }
    }
}

struct StatusTag: View {
    let server: Server
    var body: some View {
        if !server.protocolOK {
            tag("Incompatible", .gray)
        } else if server.needsWorkshop {
            tag("Mod", .purple)
        } else if server.isPrivate {
            tag("Locked", .orange)
        } else if server.isFull {
            tag("Full", .red)
        }
    }

    private func tag(_ text: String, _ color: Color) -> some View {
        Text(text)
            .font(.caption.weight(.semibold))
            .padding(.horizontal, 7)
            .padding(.vertical, 2)
            .background(color.opacity(0.18), in: Capsule())
            .foregroundStyle(color)
    }
}

struct EngineStatusView: View {
    let status: GameLauncher.EngineStatus
    var body: some View {
        HStack(spacing: 6) {
            Circle().fill(color).frame(width: 8, height: 8)
            Text(text).foregroundStyle(.secondary).lineLimit(1)
        }
        .help(status.server.isEmpty ? text : "\(text) — \(status.server)")
    }

    private var color: Color {
        switch status.state {
        case "in_game": return .green
        case "connecting", "loading", "starting", "steam", "waiting_mode": return .yellow
        case "failed": return .red
        case "offline": return .gray
        default: return .blue
        }
    }

    private var text: String {
        switch status.state {
        case "offline": return "Game not running"
        case "idle": return "Game ready"
        case "in_game": return "In game"
        default: return status.message.isEmpty ? status.state.capitalized : status.message
        }
    }
}

struct GameMissingBanner: View {
    @EnvironmentObject var launcher: GameLauncher
    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.orange)
            Text("Call of Duty: Black Ops III (Steam) wasn't found.")
            Spacer()
            Button("Locate…") { launcher.chooseGame() }
        }
        .padding(10)
        .background(.orange.opacity(0.12))
    }
}

enum ColorText {
    static let palette: [Character: Color] = [
        "0": Color(white: 0.45), "1": Color(red: 0.95, green: 0.3, blue: 0.3), "2": Color(red: 0.35, green: 0.85, blue: 0.4),
        "3": Color(red: 0.98, green: 0.82, blue: 0.25), "4": Color(red: 0.35, green: 0.55, blue: 1.0),
        "5": Color(red: 0.3, green: 0.85, blue: 0.95), "6": Color(red: 0.95, green: 0.45, blue: 0.85),
        "8": Color(red: 1.0, green: 0.6, blue: 0.2), "9": Color(white: 0.6),
    ]

    /// Renders ^N color codes; ^7 (and anything unknown) uses the default text color.
    static func attributed(_ text: String) -> AttributedString {
        var result = AttributedString()
        var current: Color?
        var buffer = ""
        var iterator = text.makeIterator()
        func flush() {
            guard !buffer.isEmpty else { return }
            var part = AttributedString(buffer)
            if let current { part.foregroundColor = current }
            result += part
            buffer = ""
        }
        while let c = iterator.next() {
            if c == "^", let code = iterator.next() {
                flush()
                current = palette[code]
            } else {
                buffer.append(c)
            }
        }
        flush()
        if result.characters.isEmpty { result = AttributedString("Unnamed server") }
        return result
    }
}
