# Organización y certificación de pruebas

## Propósito

Las pruebas certifican el sistema portable desde tres perspectivas complementarias:

- por capa, para aislar contratos y autoridades;
- por familia, para mantener simetría entre los siete eventos base;
- por integración vertical, para verificar la composición completa disponible.

La certificación vigente corresponde al baseline `9620aa2`
(`feat(share-milestone): add direct family decision`). El propietario ejecutó las
suites automatizadas y certificó 378 PASS / 0 FAIL. No es un resultado de CI.

Por separado, el propietario validó manualmente los cinco escenarios Chat del Scenario
Runner: 5 PASS / 0 FAIL. La herramienta interactiva no está registrada en CTest y ese
resultado no altera el conteo certificado.

## Runners actuales

| Runner CTest | Responsabilidad | Casos certificados |
| --- | --- | ---: |
| `TikStudioEventCoreTests` | Políticas genéricas de cola | 26 |
| `TikStudioEventPipelineTests` | Familias, repositorios, bindings, dispatch y lifecycle | 169 |
| `TikStudioEventHostTests` | FIFO, owner thread, comandos, completions y recuperación | 83 |
| `TikStudioTikFinityAdapterTests` | Conversiones TikFinity hacia inputs portables | 62 |
| `TikStudioTikFinityJsonDecoderTests` | Decodificación y validación del evento mapeado | 20 |
| `TikStudioTikFinityChecklistTests` | Cobertura del contrato de los siete eventos | 10 |
| `TikStudioVerticalIntegrationTests` | Composición portable end-to-end por familia | 8 |
| **Total** |  | **378** |

Los siete runners están declarados explícitamente como ejecutables y registrados en
CTest desde `CMakeLists.txt`.

## Estado local no certificado

GiftCombo A, B y C están publicados, compilados y certificados. La ruta
directa explícita está completa, pero no existe clasificación automática desde el
converter, semántica acumulativa, `ComboKey`, `bIsFinal` ni interpretación productiva
de `repeatEnd`.

ShareMilestone A está publicado, compilado y certificado en `9620aa2`. Reutiliza
`FTSShareInput`, que sólo contiene `User`, y añade un payload estructural y una familia
sin estado que construye candidatos `Share / ShareMilestone`.

ShareMilestone B se implementó localmente: autoriza esa pareja y añade repositorio,
admisión, binding, dispatch, completion y lifecycle dentro del Pipeline compartido.
Se registraron 12 pruebas de Coordinator sin compilarlas ni ejecutarlas. El conteo
estático local queda en 181 casos Pipeline y 390 casos automáticos totales; la
certificación vigente continúa siendo 378 PASS / 0 FAIL. No se implementaron Host,
integración vertical, acumulación, scope, thresholds ni un valor semántico de
milestone.

## Organización por familia

Las pruebas específicas de cada evento se distribuyen en:

```text
Tests/Chat/
Tests/Gift/
Tests/GiftCombo/
Tests/Like/
Tests/Follow/
Tests/Share/
Tests/ShareMilestone/
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
ShareMilestone B añade localmente el recorrido equivalente dentro del Pipeline y la
convivencia bidireccional con Share directo; aún no cuenta con certificación manual.

### Host

Verifica publicación thread-safe, FIFO global, owner thread, un comando por ciclo,
ocho rutas operativas, inputs, completions, actualizaciones de settings, lease/ack por
`Sequence`, consumo de comandos definitivamente rechazados y recuperación de fallos
reintentables.

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

Los siete casos base y el caso local explícito GiftCombo componen:

```text
JSON
→ Adapter
→ Host
→ Pipeline
→ Core
→ dispatch
→ completion
```

Esta suite recorre la composición portable disponible. GiftCombo usa el decoder y
converter Gift existentes, pero el test selecciona `PostGiftCombo` explícitamente; no
certifica bifurcación productiva. Tampoco prueba WebSocket productivo, UE5, Blueprint
ni Tick productivo.

## Registro de nuevas pruebas

- Cada familia registra sus casos explícitamente desde el runner correspondiente.
- No se incluyen archivos `.cpp` dentro de otros archivos fuente.
- No se usa autorregistro global.
- La infraestructura transversal no se asigna artificialmente a una familia.
- Un nuevo evento o flujo añade cobertura sólo en las capas que realmente modifica.
- La salida legible del runner no reemplaza las aserciones `Require`.
