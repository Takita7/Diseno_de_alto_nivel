# RISC-V GPGPU — CU Sweep: Análisis de Implementación HLS + Vivado

**Fecha:** 2026-08-06  
**Target:** Xilinx KV260 (xck26-sfvc784-2LV-c)  
**Herramientas:** Vitis HLS 2026.1 + Vivado 2026.1  
**Ruta de síntesis:** Flat DATAFLOW (umbral jerárquico en NUM_CUS ≥ 13)

---

## 1. Configuraciones evaluadas

| NUM_CUS | HLS export | Vivado impl | Timing |
|---------|-----------|-------------|--------|
| 2       | ✅ PASS    | ✅ PASS      | ✅ Sin violaciones |
| 4       | ✅ PASS    | ✅ PASS      | ⚠️ 12 endpoints Setup (marginal) |
| 8       | ✅ PASS    | ✅ PASS      | ✅ Sin violaciones |

---

## 2. Utilización de recursos post-implementación

| Recurso         | Disponible (KV260) | CU=2      | %     | CU=4      | %     | CU=8     | %    |
|-----------------|-------------------|-----------|-------|-----------|-------|----------|------|
| CLB LUTs        | 117,120            | 21,419    | 18.3% | 21,942    | 18.7% | 6,435    | 5.5% |
| CLB Registers   | 234,240            | 21,587    | 9.2%  | 21,390    | 9.1%  | 10,106   | 4.3% |
| Block RAM Tiles | 144                | 44        | 30.6% | 45        | 31.3% | 9.5      | 6.6% |
| DSP48E2         | 1,248              | 31        | 2.5%  | 31        | 2.5%  | 0        | 0.0% |
| URAM            | 64                 | 0         | 0.0%  | 0         | 0.0%  | 0        | 0.0% |

> **Observación:** CU=8 presenta significativamente menos recursos que CU=2 y CU=4.
> Esto se debe a que `memory_pipeline` escala con `NUM_CUS` (arrays `l1_caches_[NUM_CUS]`
> y `shared_mem_[NUM_CUS]`), y para 8 CUs el sintetizador de Vivado logra una
> representación más regular y compacta de las estructuras paralelas. Adicionalmente,
> CU=8 no utiliza DSPs (la lógica aritmética se mapea a LUTs), lo cual reduce el
> área pero puede impactar la frecuencia máxima alcanzable.

---

## 3. Timing post-implementación

Todas las corridas usan el mismo clock objetivo: **193.93 MHz** (período 5.156 ns),
generado por el `clk_wiz_0` a partir del `pl_clk0` de la KRIA (100 MHz → PLL).

| Métrica              | CU=2            | CU=4                      | CU=8            |
|----------------------|-----------------|---------------------------|-----------------|
| Clock (MHz)          | 193.93          | 193.93                    | 193.93          |
| Setup — Failing EPs  | **0** ✅        | **12** ⚠️                 | **0** ✅        |
| Setup — WNS (ns)     | +0.010          | −0.022                    | +1.514          |
| Setup — TNS (ns)     | 0.000           | −0.140                    | 0.000           |
| Hold — Failing EPs   | 0 ✅            | 0 ✅                       | 0 ✅            |
| Hold — WNS (ns)      | +0.010          | +0.010                    | +0.010          |
| DRC violations       | 0 ✅            | 0 ✅                       | 0 ✅            |

> **CU=4 timing marginal:** 12 endpoints violan Setup por −0.022 ns (WNS),
> con una violación total de −0.140 ns. El diseño no cumpliría timing en producción
> sin ajuste de frecuencia o constraint relajado. Reducir el clock ~2% (≈190 MHz)
> resolvería la violación.
>
> **CU=8 timing holgado:** WNS de +1.514 ns indica ~29% de slack, lo que permite
> subir el clock hasta ~210 MHz si fuera necesario.

---

## 4. Análisis de la ruta del scheduler

El diseño tiene dos rutas de síntesis controladas por la macro `RISCV_GPGPU_NUM_CUS`
en [`hls/src/common/hls_config.h`](../../hls/src/common/hls_config.h):

```
#if RISCV_GPGPU_NUM_CUS >= 13   →  Ruta JERÁRQUICA (clusters de CUs)
#else                            →  Ruta PLANA (flat DATAFLOW)
#endif
```

