/**
 * @file
 * harness_top.cc — Punto de entrada SystemC del prototipo virtual.
 *
 * Conecta el Accelerator (SystemC/TLM 2.0, sin cambios respecto a la EC
 * anterior salvo la adaptación de decodificación de direcciones en
 * accelerator.cc) a los dos puentes oficiales de gem5 (util/tlm):
 *   - Gem5SlaveTransactor: el CPU ARM64 de gem5 llega a los registros
 *   - Gem5MasterTransactor: el Accelerator llega a la RAM real de gem5
 *
 * Calcado de examples/slave_port/main.cc (oficial de gem5/util/tlm).
 */
#include <systemc>
#include <tlm>
#include "cli_parser.hh"
#include "report_handler.hh"
#include "sim_control.hh"
#include "slave_transactor.hh"
#include "master_transactor.hh"
#include "stats.hh"
#include "accelerator.h"

using namespace Gem5SystemC;

int
sc_main(int argc, char **argv)
{
    CliParser parser;
    parser.parse(argc, argv);
    sc_core::sc_report_handler::set_handler(reportHandler);

    Gem5SimControl sim_control("gem5",
                                parser.getConfigFile(),
                                parser.getSimulationEnd(),
                                parser.getDebugFlags());

    Gem5SlaveTransactor regs_bridge("regs_bridge", "regs");
    Gem5MasterTransactor dma_bridge("dma_bridge", "dma");

    Accelerator accelerator("accelerator");

    regs_bridge.socket.bind(accelerator.cfg_socket);
    accelerator.mem_socket.bind(dma_bridge.socket);

    regs_bridge.sim_control.bind(sim_control);
    dma_bridge.sim_control.bind(sim_control);

    SC_REPORT_INFO("sc_main", "Start of Simulation");
    sc_core::sc_start();
    SC_REPORT_INFO("sc_main", "End of Simulation");

    CxxConfig::statsDump();
    return EXIT_SUCCESS;
}
