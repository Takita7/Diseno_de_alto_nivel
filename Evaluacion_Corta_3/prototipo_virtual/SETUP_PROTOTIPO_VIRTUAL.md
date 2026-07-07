# Prototipo Virtual — Guía de instalación y ejecución

> Pasos necesarios para reproducir la co-simulación gem5 (ARM64) + puente
> TLM 2.0 + `Accelerator` (SystemC), verificada byte a byte contra la
> salida del sistema SystemC puro de la EC anterior.

---

## 1. Dependencias del sistema

```bash
sudo apt update
sudo apt install build-essential scons python3-dev git cmake gdb \
                  gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
                  g++-aarch64-linux-gnu \
                  libboost-all-dev libhdf5-dev
```

## 2. Compilar gem5 (build ARM)

```bash
git clone https://github.com/gem5/gem5.git
cd gem5
scons build/ARM/gem5.opt -j2          # -j2 para evitar quedarse sin RAM
```

> Validación opcional: `build/ARM/gem5.opt configs/learning_gem5/part1/simple-arm.py`
> debe imprimir `Hello world!`.

## 3. Compilar gem5 como librería (para el puente con SystemC)

```bash
scons setconfig build/ARM USE_SYSTEMC=n
scons --with-cxx-config --without-python --without-tcmalloc \
      --duplicate-sources build/ARM/libgem5_opt.so -j2
```

## 4. Instalar SystemC 2.3.4

```bash
git clone https://github.com/accellera-official/systemc.git
cd systemc && git checkout tags/2.3.4
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local/systemc \
         -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON
make -j2
sudo make install
sudo ln -s /usr/local/systemc/lib /usr/local/systemc/lib-linux64
```

```bash
echo 'export SYSTEMC_HOME=/usr/local/systemc' >> ~/.zshrc
echo 'export LD_LIBRARY_PATH=/usr/local/systemc/lib:$LD_LIBRARY_PATH' >> ~/.zshrc
source ~/.zshrc
```

## 5. Compilar `libm5.a` (para `m5_write_file` desde el binario bare-metal)

```bash
cd ~/gem5/util/m5
scons arm64.CROSS_COMPILE=aarch64-linux-gnu- build/arm64/out/m5
```

## 6. Compilar el puente oficial gem5↔SystemC (`util/tlm`)

```bash
cd ~/gem5/util/tlm
scons build/examples/slave_port/gem5.sc -j2
```

> Nota: `examples/common/cli_parser.hh` de esta versión de gem5 requiere
> `#include <cstdint>` (falta en el original). Si el build falla con
> `'uint64_t' does not name a type`, agregarlo:
> ```bash
> sed -i '1i #include <cstdint>' examples/common/cli_parser.hh
> ```

## 7. Colocar el módulo del puente propio (`accel_bridge`)

Copiar los archivos de este entregable a `~/gem5/util/tlm/examples/accel_bridge/`:
- `harness_top.cc`
- `accelerator.cc`
- `accelerator.h`
- `SConscript`

Registrar la carpeta en `~/gem5/util/tlm/SConstruct`, agregando (junto a los
demás bloques `ex_* = SConscript(...)`):

```python
ex_accel = SConscript('examples/accel_bridge/SConscript',
                      variant_dir='build/examples/accel_bridge',
                      exports=['env', 'deps'], duplicate=False)
```

Compilar:

```bash
cd ~/gem5/util/tlm
scons build/examples/accel_bridge/gem5.sc -j2
```

## 8. Compilar el programa bare-metal ARM64 (`guest/accel_test.c`)

Copiar `accel_test.c`, `startup.S`, `linker.ld`, `Makefile` a
`Evaluacion_Corta_2/guest/`, ajustando en el `Makefile` las rutas
`M5_LIB`/`M5_INC` a donde quedó `libm5.a` (paso 5) e `include/` de gem5.

```bash
cd Evaluacion_Corta_2/guest
make
```

## 9. Generar la configuración de gem5 y correr la co-simulación

Copiar `tlm_soc.py` a `~/gem5/util/tlm/conf/tlm_soc.py`, ajustando
`KERNEL_PATH` a la ruta absoluta de `accel_test.elf` generado en el paso 8.

```bash
cd ~/gem5
build/ARM/gem5.opt util/tlm/conf/tlm_soc.py
```

> El mensaje `fatal: Can't find port handler type 'tlm_master'` al final es
> **esperado** — este paso solo genera `m5out/config.ini`; no ejecuta la
> co-simulación real.

```bash
cd ~/gem5/util/tlm
export LD_LIBRARY_PATH=$HOME/gem5/util/tlm/build/systemc:$LD_LIBRARY_PATH
./build/examples/accel_bridge/gem5.sc ~/gem5/m5out/config.ini
```

Salida esperada (resumida):

```
0 s (=) : Accelerator Módulo Accelerator creado
708 ns (<) : Accelerator Inicio procesamiento | input_base=0x1000000 ...
871457 ns (<) : Accelerator Procesamiento terminado | pixels=2073600 ...
```

El programa bare-metal termina en un bucle infinito intencional tras
guardar el resultado (`m5_write_file`) — interrumpir con `Ctrl+C` una vez
que aparezca "Procesamiento terminado".

## 10. Verificación del resultado

```bash
cd Evaluacion_Corta_2/test
make run SYSTEMC_HOME=$SYSTEMC_HOME      # genera la referencia (SystemC puro)
cmp images/output.raw <ruta_del_output.raw_generado_por_gem5>
```

`cmp` sin salida = archivos idénticos byte a byte = resultado verificado.

## 11. Automatizar (una vez hecha la instalación de los pasos 1-7)

Los pasos 8-10 (recompilar el bare-metal, generar `config.ini`, correr la
co-simulación, y verificar el resultado) están automatizados en un solo
comando:

```bash
GEM5_DIR=$HOME/gem5 \
REPO_DIR=$HOME/Diseno_de_alto_nivel/Evaluacion_Corta_2 \
SYSTEMC_HOME=/usr/local/systemc \
bash run_all.sh
```

Este es el script que satisface el requisito de "scripts para automatizar
la construcción del prototipo virtual y correr todo el sistema" — los
pasos 1-7 (instalación de gem5, SystemC, y compilación inicial del puente)
son de una sola vez por máquina, y no tiene sentido "automatizarlos" en el
sentido de correrlos repetidamente.
