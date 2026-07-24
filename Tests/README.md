# Organización y certificación de pruebas

## Propósito

Las pruebas certifican el sistema portable desde tres perspectivas complementarias:

- por capa, para aislar contratos y autoridades;
- por familia, para mantener simetría entre los siete eventos base;
- por integración vertical, para verificar la composición completa disponible.

La certificación vigente corresponde al baseline `d49bf49`
(`feat(gift-combo): add direct family decision`). El propietario ejecutó las suites
automatizadas y certificó 353 PASS / 0 FAIL. No es un resultado de CI.

Por separado, el propietario validó manualmente los cinco escenarios Chat del Scenario
Runner: 5 PASS / 0 FAIL. La herramienta interactiva no está registrada en CTest y ese
resultado no altera el conteo certificado.

## Runners actuales

| Runner CTest | Responsabilidad | Casos certificados |
| --- | --- | ---: |
| `TikStudioEventCoreTests` | Políticas genéricas de cola | 26 |
| `TikStudioEventPipelineTests` | Familias, repositorios, bindings, dispatch y lifecycle | 155 |
| `TikStudioEventHostTests` | FIFO, owner thread, comandos, completions y recuperación | 73 |
| `TikStudioTikFinityAdapterTests` | Conversiones TikFinity hacia inputs portables | 62 |
| `TikStudioTikFinityJsonDecoderTests` | Decodificación y validación del evento mapeado | 20 |
| `TikStudioTikFinityChecklistTests` | Cobertura del contrato de los siete eventos | 10 |
| `TikStudioVerticalIntegrationTests` | Composición portable end-to-end por familia | 7 |
| **Total** |  | **353** |

Los siete runners están declarados explícitamente como ejecutables y registrados en
CTest desde `CMakeLists.txt`.

## Estado local no certificado

Sobre `d49bf49` se implementó localmente GiftCombo B. La pareja exacta
`FamilyKind = Gift / Flow = GiftCombo` ya está autorizada y dispone de repositorio
propio, admisión, binding, ready, dispatch, completion y limpieza de lifecycle dentro
del Pipeline. Gift y GiftCombo comparten Core, registro de bindings, ready e `InFlight`,
pero mantienen repositorios tipados separados y se enrutan por la pareja completa.
En concreto, `FTSGiftComboPayloadRepository`, `SubmitGiftCombo`,
`BeginGiftComboProcessing`, `CompleteGiftComboProcessing` y
`PeekPendingReadyFlow` cubren los lifecycle Pending, Confirm y Cancel sin introducir
clasificación ni acumulación semántica.

Se añadieron 12 casos de Coordinator sin ejecutarlos. El conteo estático local queda en
167 casos Pipeline y 365 casos totales, pero la certificación vigente continúa siendo
353 PASS / 0 FAIL sobre `d49bf49`. GiftCombo C, la bifurcación semántica, la
acumulación y la integración Host o end-to-end permanecen pendientes.

## Organización por familia

Las pruebas específicas de cada evento se distribuyen en:

```text
Tests/Chat/
Tests/Gift/
Tests/GiftCombo/
Tests/Like/
Tests/Follow/
Tests/Share/
Tests/RoomUser/
Tests/Member/
```

Cada directorio contiene sólo los casos propios de esa familia en las capas que le
corresponden. La infraestructura transversal permanece en archivos compartidos bajo
`Tests/` y no se atribuye artificialmente a una familia.

## Responsabilidad de las suites

### Core

Verifica identidad, prioridad, orden por `Sequence`, capacidad, TTL, expiración,
settings, actualización preparada de scheduling `Pending`, selección, lifecycle
genérico y preparación/commit de las mutaciones.

### Pipeline

Verifica decisiones familiares, payloads, repositorios, bindings, parejas
`FamilyKind / Flow`, acumulación y renovación Chat, ready, dispatch, completion,
lifecycle y consistencia entre autoridades externas y Core. GiftCombo B cubre además
su recorrido completo dentro del Pipeline y la convivencia bidireccional con Gift.

### Host

Verifica publicación thread-safe, FIFO global, owner thread, un comando por ciclo,
inputs, completions, actualizaciones de settings, lease/ack por `Sequence`, consumo de
comandos definitivamente rechazados y recuperación de fallos reintentables.

### Adapter

Verifica que los converters TikFinity preserven y traduzcan correctamente los contratos
decodificados hacia los siete tipos `FTS*Input` portables.

### JSON Decoder

Verifica el envelope JSON, los tipos, campos opcionales, rechazos y el variant tipado de
los eventos mapeados.

### Checklist

Verifica que los siete nombres de evento y sus rutas decoder/formatter permanezcan
cubiertos por el contrato TikFinity portable.

### Vertical Integration

Cada uno de los siete casos compone:

```text
JSON
→ Adapter
→ Host
→ Pipeline
→ Core
→ dispatch
→ completion
```

Esta suite recorre la composición portable disponible, pero no prueba WebSocket
productivo, UE5, Blueprint, Tick productivo ni flujos derivados.

## Registro de nuevas pruebas

- Cada familia registra sus casos explícitamente desde el runner correspondiente.
- No se incluyen archivos `.cpp` dentro de otros archivos fuente.
- No se usa autorregistro global.
- La infraestructura transversal no se asigna artificialmente a una familia.
- Un nuevo evento o flujo añade cobertura sólo en las capas que realmente modifica.
- La salida legible del runner no reemplaza las aserciones `Require`.
