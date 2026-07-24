# TikStudioEventQueueLab — contexto operativo

Última actualización: 2026-07-24.

Este documento describe el estado autoritativo vigente. La evolución detallada hasta
el baseline histórico `8afa3b6` se conserva en
[`docs/history/PROJECT_CONTEXT_BASELINE_8afa3b.md`](docs/history/PROJECT_CONTEXT_BASELINE_8afa3b.md).

## 1. Estado certificado

```text
Rama: main
Baseline certificado: 8b76573f94ff13f9d4dadfc4c733b16fbab255a3
Commit: feat(share-milestone): complete pipeline lifecycle
Certificación automatizada por el propietario: 390 PASS / 0 FAIL
Fecha de certificación: 2026-07-24
```

| Suite | Resultado certificado |
| --- | ---: |
| Core | 26 PASS / 0 FAIL |
| Pipeline | 181 PASS / 0 FAIL |
| Host | 83 PASS / 0 FAIL |
| Adapter | 62 PASS / 0 FAIL |
| JSON Decoder | 20 PASS / 0 FAIL |
| Checklist | 10 PASS / 0 FAIL |
| Vertical Integration | 8 PASS / 0 FAIL |
| **Total** | **390 PASS / 0 FAIL** |

Estos resultados corresponden a una ejecución automatizada de las suites realizada y
certificada por el propietario. No son un resultado de CI ni fueron ejecutados por el
agente que actualizó este documento.

### Validación manual del Scenario Runner

El propietario validó manualmente los cinco escenarios Chat disponibles en
`TikStudioEventScenarioRunner`: 5 PASS / 0 FAIL. Esta comprobación interactiva no forma
parte de CTest ni altera los 390 casos certificados.

### Estado local pendiente de certificación

GiftCombo A, B y C están publicados, compilados y certificados. Su ruta
directa explícita está completa, comparte las autoridades globales y distingue Gift de
GiftCombo mediante `FamilyKind + Flow`; la selección automática y su semántica
coalescente continúan pendientes.

ShareMilestone A y B están publicados, compilados y certificados en `8b76573`.
`FTSShareInput` alimenta tanto Share directo como el payload estructural propio de
ShareMilestone; ambos comparten `ETSEventFamilyKind::Share` y se distinguen por Flow.

Sobre ese baseline se implementó localmente ShareMilestone C. El Host añade
`PostShareMilestone`, `PostShareMilestoneCompletion`, los dos command kinds, un wrapper
privado para distinguir el input dentro del FIFO y una alternativa de dispatch propia.
El routing ready consulta `FamilyKind + Flow` antes de consumir la notificación. Se
registraron 10 pruebas Host y una integración vertical que selecciona
`PostShareMilestone` explícitamente después de convertir JSON Share.

El conteo estático local es 93 casos Host, 9 verticales y 401 casos automáticos
totales. Estos cambios no fueron compilados, ejecutados ni certificados.

## 2. Objetivo del proyecto

`TikStudioEventQueueLab` es un laboratorio portable en C++20 para diseñar, implementar,
verificar, endurecer y certificar el sistema de colas de eventos de TikStudio antes de
integrarlo en el plugin de Unreal Engine.

No es un prototipo descartable. Sus contratos y fronteras buscan que Core, Pipeline y
Host, junto con la mayor parte de sus pruebas portables, puedan reutilizarse durante la
migración. La separación actual permite validar identidad, scheduling, payloads,
lifecycle y recuperación sin introducir dependencias de engine en las autoridades del
sistema.

## 3. Preparación para Unreal Engine

La frontera futura prevista es:

```text
TikFinity o productor externo
→ adapter de entrada
→ FTS*Input portable
→ Event Host
→ Pipeline
→ Core
→ dispatch tipado
→ puente C++/Blueprint de Unreal
```

- Core no conoce Unreal Engine ni depende de `UObject`, Blueprint, reflexión o tipos
  Unreal.
- Pipeline permanece portable y administra semántica familiar, payloads tipados y la
  coordinación externa alrededor de las emisiones del Core.
