// Text symbol recovery adapted from T7Patch LuaSymbols.cpp (46b268a).
// Text recovery is layout-independent; live HKS recovery is separately opt-in.
#include "t7_runtime.h"
#include "t7_lua.h"
extern "C" {
#include "dobby.h"
}
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
namespace {
constexpr size_t MaximumStackTraceSize = 1024 * 1024;
constexpr size_t MaximumDatabaseFileSize = 16 * 1024 * 1024;
constexpr size_t MaximumDatabaseEntries = 1000000;
struct SymbolRecord { std::string source; uint32_t sourceLine; std::string functionName;
    bool operator==(const SymbolRecord &r) const { return source == r.source && sourceLine == r.sourceLine && functionName == r.functionName; } };
struct StockSymbolRecord { std::string source; std::string functionName; };
using SymbolMap = std::unordered_map<uint64_t, SymbolRecord>;
using StockSymbolMap = std::unordered_map<uint32_t, StockSymbolRecord>;
struct Prototype { uint32_t offset; std::string name; };
using PrototypeMap = std::unordered_map<uint32_t, std::vector<Prototype>>;
        std::uint64_t MakeSymbolKey(std::uint32_t functionHash,
            std::uint32_t instructionIndex)
        {
            return (static_cast<std::uint64_t>(functionHash) << 32) |
                instructionIndex;
        }

bool ParseUInt32(std::string_view text, uint32_t &value) {
    if (text.empty()) return false;
    uint32_t n = 0;
    for (char c : text) {
        if (c < '0' || c > '9' || n > (UINT32_MAX - (c - '0')) / 10) return false;
        n = n * 10 + (c - '0');
    }
    value = n; return true;
}
        std::string DisplaySource(std::string source)
        {
            if (!source.empty() && source.front() == '@')
                source.erase(source.begin());
            std::replace(source.begin(), source.end(), '\\', '/');
            return source;
        }

        bool ParseSymbolLine(std::string_view line, std::uint64_t& key,
            SymbolRecord& record)
        {
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);

            std::size_t commas[4]{};
            auto searchFrom = std::size_t{};
            for (auto& comma : commas)
            {
                comma = line.find(',', searchFrom);
                if (comma == std::string_view::npos)
                    return false;
                searchFrom = comma + 1;
            }

            std::uint32_t functionHash = 0;
            std::uint32_t instructionIndex = 0;
            std::uint32_t sourceLine = 0;
            if (!ParseUInt32(line.substr(0, commas[0]), functionHash) ||
                !ParseUInt32(line.substr(commas[0] + 1,
                    commas[1] - commas[0] - 1), instructionIndex) ||
                !ParseUInt32(line.substr(commas[2] + 1,
                    commas[3] - commas[2] - 1), sourceLine))
            {
                return false;
            }

            auto source = DisplaySource(std::string(line.substr(commas[1] + 1,
                commas[2] - commas[1] - 1)));
            if (source.empty())
                return false;

