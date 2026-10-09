import Foundation
import Darwin

enum GameMode: String, CaseIterable, Identifiable {
    case zombies = "zm", multiplayer = "mp", campaign = "cp"
    var id: String { rawValue }
    var title: String {
        switch self {
        case .zombies: return "Zombies"
        case .multiplayer: return "Multiplayer"
        case .campaign: return "Campaign"
        }
    }
}

struct Server: Identifiable, Hashable {
    let address: String          // "ip:port"
    var hostname = ""
    var map = ""
    var gametype = ""
    var mode: GameMode = .zombies
    var clients = 0
    var bots = 0
    var maxClients = 0
    var ping = 0
    /// Current Zombies round (`rounds_played` in infoResponse); 0 when unknown or not Zombies.
    var round = 0
    var isPrivate = false
    var workshopID = ""
    var modID = ""
    var modName = ""
    /// Set by the browser when the game's install is known: the map ships with the game.
    var stockMap = true
    var protocolOK = false
    var id: String { address }

    var humans: Int { max(0, clients - bots) }
    var isFull: Bool { maxClients > 0 && clients >= maxClients }
    /// A real Workshop mod on the client (not supported yet). A Workshop id with
    /// a stock map (mod running only on the server) doesn't count.
    var needsMod: Bool { !modID.isEmpty && modID != "usermaps" }
    /// A custom map: downloaded from the Workshop automatically when joining.
    var isCustomMap: Bool { !stockMap }
    var canDownloadMap: Bool { !workshopID.isEmpty && workshopID.allSatisfy(\.isNumber) }
    var needsWorkshop: Bool { needsMod || (isCustomMap && !canDownloadMap) }
    var plainName: String { ColorCodes.strip(hostname) }

    /// Why the client can't join this server, or nil when it can.
    var blocker: String? {
        if !protocolOK { return "Incompatible server" }
        if needsMod { return "Needs a Workshop mod (not supported yet)" }
        if isCustomMap && !canDownloadMap { return "Custom map not available on the Workshop" }
        if isPrivate { return "Password protected" }
        if isFull { return "Server is full" }
        return nil
    }
}

/// Queries the community master server and every listed server (UDP,
/// Quake-style out-of-band packets).
final class ServerBrowser: ObservableObject {
    @Published private(set) var servers: [Server] = []
    @Published private(set) var isRefreshing = false
    @Published private(set) var lastError: String?
    @Published private(set) var lastRefresh: Date?

    static let masters = ["master.ezz.lol:20810", "m.ezz.lol:20810"]

    /// Contents/Assets/zone of the game, to tell stock maps from custom maps.
    var zoneDirectory: URL?

    func refresh() {
        guard !isRefreshing else { return }
        let zones = zoneDirectory
        isRefreshing = true
        lastError = nil
        DispatchQueue.global(qos: .userInitiated).async {
            var addresses: [sockaddr_in] = []
            for master in Self.masters {
                addresses = Self.queryMaster(master)
                if !addresses.isEmpty { break }
            }
            var results = addresses.isEmpty ? [] : Self.queryServers(addresses)
            if let zones {
                for i in results.indices where !results[i].map.isEmpty {
                    results[i].stockMap = FileManager.default.fileExists(
                        atPath: zones.appendingPathComponent("\(results[i].map).ff").path)
                }
            }
            DispatchQueue.main.async {
                self.isRefreshing = false
                self.lastRefresh = Date()
                if addresses.isEmpty {
                    self.lastError = "Could not reach the master server."
                } else {
                    self.servers = results
                }
            }
        }
    }

    // MARK: - Networking

    private static func resolve(_ text: String) -> sockaddr_in? {
        let parts = text.split(separator: ":")
        guard parts.count == 2, let port = UInt16(parts[1]) else { return nil }
        var hints = addrinfo(ai_flags: 0, ai_family: AF_INET, ai_socktype: SOCK_DGRAM, ai_protocol: 0,
                             ai_addrlen: 0, ai_canonname: nil, ai_addr: nil, ai_next: nil)
        var result: UnsafeMutablePointer<addrinfo>?
        guard getaddrinfo(String(parts[0]), nil, &hints, &result) == 0, let info = result,
              let raw = info.pointee.ai_addr else { return nil }
        defer { freeaddrinfo(result) }
        var address = raw.withMemoryRebound(to: sockaddr_in.self, capacity: 1) { $0.pointee }
        address.sin_port = port.bigEndian
        return address
    }

