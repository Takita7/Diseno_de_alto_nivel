// fpga_rodinia_bench.cpp - FPGA hardware benchmark for RISC-V GPGPU (no GTest)
//
// Memory strategy (tried in order, first success wins):
//   1. Zynq OCM @ 0xFFFC0000 (256 KB PS SRAM) — NO memmap needed.
//      FPGA AXI masters on S_AXI_HPC0_FPD can address OCM.
//      Use kernels built with OCM_LINK (rodinia_bfs_kernel_ocm.elf).
//   2. PS DDR  @ 0x60000000 (64 MiB reserved) — requires ONE-TIME:
//      bash scripts/setup_kria_fpga_mem.sh && sudo reboot
//      Use kernels built with FPGA_LINK (rodinia_bfs_kernel_fpga.elf).
//
// Environment variables:
//   GPGPU_KERNEL_ELF   path to RISC-V ELF (OCM or FPGA linked)
//   GPGPU_TOTAL_WARPS  warps to launch (default 1, max 28 for OCM)
//   GPGPU_TIMEOUT_MS   kernel timeout in ms (default 5000)

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <chrono>
#include <thread>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

// ── Memory regions (tried in order) ─────────────────────────────────────────
struct MemRegion {
    const char* name;
    uint64_t phys;   // start of mmap region
    size_t   len;    // mmap length
    uint32_t max_warps;
    // Offsets within the mmap'd region:
    size_t elf_off;    // ELF PT_LOAD goes here
    size_t regs0_off;  // initial_regs_ptr0 (ptr0 == ptr1: one allocation)
};

// OCM: lower 128 KB of OCM (bank 0-1), safe on KV260 (firmware at top).
// ELF linked at 0xFFFC0000 → LUI encodes 0xFFFC0xxx → hits OCM directly.
static constexpr MemRegion kOCM = {
    "Zynq OCM (no memmap)", 0xFFFC0000ULL, 128*1024, 28,
    /*elf_off=*/0,
    /*regs0_off=*/0x4000   // 16 KB past ELF area
};
// DDR: reserved by memmap=64M$0x60000000. ELF linked at 0x60000000.
static constexpr MemRegion kDDR = {
    "PS DDR (memmap=64M$0x60000000)", 0x60000000ULL, 64*1024*1024, 256,
    /*elf_off=*/0,
    /*regs0_off=*/0x100000
};
static const MemRegion kRegions[] = { kOCM, kDDR };

// ── Physical addresses ───────────────────────────────────────────────────────
static constexpr uint64_t kSchedCtrl  = 0xA0000000ULL;
static constexpr uint64_t kSchedCtrlR = 0xA0010000ULL;
static constexpr uint32_t kRegPageSz  = 0x1000;

// ── HLS register offsets ─────────────────────────────────────────────────────
static constexpr uint32_t R_PROGRAM_LEN = 0x10;
static constexpr uint32_t R_TOTAL_WARPS = 0x18;
static constexpr uint32_t R_WARP_OFFSET = 0x20;
static constexpr uint32_t R_START_R     = 0x28;
static constexpr uint32_t R_BUSY        = 0x30;
static constexpr uint32_t R_DONE        = 0x40;
static constexpr uint32_t R_FAULT       = 0x50;
static constexpr uint32_t RP_PROG_LO    = 0x10;
static constexpr uint32_t RP_PROG_HI    = 0x14;
static constexpr uint32_t RP_REGS0_LO   = 0x1c;
static constexpr uint32_t RP_REGS0_HI   = 0x20;
static constexpr uint32_t RP_REGS1_LO   = 0x28;
static constexpr uint32_t RP_REGS1_HI   = 0x2c;

static constexpr int      MAX_THREADS   = 32;
static constexpr int      NUM_REGS      = 32;
static constexpr size_t   REGS_PER_WARP = MAX_THREADS * NUM_REGS * sizeof(uint32_t);

static volatile uint32_t* ctrl  = nullptr;
static volatile uint32_t* ctrlr = nullptr;

static uint32_t rd(volatile uint32_t* b, uint32_t off)          { return b[off/4]; }
static void     wr(volatile uint32_t* b, uint32_t off, uint32_t v) { b[off/4] = v; }
static void     wr64(volatile uint32_t* b, uint32_t off, uint64_t v) {
    b[off/4]     = (uint32_t)(v & 0xFFFFFFFF);
    b[off/4 + 1] = (uint32_t)(v >> 32);
}

// ── ELF32 loader ─────────────────────────────────────────────────────────────
struct Elf32Hdr {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
};
struct Elf32Phdr {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
};
static constexpr uint32_t PT_LOAD = 1;

struct KernelInfo {
    uint64_t text_phys;   // physical address of first instruction
    uint32_t program_len; // instruction word count
};

