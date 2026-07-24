# TikStudioEventQueueLab — contexto operativo

Última actualización: 2026-07-23.

Este documento describe el estado autoritativo vigente. La evolución detallada hasta
el baseline histórico `8afa3b6` se conserva en
[`docs/history/PROJECT_CONTEXT_BASELINE_8afa3b.md`](docs/history/PROJECT_CONTEXT_BASELINE_8afa3b.md).

## 1. Estado certificado

```text
Rama: main
Baseline certificado: d49bf49440abd2a0cc49d3f9f33bf76c08e37a1b
Commit: feat(gift-combo): add direct family decision
Certificación automatizada por el propietario: 353 PASS / 0 FAIL
Fecha de certificación: 2026-07-23
```

| Suite | Resultado certificado |
| --- | ---: |
| Core | 26 PASS / 0 FAIL |
| Pipeline | 155 PASS / 0 FAIL |
| Host | 73 PASS / 0 FAIL |
| Adapter | 62 PASS / 0 FAIL |
| JSON Decoder | 20 PASS / 0 FAIL |
| Checklist | 10 PASS / 0 FAIL |
| Vertical Integration | 7 PASS / 0 FAIL |
| **Total** | **353 PASS / 0 FAIL** |

Estos resultados corresponden a una ejecución automatizada de las suites realizada y
certificada por el propietario. No son un resultado de CI ni fueron ejecutados por el
agente que actualizó este documento.

### Validación manual del Scenario Runner

El propietario validó manualmente los cinco escenarios Chat disponibles en
`TikStudioEventScenarioRunner`: 5 PASS / 0 FAIL. Esta comprobación interactiva no forma
parte de CTest ni altera los 353 casos certificados.

### Estado local pendiente de certificación

GiftCombo A fue publicado y certificado en `d49bf49`. Sobre ese baseline se implementó
localmente GiftCombo B: la pareja exacta `FamilyKind = Gift / Flow = GiftCombo` está
autorizada y el carril dispone de repositorio propio, admisión, binding, ready,
dispatch, completion y limpieza de lifecycle en el Pipeline.

Gift y GiftCombo comparten el Core, el registro autoritativo de bindings, la
notificación ready, el único `InFlight` global y el lifecycle genérico. Gift usa
`FTSGiftPayloadRepository` con `Flow = Gift`; GiftCombo usa
`FTSGiftComboPayloadRepository` con `Flow = GiftCombo`. Toda selección o limpieza se
enruta mediante `FamilyKind + Flow`, evitando que el dominio compartido Gift confunda
ambos carriles.

Se registraron localmente 12 casos Pipeline adicionales, llevando el conteo estático a
167 casos Pipeline y 365 casos totales. No se compiló ni se ejecutó ninguna prueba
durante esta fase; estos conteos no sustituyen la certificación publicada de
353 PASS / 0 FAIL. GiftCombo C, la bifurcación semántica y la integración Host o
vertical continúan pendientes.

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
- un FIFO global entre todas las familias y tipos de comando;
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
derivados; GiftCombo B sólo habilita su recorrido directo dentro del Pipeline.

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

Estos valores existen en `ETSEventFlow` y poseen settings en Core. GiftCombo es la única
excepción operativa parcial: su fase B autoriza `Gift / GiftCombo` y completa el
recorrido interno del Pipeline, pero todavía no lo expone en Host ni en integración
vertical y no existe selección semántica desde Gift. Los otros carriles no tienen
semántica operativa. Sus defaults actuales son reservas técnicas, no requisitos finales
de producto.

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
| Pipeline | 155 |
| Host | 73 |
| Adapter | 62 |
| JSON Decoder | 20 |
| Checklist | 10 |
| Vertical Integration | 7 |
| **Total** | **353** |

Las suites por capa verifican contratos específicos; las pruebas familiares residen en
`Tests/<Evento>/`. Existe una prueba vertical por cada evento base y esas siete pruebas
componen Adapter, Host, Pipeline y Core. El propietario ejecutó las suites automatizadas
y certificó el resultado total de 353 PASS / 0 FAIL sobre `d49bf49`.

GiftCombo B registra localmente 12 casos adicionales en Pipeline. El conteo resultante
de 167 casos Pipeline y 365 casos totales es sólo estático y permanece sin compilar,
ejecutar ni certificar.

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

- recorrido Host/vertical C de GiftCombo y lógica de los demás flujos derivados;
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

La siguiente frontera de GiftCombo será su fase C de integración Host/vertical. La
bifurcación semántica entre Gift y GiftCombo permanece para una fase posterior,
preservando intactas las siete rutas base certificadas.