    private static func makeSocket(timeoutMs: Int) -> Int32 {
        let fd = socket(AF_INET, SOCK_DGRAM, 0)
        var timeout = timeval(tv_sec: 0, tv_usec: Int32(timeoutMs * 1000))
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        var buffer: Int32 = 1 << 20
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer, socklen_t(MemoryLayout<Int32>.size))
        return fd
    }

    private static func send(_ fd: Int32, _ payload: [UInt8], to address: sockaddr_in) {
        var target = address
        payload.withUnsafeBytes { bytes in
            _ = withUnsafePointer(to: &target) {
                $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                    sendto(fd, bytes.baseAddress, bytes.count, 0, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
                }
            }
        }
    }

    private static func receive(_ fd: Int32) -> ([UInt8], sockaddr_in)? {
        var buffer = [UInt8](repeating: 0, count: 65536)
        var from = sockaddr_in()
        var length = socklen_t(MemoryLayout<sockaddr_in>.size)
        let count = withUnsafeMutablePointer(to: &from) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                recvfrom(fd, &buffer, buffer.count, 0, $0, &length)
            }
        }
        guard count > 0 else { return nil }
        return (Array(buffer[0..<count]), from)
    }

    private static func oob(_ text: String) -> [UInt8] { [0xff, 0xff, 0xff, 0xff] + Array(text.utf8) }

    /// "getservers\nT7 8 full empty" -> "getserversResponse" + ("\\" ip:4 port:2)* + "\\EOT"
    private static func queryMaster(_ master: String) -> [sockaddr_in] {
        guard let address = resolve(master) else { return [] }
        let fd = makeSocket(timeoutMs: 400)
        defer { close(fd) }
        send(fd, oob("getservers\nT7 8 full empty"), to: address)
        var found: [sockaddr_in] = []
        var seen = Set<String>()
        let deadline = Date().addingTimeInterval(3)
        var done = false
        while !done && Date() < deadline {
            guard let (data, _) = receive(fd) else { if !found.isEmpty { break } else { continue } }
            let header = Array("getserversResponse".utf8)
            guard data.count > 4 + header.count, Array(data[4..<(4 + header.count)]) == header else { continue }
            var i = 4 + header.count
            while i < data.count {
                guard data[i] == UInt8(ascii: "\\") else { i += 1; continue }
                if i + 3 < data.count, data[i + 1] == UInt8(ascii: "E"), data[i + 2] == UInt8(ascii: "O"),
                   data[i + 3] == UInt8(ascii: "T") { done = true; break }
                guard i + 6 < data.count else { break }
                var entry = sockaddr_in()
                entry.sin_family = sa_family_t(AF_INET)
                entry.sin_addr.s_addr = UInt32(data[i + 1]) | UInt32(data[i + 2]) << 8 |
                    UInt32(data[i + 3]) << 16 | UInt32(data[i + 4]) << 24
                entry.sin_port = UInt16(data[i + 5]) | UInt16(data[i + 6]) << 8
                let key = "\(entry.sin_addr.s_addr):\(entry.sin_port)"
                if seen.insert(key).inserted { found.append(entry) }
                i += 7
            }
        }
        return found
    }

    private static func describe(_ address: sockaddr_in) -> String {
        var addr = address.sin_addr
        var text = [CChar](repeating: 0, count: Int(INET_ADDRSTRLEN))
        inet_ntop(AF_INET, &addr, &text, socklen_t(INET_ADDRSTRLEN))
        return "\(String(cString: text)):\(UInt16(bigEndian: address.sin_port))"
    }

    private static func queryServers(_ addresses: [sockaddr_in]) -> [Server] {
        let fd = makeSocket(timeoutMs: 150)
        defer { close(fd) }
        var sentAt: [String: Date] = [:]
        for address in addresses {
            sentAt[describe(address)] = Date()
            send(fd, oob("getInfo\nmacclient"), to: address)
        }
        var results: [String: Server] = [:]
        let deadline = Date().addingTimeInterval(2.0)
        while Date() < deadline && results.count < addresses.count {
            guard let (data, from) = receive(fd), data.count > 4 else { continue }
            let text = String(decoding: data[4...], as: UTF8.self)
            guard text.hasPrefix("infoResponse"), let newline = text.firstIndex(of: "\n") else { continue }
            let key = describe(from)
            guard results[key] == nil, let sent = sentAt[key] else { continue }
            let info = parseInfo(String(text[text.index(after: newline)...]))
            var server = Server(address: key)
            server.hostname = info["hostname"] ?? ""
            server.map = info["mapname"] ?? ""
            server.gametype = info["gametype"] ?? ""
            server.clients = Int(info["clients"] ?? "") ?? 0
            server.bots = Int(info["bots"] ?? "") ?? 0
            server.maxClients = Int(info["sv_maxclients"] ?? "") ?? 0
            server.isPrivate = info["isPrivate"] == "1"
            server.round = Int(info["rounds_played"] ?? "") ?? 0
            server.workshopID = info["workshop_id"] ?? info["usermapId"] ?? ""
            server.modName = ColorCodes.strip(info["modName"] ?? "")
            server.modID = info["modId"] ?? ""
            if server.modName == "usermaps" { server.modName = "" }
            server.protocolOK = info["protocol"] == "8" && info["gamename"] == "T7" && !server.map.isEmpty
            switch info["playmode"] {
            case "1": server.mode = .multiplayer
            case "2": server.mode = .campaign
            case "0": server.mode = .zombies
            default: server.mode = server.gametype.hasPrefix("z") ? .zombies : .multiplayer
            }
            server.ping = max(1, Int(Date().timeIntervalSince(sent) * 1000))
            results[key] = server
        }
        return Array(results.values)
    }

    private static func parseInfo(_ text: String) -> [String: String] {
        var parts = text.split(separator: "\\", omittingEmptySubsequences: false).map(String.init)
        if parts.first == "" { parts.removeFirst() }
        var result: [String: String] = [:]
        var i = 0
        while i + 1 < parts.count { result[parts[i]] = parts[i + 1]; i += 2 }
        return result
    }
}

