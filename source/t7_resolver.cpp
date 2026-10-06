#include "t7_runtime.h"
extern "C" {
#include "utilities.h"
}
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/fat.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <arpa/inet.h>
#include <cxxabi.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <limits>

static bool memory_accessible(const void *pointer, size_t size, vm_prot_t protection) {
    uintptr_t current = reinterpret_cast<uintptr_t>(pointer);
    if (!current || size > UINTPTR_MAX - current) return false;
    const uintptr_t end = current + size;
    while (current < end) {
        mach_vm_address_t address = current;
        mach_vm_size_t region_size = 0;
        vm_region_basic_info_data_64_t info{};
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        kern_return_t result = mach_vm_region(mach_task_self(), &address, &region_size,
            VM_REGION_BASIC_INFO_64, reinterpret_cast<vm_region_info_t>(&info), &count, &object);
        if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
        if (result != KERN_SUCCESS || address > current || !region_size ||
            (info.protection & protection) != protection || region_size > UINTPTR_MAX - address) return false;
        current = address + region_size;
    }
    return true;
}

extern "C" bool t7_memory_readable(const void *pointer, size_t size) {
    return memory_accessible(pointer, size, VM_PROT_READ);
}
extern "C" bool t7_memory_writable(const void *pointer, size_t size) {
    return memory_accessible(pointer, size, VM_PROT_READ | VM_PROT_WRITE);
}

