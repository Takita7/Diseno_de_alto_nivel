# RiscvKernel.cmake – CMake helper for compiling RISC-V GPGPU kernel sources
#
# Provides:
#   add_riscv_kernel(<name>
#       SOURCE     <path/to/kernel.cu|.cpp|.c>
#       [ENTRY     <entry_symbol>]        # defaults to <name>
#       [FLAGS     <extra_clang_flags>…]  # appended to the compile step
#       [MARCH     <march>]               # default: rv32gc
#       [MABI      <mabi>]                # default: ilp32
#   )
#
#   Creates:
#     - Custom command that produces
#         ${CMAKE_BINARY_DIR}/kernels/<name>.elf
#     - Custom target  <name>_kernel  that triggers the build
#     - Appends the target to the global meta-target compile_kernels
#
# Meta-target:
#   A single  compile_kernels  target is created the first time this module
#   is included. Calling  cmake --build <dir> --target compile_kernels  will
#   build every kernel registered via add_riscv_kernel().
#
# Toolchain detection:
#   The module searches for  clang  on PATH and caches the result in
#   RISCV_CLANG.  Build steps are skipped with a warning if clang is absent.

cmake_minimum_required(VERSION 3.16)

# ── Locate clang ───────────────────────────────────────────────────────────────
if(NOT RISCV_CLANG)
    find_program(RISCV_CLANG clang
        HINTS
            /usr/bin
            /usr/local/bin
            $ENV{LLVM_DIR}/bin
        DOC "clang compiler used to build RISC-V kernels"
    )
endif()

if(RISCV_CLANG)
    message(STATUS "RISC-V kernel compiler: ${RISCV_CLANG}")
else()
    message(WARNING
        "RiscvKernel: clang not found; add_riscv_kernel() targets will be no-ops.\n"
        "Install clang or set RISCV_CLANG to the full path.")
endif()

# ── One-time meta-target ───────────────────────────────────────────────────────
if(NOT TARGET compile_kernels)
    add_custom_target(compile_kernels
        COMMENT "Build all RISC-V GPGPU kernel ELFs"
    )
endif()

# ── Function ───────────────────────────────────────────────────────────────────
function(add_riscv_kernel KERNEL_NAME)
    cmake_parse_arguments(
        ARG          # prefix
        "FPGA_LINK;OCM_LINK"  # options: DDR @ 0x60000000 or OCM @ 0xFFFC0000
        "SOURCE;ENTRY;MARCH;MABI"  # single-value keywords
        "FLAGS"      # multi-value keywords
        ${ARGN}
    )

    if(NOT ARG_SOURCE)
        message(FATAL_ERROR "add_riscv_kernel(${KERNEL_NAME}): SOURCE is required")
    endif()
    if(NOT ARG_ENTRY)
        set(ARG_ENTRY "${KERNEL_NAME}")
    endif()
    if(NOT ARG_MARCH)
        set(ARG_MARCH "rv32gc")
    endif()
    if(NOT ARG_MABI)
        set(ARG_MABI "ilp32")
    endif()

    # Output paths
    set(KERNEL_OUT_DIR "${CMAKE_BINARY_DIR}/kernels")
    set(OBJ  "${KERNEL_OUT_DIR}/${KERNEL_NAME}.o")
    set(ELF  "${KERNEL_OUT_DIR}/${KERNEL_NAME}.elf")

    # Make sure the output directory exists at configure time.
    file(MAKE_DIRECTORY "${KERNEL_OUT_DIR}")

    if(NOT RISCV_CLANG)
        # Clang absent – create a phony target so callers can depend on it
        # without breaking the build.
        add_custom_target(${KERNEL_NAME}_kernel
            COMMAND ${CMAKE_COMMAND} -E echo
                "[riscv_kernel] Skipped ${KERNEL_NAME}: clang not found"
            COMMENT "Skipping RISC-V kernel ${KERNEL_NAME} (clang not available)"
        )
        add_dependencies(compile_kernels ${KERNEL_NAME}_kernel)
        # Export the ELF path even if it won't be built, so parent scopes
        # can still reference it in test/install rules.
        set(${KERNEL_NAME}_ELF "${ELF}" PARENT_SCOPE)
        return()
    endif()

    # Macro definitions to strip CUDA decorators from .cu files
    set(CUDA_COMPAT_DEFS
        -D__global__=
        -D__device__=
        -D__host__=
        -D__shared__=
        -D__constant__=
        -D__restrict__=
    )

    # ── Compile step: source → object ─────────────────────────────────────────
    add_custom_command(
        OUTPUT  "${OBJ}"
        COMMAND "${RISCV_CLANG}"
                -target riscv32-unknown-elf
                -march=${ARG_MARCH}
                -mabi=${ARG_MABI}
                -O2 -fno-exceptions
                -x c++ -std=c++17
                ${CUDA_COMPAT_DEFS}
                ${ARG_FLAGS}
                -c -o "${OBJ}" "${ARG_SOURCE}"
        DEPENDS "${ARG_SOURCE}"
        COMMENT "Compiling RISC-V kernel ${KERNEL_NAME} (${ARG_SOURCE})"
        VERBATIM
    )

    # ── Link step: object → ELF ───────────────────────────────────────────────
    if(ARG_OCM_LINK)
        set(LINK_LD "${CMAKE_SOURCE_DIR}/fpga/linker/riscv_gpgpu_ocm.ld")
        set(LINK_EXTRA -Wl,-T,${LINK_LD})
    elseif(ARG_FPGA_LINK)
        set(LINK_LD "${CMAKE_SOURCE_DIR}/fpga/linker/riscv_gpgpu_fpga.ld")
        set(LINK_EXTRA -Wl,-T,${LINK_LD})
    else()
        set(LINK_EXTRA -Wl,--entry,0)
    endif()
    add_custom_command(
        OUTPUT  "${ELF}"
        COMMAND "${RISCV_CLANG}"
                -target riscv32-unknown-elf
                -march=${ARG_MARCH}
                -mabi=${ARG_MABI}
                -fuse-ld=lld -nostdlib
                ${LINK_EXTRA}
                -o "${ELF}" "${OBJ}"
        DEPENDS "${OBJ}"
        COMMENT "Linking RISC-V kernel ELF ${KERNEL_NAME}"
        VERBATIM
    )

    # ── Per-kernel target ─────────────────────────────────────────────────────
    add_custom_target(${KERNEL_NAME}_kernel
        DEPENDS "${ELF}"
        COMMENT "RISC-V kernel ${KERNEL_NAME} → ${ELF}"
    )

    # Register with meta-target
    add_dependencies(compile_kernels ${KERNEL_NAME}_kernel)

    # Export the ELF path to the parent scope so sibling CMakeLists can use it.
    set(${KERNEL_NAME}_ELF "${ELF}" PARENT_SCOPE)

    message(STATUS "  RISC-V kernel registered: ${KERNEL_NAME} → ${ELF}")
endfunction()
