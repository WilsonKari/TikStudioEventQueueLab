# TikStudioEventQueueLab — contexto de transferencia

Última actualización: 2026-07-15.

Estado de referencia de esta actualización: rama `main`, partiendo de HEAD `9d8019d`
(`feat(core): add pump selection and in-flight state`). Los cambios de la Fase 4A.8
permanecen locales y sin commit.

## 1. Objetivo general

`TikStudioEventQueueLab` es un laboratorio en C++20 puro para diseñar, implementar y
probar el nuevo `EventQueueSystem` fuera de Unreal Engine. La intención es reutilizar
posteriormente la mayor parte posible del core dentro de `TikStudioPlugin` mediante un
adaptador de Unreal.

La frontera obligatoria es:

```text
Productores externos
├─ Simulador de consola
├─ TikFinity mediante WebSocket
└─ Futuro adaptador Unreal/Blueprint
        ↓
Conversión en el adaptador correspondiente
        ↓
Entradas C++ normalizadas y portables
        ↓
Semántica tipada de la familia
        ↓
Solicitud genérica para un flujo sintético
        ↓
TikStudio EventQueueSystem Core
```

El core portable no debe conocer Unreal, `UObject`, macros de reflexión, Blueprint,
`FString`, contenedores Unreal, TikFinity, WebSocket, JSON, consola ni bibliotecas
externas. El simulador, TikFinity y Unreal son adaptadores paralelos; ninguno forma
parte del core genérico.

La futura integración debe conservar este sentido:

```text
Blueprint
→ UObject adaptador de Unreal
→ conversión a contratos portables
→ lógica de familia
→ EventQueueSystem portable
```

Blueprint nunca será dependencia directa del core.

## 2. Datos entrantes y familias portables

Existen contratos portables para siete familias nativas:

- `FTSChatInput`
- `FTSGiftInput`
- `FTSLikeInput`
- `FTSMemberInput`
- `FTSRoomUserInput`
- `FTSShareInput`
- `FTSFollowInput`

También existen contratos auxiliares tipados como `FTSUserSnapshot`, `FTSEmoteInfo`
y `FTSRoomUserTopViewer`. Sólo usan tipos de la biblioteca estándar.

Estos contratos describen datos entrantes, pero el core genérico de emisiones todavía
no los recibe, interpreta ni almacena. No se ha implementado lógica específica de Chat,
Gift, Like, Member, RoomUser, Share o Follow.

Decisión arquitectónica aprobada:

- Las familias administrarán su semántica, estados y payloads tipados.
- El core administrará emisiones y políticas globales.
- El core no almacenará un payload universal (`any`, `variant`, `void*`, JSON o string
  genérico).
- Los payloads permanecerán en la familia o capa superior y se asociarán mediante
  `FTSEmissionId`.
- Los eventos terminales del core indicarán cuándo liberar o consolidar ese payload.

## 3. Flujos sintéticos

Los trece valores de `ETSEventFlow` son carriles semánticos de cola, no trece tipos de
payload. La relación conceptual actual es:

```text
Chat     → Chat
Gift     → Gift | GiftCombo
Follow   → Follow
Like     → Like | LikeUser
Member   → MemberIdentity | MemberNormalized
RoomUser → RoomUser | RoomUserMilestone | RoomUserTop1Change
Share    → Share | ShareMilestone
```

Son “flujos sintéticos” porque representan una decisión semántica futura de la familia.
Todavía no existe código que lea un evento concreto y produzca esos flujos. Los siete
archivos de `Core/Private/EventQueueSystem/Events/` sólo incluyen el header central y
no contienen implementación.

El pipeline previsto y el punto exacto alcanzado son:

```text
Fuente externa                                      [futuro]
→ adaptador de fuente                               [placeholder]
→ FTS*Input portable                                [contratos listos]
→ familia interpreta payload y elige flujo          [no implementado]
→ FTSEnqueueRequest                                  [contrato listo]
→ validar flujo, enabled y TTL efectivo              [implementado en Enqueue]
→ comprobar capacidad por flujo                     [implementado por escaneo O(n)]
→ capturar tiempo / prioridad / expiración / ID      [implementado en Enqueue]
→ construir FTSEmissionEnvelope                     [implementado]
→ crear y almacenar record autoritativo Pending      [implementado]
→ indexar prioridad y expiración finita               [implementado]
→ GetNextWakeTime consulta próximo vencimiento        [implementado]
→ ProcessDueExpirations elimina Pending vencidos      [implementado]
→ Pump selecciona y cambia Pending a InFlight          [implementado]
→ host despacha payload tipado                         [no implementado]
→ Confirm / Cancel elimina InFlight y emite terminal   [implementado]
──────────────────────── PUNTO ACTUAL ────────────────────────
→ lifecycle event libera o consolida payload         [contrato listo; lógica pendiente]
```

