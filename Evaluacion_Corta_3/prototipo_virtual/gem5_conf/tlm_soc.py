"""
tlm_soc.py — Configuración final de gem5 para el prototipo virtual.

Arma un sistema ARM64 (bare-metal, sin Linux) con dos puertos externos
TLM 2.0 hacia el Accelerator (SystemC):
  - "regs": el CPU ARM64 llega a los registros del Accelerator
  - "dma":  el Accelerator llega a la RAM real de gem5

Ejecutar con: build/ARM/gem5.opt util/tlm/conf/tlm_soc.py
(el "fatal: Can't find port handler type" al final es esperado — este
comando solo genera m5out/config.ini, no corre la co-simulación real)
"""

import m5
from m5.objects import *

# --------------------------------------------------------------------------
# Parámetros — deben coincidir con guest/accel_test.c y con
# examples/accel_bridge/accelerator.cc (constante REGS_BASE)
# --------------------------------------------------------------------------
REGS_BASE = 0x30000000   # fuera del rango de RAM (512 MiB = 0x20000000), sin choques
REGS_SIZE = 0x100

# Ajustar a la ruta absoluta real de accel_test.elf en su máquina
KERNEL_PATH = "/home/sebastian/Diseno_de_alto_nivel/Evaluacion_Corta_2/guest/accel_test.elf"

# --------------------------------------------------------------------------
# Sistema ARM64
# --------------------------------------------------------------------------
system = ArmSystem()
system.highest_el_is_64 = True
system.release = ArmDefaultRelease()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
system.mem_ranges = [AddrRange("512MB")]

# --------------------------------------------------------------------------
# CPU — armado explícito (isa, decoder, mmu) porque no se usa la librería
# estándar de gem5, que normalmente crea estas piezas automáticamente.
# --------------------------------------------------------------------------
system.cpu = TimingSimpleCPU()
system.cpu.isa = [ArmISA()]
system.cpu.decoder = [ArmDecoder(isa=system.cpu.isa[0])]
system.cpu.mmu = ArmMMU()
system.cpu.mmu.release_se = system.release
system.cpu.createInterruptController()

system.membus = SystemXBar()
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports
system.system_port = system.membus.cpu_side_ports

system.mem_ctrl = SimpleMemory()
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# --------------------------------------------------------------------------
# Puerto externo #1: registros del Accelerator (gem5 CPU -> SystemC)
# port_type debe ser "tlm_slave" (no "tlm" — nombre real usado por gem5.sc)
# --------------------------------------------------------------------------
system.external_regs = ExternalSlave(
    port_type="tlm_slave",
    port_data="regs",
    port=system.membus.mem_side_ports,
    addr_ranges=[AddrRange(REGS_BASE, size=REGS_SIZE)],
)

# --------------------------------------------------------------------------
# Puerto externo #2: acceso del Accelerator a la RAM de gem5 (SystemC -> gem5)
# port_type debe ser "tlm_master"
# --------------------------------------------------------------------------
system.external_dma = ExternalMaster(
    port_type="tlm_master",
    port_data="dma",
    port=system.membus.cpu_side_ports,
)

# --------------------------------------------------------------------------
# Workload: nuestro binario bare-metal ARM64 (no Linux)
# --------------------------------------------------------------------------
system.workload = ArmFsWorkload()
system.workload.object_file = KERNEL_PATH

root = Root(full_system=True, system=system)
m5.instantiate()

print("Config generada en m5out/config.ini")