static bool loadElf(const char* path, const MemRegion& region,
                    volatile uint8_t* mem, KernelInfo& info) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "  cannot open ELF: %s\n", strerror(errno)); return false; }

    Elf32Hdr hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        hdr.e_ident[0] != 0x7f || hdr.e_ident[1] != 'E') {
        fclose(f); fprintf(stderr, "  not a valid ELF\n"); return false;
    }
    if (hdr.e_machine != 0xF3) {
        fclose(f); fprintf(stderr, "  not RISC-V ELF (machine=0x%x)\n", hdr.e_machine); return false;
    }

    uint64_t text_start = UINT64_MAX;
    size_t   text_words = 0;

    for (int i = 0; i < hdr.e_phnum; ++i) {
        Elf32Phdr ph;
        fseek(f, hdr.e_phoff + i * hdr.e_phentsize, SEEK_SET);
        if (fread(&ph, 1, sizeof(ph), f) != sizeof(ph)) continue;
        if (ph.p_type != PT_LOAD || ph.p_filesz == 0) continue;

        uint64_t vbase = region.phys + region.elf_off;
        uint64_t vlim  = vbase + 0x4000;  // 16 KB ELF area
        if (ph.p_vaddr < vbase || (uint64_t)ph.p_vaddr + ph.p_filesz > vlim) {
            fprintf(stderr, "  PT_LOAD vaddr 0x%08x outside region [0x%llx..0x%llx)\n",
                    ph.p_vaddr, (unsigned long long)vbase, (unsigned long long)vlim);
            fprintf(stderr, "  Use the matching _ocm or _fpga kernel variant\n");
            fclose(f); return false;
        }

        // Read into a normal (non-device) heap buffer first to avoid NEON on mmap'd mem.
        uint8_t* tmp = (uint8_t*)malloc(ph.p_filesz);
        if (!tmp) { fclose(f); fprintf(stderr, "  OOM\n"); return false; }
        fseek(f, ph.p_offset, SEEK_SET);
        if (fread(tmp, 1, ph.p_filesz, f) != ph.p_filesz) {
            free(tmp); fclose(f); fprintf(stderr, "  read error\n"); return false;
        }

        // Copy word-by-word via volatile pointer (avoids SIMD/NEON on device-mapped memory).
        size_t off = ph.p_vaddr - region.phys;
        volatile uint32_t* dst = (volatile uint32_t*)(mem + off);
        const uint32_t*    src = (const uint32_t*)tmp;
        for (size_t w = 0; w < (ph.p_filesz + 3) / 4; ++w) dst[w] = src[w];
        free(tmp);

        // Zero BSS word-by-word.
        if (ph.p_memsz > ph.p_filesz) {
            volatile uint32_t* bss = (volatile uint32_t*)(mem + off + ph.p_filesz);
            for (size_t w = 0; w < (ph.p_memsz - ph.p_filesz + 3) / 4; ++w) bss[w] = 0;
        }

        if ((ph.p_flags & 1) && ph.p_vaddr < text_start) {
            text_start = ph.p_vaddr;
            text_words = (ph.p_memsz + 3) / 4;
        }
        printf("    PT_LOAD vaddr=0x%08x filesz=%u -> region+0x%zx\n",
               ph.p_vaddr, ph.p_filesz, off);
    }
    fclose(f);

    if (text_start == UINT64_MAX) { fprintf(stderr, "  no executable PT_LOAD\n"); return false; }
    info.text_phys   = text_start;
    info.program_len = (uint32_t)text_words;
    return true;
}