## 4. Contratos públicos actuales

### Metadatos

`FTSEmissionEnvelope` contiene solamente:

- `EmissionId`
- `Flow`
- `Sequence`
- `CreatedAt`
- `ExpiresAt`
- `PriorityScore`

`PriorityScore` representa la prioridad base congelada al admitir. El aging futuro se
evaluará aparte.

### Solicitud y operaciones

`FTSEnqueueRequest` contiene flujo, ajuste de prioridad, override opcional de TTL y
protección ante evicción. Existen contratos de resultado para `Enqueue`, `Pump`,
`Confirm`, `CancelInFlight`, `ProcessDueExpirations` y `GetNextWakeTime`.

`Enqueue` ya tiene una implementación básica funcional: valida la solicitud, comprueba
la capacidad del flujo, captura el tiempo una vez, asigna identidad, construye el
envelope, almacena un record `Pending` e inserta sus snapshots en los índices derivados
de prioridad y expiración finita. `GetNextWakeTime` y `ProcessDueExpirations` ya son
operativas. `Pump` procesa expiraciones, selecciona por prioridad y mantiene una única
emisión `InFlight`. `Confirm` y `CancelInFlight` validan el ID solicitado contra esa
emisión activa, generan exactamente un terminal en caso de éxito y eliminan su record,
liberando el slot. Ninguna de las dos realiza Auto Pump todavía.

Estados relevantes de admisión:

- `Accepted`
- `AcceptedWithEviction`
- `RejectedInvalidFlow`
- `RejectedDisabled`
- `RejectedInvalidTTL`
- `RejectedIdentityExhausted`
- `RejectedAtCapacity`

`RejectedInvalidTTL` cubre un TTL efectivo negativo tanto desde el override como desde
los settings del flujo. `RejectedIdentityExhausted` cubre agotamiento de ID o Sequence.

### Lifecycle

Los terminal reasons públicos son:

- `Confirmed`
- `ExpiredDiscard`
- `ExpiredConsolidate`
- `Evicted`
- `Cancelled`

`FTSEmissionLifecycleEvent` entrega un snapshot del envelope y el motivo. No contiene
payload. Los eventos de una operación deben conservar el orden real de transición, sin
reordenarse por ID, flujo o prioridad.

## 5. Semánticas aprobadas que deben conservarse

- TTL mayor que cero: expira sólo mientras la emisión permanezca `Pending`.
- TTL igual a cero: sin expiración.
- TTL negativo: inválido.
- Una emisión `InFlight` no expira por TTL de cola.
- El host programa temporizadores; el core no crea Tick ni timers.
- `FTSNowProvider` es inyectable. Vacío se normaliza a
  `FTSEventQueueClock::now()`.
- Regla futura: capturar `Now` una sola vez por operación pública.
- `MaxSlots` cuenta `Pending + InFlight`.
- `MaxSlots == 0` significa sin capacidad, no ilimitado.
- Pump no libera slot; Confirm, Cancel, expiración y evicción sí.
- Prioridad base futura: suma saturada de `BaseWeight + PriorityAdjustment`.
- Una prioridad negativa es válida.
- Aging todavía no está implementado y no dependerá de un toggle de corrección.
- Evicción futura inicialmente intraflujo.
- Auto Pump después de Enqueue significa “intentar Pump tras una admisión si no hay
  InFlight”, no “primera emisión histórica”.
- CancelInFlight no realiza Auto Pump automáticamente.
- El core es single-threaded. Adaptador/host debe serializar llamadas al hilo propietario.

## 6. Settings actuales

Todos los flujos están habilitados, usan `Discard` y no están exentos de evicción. Los
defaults son `Flow: BaseWeight, TTL(ms), MaxSlots`:

```text
Chat:               40,  8000,    30
Gift:               70, 45000,  1000
GiftCombo:          80, 60000,  1000
Follow:             60, 30000,    10
Like:               25, 10000,     1
LikeUser:           10,  5000,     5
MemberIdentity:      5,  6000,    10
MemberNormalized:   20, 12000,     1
RoomUser:           35, 15000,     1
RoomUserMilestone:  30,  8000,     1
RoomUserTop1Change: 50, 10000,     2
Share:              55, 25000,    10
ShareMilestone:     50, 15000,     1
```

Defaults globales:

- Competitive eviction: desactivada.
- Tracking de métricas de evicción: desactivado.
- Aging: `0.0` puntos/segundo; bonus máximo configurado `20`.
- Pump después de Enqueue cuando idle: activado.
- Pump después de Confirm: activado.

## 7. Estado privado implementado hasta 4A.8

`TikStudioEventQueueSystem` usa PImpl con `std::unique_ptr<FImpl>`. Es no copiable,
movible con operaciones `noexcept`, y su destructor está fuera de línea. Un objeto
movido puede quedar con `Impl == nullptr` y sólo se considera válido para destrucción o
reasignación por movimiento.

Campos actuales de `FImpl`:

```cpp
FTSEventQueueSettings Settings;
FTSNowProvider NowProvider;
FTSEmissionId NextEmissionId = 1;
FTSEmissionSequence NextSequence = 1;
FTSEmissionId InFlightEmissionId = 0;
std::unordered_map<FTSEmissionId, FInternalEmissionRecord> Records;
std::priority_queue<
    FPriorityIndexEntry,
    std::vector<FPriorityIndexEntry>,
    FPriorityIndexCompare
> PriorityIndex;
std::priority_queue<
    FExpirationIndexEntry,
    std::vector<FExpirationIndexEntry>,
    FExpirationIndexCompare
> ExpirationIndex;
```

`Records` es la primera fuente autoritativa de emisiones. Cada
`FInternalEmissionRecord` conserva:

- el `FTSEmissionEnvelope` admitido;
- estado interno `Pending` o `InFlight`;
- snapshot de `ExpirePolicy`;
- protección efectiva ante evicción, calculada como OR entre request y settings;
- `Revision = 1` para la futura invalidación diferida de índices.

Helpers internos conectados a `Enqueue`:

- `TryAllocateEmissionIdentity`: asigna ID+Sequence como una operación lógica; cero
  indica agotamiento; `UINT64_MAX` puede emitirse una sola vez; no existe wrap.
- `ValidateEnqueueRequest`: valida flujo, obtiene settings mediante
  `TryGetFlowSettings`, resuelve TTL, aplica precedencia `InvalidFlow → Disabled →
  InvalidTTL → Valid`.
- `CalculatePriorityScore`: suma saturada sin overflow firmado.
- `CaptureNow`: devuelve `NowProvider()` y no es `noexcept` porque un callable externo
  podría lanzar.
- `IsFlowAtCapacity`: cuenta records vivos del flujo mediante un escaneo O(n);
  `MaxSlots == 0` rechaza toda admisión.
- `CalculateExpiresAt`: representa TTL cero con `time_point::max()` y satura en el
  máximo cuando la suma temporal no es representable. Compara en una duración común
  amplia y redondea TTL positivos hacia arriba al tick del reloj.

Las validaciones y el rechazo por capacidad ocurren antes de capturar tiempo o consumir
identidad. Una admisión aceptada construye el record completo y lo inserta en `Records`
antes de indexarlo. El max-heap de prioridad ordena por mayor `PriorityScore` y luego
menor `Sequence`; el min-heap temporal ordena por menor `ExpiresAt` y luego menor
`Sequence`. Ambos guardan ID + Revision, sin punteros, referencias ni iteradores al map.
Sólo las expiraciones finitas entran al índice temporal. Todavía no hay contadores.

El índice temporal ya dispone de validación contra `Records` mediante existencia,
estado `Pending`, Revision, Sequence, ExpiresAt y finitud. La limpieza lazy sólo retira
entradas stale desde el frente. `GetNextWakeTime` limpia y devuelve el próximo tiempo
sin capturar `Now`; `ProcessDueExpirations` captura `Now` una vez, procesa
`ExpiresAt <= Now`, emite `ExpiredDiscard` o `ExpiredConsolidate` y elimina el record.