            key = MakeSymbolKey(functionHash, instructionIndex);
            record = SymbolRecord
            {
                std::move(source),
                sourceLine,
                std::string(line.substr(commas[3] + 1))
            };
            return true;
        }

        bool ParseFramePrefix(std::string_view line, std::size_t& prefixStart,
            std::size_t& prefixEnd, std::uint64_t& key)
        {
            prefixStart = 0;
            while (prefixStart < line.size() &&
                (line[prefixStart] == ' ' || line[prefixStart] == '\t'))
            {
                ++prefixStart;
            }

            auto cursor = prefixStart;
            while (cursor < line.size() && line[cursor] >= '0' && line[cursor] <= '9')
                ++cursor;
            if (cursor == prefixStart || cursor >= line.size() || line[cursor] != ':')
                return false;

            std::uint32_t functionHash = 0;
            if (!ParseUInt32(line.substr(prefixStart, cursor - prefixStart), functionHash))
                return false;

            const auto instructionStart = ++cursor;
            while (cursor < line.size() && line[cursor] >= '0' && line[cursor] <= '9')
                ++cursor;
            if (cursor == instructionStart || cursor >= line.size() || line[cursor] != ':')
                return false;

            std::uint32_t instructionIndex = 0;
            if (!ParseUInt32(line.substr(instructionStart,
                cursor - instructionStart), instructionIndex))
            {
                return false;
            }

            prefixEnd = cursor + 1;
            key = MakeSymbolKey(functionHash, instructionIndex);
            return true;
        }

        std::string RewriteStackTrace(std::string_view stackTrace,
            const SymbolMap& symbols, const StockSymbolMap& stockSymbols,
            std::size_t& resolvedFrames)
        {
            constexpr std::string_view StrippedFunction = "in function '(*stripped)'";

            std::string rewritten;
            rewritten.reserve(stackTrace.size());
            auto position = std::size_t{};

            while (position < stackTrace.size())
            {
                const auto newline = stackTrace.find('\n', position);
                const auto lineEnd = newline == std::string_view::npos ?
                    stackTrace.size() : newline;
                const auto line = stackTrace.substr(position, lineEnd - position);

                std::size_t prefixStart = 0;
                std::size_t prefixEnd = 0;
                std::uint64_t key = 0;
                const auto parsed = ParseFramePrefix(line, prefixStart, prefixEnd, key);
                const auto symbol = parsed ? symbols.find(key) : symbols.end();
                const auto functionHash = static_cast<std::uint32_t>(key >> 32);
                const auto instructionIndex = static_cast<std::uint32_t>(key);
                const auto stockSymbol = parsed && symbol == symbols.end() ?
                    stockSymbols.find(functionHash) : stockSymbols.end();
                if (symbol == symbols.end() && stockSymbol == stockSymbols.end())
                {
                    rewritten.append(line);
                }
                else if (symbol == symbols.end())
                {
                    rewritten.append(line.substr(0, prefixStart));
                    rewritten.append(stockSymbol->second.source);
                    rewritten.append(":pc");
                    rewritten.append(std::to_string(instructionIndex));
                    rewritten.push_back(':');

                    const auto remainder = line.substr(prefixEnd);
                    const auto stripped = remainder.find(StrippedFunction);
                    if (stripped == std::string_view::npos ||
                        stockSymbol->second.functionName.empty())
                    {
                        rewritten.append(remainder);
                    }
                    else
                    {
                        rewritten.append(remainder.substr(0, stripped));
                        rewritten.append("in function '");
                        rewritten.append(stockSymbol->second.functionName);
                        rewritten.push_back('\'');
                        rewritten.append(remainder.substr(stripped +
                            StrippedFunction.size()));
                    }
                    ++resolvedFrames;
                }
                else
                {
                    rewritten.append(line.substr(0, prefixStart));
                    rewritten.append(symbol->second.source);
                    rewritten.push_back(':');
                    rewritten.append(std::to_string(symbol->second.sourceLine));
                    rewritten.push_back(':');

                    const auto remainder = line.substr(prefixEnd);
                    const auto stripped = remainder.find(StrippedFunction);
                    if (stripped == std::string_view::npos ||
                        symbol->second.functionName.empty())
                    {
                        rewritten.append(remainder);
                    }
                    else
                    {
                        rewritten.append(remainder.substr(0, stripped));
                        rewritten.append("in function '");
                        rewritten.append(symbol->second.functionName);
                        rewritten.push_back('\'');
                        rewritten.append(remainder.substr(stripped + StrippedFunction.size()));
                    }
                    ++resolvedFrames;
                }

                if (newline == std::string_view::npos)
                    break;
                rewritten.push_back('\n');
                position = newline + 1;

                if (rewritten.size() > MaximumStackTraceSize)
                    return {};
            }
            return rewritten;
        }