static void setupRegs(volatile uint8_t* base, uint32_t total_warps) {
    for (uint32_t w = 0; w < total_warps; ++w) {
        // Word-by-word via volatile: avoids NEON/memset on device-mapped OCM.
        volatile uint32_t* rf = (volatile uint32_t*)(base + w * REGS_PER_WARP);
        for (int r = 0; r < MAX_THREADS * NUM_REGS; ++r) rf[r] = 0;
        rf[10] = w;            // a0 = warp_id
        rf[11] = total_warps;  // a1 = total_warps
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    const char* elf_path    = getenv("GPGPU_KERNEL_ELF");
    uint32_t    total_warps = (uint32_t)atoi(getenv("GPGPU_TOTAL_WARPS") ? : "1");
    uint32_t    timeout_ms  = (uint32_t)atoi(getenv("GPGPU_TIMEOUT_MS")  ? : "5000");

    if (!elf_path) { fprintf(stderr, "ERROR: GPGPU_KERNEL_ELF not set\n"); return 1; }
    if (total_warps == 0) { fprintf(stderr, "ERROR: GPGPU_TOTAL_WARPS must be >= 1\n"); return 1; }

    printf("GPGPU FPGA benchmark\n");
    printf("  kernel:      %s\n", elf_path);
    printf("  total_warps: %u\n", total_warps);
    printf("  timeout:     %u ms\n", timeout_ms);

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { fprintf(stderr, "open /dev/mem: %s\n", strerror(errno)); return 1; }

    // ── Map AXI control registers ─────────────────────────────────────────────
    void* mc = mmap(nullptr, kRegPageSz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off_t)kSchedCtrl);
    void* mr = mmap(nullptr, kRegPageSz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off_t)kSchedCtrlR);
    if (mc == MAP_FAILED || mr == MAP_FAILED) {
        fprintf(stderr, "mmap AXI regs: %s (bitstream loaded?)\n", strerror(errno));
        close(fd); return 1;
    }
    ctrl  = (volatile uint32_t*)mc;
    ctrlr = (volatile uint32_t*)mr;

    // ── Find accessible memory region ─────────────────────────────────────────
    void* mem = MAP_FAILED;
    const MemRegion* region = nullptr;
    for (auto& r : kRegions) {
        void* m = mmap(nullptr, r.len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off_t)r.phys);
        if (m != MAP_FAILED) { mem = m; region = &r; break; }
        printf("  [SKIP] %s: %s\n", r.name, strerror(errno));
    }
    if (mem == MAP_FAILED) {
        fprintf(stderr, "ERROR: no accessible memory region\n");
        fprintf(stderr, "  For OCM: ensure kernel ELF is built with OCM_LINK\n");
        fprintf(stderr, "  For DDR: bash scripts/setup_kria_fpga_mem.sh && reboot\n");
        close(fd); return 1;
    }
    printf("  [OK] memory: %s\n", region->name);

    if (total_warps > region->max_warps) {
        fprintf(stderr, "ERROR: total_warps=%u exceeds region max %u\n",
                total_warps, region->max_warps);
        munmap(mem, region->len); close(fd); return 1;
    }

    volatile uint8_t* ddr = (volatile uint8_t*)mem;

    // ── Load ELF ─────────────────────────────────────────────────────────────
    printf("Loading ELF...\n");
    KernelInfo ki{};
    if (!loadElf(elf_path, *region, ddr, ki)) {
        munmap(mem, region->len); close(fd); return 1;
    }
    printf("  text_phys=0x%llx  program_len=%u words\n",
           (unsigned long long)ki.text_phys, ki.program_len);

    // ── Initial register files ────────────────────────────────────────────────
    printf("Setting up initial regs (%u warps x 4 KiB)...\n", total_warps);
    size_t regs_needed = total_warps * REGS_PER_WARP;
    size_t regs_avail  = region->len - region->regs0_off;
    if (regs_needed > regs_avail) {
        fprintf(stderr, "ERROR: regs need %zu bytes, only %zu available in region\n",
                regs_needed, regs_avail);
        munmap(mem, region->len); close(fd); return 1;
    }
    volatile uint8_t* regs_ptr = ddr + region->regs0_off;
    setupRegs(regs_ptr, total_warps);
    uint64_t regs0_phys = region->phys + region->regs0_off;
    uint64_t regs1_phys = regs0_phys;  // both clusters share the same initial regs

    // ── Configure HLS ─────────────────────────────────────────────────────────
    wr64(ctrlr, RP_PROG_LO,  ki.text_phys);
    wr64(ctrlr, RP_REGS0_LO, regs0_phys);
    wr64(ctrlr, RP_REGS1_LO, regs1_phys);
    wr(ctrl, R_PROGRAM_LEN, ki.program_len);
    wr(ctrl, R_TOTAL_WARPS, total_warps);
    wr(ctrl, R_WARP_OFFSET, 0);

    printf("  program_ptr=0x%llx  regs0=regs1=0x%llx\n",
           (unsigned long long)ki.text_phys, (unsigned long long)regs0_phys);

    // ── Start and time ────────────────────────────────────────────────────────
    printf("Starting kernel...\n");
    auto t0 = std::chrono::steady_clock::now();
    wr(ctrl, R_START_R, 1);

    bool done = false, fault = false;
    while (true) {
        if (rd(ctrl, R_FAULT) & 1) { fault = true; break; }
        if (rd(ctrl, R_DONE)  & 1) { done  = true; break; }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        if (ms > timeout_ms) break;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    long elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0).count();

    printf("elapsed_ms=%ld  done=%d  fault=%d  timeout=%d\n",
           elapsed_ms, (int)done, (int)fault, (int)(!done && !fault));

    munmap(mem, region->len);
    munmap(mr, kRegPageSz);
    munmap(mc, kRegPageSz);
    close(fd);

    if (!done || fault) {
        fprintf(stderr, "FAIL: kernel did not complete successfully\n");
        return 1;
    }
    printf("PASS: kernel_elapsed_ms=%ld warps=%u region=%s\n",
           elapsed_ms, total_warps, region->name);
    return 0;
}