La misma lógica vive en `ProcessDueExpirationsAt`, que recibe un `Now` ya capturado y
es compartido por la operación pública y `Pump`. El índice de prioridad valida
existencia, estado `Pending`, Revision, Sequence y PriorityScore, y descarta entradas
stale sólo desde el frente.

`Pump` captura tiempo una vez, procesa expiraciones, devuelve `Busy` si
`InFlightEmissionId` identifica un record `InFlight`, o selecciona el top vigente. La
selección copia el envelope público, cambia el record `Pending → InFlight`, conserva el
record en `Records`, retira la clave de prioridad y deja stale la clave temporal.

`Confirm` y `CancelInFlight` comparten una validación que distingue ausencia de emisión
activa, ID solicitado distinto e invariante interna rota. El ciclo operativo actual es:

```text
Enqueue → Pending → Pump → InFlight → Confirmed | Cancelled
```

La finalización publica primero el lifecycle event. Sólo después elimina el record y
restablece `InFlightEmissionId` a cero; si la publicación lanza, la emisión permanece
íntegramente `InFlight`. El borrado autoritativo libera capacidad y deja las claves
derivadas restantes como stale para su limpieza lazy.

## 8. Arquitectura privada aprobada para fases futuras

La dirección aprobada es:

- `std::unordered_map<FTSEmissionId, Record>` como fuente autoritativa
  (implementado en 4A.4).
- Max-heap de prioridad con claves/snapshots pequeños (implementado en 4A.5).
- Min-heap de expiración (implementado en 4A.5).
- Invalidación diferida mediante ID + revisión y compactación controlada.
- Contadores vivos por flujo.
- Una sola emisión `InFlight` (implementado en 4A.7).
- Orden: mayor prioridad primero; empate por menor Sequence (FIFO).

Ya existen `Records`, records `Pending`, ambos heaps derivados, limpieza lazy del frente
temporal y de prioridad, próximo despertar, expiración operativa, Pump y una única
emisión `InFlight`. Confirm y CancelInFlight ya completan y eliminan esa emisión. No
existen todavía Auto Pump, compactación, contadores por flujo, almacenamiento de
payloads, evicción ni aging. La capacidad se determina por escaneo del map: un record
`InFlight` ocupa slot hasta su confirmación o cancelación exitosa.

## 9. Estructura y CMake

Targets explícitos, sin `file(GLOB ...)`:

- `TikStudioEventCore` (STATIC): core central, settings y siete translation units de
  familias.
- `TikStudioEventSimulator` (STATIC): enlaza con Core; actualmente placeholder.
- `TikStudioTikFinityAdapter` (STATIC): enlaza con Core; actualmente placeholder, sin
  WebSocket ni JSON.
- `TikStudioEventConsole` (executable): enlaza los tres targets; actualmente sólo
  imprime `TikStudioEventQueueLab ready.`.

Tests contiene únicamente `.gitkeep`. No hay framework de pruebas ni dependencias
externas.

## 10. Historial de tareas y commits

### Base generada por Visual Studio

- Commit `a3639e6` — `first commit`.
- Contenía el proyecto CMake/hello-world generado, `CMakePresets.json`, README y
  `.gitignore`.

### Fase 1 — Estructura inicial y fronteras CMake

- No tuvo commit independiente al terminar la tarea.
- Se inspeccionó primero el proyecto generado, se preservaron `CMakePresets.json` y la
  configuración útil, y se creó la estructura Core/Adapters/Console/Tests con cuatro
  targets explícitos.
- Se eliminó el hello-world raíz sólo después de comprobar su función y se trasladó el
  ejecutable mínimo a `Console/main.cpp`.
- Estos cambios quedaron incluidos después en `f067331` junto con Fase 2/2B.

### Fase 2 — Contratos portables de entrada

- Se añadieron los contratos estándar para las siete familias y tipos auxiliares.
- No se añadió lógica de eventos ni dependencias externas.
- Commit compartido con Fase 1 y Fase 2B: `f067331` —
  `feat(core): scaffold event queue and portable input contracts`.

### Fase 2B — Auditoría exacta de contratos portables