- Host serializa la ejecución del Coordinator sobre el owner thread que lo construyó.
- La futura capa UE5 será un adaptador o puente externo; Blueprint nunca será una
  dependencia directa de Core.
- La conexión productiva WebSocket → Host y el puente UE5 aún no están implementados.

La migración, por tanto, añadirá una frontera de integración sin trasladar conocimiento
de Unreal a Core o Pipeline.

## 4. Dirección de dependencias

La dirección productiva portable es:

```text
Host → Pipeline → Core
```

La frontera de entrada es:

```text
Productores externos
→ Adapters
→ FTS*Input
→ Host
```

### Core

`TikStudioEventQueueSystem` es la autoridad de las emisiones genéricas. Administra:

- identidad global;
- estados `Pending` e `InFlight`;
- prioridad y desempate determinista por `Sequence`;
- TTL, expiración y capacidad por flujo;
- settings y snapshots efectivos por emisión;
- actualización preparada de prioridad o vencimiento para emisiones `Pending`;
- selección y lifecycle genérico;
- preparación completa antes del commit autoritativo.

Core no interpreta payloads concretos ni conoce familias, Host, adapters o Unreal.

### Pipeline

`FTSEventPipelineCoordinator` administra:

- familias y decisiones tipadas;
- payloads y repositorios independientes por familia;
- bindings globales por `EmissionId`;
- parejas autorizadas `FamilyKind / Flow`;
- ready, dispatch y completion;
- sincronización y limpieza del lifecycle externo.

Pipeline depende de Core y no invierte esa dependencia.

### Host

`FTSEventExecutionHost` proporciona:

- publicación thread-safe de inputs, completions y actualizaciones de settings;
- un FIFO global entre todas las rutas y tipos de comando;
- ejecución del Coordinator exclusivamente en el owner thread;
- procesamiento de como máximo un comando por ciclo;
- un único procesamiento global compartido;
- lease y reconocimiento del comando por `Sequence`;
- consumo de completions rechazadas definitivamente y conservación de fallos
  reintentables.

### Adapters

Los adapters contienen decoder y converters TikFinity y traducen datos externos hacia
contratos `FTS*Input` portables. El transporte y la ejecución permanecen separados: el
adapter productivo no depende de Host o Pipeline y no existe todavía una conexión
WebSocket → Host.

## 5. Eventos base implementados

Las siete familias base vigentes son:

```text
Chat
Gift
Like
Follow
Share
RoomUser
Member
```

Cada una dispone de un recorrido portable completo que cubre:

```text
JSON TikFinity
→ decoder
→ converter
→ FTS*Input
→ familia directa
→ payload y repositorio
→ Pipeline
→ Core
→ Host
→ dispatch
→ completion
→ lifecycle
```

Estas rutas base no son placeholders: conservan sus datos portables y producen el flujo
correspondiente. Follow, Share, Like, RoomUser, Gift y Member mantienen una semántica
directa. Chat agrega mensajes por usuario mientras su lote continúa `Pending`, conserva
la prioridad admitida y puede renovar el vencimiento usando el TTL efectivo congelado
por Core. Las rutas base no calculan todavía tasas, milestones ni seleccionan flujos
derivados. GiftCombo dispone además de una ruta directa explícita A → B → C publicada,
sin clasificación productiva ni semántica coalescente.

## 6. Flujos derivados reservados

Los únicos carriles derivados reservados son:

```text
GiftCombo
LikeMilestone
MemberRate
RoomUserMilestone
RoomUserTop1Change
ShareMilestone
```

Estos valores existen en `ETSEventFlow` y poseen settings en Core. GiftCombo conserva
su ruta derivada A → B → C publicada. ShareMilestone A → B está publicado y su fase C
completa localmente la ruta directa explícita en Host e integración vertical.

