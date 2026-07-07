/*
 * accel_test.c
 *
 * Programa bare-metal AArch64 (sin sistema operativo) que corre dentro de
 * gem5 y actúa como "driver" del Accelerator: configura sus registros,
 * dispara el procesamiento y espera a que termine.
 *
 * Mapa de registros IDÉNTICO al de accelerator.h (EC anterior); si cambian
 * algo allá, cambien también acá.
 */

#include <stdint.h>

/* ---- Debe coincidir con gem5/tlm_soc.py (system.external_regs) -------- */
#define REGS_BASE        0x30000000u

#define REG_INPUT_ADDR   (*(volatile uint32_t *)(REGS_BASE + 0x00))
#define REG_OUTPUT_ADDR  (*(volatile uint32_t *)(REGS_BASE + 0x04))
#define REG_NUM_PIXELS   (*(volatile uint32_t *)(REGS_BASE + 0x08))
#define REG_CONTROL      (*(volatile uint32_t *)(REGS_BASE + 0x0C))
#define REG_STATUS       (*(volatile uint32_t *)(REGS_BASE + 0x10))

#define CONTROL_START    0x1u

#define STATUS_IDLE      0u
#define STATUS_BUSY      1u
#define STATUS_DONE      2u
#define STATUS_ERROR     3u

/* ---- Debe coincidir con el mapa de memoria y con guest/linker.ld ------- */
#define IMG_INPUT_ADDR   0x01000000u   /* imagen RGB, embebida vía .incbin */
#define IMG_OUTPUT_ADDR  0x02000000u   /* región destino en escala de grises */

#define IMG_WIDTH        1920u
#define IMG_HEIGHT       1080u
#define NUM_PIXELS       (IMG_WIDTH * IMG_HEIGHT)
#define OUTPUT_BYTES     NUM_PIXELS  /* 1 byte por pixel en escala de grises */

/*
 * m5_write_file: vuelca una región de memoria del guest a un archivo en el
 * host. Es el mecanismo estándar de gem5 (util/m5) para "escribir a disco"
 * desde un binario bare-metal sin sistema de archivos simulado.
 *
 * IMPORTANTE: verificar esta firma contra util/m5/README y
 * include/gem5/m5ops.h de su checkout de gem5 (puede variar ligeramente
 * entre versiones); y linkear contra la libm5 compilada para AArch64
 * bare-metal (util/m5, "scons build/arm64/out/m5" o el target equivalente).
 */
extern void m5_write_file(void *buffer, uint64_t len, uint64_t offset,
                          const char *filename);

/* Espera ocupada simple (sin temporizador real disponible en bare-metal). */
static void busy_wait(volatile uint32_t n)
{
    while (n--) {
        __asm__ volatile("nop");
    }
}

void _c_entry(void)
{
    /* 1. Configurar el acelerador: misma secuencia que el CPU de la EC
     *    anterior (ver cpu.h), solo que ahora es un programa en C real
     *    corriendo sobre un core ARM64 simulado, no un hilo SC_THREAD. */
    REG_INPUT_ADDR  = IMG_INPUT_ADDR;
    REG_OUTPUT_ADDR = IMG_OUTPUT_ADDR;
    REG_NUM_PIXELS  = NUM_PIXELS;

    /* 2. Disparar el procesamiento */
    REG_CONTROL = CONTROL_START;

    /* 3. Poll de REG_STATUS hasta DONE o ERROR */
    uint32_t status;
    do {
        busy_wait(1000);
        status = REG_STATUS;
    } while (status != STATUS_DONE && status != STATUS_ERROR);

    if (status == STATUS_ERROR) {
        /* Sin UART configurada en este esqueleto: quedarse aquí permite
         * inspeccionar el estado con el depurador de gem5 (m5 debug). */
        while (1) { }
    }

    /* 4. Volcar el resultado a un archivo en el host, como "guardar en
     *    disco" (paso 6 del flujo original). */
    m5_write_file((void *)(uintptr_t)IMG_OUTPUT_ADDR, OUTPUT_BYTES, 0,
                 "output.raw");

    /* 5. Fin del programa: loop infinito (o m5_exit() si está disponible
     *    en su libm5, para terminar la simulación automáticamente). */
    while (1) { }
}