bool database_ok(const std::string &path) {
    struct stat info{};
    return !stat(path.c_str(), &info) && S_ISREG(info.st_mode) && info.st_size > 0 &&
        static_cast<uint64_t>(info.st_size) <= MaximumDatabaseFileSize;
}
void load_mod(const std::string &path, SymbolMap &symbols, std::unordered_set<uint64_t> &ambiguous) {
    if (!database_ok(path)) return;
    std::ifstream file(path); std::string line;
    size_t count = 0;
    while (count++ < MaximumDatabaseEntries && symbols.size() + ambiguous.size() < MaximumDatabaseEntries && std::getline(file, line)) {
        if (line.size() > 8192) continue;
        uint64_t key; SymbolRecord record;
        if (!ParseSymbolLine(line, key, record) || ambiguous.find(key) != ambiguous.end()) continue;
        auto found = symbols.find(key);
        if (found == symbols.end()) symbols.emplace(key, std::move(record));
        else if (!(found->second == record)) { symbols.erase(found); ambiguous.insert(key); }
    }
}
void load_stock(const std::string &path, StockSymbolMap &symbols, PrototypeMap *prototypes = nullptr) {
    if (!database_ok(path)) return;
    std::ifstream file(path); std::string line;
    std::unordered_set<uint32_t> ambiguous;
    size_t count = 0;
    while (count++ < MaximumDatabaseEntries && std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line.size() > 8192) continue;
        if (line.back() == '\r') line.pop_back();
        auto first = line.find(',');
        if (first == std::string::npos) continue;
        uint32_t hash;
        if (!ParseUInt32(std::string_view(line).substr(0, first), hash) || !hash || ambiguous.find(hash) != ambiguous.end()) continue;
        auto second = line.find(',', first + 1);
        auto source = DisplaySource(line.substr(first + 1, second == std::string::npos ? second : second - first - 1));
        if (source.empty() || source.size() > 4096) continue;
        std::string name; uint32_t code_offset = 0;
        if (second != std::string::npos) {
            auto third = line.find(',', second + 1); uint32_t offset;
            if (third != std::string::npos && ParseUInt32(std::string_view(line).substr(second + 1, third-second-1), offset)) { name = line.substr(third + 1); code_offset = offset; }
            else name = line.substr(second + 1);
        }
        if (name.size() > 4096) continue;
        if (prototypes && code_offset && !name.empty()) (*prototypes)[hash].push_back({code_offset, name});
        auto found = symbols.find(hash);
        if (found == symbols.end()) symbols.emplace(hash, StockSymbolRecord{source, name});
        else if (found->second.source != source) { symbols.erase(found); ambiguous.insert(hash); if (prototypes) prototypes->erase(hash); }
        else if (found->second.functionName != name) found->second.functionName.clear();
    }
}
void load_directory(const std::string &path, SymbolMap &symbols, std::unordered_set<uint64_t> &ambiguous, unsigned depth, unsigned &visited) {
    if (depth > 8 || visited >= 4096) return;
    DIR *dir = opendir(path.c_str()); if (!dir) return;
    // Collect first, close before descending or allocating database objects.
    std::vector<std::string> children;
    while (auto *entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        if (++visited > 4096) break;
        children.push_back(path + "/" + entry->d_name);
    }
    closedir(dir);
    for (const auto &child : children) {
        struct stat info{}; if (lstat(child.c_str(), &info)) continue;
        if (S_ISDIR(info.st_mode)) load_directory(child, symbols, ambiguous, depth+1, visited);
        else if (S_ISREG(info.st_mode) && child.size() >= 15 && !strcasecmp(child.c_str() + child.size() - 15, ".luacallstackdb")) load_mod(child, symbols, ambiguous);
    }
}
std::string game_root() {
    const char *image = _dyld_get_image_name(0); if (!image) return {};
    std::string path(image); auto slash = path.find_last_of('/');
    if (slash == std::string::npos) return {};
    char root[PATH_MAX];
    if (!realpath((path.substr(0,slash) + "/../../..").c_str(), root)) return {};
    return root;
}
// The following HKS layouts come from T7Patch and are explicitly opt-in.
static bool vm_layout_enabled;
using ObjectToString = const char *(*)(int64_t *, int64_t, size_t *);
using PushString = void (*)(int64_t *, const char *, size_t);
static ObjectToString object_to_string;
static PushString push_string;
static int (*original_getinfo)(int64_t *, const char *, void *);
template<class T> bool read_value(uintptr_t base, size_t offset, T &out) {
    if (offset > UINTPTR_MAX - base) return false;
    const void *address = reinterpret_cast<const void *>(base + offset);
    if (!t7_memory_readable(address, sizeof(T))) return false;
    memcpy(&out, address, sizeof(T)); return true;
}
static const PrototypeMap &stock_prototypes() {
    static const PrototypeMap result = [] {
        PrototypeMap prototypes; StockSymbolMap symbols;
        auto root = game_root();
        load_stock(root + "/BO3MacFix/t7patch.stockluadb", symbols, &prototypes);
        if (symbols.empty()) load_stock(root + "/t7patch.stockluadb", symbols, &prototypes);
        return prototypes;
    }();
    return result;
}
static const char *live_function_name(int64_t *state, void *debug_record) {
    uintptr_t records, current, base;
    int32_t level, tail;
    uintptr_t state_address = reinterpret_cast<uintptr_t>(state);
    uintptr_t debug = reinterpret_cast<uintptr_t>(debug_record);
    if (!read_value(state_address,0x18,records) || !read_value(state_address,0x28,current) ||
        !read_value(debug,0x240,level) || !read_value(debug,0x244,tail) ||
        !records || current < records || level < 0 || tail) return nullptr;
    uintptr_t bytes = current - records, current_level = bytes / 24;
    if (bytes % 24 || current_level > 4096 || static_cast<uintptr_t>(level) > current_level) return nullptr;
    if (static_cast<uintptr_t>(level) == current_level) {
        if (!read_value(state_address,0x50,base)) return nullptr;
    } else if (!read_value(records,24*(static_cast<size_t>(level)+1),base)) return nullptr;
    if (base < 16) return nullptr;
    uintptr_t type, closure, method, instructions;
    uint32_t count, hash;
    if (!read_value(base-16,0,type) || (type & 15) != 9 || !read_value(base-8,0,closure) || !closure ||
        !read_value(closure,0x10,method) || !method || !read_value(method,0x20,count) ||
        !read_value(method,0x28,instructions) || !read_value(method,0x10,hash) ||
        !hash || !instructions || !count || count > MaximumDatabaseEntries) return nullptr;
    const auto &all = stock_prototypes();
    auto found = all.find(hash); if (found == all.end()) return nullptr;
    const Prototype *match = nullptr;
    for (const auto &candidate : found->second) {
        if (candidate.offset > MaximumDatabaseFileSize || instructions < candidate.offset) continue;
        uint32_t magic;
        if (!read_value(instructions-candidate.offset,0,magic) || magic != 0x61754c1b) continue;
        if (match && (match->offset != candidate.offset || match->name != candidate.name)) return nullptr;
        match = &candidate;
    }
    return match ? match->name.c_str() : nullptr;
}
static int getinfo_hook(int64_t *state,const char *what,void *record) {
    int result = original_getinfo(state,what,record);
    if (!result || !record) return result;
    try {
        const char *name;
        uintptr_t address = reinterpret_cast<uintptr_t>(record);
        if (read_value(address,8,name) && t7_memory_readable(name,12) && !memcmp(name,"(*stripped)",12)) {
            if (const char *resolved = live_function_name(state,record)) {
                if (t7_memory_writable(reinterpret_cast<void *>(address+8),sizeof(resolved)))
                    memcpy(reinterpret_cast<void *>(address+8),&resolved,sizeof(resolved));
            }
        }
    } catch (...) { }
    return result;
}
static bool replace_vm_top(int64_t *state, const std::string &text) {
    uintptr_t address = reinterpret_cast<uintptr_t>(state), top, base;
    if (!state || !push_string || !read_value(address,0x48,top) || !read_value(address,0x50,base) ||
        top < base || top-base < 16 || top-base > MaximumStackTraceSize ||
        !t7_memory_writable(reinterpret_cast<void *>(address+0x48),8)) return false;
    const uintptr_t previous_depth = top-base;
    push_string(state,text.data(),text.size());
    uintptr_t new_top, new_base;
    if (!read_value(address,0x48,new_top) || !read_value(address,0x50,new_base) ||
        new_top < new_base || new_top-new_base != previous_depth+16 || new_top < 32 ||
        !t7_memory_readable(reinterpret_cast<void *>(new_top-16),16) ||
        !t7_memory_writable(reinterpret_cast<void *>(new_top-32),16)) return false;
    // A push can relocate the VM stack. Use its NEW addresses when replacing
    // and popping, never restore the stale pre-allocation top pointer.
    memcpy(reinterpret_cast<void *>(new_top-32),reinterpret_cast<void *>(new_top-16),16);
    new_top -= 16;
    memcpy(reinterpret_cast<void *>(address+0x48),&new_top,8);
    return true;
}
static void resolve_vm_error(int64_t *state) {
    if (!state || !object_to_string || !push_string) return;
    uintptr_t address = reinterpret_cast<uintptr_t>(state), top, base;
    if (!read_value(address,0x48,top) || !read_value(address,0x50,base) || top < base ||
        top-base < 16 || top-base > MaximumStackTraceSize || top > UINTPTR_MAX-16 ||
        !t7_memory_writable(reinterpret_cast<void *>(address+0x48),8) ||
        !t7_memory_writable(reinterpret_cast<void *>(top-16),16)) return;
    size_t length = 0;
    const char *trace = object_to_string(state,static_cast<int64_t>(top-16),&length);
    if (!trace || !length || length > MaximumStackTraceSize || !t7_memory_readable(trace,length)) return;
    SymbolMap mod; StockSymbolMap stock; std::unordered_set<uint64_t> ambiguous;
    auto root = game_root();
    load_stock(root + "/BO3MacFix/t7patch.stockluadb",stock);
    if (stock.empty()) load_stock(root + "/t7patch.stockluadb",stock);
    const char *name = getenv("BO3MACFIX_LUA_MOD");
    if (name && *name && strnlen(name,64) < 64 && strspn(name,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == strlen(name)) {
        unsigned visited = 0; load_directory(root+"/mods/"+name,mod,ambiguous,0,visited);
    }
    size_t resolved = 0;
    auto rewritten = RewriteStackTrace(std::string_view(trace,length),mod,stock,resolved);
    if (!resolved || rewritten.empty() || rewritten.size() > MaximumStackTraceSize) return;
    (void)replace_vm_top(state, rewritten);
}
static void (*original_error)(const char *, int64_t *);
static std::mutex symbol_lock;
static void error_hook(const char *error, int64_t *state) {
    try {
        if (vm_layout_enabled) resolve_vm_error(state);
        if (error && strnlen(error, MaximumStackTraceSize+1) <= MaximumStackTraceSize) {
            std::lock_guard<std::mutex> lock(symbol_lock);
            SymbolMap mod; StockSymbolMap stock; std::unordered_set<uint64_t> ambiguous;
            auto root = game_root();
            load_stock(root + "/BO3MacFix/t7patch.stockluadb", stock);
            if (stock.empty()) load_stock(root + "/t7patch.stockluadb", stock);
            // Explicit active mod selection avoids borrowing Windows' global.
            const char *name = getenv("BO3MACFIX_LUA_MOD");
            if (name && *name && strnlen(name,64) < 64 && strspn(name,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == strlen(name)) {
                unsigned visited = 0;
                load_directory(root + "/mods/" + name, mod, ambiguous, 0, visited);
            }
            size_t resolved = 0;
            auto rewritten = RewriteStackTrace(error, mod, stock, resolved);
            if (resolved && !rewritten.empty() && rewritten.size() <= MaximumStackTraceSize) {
                t7_log("[BO3MacFix] Resolved %zu Lua error-text frames.\n", resolved);
                original_error(rewritten.c_str(), state); return;
            }
        }
    } catch (...) { t7_log("[BO3MacFix] Lua text resolver failed; original error retained.\n"); }
    original_error(error, state);
}
}
extern "C" char *t7_rewrite_trace(const char *trace, const char *mod_path, const char *stock_path) {
    try {
        if (!trace || strnlen(trace, MaximumStackTraceSize+1) > MaximumStackTraceSize) return nullptr;
        SymbolMap mod; StockSymbolMap stock; std::unordered_set<uint64_t> ambiguous;
        if (mod_path) load_mod(mod_path, mod, ambiguous);
        if (stock_path) load_stock(stock_path, stock);
        size_t resolved = 0;
        auto rewritten = RewriteStackTrace(trace, mod, stock, resolved);
        if (rewritten.size() > MaximumStackTraceSize) return nullptr;
        return strdup(rewritten.c_str());
    } catch (...) { return nullptr; }
}
extern "C" void t7_install_lua_symbols(void) {
    const char *enabled = getenv("BO3MACFIX_EXPERIMENTAL_HKS_LAYOUT");
    vm_layout_enabled = enabled && !strcmp(enabled,"1");
    if (vm_layout_enabled) {
        object_to_string = reinterpret_cast<ObjectToString>(t7_resolve_function("HksObjectToString"));
        push_string = reinterpret_cast<PushString>(t7_resolve_function("lua_pushlstring"));
        void *getinfo = t7_resolve_function("hksi_lua_getinfo");
        if (getinfo) t7_log("[BO3MacFix] Live HKS symbols: %s\n", !DobbyHook(getinfo,reinterpret_cast<void *>(getinfo_hook),reinterpret_cast<void **>(&original_getinfo)) ? "experimental active" : "failed");
        else t7_log("[BO3MacFix] Live HKS symbols: missing hksi_lua_getinfo.\n");
        t7_log("[BO3MacFix] HKS stack rewrite: %s\n", object_to_string && push_string ? "experimental ready" : "missing dependencies");
    } else t7_log("[BO3MacFix] Live HKS layout: opt-in disabled.\n");
    void *address = t7_resolve_function("LuaStateManager_Error");
    if (!address) address = t7_resolve_function("LuaStateManagerError");
    if (!address) { t7_log("[BO3MacFix] Lua error-text hook: missing symbol.\n"); return; }
    int result = DobbyHook(address, reinterpret_cast<void *>(error_hook), reinterpret_cast<void **>(&original_error));
    t7_log("[BO3MacFix] Lua error-text hook: %s\n", result == 0 ? "active" : "failed");
}
