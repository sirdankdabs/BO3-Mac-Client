import SwiftUI

@main
struct BO3MacClientApp: App {
    @StateObject private var browser = ServerBrowser()
    @StateObject private var launcher = GameLauncher()

    var body: some Scene {
        Window("BO3 Mac Client", id: "main") {
            ContentView()
                .environmentObject(browser)
                .environmentObject(launcher)
                .frame(minWidth: 900, minHeight: 540)
        }
        .defaultSize(width: 1100, height: 680)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }

        Settings {
            SettingsView()
                .environmentObject(launcher)
        }
    }
}

struct SettingsView: View {
    @EnvironmentObject var launcher: GameLauncher
    @AppStorage("playerName") private var playerName = ""

    var body: some View {
        Form {
            Section("Game") {
                LabeledContent("Black Ops III") {
                    HStack {
                        Text(launcher.gameAppURL?.deletingLastPathComponent().lastPathComponent ?? "Not found")
                            .foregroundStyle(launcher.gameAppURL == nil ? .orange : .secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                        Button("Choose…") { launcher.chooseGame() }
                    }
                }
                LabeledContent("Player name") {
                    TextField("Steam name", text: $playerName)
                        .frame(width: 180)
                }
            }
            Section("Data") {
                LabeledContent("Logs and identity") {
                    Button("Show in Finder") { launcher.revealDataFolder() }
                }
            }
            Section {
                VStack(alignment: .leading, spacing: 6) {
                    Text("BO3 Mac Client").font(.headline)
                    Text("Play Call of Duty: Black Ops III for Mac on community dedicated servers. Requires the Steam version of the game.")
                        .foregroundStyle(.secondary)
                    Text("Built on BO3MacFix by Emma / InvoxiPlayGames and Anthony / serious, with T7Patch work by the T7Patch authors. Not affiliated with Activision, Treyarch, Aspyr or Valve.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .padding(.vertical, 4)
            }
        }
        .formStyle(.grouped)
        .frame(width: 520)
        .padding(.vertical, 8)
    }
}
