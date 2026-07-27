// elf_loader.cpp - ELF32 loader implementation
//
// Parses ELF32 little-endian RISC-V binaries without external dependencies.
// Loads each PT_LOAD segment into the MemoryHierarchy byte store.
// Parses the SHT_SYMTAB section to expose function symbols.
//

#include "elf_loader.h"
#include "../src/memory/memory_hierarchy.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace riscv_gpgpu {

// ─── ELF32 structures (little-endian, no-padding expected) ────────────────────

#pragma pack(push, 1)

struct Elf32_Ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;   // Program header offset
    uint32_t e_shoff;   // Section header offset
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32_Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

struct Elf32_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
};

struct Elf32_Sym {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
};

#pragma pack(pop)

// ELF constants
static constexpr uint32_t PT_LOAD   = 1;
static constexpr uint32_t SHT_SYMTAB = 2;
static constexpr uint32_t SHT_STRTAB = 3;
static constexpr uint8_t  STT_FUNC  = 2;
static constexpr uint8_t  STT_OBJECT = 1;
static constexpr uint8_t  ELF_ST_TYPE(uint8_t i) { return i & 0xF; }

// ─── ElfLoader::load ──────────────────────────────────────────────────────────

bool ElfLoader::load(const std::string& path, MemoryHierarchy& mem) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "[elf_loader] Cannot open: " << path << "\n";
        return false;
    }

    // Read the whole file
    f.seekg(0, std::ios::end);
    size_t file_size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> buf(file_size);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(file_size));
    if (!f) {
        std::cerr << "[elf_loader] Read error: " << path << "\n";
        return false;
    }

    // Validate ELF magic
    if (file_size < sizeof(Elf32_Ehdr) ||
        buf[0] != 0x7F || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') {
        std::cerr << "[elf_loader] Not a valid ELF file: " << path << "\n";
        return false;
    }

    // ELF32 only
    if (buf[4] != 1 /* ELFCLASS32 */) {
        std::cerr << "[elf_loader] Only ELF32 supported\n";
        return false;
    }

    Elf32_Ehdr ehdr;
    std::memcpy(&ehdr, buf.data(), sizeof(ehdr));

    entry_point_ = ehdr.e_entry;
    text_base_   = 0xFFFFFFFFu;
    text_end_    = 0;

    std::cout << "[elf_loader] Loading " << path << "  e_entry=0x"
              << std::hex << entry_point_ << std::dec
              << "  phnum=" << ehdr.e_phnum
              << "  shnum=" << ehdr.e_shnum << "\n";

    // ── Load PT_LOAD segments ────────────────────────────────────────────────
    for (uint16_t i = 0; i < ehdr.e_phnum; ++i) {
        size_t off = ehdr.e_phoff + i * ehdr.e_phentsize;
        if (off + sizeof(Elf32_Phdr) > file_size) break;

        Elf32_Phdr phdr;
        std::memcpy(&phdr, buf.data() + off, sizeof(phdr));

        if (phdr.p_type != PT_LOAD || phdr.p_memsz == 0) continue;

        uint32_t vaddr = phdr.p_vaddr;
        uint32_t fsz   = phdr.p_filesz;
        uint32_t msz   = phdr.p_memsz;

        // Load file bytes into memory
        if (fsz > 0) {
            size_t seg_off = phdr.p_offset;
            if (seg_off + fsz <= file_size) {
                mem.writeBytes(vaddr, buf.data() + seg_off, fsz);
            }
        }
        // BSS: zero remaining bytes
        for (uint32_t b = fsz; b < msz; ++b) {
            uint8_t zero = 0;
            mem.writeBytes(vaddr + b, &zero, 1);
        }

        // Track text bounds (executable segment, flags & PF_X)
        if (phdr.p_flags & 1 /* PF_X */) {
            if (vaddr < text_base_) text_base_ = vaddr;
            if (vaddr + msz > text_end_) text_end_ = vaddr + msz;
        }

        std::cout << "[elf_loader]   PT_LOAD vaddr=0x" << std::hex << vaddr
                  << " size=" << std::dec << msz << " bytes\n";
    }

    // ── Parse symbol table ────────────────────────────────────────────────────
    // Find SHT_STRTAB for symbol names (different from section name strtab)
    if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0) return true;

    // Build section header array
    std::vector<Elf32_Shdr> shdrs(ehdr.e_shnum);
    size_t sh_off = ehdr.e_shoff;
    for (uint16_t i = 0; i < ehdr.e_shnum; ++i) {
        size_t o = sh_off + i * ehdr.e_shentsize;
        if (o + sizeof(Elf32_Shdr) > file_size) break;
        std::memcpy(&shdrs[i], buf.data() + o, sizeof(Elf32_Shdr));
    }

    for (uint16_t i = 0; i < ehdr.e_shnum; ++i) {
        if (shdrs[i].sh_type != SHT_SYMTAB) continue;

        uint32_t sym_off  = shdrs[i].sh_offset;
        uint32_t sym_size = shdrs[i].sh_size;
        uint32_t sym_ent  = shdrs[i].sh_entsize;
        if (sym_ent == 0) sym_ent = sizeof(Elf32_Sym);

        uint32_t strtab_idx = shdrs[i].sh_link;
        if (strtab_idx >= ehdr.e_shnum) continue;

        const char* strtab = reinterpret_cast<const char*>(buf.data()) +
                             shdrs[strtab_idx].sh_offset;
        size_t strtab_end  = shdrs[strtab_idx].sh_offset + shdrs[strtab_idx].sh_size;

        for (uint32_t s = 0; s + sym_ent <= sym_size; s += sym_ent) {
            Elf32_Sym sym;
            std::memcpy(&sym, buf.data() + sym_off + s, sizeof(sym));

            uint8_t typ = sym.st_info & 0xF;
            if (typ != STT_FUNC && typ != STT_OBJECT) continue;
            if (sym.st_value == 0) continue;

            size_t name_off = shdrs[strtab_idx].sh_offset + sym.st_name;
            if (name_off >= strtab_end) continue;

            std::string sym_name(strtab + sym.st_name);
            if (sym_name.empty()) continue;

            SymbolEntry e;
            e.name    = sym_name;
            e.address = sym.st_value;
            e.size    = sym.st_size;
            symbols_[sym_name] = e;
        }
    }

    std::cout << "[elf_loader] Loaded " << symbols_.size() << " symbols\n";
    return true;
}

// ─── Symbol lookup ────────────────────────────────────────────────────────────

bool ElfLoader::findSymbol(const std::string& partial, SymbolEntry& out) const {
    for (const auto& kv : symbols_) {
        if (kv.first.find(partial) != std::string::npos) {
            out = kv.second;
            return true;
        }
    }
    return false;
}

} // namespace riscv_gpgpu
