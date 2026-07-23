# TikStudio Event Scenario Runner

Herramienta manual e interactiva para construir una secuencia temporal y
ejecutarla contra el recorrido real:

```text
FTSEventExecutionHost → Pipeline → Core
```

Actualmente sólo existe el provider `Chat`. El menú muestra únicamente
providers registrados; los flows reservados no se presentan como opciones
ficticias.

## Construcción

La herramienta está desactivada por defecto. Configura CMake con:

```text
-DTIKSTUDIO_BUILD_EVENT_SCENARIO_RUNNER=ON
```

El ejecutable generado se llama:

```text
TikStudioEventScenarioRunner
```

No se registra mediante `add_test`: es una herramienta manual y no cambia las
suites certificadas.

## Modelo temporal

El wizard solicita intervalos relativos entre mensajes y los acumula para
obtener cada `ArrivalOffset`, que es absoluto respecto al inicio del escenario.
El reloj avanza directamente entre offsets; no consulta la hora del sistema ni
espera tiempo real.

`ProcessingDuration` controla cuándo el runner publica el completion exitoso
después de un dispatch. El TTL pertenece al flow del Core y sólo limita cuánto
puede permanecer una emisión `Pending`; no mide el procesamiento `InFlight`.
Un TTL de cero significa sin expiración. Un `MaxSlots` de cero significa que el
flow no tiene capacidad.

Cuando varios eventos comparten offset, el orden es deliberadamente:

```text
completion activo
→ inputs por Sequence
→ mantenimiento sin comando, sólo si no hubo comando
```

Cada acción se procesa mediante `FTSEventExecutionHost::RunOneCycle`. La
admisión, acumulación, prioridad, expiración, selección y lifecycle proceden de
los resultados públicos del Host; la herramienta no mantiene una cola paralela.

## Añadir otro provider

Un provider implementa `ITSFlowScenarioProvider`, configura sus acciones de
entrada, observa su dispatch tipado y construye el callback de completion.
Después se registra en `main.cpp`; el menú se actualiza sin modificar el motor
temporal.

Un futuro provider de flow derivado podrá publicar entradas de su familia de
origen y observar el resultado semántico correspondiente. No debe inyectar
directamente una emisión derivada ni duplicar decisiones del Pipeline o Core.
