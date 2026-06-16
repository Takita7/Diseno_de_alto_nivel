# Declaración de Uso de Inteligencia Artificial

De acuerdo con la política de uso de IA del curso MP6160, se declara lo siguiente:

## Herramienta utilizada
**Claude (Anthropic)** — interfaz de chat web (claude.ai)

## Resumen del uso

Se utilizó Claude como apoyo conceptual durante el diseño del sistema, mediante
una conversación guiada paso a paso en lugar de generación masiva de código.

- **Consulta de conceptos:** se consultaron los fundamentos de TLM 2.0 (sockets,
  `b_transport`, `tlm_generic_payload`, `sc_event`) antes de escribir cada
  módulo, así como la justificación técnica de la fórmula de conversión RGB→
  escala de grises (se comparó ITU-R BT.601 vs BT.709, eligiendo BT.709 por
  ser el estándar correcto para contenido HD).

- **Consulta de documentación oficial:** se verificó que los patrones usados
  (sockets `simple_initiator_socket`/`simple_target_socket`, modelo
  `b_transport`) estuvieran alineados con los ejemplos del repositorio oficial
  de Accellera (`accellera-official/systemc`) y el estándar IEEE 1666-2023.

- **Implementación guiada:** el equipo escribió a mano el código de los cinco
  módulos (`Storage`, `RAM`, `Bus`, `Accelerator`, `CPU`), guiándose por
  explicaciones conceptuales y código de referencia generado con apoyo de
  Claude (sockets, `b_transport`, manejo de hilos con `SC_THREAD`/`sc_event`).
  Durante la implementación se cometieron y corrigieron errores propios.

- **Generación de diagramas:** el diagrama de bloques de la arquitectura
  (SVG) y el diagrama de secuencias (texto) se generaron con apoyo de IA.

- **Mejora de redacción:** se usó para estructurar el README técnico.

## Tipo de utilización (según categorías del enunciado)

| Categoría | ¿Se usó? |
|---|---|
| Consulta de conceptos | Sí — TLM 2.0, sockets, fórmulas de luminancia |
| Revisión de código | Sí — corrección de errores de transcripción |
| Depuración | No |
| Generación de diagramas | Sí — bloques y secuencias |
| Mejora de redacción | Sí — README técnico |