- Se revisaron nombres, tipos, defaults y separación respecto de Unreal/TikFinity.
- Commit compartido: Fases 1, 2 y 2B quedaron publicadas juntas en:
  `f067331` — `feat(core): scaffold event queue and portable input contracts`.

### Fase 3A — Contratos generales del core y envelope

- Se añadieron trece flujos sintéticos, reloj monotónico, IDs, Sequence,
  `FTSEmissionEnvelope`, settings generales y defaults de los trece flujos.
- Commit `b4bcd0a` — `feat(core): add flow settings and emission envelope contracts`.

### Fase 3B — Contrato operativo público

- Se añadieron `FTSEnqueueRequest`, resultados/status iniciales y declaraciones de
  `Enqueue`, `Pump` y `Confirm`.
- Commit `a66b449` — `feat(core): add operational queue contracts`.

### Fase 4A.0 — Análisis crítico del estado interno

- Trabajo exclusivamente de diseño; no modificó archivos y no tuvo commit.
- Aprobó PImpl, core single-threaded, payload externo, map autoritativo, heaps de
  prioridad/expiración, lazy invalidation, contadores por flujo y un InFlight.
- También detectó contratos que debían endurecerse antes de crear almacenamiento.

### Fase 4A.1 — Corrección de contratos

- Añadió validación segura de flujos, `TryGetFlowSettings`, semántica explícita de TTL,
  reloj inyectable, lifecycle events, separación Pump outcome/result, expiraciones,
  próximo despertar, cancelación de InFlight y Auto Pump sin ambigüedad.
- Commit `6f6fa11` — `refactor(core): harden queue lifecycle and timing contracts`.

### Fase 4A.1b — Sincronización de defaults

- Actualizó `TSEventQueueSystemSettings.cpp` para usar
  `Flows[ToIndex(ETSEventFlow::...)]`, renombró Auto Pump y eliminó el toggle antiguo de
  recomputación, sin cambiar ningún default.
- Commit `9dfb375` — `fix(core): align settings defaults with hardened contracts`.

### Fase 4A.2 — PImpl, ownership y proveedor de tiempo

- Añadió FImpl oculto, `unique_ptr`, ownership de Settings/NowProvider, proveedor real
  por defecto, destructor fuera de línea, copia eliminada y movimiento `noexcept`.
- Commit `21f9992` — `refactor(core): add private implementation ownership`.

### Fase 4A.3 — Identidad, secuencias y validación básica

- Añadió estado y helpers internos para identidad monotónica sin wrap, validación de
  solicitudes, TTL efectivo, prioridad saturada y captura de tiempo.
- Añadió públicamente `RejectedIdentityExhausted` y amplió la semántica de
  `RejectedInvalidTTL`.
- No implementó Enqueue ni almacenamiento.
- Commit `3d45a9b` —
  `feat(core): add admission validation and emission identity primitives`.

### Fase 4A.4 — Almacenamiento autoritativo y Enqueue básico

- Añadió `Records` como `std::unordered_map<FTSEmissionId,
  FInternalEmissionRecord>` y el primer estado interno `Pending`.
- Implementó `Enqueue` con precedencia de validación, capacidad intraflujo por escaneo
  O(n), una sola captura de tiempo, prioridad saturada, expiración segura e identidad
  monotónica.
- El record congela envelope, política de expiración, protección efectiva y revisión
  inicial antes de insertarse autoritativamente.
- No añadió Pump, InFlight, heaps, expiración operativa, evicción, Auto Pump ni lógica
  específica de familias.
- Commit `72541ff` —
  `feat(core): add authoritative emission storage and basic enqueue`.

### Fase 4A.5 — Índices internos de prioridad y expiración

- Añadió un max-heap derivado de prioridad y un min-heap derivado de expiración; sus
  entradas sólo contienen claves congeladas, `EmissionId` y `Revision`.
- Toda admisión aceptada se indexa por prioridad; sólo una expiración finita se indexa
  temporalmente.
- `Enqueue` comprueba la inserción autoritativa y retira el record si falla la
  indexación, propagando la excepción. Una entrada stale sin record puede permanecer.
- Endureció `CalculateExpiresAt` para resoluciones distintas a milisegundos y redondeo
  no anticipado.
- No implementó consumo o limpieza de heaps, Pump, InFlight, expiración operativa,
  evicción ni lógica de familias.
- Commit `11631ba` —
  `feat(core): add priority and expiration indexes`.

