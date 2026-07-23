# Organización y certificación de pruebas

## Propósito

Las pruebas certifican el sistema portable desde tres perspectivas complementarias:

- por capa, para aislar contratos y autoridades;
- por familia, para mantener simetría entre los siete eventos base;
- por integración vertical, para verificar la composición completa disponible.

La certificación vigente corresponde al baseline `8afa3b6`
(`fix(host): consume rejected completion commands`). El propietario ejecutó manualmente
las suites el 2026-07-21 y certificó 305 PASS / 0 FAIL. No es un resultado de CI.

## Runners actuales

| Runner CTest | Responsabilidad | Casos certificados |
| --- | --- | ---: |
| `TikStudioEventCoreTests` | Políticas genéricas de cola | 20 |
| `TikStudioEventPipelineTests` | Familias, repositorios, bindings, dispatch y lifecycle | 117 |
| `TikStudioEventHostTests` | FIFO, owner thread, comandos, completions y recuperación | 69 |
| `TikStudioTikFinityAdapterTests` | Conversiones TikFinity hacia inputs portables | 62 |
| `TikStudioTikFinityJsonDecoderTests` | Decodificación y validación del evento mapeado | 20 |
| `TikStudioTikFinityChecklistTests` | Cobertura del contrato de los siete eventos | 10 |
| `TikStudioVerticalIntegrationTests` | Composición portable end-to-end por familia | 7 |
| **Total** |  | **305** |

Los siete runners están declarados explícitamente como ejecutables y registrados en
CTest desde `CMakeLists.txt`.

## Estado local no certificado

Sobre `f11fd03` se añadieron seis casos Core para la actualización de scheduling y tres
casos Pipeline netos para la renovación Chat. El registro local queda en 26 casos Core,
153 Pipeline y 347 casos totales. Son conteos estáticos: no se compiló ni se ejecutó
ninguna suite durante esta tarea, por lo que la certificación vigente continúa siendo
305 PASS / 0 FAIL sobre `8afa3b6`.

## Organización por familia

Las pruebas específicas de cada evento se distribuyen en:

```text
Tests/Chat/
Tests/Gift/
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
lifecycle y consistencia entre autoridades externas y Core.

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