enum ColorCodes {
    static func strip(_ text: String) -> String {
        var out = ""
        var skip = false
        for c in text {
            if skip { skip = false; continue }
            if c == "^" { skip = true; continue }
            out.append(c)
        }
        return out.trimmingCharacters(in: CharacterSet(charactersIn: " |"))
    }
}

enum Names {
    static let maps: [String: String] = [
        "zm_zod": "Shadows of Evil", "zm_factory": "The Giant", "zm_castle": "Der Eisendrache",
        "zm_island": "Zetsubou No Shima", "zm_stalingrad": "Gorod Krovi", "zm_genesis": "Revelations",
        "zm_prototype": "Nacht der Untoten", "zm_asylum": "Verrückt", "zm_sumpf": "Shi No Numa",
        "zm_theater": "Kino der Toten", "zm_cosmodrome": "Ascension", "zm_temple": "Shangri-La",
        "zm_moon": "Moon", "zm_tomb": "Origins",
        "mp_biodome": "Aquarium", "mp_spire": "Breach", "mp_sector": "Combine", "mp_apartments": "Evac",
        "mp_chinatown": "Exodus", "mp_veiled": "Fringe", "mp_havoc": "Havoc", "mp_ethiopia": "Hunted",
        "mp_infection": "Infection", "mp_metro": "Metro", "mp_redwood": "Redwood",
        "mp_stronghold": "Stronghold", "mp_nuketown_x": "Nuk3town",
    ]
    static let gametypes: [String: String] = [
        "zclassic": "Classic", "zstandard": "Standard", "tdm": "Team Deathmatch", "dm": "Free-for-All",
        "sd": "Search & Destroy", "koth": "Hardpoint", "dom": "Domination", "conf": "Kill Confirmed",
        "gun": "Gun Game", "ctf": "Capture the Flag", "dem": "Demolition", "ball": "Uplink",
        "escort": "Safeguard", "clean": "Fracture", "sniperonly": "Snipers Only", "prop": "Prop Hunt",
        "infect": "Infected", "coop": "Co-op",
    ]
    static func map(_ code: String) -> String {
        if let name = maps[code] { return name }
        var name = code
        for prefix in ["zm_", "mp_", "cp_"] where name.hasPrefix(prefix) { name.removeFirst(prefix.count) }
        return name.replacingOccurrences(of: "_", with: " ").capitalized
    }
    static func gametype(_ code: String) -> String { gametypes[code] ?? code.uppercased() }
}