### Fase 4A.6 — Expiración operativa y próximo despertar

- Añadió validación completa de entradas temporales contra el record autoritativo y
  limpieza lazy limitada al frente de `ExpirationIndex`.
- Implementó `GetNextWakeTime` sin captura de tiempo, eliminación de records ni
  procesamiento de vencimientos.
- Implementó `ProcessDueExpirations` con una captura de `Now`, comparación inclusiva,
  orden del min-heap y terminales según `ExpirePolicy`.
- Cada expiración elimina su entrada temporal y su record después de añadir el lifecycle
  event; la entrada derivada de prioridad queda stale.
- No implementó Pump, InFlight, Auto Pump, consumo de prioridad, evicción ni aging.
- Commit `5f56c23` —
  `feat(core): add expiration processing and wake scheduling`.

### Pausa de documentación — Comentarios internos del core

- Documentó responsabilidades e invariantes de records, índices derivados, identidad,
  admisión, prioridad, TTL, capacidad, Enqueue y expiración operativa.
- Reforzó por comentarios la autoridad exclusiva de `Records`, la validación mediante
  `EmissionId + Revision`, el orden de heaps y las garantías ante excepciones.
- No modificó comportamiento, firmas, nombres, estructuras, contratos ni el punto
  funcional alcanzado después de 4A.6.
- Commit `c64db91` —
  `docs(core): document queue internals and expiration lifecycle`.

### Fase 4A.7 — Selección por prioridad y primera transición a InFlight

- Añadió el estado interno `InFlight` y `InFlightEmissionId` como localizador único del
  record activo dentro de `Records`.
- Extrajo el procesamiento de expiraciones a un helper que recibe `Now` y es compartido
  por `ProcessDueExpirations` y `Pump`.
- Añadió validación y limpieza lazy del frente de `PriorityIndex` mediante ID, Revision,
  estado y snapshots de orden.
- Implementó `Pump` con expiraciones previas, estados Busy/QueueEmpty/EmissionReady y
  transición `Pending → InFlight`.
- No implementó Confirm, CancelInFlight, Auto Pump, evicción, aging ni payloads.
- Commit `9d8019d` —
  `feat(core): add pump selection and in-flight state`.

### Fase 4A.8 — Confirmación y cancelación de InFlight

- Añadió validación compartida del target activo con estados para ausencia, mismatch e
  invariante interna rota.
- Implementó una ruta terminal compartida que publica el lifecycle antes de borrar el
  record autoritativo y restablecer el ID activo.
- Implementó `Confirm` con terminal `Confirmed` y `CancelInFlight` con terminal
  `Cancelled`; los fallos no mutan estado ni generan lifecycle events.
- El borrado del record libera el slot y deja stale cualquier clave derivada restante.
- No añadió Auto Pump, captura temporal, expiraciones, evicción, aging ni payloads a
  estas operaciones.
- Cambios locales actuales; commit sugerido:
  `feat(core): add in-flight confirmation and cancellation`.

## 11. Reglas de trabajo para la siguiente sesión

- Leer este documento y comprobar el estado Git actual antes de asumir que sigue en
  `9d8019d`.
- Existe `.codegraph/`; usar CodeGraph antes de buscar o leer código.
- Obedecer literalmente el alcance de cada fase. No continuar automáticamente a la
  siguiente.
- No compilar ni ejecutar CMake cuando la especificación indique que el propietario lo
  hará manualmente desde Visual Studio.
- Validar estáticamente con `git diff --check` y confirmar exactamente qué archivos
  cambiaron.
- No crear commit ni push salvo petición explícita. Cuando se pida publicar, usar
  comandos `git`; `gh` no está instalado.
- No añadir Boost, Asio, WebSocket++, CPR, curl, JSON, frameworks de tests ni otras
  dependencias hasta una fase que lo autorice expresamente.
- No introducir Unreal, TikFinity, WebSocket, JSON, threads, mutex o callbacks dentro
  del core portable.
- Preservar la separación: adaptadores convierten fuentes, familias interpretan
  payloads, core administra emisiones.
- El próximo trabajo debe partir del ciclo completo `Pending → InFlight → terminal` de
  4A.8. Auto Pump sigue pendiente y debe esperarse la especificación exacta de la
  siguiente fase.