namespace {
struct Range { uint64_t first, last; };
struct Resolver {
    std::unordered_map<std::string, uintptr_t> symbols;
    std::vector<Range> executable;
    uint64_t base = 0;
    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    bool is_code(uint64_t address, size_t size = 1) const {
        for (auto r : executable)
            if (address >= r.first && address < r.last && size <= r.last - address) return true;
        return false;
    }
    void add(const std::string &name, uintptr_t value) {
        auto found = symbols.find(name);
        if (found != symbols.end() && found->second != value) found->second = 0;
        else if (found == symbols.end()) symbols.emplace(name, value);
    }
    Resolver() {
        const char *path = _dyld_get_image_name(0);
        if (!path) return;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        auto length = file.tellg();
        if (length < static_cast<std::streamoff>(sizeof(mach_header_64)) || length > 512 * 1024 * 1024) return;
        std::vector<uint8_t> bytes(static_cast<size_t>(length));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char *>(bytes.data()), length)) return;
        size_t slice = 0, slice_size = bytes.size();
        uint32_t magic; memcpy(&magic, bytes.data(), 4);
        if (magic == FAT_CIGAM) {
            fat_header fat; memcpy(&fat, bytes.data(), sizeof(fat));
            uint32_t count = ntohl(fat.nfat_arch);
            if (count > (bytes.size() - sizeof(fat)) / sizeof(fat_arch)) return;
            bool found = false;
            for (uint32_t i = 0; i < count; ++i) {
                fat_arch arch; memcpy(&arch, bytes.data() + sizeof(fat) + i * sizeof(arch), sizeof(arch));
                if (ntohl(arch.cputype) == CPU_TYPE_X86_64) {
                    slice = ntohl(arch.offset); slice_size = ntohl(arch.size); found = true; break;
                }
            }
            if (!found || slice > bytes.size() || slice_size > bytes.size() - slice) return;
        }
        if (slice_size < sizeof(mach_header_64)) return;
        const uint8_t *data = bytes.data() + slice;
        mach_header_64 header; memcpy(&header, data, sizeof(header));
        if (header.magic != MH_MAGIC_64 || header.cputype != CPU_TYPE_X86_64 || header.filetype != MH_EXECUTE ||
            header.sizeofcmds > slice_size - sizeof(header)) return;
        symtab_command symtab{};
        size_t cursor = sizeof(header), end = cursor + header.sizeofcmds;
        for (uint32_t i = 0; i < header.ncmds; ++i) {
            if (cursor > end || end - cursor < sizeof(load_command)) return;
            load_command cmd; memcpy(&cmd, data + cursor, sizeof(cmd));
            if (cmd.cmdsize < sizeof(cmd) || cmd.cmdsize > end - cursor) return;
            if (cmd.cmd == LC_SYMTAB && cmd.cmdsize >= sizeof(symtab)) memcpy(&symtab, data + cursor, sizeof(symtab));
            if (cmd.cmd == LC_SEGMENT_64 && cmd.cmdsize >= sizeof(segment_command_64)) {
                segment_command_64 segment; memcpy(&segment, data + cursor, sizeof(segment));
                if (!strncmp(segment.segname, "__TEXT", 16)) base = segment.vmaddr;
                if ((segment.initprot & VM_PROT_EXECUTE) && segment.vmsize <= UINT64_MAX - segment.vmaddr)
                    executable.push_back({segment.vmaddr, segment.vmaddr + segment.vmsize});
            }
            cursor += cmd.cmdsize;
        }
        if (!base) return;
        if (symtab.symoff <= slice_size && symtab.stroff <= slice_size &&
            symtab.strsize <= slice_size - symtab.stroff &&
            symtab.nsyms <= (slice_size - symtab.symoff) / sizeof(nlist_64)) {
            for (uint32_t i = 0; i < symtab.nsyms; ++i) {
                nlist_64 symbol; memcpy(&symbol, data + symtab.symoff + i * sizeof(symbol), sizeof(symbol));
                if ((symbol.n_type & N_STAB) || (symbol.n_type & N_TYPE) != N_SECT ||
                    !is_code(symbol.n_value) || symbol.n_un.n_strx >= symtab.strsize) continue;
                const char *raw = reinterpret_cast<const char *>(data + symtab.stroff + symbol.n_un.n_strx);
                if (!memchr(raw, 0, symtab.strsize - symbol.n_un.n_strx)) continue;
                if (*raw == '_') ++raw; // Mach-O symbol prefix, not C++ decoration.
                uintptr_t address = symbol.n_value + slide;
                add(raw, address);
                int status = 0;
                char *demangled = abi::__cxa_demangle(raw, nullptr, nullptr, &status);
                if (status == 0 && demangled) {
                    std::string name(demangled);
                    auto paren = name.find('(');
                    if (paren != std::string::npos) add(name.substr(0, paren), address);
                }
                free(demangled);
            }
        }
        // Optional analyst-supplied Mac map. Exact executable hash AND prologue
        // bytes are mandatory. There are deliberately no default Windows RVAs.
        const char *map_path = getenv("BO3MACFIX_SYMBOL_MAP");
        if (!map_path || !*map_path) return;
        std::ifstream map(map_path);
        std::string keyword, hash;
        if (!(map >> keyword >> hash) || keyword != "sha1" || hash.size() != 40) return;
        uint8_t digest[20]; char actual[41];
        if (!get_file_sha1(path, digest) || !bytes_to_hex(digest, 20, actual, sizeof(actual)) || hash != actual) {
            t7_log("[BO3MacFix] Rejected symbol map: executable hash mismatch.\n"); return;
        }
        std::string line; std::getline(map, line);
        while (std::getline(map, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream fields(line);
            std::string name, hex, extra; uint64_t rva;
            if (!(fields >> name >> std::hex >> rva >> hex) || (fields >> extra) ||
                hex.size() < 16 || hex.size() > 64 || hex.size() % 2 || rva > UINT64_MAX - base) continue;
            uint64_t address = base + rva;
            if (!is_code(address, hex.size() / 2)) continue;
            uint8_t expected[32]; bool valid = true;
            auto digit = [](char c) -> int { if (c >= '0' && c <= '9') return c-'0'; if(c >= 'a' && c <= 'f') return c-'a'+10; return -1; };
            for (size_t i = 0; i < hex.size(); i += 2) {
                int a = digit(hex[i]), b = digit(hex[i+1]);
                if (a < 0 || b < 0) { valid = false; break; }
                expected[i / 2] = (a << 4) | b;
            }
            const void *pointer = reinterpret_cast<void *>(address + slide);
            if (valid && t7_memory_readable(pointer, hex.size()/2) && !memcmp(pointer, expected, hex.size()/2))
                add(name, address + slide);
        }
    }
};
}
extern "C" void *t7_resolve_function(const char *name) {
    try {
        static Resolver resolver;
        auto found = resolver.symbols.find(name);
        return found == resolver.symbols.end() ? nullptr : reinterpret_cast<void *>(found->second);
    } catch (...) {
        t7_log("[BO3MacFix] Symbol resolution failed for %s.\n", name);
        return nullptr;
    }
}