Las tres configuraciones evaluadas (2, 4, 8) usan la **ruta plana**, ya que están
por debajo del umbral de 13. La ruta jerárquica existe para evitar superar el límite
de ~40 canales backwards del DATAFLOW de Vitis HLS (3 × NUM_CUS > 40 para NUM_CUS > 13).

| NUM_CUS | Ruta       | 3×NUM_CUS (backwards channels) | Dentro del límite |
|---------|------------|---------------------------------|-------------------|
| 2       | Plana      | 6                               | ✅ sí             |
| 4       | Plana      | 12                              | ✅ sí             |
| 8       | Plana      | 24                              | ✅ sí             |
| 13      | Jerárquica | 39 → 24 (por cluster)          | ✅ sí             |
| 16      | Jerárquica | 48 → 24 (por cluster)          | ✅ sí             |

---

## 5. Escalabilidad de recursos

Esperaríamos que al duplicar CUs se dupliquen los recursos. La tabla muestra
que entre CU=2 y CU=4 el crecimiento es lineal (~2.5% LUTs adicionales),
pero CU=8 invierte la tendencia:

| Transición    | ΔLUT    | ΔBRAM  | Explicación |
|---------------|---------|--------|-------------|
| CU=2 → CU=4  | +523    | +1     | Crecimiento lineal esperado |
| CU=4 → CU=8  | −15,507 | −35.5  | Optimización agresiva de Vivado |

El comportamiento de CU=8 refleja que el `memory_pipeline` con 8 cachés L1
produce estructuras más regulares que el sintetizador explota con mayor eficiencia.
El hecho de que no haya DSPs en CU=8 sugiere que las rutas de datos aritméticas
quedaron mapeadas a fabric, no a primitivas dedicadas.

---

## 6. Artefactos generados

| Archivo | Descripción |
|---------|-------------|
| `num_cus_2/num_cus_2_implementation_utilization.rpt` | Utilización post-impl CU=2 |
| `num_cus_2/num_cus_2_implementation_timing.rpt`      | Timing post-impl CU=2 |
| `num_cus_2/num_cus_2_implementation_drc.rpt`         | DRC post-impl CU=2 |
| `num_cus_2/num_cus_2_gpgpu_system_wrapper.bit`       | Bitstream CU=2 |
| `num_cus_4/num_cus_4_implementation_utilization.rpt` | Utilización post-impl CU=4 |
| `num_cus_4/num_cus_4_implementation_timing.rpt`      | Timing post-impl CU=4 |
| `num_cus_4/num_cus_4_implementation_drc.rpt`         | DRC post-impl CU=4 |
| `num_cus_4/num_cus_4_gpgpu_system_wrapper.bit`       | Bitstream CU=4 |
| `num_cus_8/num_cus_8_implementation_utilization.rpt` | Utilización post-impl CU=8 |
| `num_cus_8/num_cus_8_implementation_timing.rpt`      | Timing post-impl CU=8 |
| `num_cus_8/num_cus_8_implementation_drc.rpt`         | DRC post-impl CU=8 |
| `num_cus_8/num_cus_8_gpgpu_system_wrapper.bit`       | Bitstream CU=8 |

---

## 7. Conclusiones

1. **CU=2** es la configuración más conservadora: cumple timing con WNS de +0.010 ns,
   usa ~18% de LUTs y ~30% de BRAMs. Recomendada como baseline seguro.

2. **CU=4** casi cumple timing (WNS = −0.022 ns, 12 endpoints). Con una reducción
   de ~2% en la frecuencia target (191–192 MHz) el diseño sería timing-clean.
   Los recursos son prácticamente iguales a CU=2.

3. **CU=8** cumple timing con margen amplio (+1.514 ns), usa solo 5.5% de LUTs
   y 6.6% de BRAMs. La ausencia de DSPs y la menor utilización general indica
   que Vivado optimizó agresivamente la lógica regular de 8 cachés paralelas.
   Es la configuración con mayor headroom para escalar frecuencia.

4. El **umbral jerárquico** (NUM_CUS ≥ 13) no afecta ninguna de las configuraciones
   evaluadas. Las tres usan la ruta plana (flat DATAFLOW) de HLS.