Share y ShareMilestone comparten `FTSShareInput`, `ETSEventFamilyKind::Share`, owner
thread, FIFO Host, Coordinator, Core, BindingRegistry, ready e `InFlight`. Share usa
`ETSEventFlow::Share` y `FTSShareProcessingDispatch`; ShareMilestone usa
`ETSEventFlow::ShareMilestone` y `FTSShareMilestoneProcessingDispatch`. No existe
selección automática desde el converter, acumulación, scope, thresholds ni valor
semántico calculado de milestone. Los otros carriles continúan como reservas técnicas.

## 7. Invariantes compartidas vigentes

- Existe un solo Core compartido y un único `InFlight` global.
- La prioridad es global y determinista; `Sequence` actúa como desempate FIFO.
- Toda emisión administrada por el Coordinator conserva binding y payload coherentes.
- Ready es una notificación derivada, nunca una tercera autoridad.
- El dispatch se construye antes de confirmar `Bound → Processing` y consumir ready.
- Un terminal elimina exactamente una vez binding, payload y ready coincidente.
- Las mutaciones del Core se preparan por completo antes del commit autoritativo.
- Tras la preparación, los commits internos y externos no ejecutan operaciones
  potencialmente lanzables.
- Una completion definitivamente rechazada se consume y no puede envenenar el FIFO.
- Un fallo no clasificado conserva el comando frontal para un reintento sin reorder.
- Las actualizaciones de settings viajan por el FIFO y sólo afectan admisiones futuras.
- Las emisiones vivas conservan snapshots de sus settings efectivos.
- Una renovación Chat aceptada usa el mismo `Now` de su decisión y nunca revive una
  emisión vencida; la terminalización continúa perteneciendo a expiraciones.

## 8. Pruebas y certificación

La organización vigente de runners, suites y archivos se documenta en
[`Tests/README.md`](Tests/README.md).

| Capa | Casos certificados |
| --- | ---: |
| Core | 26 |
| Pipeline | 181 |
| Host | 83 |
| Adapter | 62 |
| JSON Decoder | 20 |
| Checklist | 10 |
| Vertical Integration | 8 |
| **Total** | **390** |

Las suites por capa verifican contratos específicos; las pruebas familiares residen en
`Tests/<Evento>/`. Existe una prueba vertical por cada evento base y esas siete pruebas
componen Adapter, Host, Pipeline y Core. El propietario ejecutó las suites automatizadas
y certificó el resultado total de 390 PASS / 0 FAIL sobre `8b76573`.

ShareMilestone C añade localmente 10 casos Host y una integración vertical explícita.
El conteo resultante de 93 casos Host, 9 verticales y 401 casos automáticos totales es
sólo estático y permanece sin compilar, ejecutar ni certificar.

## 9. Flujo de trabajo

- El agente inspecciona el repositorio y realiza los cambios autorizados.
- El propietario compila el proyecto.
- El propietario ejecuta y certifica las pruebas.
- El propietario crea el commit.
- El propietario realiza el push.
- CodeGraph debe usarse como método principal de navegación cuando el índice esté
  disponible.
- No se avanza automáticamente a la siguiente fase.
- La documentación se reconcilia siempre con el `HEAD` real de `main`.

## 10. Trabajo fuera de alcance

Continúan fuera del alcance operativo actual:

- bifurcación automática, acumulación y claves semánticas de GiftCombo;
- clasificación automática Share/ShareMilestone desde el converter;
- acumulación, scope, thresholds y valor semántico de ShareMilestone;
- WebSocket productivo hacia Host;
- integración UE5 y Blueprint;
- scheduler o Tick productivo;
- shutdown coordinado;
- procesadores y efectos concretos posteriores al dispatch;
- persistencia;
- Simulator y Console funcionales;
- aging y evicción competitiva;
- métricas productivas.

Estas capacidades requieren fases independientes y no se diseñan en este baseline.

## 11. Próxima frontera

La ruta directa explícita ShareMilestone A → B → C está completa localmente. La
siguiente frontera requiere un diseño semántico independiente para clasificación
automática Share/ShareMilestone, acumulación, scope, thresholds y valor del milestone.
