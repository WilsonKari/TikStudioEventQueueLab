# TikStudioEventQueueLab — contexto de transferencia

Última actualización: 2026-07-18.

Estado de referencia:
rama `main`, partiendo de HEAD `3321a2a`
(`feat(host): complete share vertical integration`).

Los cambios de la Fase 4F.1 permanecen locales y sin commit.

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

Las fronteras portables conservan una única dirección:

```text
TikStudioEventHost
        ↓
TikStudioEventPipeline
        ↓
TikStudioEventCore
```

El Pipeline no incluye ni enlaza el Host. El Core no incluye ni enlaza Pipeline o Host.

La frontera TikFinity queda separada del transporte y de la ejecución:

```text
frame JSON TikFinity                         [adaptador]
        ↓
decoder tipado de siete eventos              [adaptador]
        ↓
FTSTikFinityMappedEvent                      [sólo frontera TikFinity]
        ↓
Chat → FTSTikFinityChatConverter             [implementado]
Follow → FTSTikFinityFollowConverter         [implementado en 4D.1]
Share → FTSTikFinityShareConverter           [implementado en 4E.1]
Like → FTSTikFinityLikeConverter             [implementado localmente en 4F.1]
otros tres → converters tipados              [pendientes]
        ↓
FTS*Input portable                            [Chat, Follow, Share y Like implementados]
        ↓
composición externa → Event Host             [Chat, Follow y Share implementados]
```

El adaptador todavía no depende de Host o Pipeline. Las composiciones JSON
Chat/Follow/Share → converter → Host existen sólo en el runner vertical; no hay
dependencia Adapter → Host en producción.

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

Estos contratos describen datos entrantes, pero el core genérico de emisiones no los
interpreta ni almacena. Chat, Follow y Share disponen del recorrido portable completo
hasta Host y lifecycle. Like dispone localmente de conversión y decisión familiar
directa; Gift, Member y RoomUser continúan sin implementación semántica.

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

Son “flujos sintéticos” porque representan una decisión semántica de la familia. Chat,
Follow, Share y Like producen sus flujos directos; todavía no existe lógica para los
flujos derivados ni para Gift, Member o RoomUser. Los siete archivos de
`Core/Private/EventQueueSystem/Events/` sólo incluyen el header central y no contienen
implementación.

El recorrido previsto y el punto exacto alcanzado son:

```text
texto JSON TikFinity                                [implementado en Adapter]
→ decoder tipado de siete eventos                   [implementado]
→ FTSTikFinityMappedEvent                           [implementado]
→ contratos decodificados Chat, Follow, Share y Like [implementados]
→ FTSTikFinityChatConverter                         [implementado]
→ FTSChatInput portable                             [conversión implementada]
→ familia interpreta payload y elige flujo          [Chat, Follow, Share y Like]
→ decisión familiar / candidato de admisión tipado  [Chat, Follow, Share y Like]
→ coordinador inserta payload provisional            [Chat, Follow y Share]
→ repositorio tipado de payloads                     [Chat, Follow y Share]
→ FTSEnqueueRequest                                  [Chat, Follow y Share llaman Core.Enqueue]
→ validar flujo, enabled y TTL efectivo              [implementado en Enqueue]
→ comprobar capacidad por flujo                     [implementado por escaneo O(n)]
→ capturar tiempo / prioridad / expiración / ID      [implementado en Enqueue]
→ construir FTSEmissionEnvelope                     [implementado]
→ crear y almacenar record autoritativo Pending      [implementado]
→ indexar prioridad y expiración finita               [implementado]
→ Auto Pump tras Enqueue aceptado e idle               [implementado]
→ binding externo EmissionId → PayloadHandle          [Chat, Follow y Share conectados]
→ lifecycle de Enqueue libera binding y payload       [generalizado Chat/Follow/Share]
→ GetNextWakeTime consulta próximo vencimiento        [implementado]
→ ProcessDueExpirations elimina Pending vencidos      [implementado]
→ Pump selecciona y cambia Pending a InFlight          [implementado]
→ coordinador captura ready global de un solo uso       [Chat, Follow y Share]
→ Begin*Processing produce copia propietaria            [Chat, Follow y Share]
→ binding externo Bound → Processing                    [Chat, Follow y Share]
→ coordinador entrega despacho tipado propietario       [Chat, Follow y Share]
→ Confirm / Cancel elimina InFlight y emite terminal   [implementado]
→ Auto Pump tras Confirm exitoso                       [implementado]
→ Succeeded coordina Confirm                            [Chat, Follow y Share]
→ Cancelled / Failed coordinan CancelInFlight           [Chat, Follow y Share]
→ lifecycle terminal enruta y limpia payload tipado     [Chat, Follow y Share]
→ Confirm captura el siguiente ready multi-familia      [implementado]
→ Pump y expiración se exponen por el coordinador       [Chat, Follow y Share]
→ fuentes publican input/completion en bandeja segura   [implementado en Host]
→ RunOneCycle serializa el coordinador en owner thread  [implementado en Host]
→ mantenimiento, Pump y wake quedan encapsulados        [implementado en Host]
→ Host devuelve como máximo un despacho propietario     [implementado en Host]
→ decoder JSON valida siete eventos TikFinity            [implementado en Adapter]
→ formatter y checklist 0/7 a 7/7                       [implementados]
→ probe WebSocket manual y opcional                      [implementado; no producción]
→ FTSTikFinityFollowConverter                             [implementado en 4D.1]
→ FTSFollowInput portable                                 [conversión implementada]
→ FTSFollowFamily produce candidato Flow Follow           [implementado en 4D.1]
→ repositorio, binding y admisión Follow                  [implementados en 4D.2]
→ dispatch y completion Follow                            [implementados en 4D.2]
→ lifecycle mixto Chat/Follow                             [generalizado en 4D.2]
→ Host y certificación vertical Follow                    [implementados en 4D.3]
→ certificación vertical equivalente Chat                 [publicada en 4D.3.1]
→ FTSTikFinityShareConverter                               [implementado en 4E.1]
→ FTSShareFamily produce candidato Flow Share              [implementado en 4E.1]
→ repositorio, binding y admisión Share                    [publicados en 4E.2]
→ dispatch y completion Share                              [publicados en 4E.2]
→ lifecycle mixto Chat/Follow/Share                        [generalizado en 4E.2]
→ Host compartido Chat/Follow/Share                        [publicado en 4E.3]
→ certificación JSON Share → Host                          [publicada en 4E.3]
→ FTSTikFinityLikeConverter                                [implementado localmente en 4F.1]
→ FTSLikePayload y candidato directo Flow Like             [implementados localmente en 4F.1]
──────────────────────── PUNTO ACTUAL ────────────────────────
Chat A → B → C                                             [completo]
Follow A → B → C                                           [completo]
Share A → B → C                                            [completo]
Like A                                                     [completo localmente]
Like B                                                     [pendiente]
Like C                                                     [pendiente]
→ puente UE5 TikFinityPlugin → Event Host                [trabajo futuro separado]
```

El MVP del core quedó compilado correctamente en 4B.1 y su runner portable terminó con
10 pruebas aprobadas y 0 fallos. Los contratos mínimos de 4B.2 fueron publicados y
compilados en `62d8491`, la familia Chat fue publicada y compilada en `b9c3998`, y el
repositorio tipado fue publicado y compilado en `bb7fdbd`, y el registro externo de
bindings fue publicado en `ca936b6`. El endurecimiento de ownership fue publicado y
compilado en `2923fb5`; su runner manual terminó con 8 PASS y 0 FAIL. La Fase 4B.6 fue
publicada y compilada en `2ff496e`: Core terminó con 10 PASS y 0 FAIL, y Pipeline con
13 PASS y 0 FAIL. La Fase 4B.7 fue publicada y compilada en `20ca6e2`: Core terminó
con 10 PASS y 0 FAIL, y Pipeline con 18 PASS y 0 FAIL. La Fase 4B.8 fue publicada en
`2bce321`: Core terminó con 10 PASS y 0 FAIL, y Pipeline con 28 PASS y 0 FAIL. Con ello
queda completo el vertical slice portable interno de Chat. La Fase 4C.1 añade
la capa Host y fue publicada en `6f71b20`; los resultados manuales fueron Core
10 PASS / 0 FAIL, Pipeline 28 PASS / 0 FAIL y Host 9 PASS / 0 FAIL. La Fase 4C.2 fue
publicada en `6f8c84a`; sus resultados manuales fueron Core 10 PASS / 0 FAIL,
Pipeline 28 PASS / 0 FAIL, Host 9 PASS / 0 FAIL y TikFinity Adapter 10 PASS / 0 FAIL.
La Fase 4C.3 fue publicada en `23dd4d2`. La validación manual certificó Core 10 PASS / 0
FAIL, Pipeline 28 PASS / 0 FAIL, Host 9 PASS / 0 FAIL, TikFinity Adapter 10 PASS / 0
FAIL, JSON Decoder 20 PASS / 0 FAIL y Checklist 10 PASS / 0 FAIL: 87 pruebas aprobadas
y 0 fallos. El probe manual alcanzó 7/7 eventos con 119 frames conocidos, 1 desconocido
(`config`), 0 inválidos, 0 errores de transporte y 0 frames binarios. La Fase 4D.1 fue
publicada en `c0bf886` y compilada por el propietario; no se registraron resultados
exactos de sus runners. La Fase 4D.2 fue publicada en `dc4f574`. La validación manual
certificó Core 10 PASS / 0 FAIL, Pipeline 42 PASS / 0 FAIL, Host 9 PASS / 0 FAIL,
Adapter 17 PASS / 0 FAIL, JSON Decoder 20 PASS / 0 FAIL y Checklist 10 PASS / 0 FAIL:
108 pruebas aprobadas y 0 fallos. El probe WebSocket no fue repetido después de 4D.2;
esa fase no modificó decoder, transporte ni probe.
La Fase 4D.3 fue publicada en `2416bf6` y completó el Host compartido y la primera
certificación JSON Follow → Host. La Fase 4D.3.1 fue publicada en `a63ad16`, separó
ambas integraciones del runner Host y añadió la certificación equivalente Chat. El
propietario certificó 118 PASS / 0 FAIL.
La Fase 4E.1 fue publicada en `f2527b2`; el propietario certificó el baseline completo
con 127 PASS / 0 FAIL. La Fase 4E.2 fue publicada en `b890407` y su certificación manual
terminó con 139 PASS / 0 FAIL. La Fase 4E.3 fue publicada en `3321a2a`; el propietario
certificó 148 PASS / 0 FAIL. La Fase 4F.1 implementa localmente Like A, sin compilación
ni ejecución de pruebas por el agente.

## 4. Contratos públicos actuales

### Frontera decodificada TikFinity Chat

`FTSTikFinityDecodedChatMessage` representa únicamente los datos necesarios para intentar
convertir un evento Chat ya decodificado. Conserva `EventName`, data opcional, usuario,
comentario y emotes mediante opcionales tipados; ausencia y cadena vacía siguen siendo
situaciones distintas. Los campos numéricos usan `int64_t` en esta frontera para validar
signo y rango antes de producir los `int32_t` portables.

`FTSTikFinityChatConverter` es estático y sin estado. Aplica una comparación exacta con
`"chat"`, exige data, identidad real y contenido, valida todos los campos numéricos y
emotes y sólo entonces construye `FTSChatInput`. Preserva comentario, orden y duplicados,
sin trim, inferencias, deduplicación ni validación de red.

Los rechazos normales se comunican mediante `ETSTikFinityChatConversionStatus`; sólo
`Converted` contiene `Input`. El convertidor termina al producir `FTSChatInput`: no
conoce Host, Pipeline operativo, cola, scheduling ni lifecycle. El decoder JSON nuevo
lo alimenta sin crear una conexión Adapter → Host.

### Decoder y checklist TikFinity de siete eventos

`TikFinityPlugin`, en el commit autoritativo
`d6af0eb9c6f329f314319e7ed759eea31cc90ccb`, fija los nombres, claves, tipos y rutas
que reproduce el adaptador portable. La unión cerrada `FTSTikFinityMappedEvent` contiene
exclusivamente `chat`, `gift`, `like`, `follow`, `share`, `roomUser` y `member`; pertenece
al adaptador y nunca entra al Host, Pipeline, repositorios o Core.

`FTSTikFinityJsonEventDecoder` valida el envelope y todos los campos presentes. Los
opcionales ausentes permanecen como `nullopt`; strings, booleanos, enteros, arrays y
objetos anidados exigen su tipo exacto. Los seis eventos basados en usuario común leen
los campos directamente desde `data`; `roomUser` conserva la estructura distinta
`data.topViewers[].user`. El decoder produce los contratos decodificados que consumen
los converters Chat, Follow, Share y Like; todavía no existen converters para las otras
tres familias.

`FTSTikFinityMappedEventFormatter` genera una representación estable para diagnóstico.
`FTSTikFinitySevenEventChecklist` cuenta frames válidos e inválidos por evento y mantiene
progreso monotónico de 0/7 a 7/7. Dos runners nuevos cubren fixtures JSON y checklist sin
abrir sockets.

`TikStudioTikFinitySevenEventProbe` es un ejecutable manual opcional, desactivado por
default. IXWebSocket sólo se descarga al habilitarlo y su `main` delega reglas JSON,
formato y cobertura a las piezas reutilizables. No está conectado al Event Host, no
admite eventos en la cola y no es un cliente portable de producción. La integración
definitiva de Unreal continuará usando `TikFinityPlugin` mediante un puente Blueprint/C++.

### Pipeline portable

`TikStudioEventPipelineContracts.h` define:

- `ETSEventFamilyKind` para las siete familias nativas;
- `FTSPayloadHandle` como identidad opaca y distinta de `FTSEmissionId`, con cero
  inválido;
- `ETSExternalEmissionState` para el estado externo del binding;
- `ETSProcessingResult` para el resultado futuro del procesamiento;
- `TTSAdmissionCandidate<TPayload>` con familia, request y payload tipado;
- `TTSFamilyDecision<TPayload>` como decisión opcional, donde vacío significa
  `NoEmission`;
- `FTSEmissionBinding` para asociar la identidad global del core con familia, flujo
  esperado, handle tipado externo y estado externo.

`EmissionId` es la única clave global. `FamilyKind` y `ExpectedFlow` sólo sirven como
metadatos de verificación y enrutamiento. Estos contratos no implementan ownership,
repositorios, procesamiento ni coordinación.

### Primera familia semántica: Chat

`FTSChatPayload` posee un snapshot completo de `FTSChatInput`. La clase sin estado
`FTSChatFamily` recibe el input por valor y devuelve siempre un
`TTSFamilyDecision<FTSChatPayload>` con candidato para un input normalizado válido:

- `FamilyKind = Chat`;
- `Flow = Chat`;
- ajuste de prioridad cero;
- sin override de TTL;
- sin protección especial ante evicción;
- snapshot completo de Comment, emotes y usuario.

La familia sólo produce datos. No llama al core, no asigna `EmissionId`, no almacena el
payload y no conoce repositorios, bindings ni procesadores.

### Segunda familia semántica: Follow

`FTSTikFinityFollowConverter` valida en orden envelope, nombre exacto `"follow"`, data,
usuario, identidad y representación numérica. El helper privado común convierte el
usuario decodificado a `FTSUserSnapshot`; Chat lo reutiliza sin cambiar su API, contenido,
defaults ni precedencia de errores. La identidad continúa siendo precondición de cada
converter, no responsabilidad del helper.

`FTSFollowPayload` posee un snapshot completo de `FTSFollowInput` y
`FTSFollowFamily::Decide` produce siempre un candidato directo con `FamilyKind = Follow`,
`Flow = Follow`, prioridad cero, sin override de TTL y sin protección especial. En 4D.2,
ese candidato ya recorre repositorio tipado, admisión, binding, dispatch, completion y
lifecycle compartido con Chat. La integración con Host quedó completa en 4D.3.

```text
Follow decoder          [implementado]
Follow decoded contract [implementado]
Follow converter        [implementado en 4D.1]
FTSFollowInput           [existente]
Follow family            [implementada en 4D.1]
Follow repository        [implementado en 4D.2]
Follow admission/binding [implementado en 4D.2]
Follow dispatch          [implementado en 4D.2]
Follow completion        [implementado en 4D.2]
Lifecycle Chat/Follow    [generalizado en 4D.2]
Follow Host              [implementado en 4D.3]
```

### Tercera familia semántica: Share

La fuente autoritativa Share contiene únicamente el usuario común. El contrato portable
existente `FTSShareInput` ya representa exactamente esos datos y no requiere cambios.
`FTSTikFinityShareConverter` aplica la misma frontera determinista de identidad y rangos
que Follow, reutilizando exclusivamente `FTSTikFinityDecodedUserConverter::TryConvert`
para construir el snapshot portable.

`FTSSharePayload` posee el input normalizado y `FTSShareFamily::Decide` produce siempre
un candidato directo con `FamilyKind = Share`, `Flow = Share`, prioridad cero, sin
override de TTL y sin protección especial. `ShareMilestone` permanece reservado porque
TikFinity no aporta un contador de hito y no existe umbral ni regla aprobada que permita
inferirlo.

En 4E.2, Share entra en el Coordinator compartido mediante un repositorio tipado propio,
admisión y binding `Share / Share`, dispatch propietario y completion terminal. Reutiliza
las rutas comunes sin crear otra cola, otro ready o un segundo `InFlight`. En 4E.3 se
incorpora al Host compartido y obtiene certificación vertical JSON → Host.

### Cuarta familia semántica: Like

`FTSTikFinityDecodedLikeMessage` y `FTSLikeInput` ya existían. En 4F.1,
`FTSTikFinityLikeConverter` valida envelope, data, usuario, identidad y los dos
contadores obligatorios antes de producir el input portable. `LikeCount` y
`TotalLikeCount` deben ser no negativos y representables en `int32_t`; cero es válido y
no se exige ninguna relación entre ambos. Los campos numéricos del usuario continúan
convirtiéndose mediante `FTSTikFinityDecodedUserConverter`.

`FTSLikePayload` posee el input normalizado completo. `FTSLikeFamily::Decide` es sin
estado y produce exclusivamente `FamilyKind = Like`, `Flow = Like`, prioridad cero,
sin override de TTL ni protección especial. Los contadores permanecen como datos del
snapshot: no calculan prioridad, acumulación, umbrales ni una segunda emisión.
`LikeUser` permanece reservado y todavía no existen repositorio, Coordinator, dispatch,
completion, Host ni integración vertical Like.

### Repositorios tipados de payloads

`TTSPayloadRepository<TPayload>` es un contenedor header-only reutilizable. Cada
instancia posee exclusivamente payloads de su tipo y asigna `FTSPayloadHandle` no cero
de forma monotónica, sin reutilizarlos durante la vida de la instancia. Su API ofrece:

- `Insert(TPayload)` por valor, con resultado opcional ante agotamiento;
- `Visit(Handle, Callback)` con acceso `const` limitado a la llamada;
- `Erase(Handle)`, que sólo tiene éxito una vez por entrada;
- `Size()` y `Empty()`.

`FTSChatPayloadRepository`, `FTSFollowPayloadRepository` y
`FTSSharePayloadRepository` son aliases tipados de instancias independientes. Ninguno
conoce identidades de emisión, flujos, familias, bindings, lifecycle events ni
procesadores. El handle sólo identifica una entrada de su instancia; varios
repositorios pueden asignar el mismo valor numérico porque `FamilyKind` enruta la
autoridad correcta. Los tres conservan autoridad exclusiva y no pueden copiarse ni
moverse.

`Provisional` no es un estado almacenado en el repositorio ni en los contratos. Es una
condición que sólo existe desde la perspectiva de la coordinación externa durante este
recorrido:

```text
payload insertado antes de la admisión
→ uso provisional definido por el coordinador

admisión aceptada
→ el mismo handle se vinculará a un EmissionId
→ el payload permanece en la misma entrada

admisión rechazada
→ el coordinador eliminará el payload provisional

terminal manejado
→ se eliminarán binding y payload
```

El repositorio no conoce `Enqueue`, lifecycle ni ninguna de estas etapas.

### Registro externo de bindings

`FTSEmissionBindingRegistry` es un componente header-only que posee exclusivamente los
metadatos externos asociados a cada `EmissionId`. Usa `EmissionId` como única clave y
rechaza IDs cero, handles cero, flujos inválidos y duplicados sin sobrescribir el binding
original.

Su API ofrece inserción, `Visit` con acceso `const` limitado a la llamada, transición
condicional por estado esperado, eliminación única y consultas `Size`/`Empty`. El
registro no almacena payloads, no conoce repositorios o familias concretas y no replica
los estados internos `Pending`/`InFlight` del core. El coordinador ya lo usa para crear
el binding Chat después de una admisión aceptada. Como autoridad estable de los bindings
por `EmissionId`, no puede copiarse, asignarse ni moverse.

### Coordinador de admisión Chat, Follow y Share

`FTSEventPipelineCoordinator` posee de forma privada y exclusiva el core, los
repositorios Chat/Follow/Share y el registro de bindings. Es no copiable y no movible,
y no expone referencias mutables a ninguna autoridad.

`SubmitChat`, `SubmitFollow` y `SubmitShare` conservan este orden mediante una guarda
provisional templada:

```text
familia tipada::Decide
→ insertar payload provisional con guarda RAII
→ Core.Enqueue
→ rechazo: procesar lifecycle y eliminar payload provisional
→ aceptación: liberar la guarda sin borrar
→ validar envelope e insertar binding
→ procesar lifecycle en orden
→ devolver el FTSEnqueueResult completo
```

`ETSPipelineAdmissionStatus` distingue `NoEmission`, agotamiento de identidad del
repositorio, rechazo del core y aceptación. `FTSPipelineAdmissionResult::EnqueueResult`
sólo contiene valor si el coordinador llegó a llamar al core.

Ante aceptación, el payload permanece en la misma entrada y el binding existe antes de
exponer un posible `AutoPumpOutcome`. Un fallo posterior se trata como
invariante interna mediante `std::logic_error`; nunca se simula rollback del core.

El handler privado de lifecycle acepta tandas mixtas Chat/Follow/Share. Valida primero
toda la tanda, incluida la pareja familia/flujo y la existencia del payload en su
repositorio; después aplica en orden `TerminalPendingHandling`, borrado tipado y borrado
del binding. Una familia todavía no integrada produce `std::logic_error`.

La inspección pública permite visitar bindings y payloads Chat/Follow/Share mediante
`EmissionId`, además de consultar sus conteos, sin exponer ownership ni referencias
mutables.

### Despacho autorizado de Chat, Follow y Share

El coordinador conserva como máximo una copia privada de `FTSEmissionEnvelope` en
`PendingReadyEmission`, compartida por Chat, Follow y Share porque el core sólo posee
un `InFlight`. Es una notificación de despacho pendiente, no una réplica del estado
autoritativo.

`CaptureCorePumpOutcome` ignora `NotRequested`, `QueueEmpty` y `Busy` sin eliminar una
notificación previa. Para `EmissionReady`, valida identidad, binding, pareja
familia/flujo soportada y estado `Bound`; nunca sobrescribe otro ready pendiente. El
`AutoPumpOutcome` que permanece dentro de `FTSEnqueueResult` es diagnóstico y no autoriza
un despacho creado por código externo.

`BeginChatProcessing()`, `BeginFollowProcessing()` y `BeginShareProcessing()` sólo
autorizan su propia familia. Si el ready pertenece a otra, devuelven
`NoEmissionReady` y preservan ready, binding y payload.
`PeekPendingReadyFamilyKind()` inspecciona el enrutamiento sin consumirlo ni autorizar
procesamiento. Los dispatches siguen siendo copias propietarias tipadas.

Las comprobaciones estáticas exigen que despacho y resultado puedan moverse sin lanzar.
Si cualquier copia o validación falla antes de la transición, binding, payload y ready
permanecen intactos y la operación puede reintentarse. Después de una transición
exitosa sólo quedan operaciones no lanzables. El payload original y el binding continúan
almacenados mientras la emisión permanece `Processing`.

### Finalización y lifecycle completo de Chat, Follow y Share

`CompleteChatProcessing`, `CompleteFollowProcessing` y `CompleteShareProcessing`
conservan wrappers públicos tipados sobre un helper común. Validan identidad, ausencia
de cualquier ready pendiente, familia, flujo, handle, estado y payload antes de
solicitar una transición terminal al core. `Succeeded` llama a `Confirm`; `Cancelled`
y `Failed` llaman a `CancelInFlight`, sin retry implícito.

El resultado portable conserva el `ETSProcessingResult` comunicado por el procesador y
expone exactamente uno de los resultados del core: `ConfirmResult` para `Succeeded` o
`CancelResult` para `Cancelled`/`Failed`. El lifecycle se valida en dos pasadas: primero
se comprueba la forma completa del lote y todas sus referencias externas sin mutación;
después se aplica, en el orden autoritativo del core, la transición a
`TerminalPendingHandling` y la eliminación del payload y binding. `Confirmed` y
`Cancelled` exigen estado `Processing`; expiración y evicción exigen `Bound`.

Tras un Confirm exitoso, el coordinador procesa primero todos los terminales y después
captura el posible siguiente `EmissionReady` del Auto Pump. Cancel no ejecuta Auto Pump;
el host debe llamar `Pump()` explícitamente para avanzar otra emisión. `Pump()`,
`ProcessDueExpirations()` y `GetNextWakeTime()` se exponen sin entregar una referencia
al core. Expiraciones `Discard` y `Consolidate` eliminan actualmente binding y payload
de su repositorio tipado; la consolidación semántica continúa fuera de alcance.

### Host portable compartido de ejecución Chat, Follow y Share

`FTSEventExecutionHost` es una biblioteca separada que posee un único coordinador y Core
mediante un PImpl no copiable ni movible. Su API pública permite `PostChat`,
`PostFollow`, `PostShare` y sus completions tipadas desde cualquier hilo, pero reserva
`RunOneCycle` al hilo que construyó la instancia. El Host no expone mutexes, bandeja,
thread ID ni el coordinador.

Una sola bandeja privada conserva inputs y finalizaciones Chat/Follow/Share bajo el mismo
mutex. Así, el FIFO global queda definido por el orden efectivo de inserción entre
familias. El mutex nunca permanece bloqueado durante una llamada a Pipeline. El booleano
de cada `Post*` sólo indica la transición de bandeja vacía a ocupada para que la
composición externa solicite un ciclo.

Cada `RunOneCycle` consume como máximo un comando, procesa expiraciones y consulta
`PeekPendingReadyFamilyKind()` antes de invocar exclusivamente el Begin tipado de Chat o
Follow o Share. Si no existe ready, llama Pump una vez; un `EmissionReady` se enruta y
entrega en ese mismo ciclo. El resultado contiene como máximo un dispatch propietario
dentro de un variant de las tres familias que permanece fuera del Pipeline y del Core.
Después consulta el próximo wake y si quedan comandos. La política continúa siendo
work-conserving.

El Host no crea threads, timers, callbacks, procesadores ni efectos. Una excepción al
procesar un comando consume únicamente ese comando y se propaga; los comandos posteriores
permanecen en la bandeja. La composición propietaria debe solicitar explícitamente otro
ciclo después de capturar el fallo si desea continuar.

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
liberando el slot. `Enqueue` intenta Auto Pump tras una admisión cuando el setting está
activo y el core está idle; `Confirm` hace lo mismo después de confirmar cuando su
setting está activo. `CancelInFlight` permanece sin Auto Pump.

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
- El scheduler externo decide cuándo ejecutar el Host; ni Host ni Core crean Tick o
  timers.
- `FTSNowProvider` es inyectable. Vacío se normaliza a
  `FTSEventQueueClock::now()`.
- Cada operación pública captura `Now` como máximo una vez y reutiliza esa instantánea
  en cualquier Auto Pump que forme parte de la misma llamada.
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
- El Host es work-conserving: los settings de Auto Pump no prohíben su Pump explícito.

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

## 7. Estado privado implementado hasta 4A.9

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
es compartido por la ruta interna de Pump. El índice de prioridad valida
existencia, estado `Pending`, Revision, Sequence y PriorityScore, y descarta entradas
stale sólo desde el frente.

`HasInFlightEmission` valida que un ID activo siga correspondiendo al record `InFlight`
autoritativo. `PumpAt` recibe un `Now`, procesa expiraciones, devuelve `Busy` si existe
una emisión activa, o selecciona el top vigente. La selección copia el envelope
público, cambia el record `Pending → InFlight`, conserva el record en `Records`, retira
la clave de prioridad y deja stale la clave temporal. `Pump`, `Enqueue` y `Confirm`
reutilizan esta única ruta interna.

`Confirm` y `CancelInFlight` comparten una validación que distingue ausencia de emisión
activa, ID solicitado distinto e invariante interna rota. El ciclo operativo actual es:

```text
Enqueue → Pending → Pump → InFlight → Confirmed | Cancelled
```

La finalización publica primero el lifecycle event. Sólo después elimina el record y
restablece `InFlightEmissionId` a cero; si la publicación lanza, la emisión permanece
íntegramente `InFlight`. El borrado autoritativo libera capacidad y deja las claves
derivadas restantes como stale para su limpieza lazy.

Tras una admisión completa, `Enqueue` ejecuta `PumpAt` sólo si
`bPumpAfterEnqueueWhenIdle` está activo y no existe emisión `InFlight`; reutiliza el
`Now` ya capturado para admitir. Si está ocupado, el Auto Pump queda `NotRequested`.
La emisión nueva compite normalmente con todos los candidatos por prioridad y
Sequence.

Tras validar un Confirm exitoso, `bPumpAfterConfirm` decide si se captura `Now`. Con el
setting desactivado no se captura tiempo. Con el setting activo, la captura ocurre antes
de mutar, se publica `Confirmed`, se elimina el record y luego `PumpAt` añade posibles
expiraciones antes de seleccionar. `CancelInFlight` no captura tiempo ni ejecuta Auto
Pump.

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
emisión `InFlight`. Confirm y CancelInFlight ya completan y eliminan esa emisión, y los
Auto Pump configurables de Enqueue y Confirm ya reutilizan `PumpAt`. Con esto queda
completo el MVP funcional básico del core. No existen todavía compactación, contadores
por flujo, almacenamiento de payloads, evicción ni aging. La capacidad se determina por
escaneo del map: un record `InFlight` ocupa slot hasta su confirmación o cancelación
exitosa.

## 9. Estructura, CMake y pruebas

Targets explícitos, sin `file(GLOB ...)`:

- `TikStudioEventCore` (STATIC): core central, settings y siete translation units de
  familias.
- `TikStudioEventPipeline` (STATIC): contratos portables, familias Chat, Follow, Share
  y Like, y coordinador Chat/Follow/Share de admisión, despacho y finalización; publica
  `Pipeline/Public` y enlaza públicamente sólo con Core. Like todavía no entra al
  Coordinator.
- `TikStudioEventHost` (STATIC): PImpl, bandeja thread-safe compartida y ciclo
  propietario de Chat/Follow/Share; publica `Host/Public`, enlaza públicamente con
  Pipeline y privadamente con `Threads::Threads`.
- `TikStudioEventSimulator` (STATIC): enlaza con Core; actualmente placeholder.
- `TikStudioTikFinityAdapter` (STATIC): publica contratos, decoder, formatter,
  checklist y converters Chat/Follow/Share/Like; enlaza públicamente sólo con Core y
  privadamente con `nlohmann_json::nlohmann_json`. El cliente de transporte sigue
  siendo placeholder.
- `TikStudioEventConsole` (executable): enlaza los tres targets; actualmente sólo
  imprime `TikStudioEventQueueLab ready.`.
- `TikStudioEventCoreTests` (executable): enlaza únicamente con Core y está registrado
  en CTest mediante `add_test`.
- `TikStudioEventPipelineTests` (executable): enlaza únicamente con Pipeline y está
  registrado en CTest mediante `add_test`.
- `TikStudioEventHostTests` (executable): enlaza únicamente con Host y certifica su
  comportamiento desde inputs normalizados; está registrado en CTest mediante
  `add_test`.
- `TikStudioVerticalIntegrationTests` (executable): compone Adapter y Host sólo para
  certificar los recorridos JSON Chat/Follow/Share → converter → Host → Pipeline → Core;
  está registrado en CTest mediante `add_test`.
- `TikStudioTikFinityAdapterTests` (executable): enlaza únicamente con el adaptador
  TikFinity y está registrado en CTest mediante `add_test`.
- `TikStudioTikFinityJsonDecoderTests` y `TikStudioTikFinityChecklistTests`
  (executables): enlazan únicamente con el adaptador y se registran en CTest.
- `TikStudioTikFinitySevenEventProbe` (executable opcional): sólo existe con
  `TIKSTUDIO_BUILD_TIKFINITY_PROBE=ON` y enlaza Adapter, IXWebSocket y Threads. No se
  registra como prueba.

La Fase 4D.2.1 organizó las suites por responsabilidad sin cambiar los seis ejecutables
automáticos existentes ni sus registros CTest. `TSTestHarness.h` conserva el contrato
común de ejecución y `TSTestSuites.h` declara registros explícitos, sin autorregistro
global ni dependencia del orden de link. El refinamiento 4D.3.1 añadió un séptimo runner
automático. Después de 4F.1, Pipeline, Host, Adapter y Vertical registran localmente
respectivamente 58, 25, 32 y 3 casos.

La estructura familiar queda así:

```text
Tests/Chat/  → Pipeline, Host, Adapter y certificación vertical Chat
Tests/Follow/ → Pipeline, Host, Adapter y certificación vertical Follow
Tests/Share/ → Pipeline, Host, Adapter y certificación vertical Share
Tests/Like/ → converter Adapter y familia Pipeline directa
Tests/Gift|Member|RoomUser/ → sólo .gitkeep
Tests/TSPipelineInfrastructureTests.cpp → repositorios, bindings y Coordinator
```

Core, JSON Decoder y Checklist permanecen en sus runners transversales sin dividirse.
La organización fue publicada en `0f57a675`. La primera compilación posterior detectó
la pérdida del ámbito de `std::chrono_literals` en las suites Pipeline y una llave
adicional en `TSChatHostTests.cpp`. La corrección publicada en `d2cfca3` restauró esos
ámbitos y la comprobación estática omitida. La validación manual posterior certificó
108 PASS / 0 FAIL.

`Tests/TikStudioEventQueueSystemTests.cpp` contiene un runner mínimo estándar que
continúa tras fallos, imprime PASS/FAIL y devuelve un código distinto de cero si alguna
prueba falla. Usa un `FTSNowProvider` controlado, sin tiempo real, sleeps ni threads.
No hay framework de pruebas ni dependencias externas.

La cobertura local comprueba mediante API pública:

- prioridad y FIFO;
- Auto Pump después de Enqueue y Enqueue ocupado;
- Confirm con Auto Pump y orden de lifecycle events;
- Cancel sin Auto Pump;
- expiración exacta en el límite de TTL e inmunidad de InFlight;
- capacidad contando Pending + InFlight y liberación por Confirm/Cancel;
- preservación de InFlight ante un ID de Confirm incorrecto.

Estas pruebas no acceden a `FImpl`, `Records` ni índices privados. La compilación
publicada de 4B.1 fue correcta y el runner terminó con 10 PASS y 0 FAIL.

`Tests/TikStudioEventPipelineTests.cpp` comprueba mediante API pública que Chat produce
un candidato con los defaults esperados, conserva íntegramente Comment, emotes y datos
de usuario, y no modifica el input original recibido por copia. La cobertura publicada
de 4B.4 también comprueba handles no cero y distintos, snapshots independientes,
`Visit`, handles inválidos, `Erase`, `Size`, `Empty` y no reutilización. La cobertura
publicada de 4B.5 comprueba inserción y consulta de bindings, validaciones, duplicados,
transiciones condicionales, eliminación única y tamaño/vacío. La Fase 4B.5a añadió
comprobaciones estáticas de que repositorio y registro no son copiables ni movibles; su
resultado manual publicado fue 8 PASS y 0 FAIL.

La cobertura publicada de 4B.6 añade cinco escenarios públicos: primera admisión con
Auto Pump, segunda admisión ocupada con `NotRequested`, rechazo por flujo deshabilitado,
rechazo por capacidad sin dañar la emisión previa e inspección de ID desconocido. El
resultado manual publicado fue Core 10 PASS / 0 FAIL y Pipeline 13 PASS / 0 FAIL.

La cobertura publicada de 4B.7 añade ausencia de ready, despacho autorizado,
preservación del primer ready ante un segundo `NotRequested`, copia propietaria
independiente y consumo único. En `20ca6e2`, los resultados manuales fueron Core
10 PASS / 0 FAIL y Pipeline 18 PASS / 0 FAIL.

La cobertura publicada de 4B.8 añade diez escenarios para finalización exitosa, captura
del siguiente ready, orden Confirmed → expiración, Cancelled, Failed terminal, rechazo
de un binding todavía `Bound`, Pump explícito tras Cancel, Busy sin perder ready y
expiraciones Discard/Consolidate con reloj controlado. En `2bce321`, los resultados
manuales fueron Core 10 PASS / 0 FAIL y Pipeline 28 PASS / 0 FAIL.

La cobertura publicada del tercer runner añade nueve escenarios: Host vacío,
publicaciones desde worker, FIFO y procesamiento asíncrono, completion desde worker,
avance tras Cancel, preservación de comandos posteriores a un fallo, expiración durante
procesamiento, rechazo del hilo incorrecto y Pump explícito con Auto Pump desactivado.
En `6f71b20`, los resultados manuales fueron Core 10 PASS / 0 FAIL, Pipeline 28 PASS /
0 FAIL y Host 9 PASS / 0 FAIL.

La cobertura publicada de 4D.2 conserva diez escenarios Adapter Chat y añade siete
Follow: conversión completa, defaults, evento no Follow, envelope/data/user inválidos,
identidad, límites numéricos e integración decoder → variante → converter. Pipeline
conserva los 30 casos previos y añade 12 escenarios de admisión, ready global, dispatch,
completion, expiración y lifecycle mixto Follow. La validación manual de `dc4f574`
terminó con Core 10, Pipeline 42, Host 9, Adapter 17, JSON Decoder 20 y Checklist 10
casos aprobados, sin fallos: 108 PASS / 0 FAIL.

La cobertura publicada de 4D.3 conserva los nueve casos Host Chat, añade ocho casos Host
Follow y una certificación vertical JSON Follow → decoder → converter → Host →
completion. La Fase 4D.3.1 movió ese caso a un runner vertical y añadió el equivalente
Chat; el propietario certificó 118 PASS / 0 FAIL.

La cobertura publicada de 4E.1 añadió siete casos Adapter Share y dos casos de familia
Pipeline Share; el propietario certificó 127 PASS / 0 FAIL. La Fase 4E.2 añadió doce
casos Coordinator Share para admisión, ready global, dispatch, completion, interacción
con Chat/Follow y expiración; fue publicada en `b890407` y certificada con 139 PASS / 0
FAIL. La Fase 4E.3 añadió ocho casos Host Share y una certificación JSON Share → Host;
fue publicada en `3321a2a` y certificada con 148 PASS / 0 FAIL. La cobertura local de
4F.1 añade dos casos de familia Pipeline Like y ocho casos Adapter Like. Sin ejecutar
los runners durante esta implementación, el resultado esperado para la validación
manual es: Core 10, Pipeline 58, Host 25, Adapter 32, JSON Decoder 20, Checklist 10 y
Vertical Integration 3; 158 casos.

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
- Commit `d504df7` —
  `feat(core): add in-flight confirmation and cancellation`.

### Fase 4A.9 — Auto Pump después de Enqueue y Confirm

- Añadió `HasInFlightEmission` como validación compartida del localizador activo.
- Extrajo `PumpAt` como única ruta interna de expiración y selección para Pump público,
  Enqueue y Confirm.
- Implementó Auto Pump tras Enqueue aceptado cuando el setting está activo y el core
  está idle, reutilizando el `Now` de admisión.
- Implementó Auto Pump tras Confirm cuando su setting está activo, capturando `Now`
  antes de finalizar y conservando el orden Confirmed → expiraciones.
- `CancelInFlight` permanece sin captura temporal ni Auto Pump.
- El MVP funcional básico del core queda completo; evicción, aging, payloads y familias
  siguen fuera.
- Commit `a1be3ce` —
  `feat(core): add auto pump after enqueue and confirm`.

### Fase 4B.1 — Pruebas portables deterministas del MVP

- Añadió `TikStudioEventCoreTests`, enlazado únicamente con `TikStudioEventCore` y
  registrado mediante CTest.
- Añadió un runner sin frameworks externos que ejecuta pruebas por nombre, continúa
  después de fallos y devuelve un código no cero cuando alguna falla.
- Añadió un reloj controlado mediante `FTSNowProvider`, sin esperas ni tiempo real.
- Cubrió prioridad, FIFO, Auto Pump, InFlight, Confirm, Cancel, TTL, capacidad,
  lifecycle events e ID incorrecto exclusivamente mediante la API pública.
- No modificó código de producción. La compilación fue correcta y las 10 pruebas
  terminaron con 0 fallos; el pipeline semántico continuó fuera de alcance.
- Commit `9efc3b9` —
  `test(core): add deterministic MVP queue tests`.

### Fase 4B.2 — Contratos mínimos del pipeline portable

- Creó `TikStudioEventPipeline` como biblioteca estática dependiente públicamente de
  `TikStudioEventCore`, sin dependencia inversa.
- Añadió los contratos de familia, handle opaco, estado externo, resultado de
  procesamiento, candidato tipado, decisión opcional y binding por EmissionId.
- Conservó el payload como parámetro de template y evitó payloads universales, estados
  internos del core y lógica específica de flujos derivados.
- No añadió familias reales, repositorios, coordinador, procesadores, bindings
  operativos ni nuevas pruebas.
- La biblioteca fue compilada correctamente.
- Commit `62d8491` —
  `feat(pipeline): add portable pipeline contracts`.

### Fase 4B.3 — Familia Chat y candidato tipado

- Añadió `FTSChatPayload` como snapshot propietario del input normalizado completo.
- Añadió `FTSChatFamily` sin estado, que produce un candidato Chat con defaults de
  admisión explícitos y payload tipado.
- La familia no llama al core, no genera IDs y no conoce almacenamiento ni coordinación.
- Añadió `TikStudioEventPipelineTests`, enlazado únicamente con Pipeline, para validar
  candidato, defaults, snapshot completo y preservación del input original.
- No añadió repositorios, coordinador, Enqueue real, bindings operativos, procesadores,
  otras familias ni flujos derivados.
- La fase fue compilada correctamente.
- Commit `b9c3998` —
  `feat(pipeline): add chat family candidate`.

### Fase 4B.4 — Repositorio tipado de payloads

- Añadió `TTSPayloadRepository<TPayload>` como contenedor header-only con ownership
  tipado, identidad monotónica no reutilizable y agotamiento explícito.
- Añadió acceso controlado mediante `Visit`, eliminación única mediante `Erase` y
  consultas `Size`/`Empty`, sin exponer punteros o referencias persistentes.
- Añadió `FTSChatPayloadRepository` como alias específico de `FTSChatPayload`.
- Amplió las pruebas del Pipeline para cubrir inserción, unicidad, snapshot, handles
  inválidos, borrado, tamaño, vacío y no reutilización.
- No conectó el repositorio con Chat ni añadió bindings, coordinador o llamada a
  `Enqueue`.
- La fase fue compilada correctamente.
- Commit `bb7fdbd` —
  `feat(pipeline): add typed payload repository`.

### Fase 4B.5 — Registro autoritativo de bindings

- Añadió `FTSEmissionBindingRegistry` como registro header-only indexado únicamente por
  `EmissionId`.
- Valida identidad, handle y flujo; rechaza duplicados; ofrece consulta acotada,
  transiciones condicionales, eliminación única y tamaño/vacío.
- No almacena payloads, no replica estados operativos del core y no conoce repositorios
  ni familias concretas.
- Amplió las pruebas del Pipeline para cubrir las invariantes del registro, sin conectar
  todavía Chat con `Enqueue`.
- Commit `ca936b6` —
  `feat(pipeline): add emission binding registry`.

### Fase 4B.5a — Endurecimiento de ownership

- Declaró explícitamente que `TTSPayloadRepository<TPayload>` y
  `FTSEmissionBindingRegistry` no pueden copiarse, asignarse ni moverse para preservar
  una única autoridad estable sobre handles y bindings.
- Aclaró que las referencias entregadas por `Visit` sólo viven durante el callback, no
  deben conservarse ni usarse para mutar reentrantemente la misma instancia.
- Documentó que el uso provisional del payload pertenece a la coordinación externa y
  no es un estado almacenado por el repositorio.
- Añadió comprobaciones estáticas de las garantías de ownership sin cambiar el
  comportamiento de las pruebas existentes.
- Chat continúa sin conectarse a `Enqueue`.
- La fase fue compilada correctamente y su runner manual terminó con 8 PASS y 0 FAIL.
- Commit `2923fb5` —
  `fix(pipeline): harden payload and binding ownership`.

### Fase 4B.6 — Coordinador de admisión Chat

- Añadió `FTSEventPipelineCoordinator` como propietario exclusivo del core, repositorio
  Chat y registro de bindings, sin copia ni movimiento.
- Conectó decisión Chat, payload provisional, `Core.Enqueue` y binding por `EmissionId`.
- Implementó rollback RAII sólo antes del compromiso del core y preservación del payload
  ante fallos posteriores a una aceptación.
- Añadió manejo ordenado de lifecycle de `Enqueue` para expiración y evicción, cuya
  cobertura por expiración queda aplazada.
- Expuso inspección controlada por `EmissionId` y conteos sin referencias mutables.
- Añadió cinco escenarios de admisión e inspección; no añadió despacho, procesamiento,
  `Confirm` ni `Cancel` coordinados.
- La fase fue compilada correctamente. Resultados manuales: Core 10 PASS / 0 FAIL y
  Pipeline 13 PASS / 0 FAIL.
- Commit `2ff496e` —
  `feat(pipeline): coordinate chat admission`.

### Fase 4B.7 — Despacho autorizado de Chat InFlight

- Añadió `FTSChatProcessingDispatch` como copia propietaria del envelope y payload Chat.
- Conserva una única notificación ready privada, escrita sólo desde outcomes obtenidos
  directamente por el coordinador y consumida una sola vez.
- Añadió `BeginChatProcessing()` sin parámetros; outcomes públicos permanecen sólo como
  información diagnóstica.
- Construye el resultado completo antes de `Bound → Processing`, preservando ready,
  binding y payload si falla cualquier operación previa.
- Mantiene el payload original almacenado e independiente de la copia entregada al host.
- Añadió cinco escenarios de despacho; no añadió `Confirm`, `CancelInFlight`, limpieza
  terminal, retries, timers, otras familias ni interfaces universales.
- La fase fue compilada correctamente. Resultados manuales: Core 10 PASS / 0 FAIL y
  Pipeline 18 PASS / 0 FAIL.
- Commit `20ca6e2` —
  `feat(pipeline): authorize chat processing dispatch`.

### Fase 4B.8 — Finalización y lifecycle completo de Chat

- Añadió `FTSChatProcessingCompletionResult` con invariantes exclusivas para el resultado
  de Confirm o Cancel.
- Conectó `Succeeded → Confirm` y `Cancelled/Failed → CancelInFlight`, validando primero
  que binding y payload pertenecen a la emisión Chat en `Processing`.
- Generalizó el lifecycle terminal a los cinco motivos mediante validación completa en
  dos pasadas y limpieza ordenada de binding y payload.
- Expuso `Pump`, `ProcessDueExpirations` y `GetNextWakeTime` a través del coordinador, sin
  exponer el core.
- Confirm captura el siguiente ready después de limpiar terminales; Cancel requiere Pump
  explícito. Discard y Consolidate limpian Chat sin acumulación semántica.
- Añadió diez escenarios deterministas. La fase fue compilada correctamente. Resultados
  manuales: Core 10 PASS / 0 FAIL y Pipeline 28 PASS / 0 FAIL.
- Commit `2bce321` —
  `feat(pipeline): complete chat processing lifecycle`.

### Fase 4C.1 — Host portable de ejecución Chat

- Añadió `TikStudioEventHost` como biblioteca separada sobre Pipeline, con PImpl y
  dependencia privada de `Threads::Threads`.
- Añadió una bandeja thread-safe para publicar inputs y finalizaciones desde cualquier
  hilo; el mutex serializa el orden efectivo de inserción FIFO.
- Fijó el hilo propietario durante la construcción y restringió a éste todas las llamadas
  al coordinador mediante `RunOneCycle`.
- Cada ciclo consume como máximo un comando, procesa expiraciones, intenta un ready,
  ejecuta Pump explícito si hace falta y devuelve como máximo un despacho propietario.
- Documentó y cubrió la política work-conserving: Auto Pump desactivado no prohíbe el
  Pump explícito del Host.
- Añadió un tercer runner con nueve escenarios. No fue compilado ni ejecutado por el
  agente durante la implementación; la validación manual posterior terminó con 9 PASS y
  0 FAIL, junto con Core 10 PASS / 0 FAIL y Pipeline 28 PASS / 0 FAIL.
- Simulator, TikFinity y Console permanecen desconectados; no existen threads, timers,
  callbacks o procesadores internos.
- Commit `6f71b20` —
  `feat(host): add portable chat execution host`.

### Fase 4C.2 — Contratos decodificados y conversión TikFinity Chat

- Añadió contratos decodificados específicos de Chat que distinguen ausencia de valores
  vacíos y conservan los numéricos como `int64_t` antes de normalizarlos.
- Añadió un convertidor sin estado que produce exclusivamente `FTSChatInput` y comunica
  rechazos normales mediante estados explícitos.
- Valida envelope, data, usuario, identidad, contenido, rangos `int32_t` y todos los
  emotes antes de construir el resultado convertido.
- Preserva comentario, orden y duplicados sin trim, inferencias o conversiones parciales.
- Actualizó el adaptador manteniendo sólo su enlace público con Core; no depende de
  Pipeline, Host o Threads.
- Añadió un cuarto runner con diez escenarios. La validación manual terminó con Core
  10 PASS / 0 FAIL, Pipeline 28 PASS / 0 FAIL, Host 9 PASS / 0 FAIL y TikFinity
  Adapter 10 PASS / 0 FAIL.
- El cliente de transporte continúa como placeholder y no existe conexión Adapter →
  Host.
- Commit `6f8c84a` —
  `feat(tikfinity): add chat conversion boundary`.

### Fase 4C.3 — Decoder JSON y checklist de siete eventos TikFinity

- Usa como referencia autoritativa el mapeo de `TikFinityPlugin` en
  `d6af0eb9c6f329f314319e7ed759eea31cc90ccb`.
- Añade contratos decodificados y una variante cerrada del adaptador para `chat`,
  `gift`, `like`, `follow`, `share`, `roomUser` y `member`.
- Añade un decoder determinista que valida envelope, tipos exactos, enteros `int64_t`,
  arrays y objetos anidados; los campos opcionales ausentes permanecen opcionales.
- Reutiliza directamente los contratos y el converter Chat de 4C.2. No añade converters
  para las otras seis familias.
- Añade un formatter estable y un checklist reutilizable con conteos por evento y
  progreso monotónico de 0/7 a 7/7.
- Añade dos runners automáticos sin sockets: uno con veinte áreas de decoder/formatter
  e integración Chat y otro con diez escenarios de checklist.
- Añade un probe WebSocket manual opcional. El ejecutable no duplica reglas JSON, no se
  registra en CTest y no se conecta al Host, Pipeline o Core operativo.
- Fija `nlohmann/json` v3.12.0 como dependencia privada del adapter e IXWebSocket v12.0.1
  exclusivamente detrás de `TIKSTUDIO_BUILD_TIKFINITY_PROBE=ON`.
- El cliente portable existente sigue siendo placeholder; el probe no es producción ni
  sustituye `TikFinityPlugin` en UE5.
- La validación manual terminó con Core 10 PASS / 0 FAIL, Pipeline 28 PASS / 0 FAIL,
  Host 9 PASS / 0 FAIL, TikFinity Adapter 10 PASS / 0 FAIL, JSON Decoder 20 PASS / 0
  FAIL y Checklist 10 PASS / 0 FAIL: 87 PASS / 0 FAIL en total.
- El probe manual alcanzó cobertura 7/7 con 119 frames conocidos, 1 desconocido
  (`config`), 0 inválidos, 0 errores de transporte y 0 frames binarios.
- Commit `23dd4d2` —
  `feat(tikfinity): decode and validate seven mapped events`.

### Fase 4D.1 — Follow: converter y decisión familiar

- Añade contratos y converter TikFinity Follow sin estado, con estados explícitos y
  validación ordenada antes de producir `FTSFollowInput`.
- Extrae la normalización común de usuario a un helper privado que valida los cuatro
  numéricos antes de construir el snapshot; Chat conserva comportamiento y API.
- Añade `FTSFollowPayload` propietario y `FTSFollowFamily`, que produce el candidato
  directo `FamilyKind = Follow` y `Flow = Follow` con defaults de admisión.
- Añade siete casos al runner del adapter y dos al runner del Pipeline.
- No añade repositorio, coordinador, bindings, lifecycle, procesamiento ni Host Follow.
- Fue publicada en `c0bf886` y compilada por el propietario; no se registraron resultados
  exactos de los runners.
- Commit `c0bf886` — `feat(follow): add conversion and family decision`.

### Fase 4D.2 — Follow: Pipeline completo y lifecycle

- Añade repositorio tipado Follow, aliases comunes de dispatch/completion y API pública
  de admisión, inspección, despacho y finalización Follow.
- Conserva una sola notificación ready global y permite inspeccionar su familia sin
  consumirla; un Begin de otra familia preserva todas las autoridades.
- Generaliza guarda provisional, admisión, dispatch, completion y lifecycle únicamente
  mediante templates y helpers privados sobre repositorios tipados separados.
- El lifecycle valida tandas mixtas completas antes de aplicar en orden la transición y
  limpieza en el repositorio seleccionado por `FamilyKind`.
- Añade doce escenarios Pipeline.
- No modifica Core, Host, Adapter, probe ni CMake.
- Fue publicada en `dc4f574`. La validación manual totalizó 108 PASS / 0 FAIL: Core 10,
  Pipeline 42, Host 9, Adapter 17, JSON Decoder 20 y Checklist 10.
- El probe WebSocket no fue repetido; decoder, transporte y probe permanecieron intactos.
- Commit `dc4f574` — `feat(follow): complete pipeline lifecycle`.

### Fase 4D.2.1 — Modularización de tests por familia

- Añade un harness común, declaraciones explícitas de suites y soporte compartido del
  Pipeline sin autorregistro global.
- Separa los cuerpos existentes en suites Chat, Follow e infraestructura, conservando
  los 42 casos Pipeline, 9 Host y 17 Adapter con los mismos nombres y orden.
- Crea los siete directorios familiares; Gift, Like, Member, RoomUser y Share sólo
  contienen `.gitkeep`.
- No cambia producción, lógica de pruebas, ejecutables, enlaces, CTest ni resultados
  esperados.
- Fue publicada en `0f57a675` con el nombre accidental
  `Simplify event queue lab implementation`, que se conserva sin reescritura.
- La primera compilación detectó que las suites Pipeline perdieron el ámbito de
  `chrono_literals` y que `TSChatHostTests.cpp` contenía una llave extra.
- La corrección restauró los ámbitos y el `static_assert` omitido sin alterar los casos
  existentes. Fue publicada en `d2cfca3` como
  `fix(tests): restore scopes after suite split`.
- La validación manual posterior certificó 108 PASS / 0 FAIL.

### Fase 4D.3 — Follow C: Host compartido y certificación vertical

- Generaliza el Host de Chat a `FTSEventExecutionHost`, sin alias ni conservación del
  contrato anterior, y mantiene PImpl, ownership exclusivo y owner thread.
- Chat y Follow comparten una sola bandeja FIFO, mutex, Coordinator y Core; cada ciclo
  procesa como máximo un comando y entrega como máximo un dispatch tipado.
- El ready se inspecciona mediante `PeekPendingReadyFamilyKind()` y se consume sólo con
  el `Begin*Processing` de la familia correspondiente. Un ready producido por Pump se
  despacha en el mismo ciclo.
- Añade `PostFollow` y `PostFollowCompletion`; la familia del ID se valida al ejecutar
  el comando en el owner thread, no durante la publicación.
- Conserva los nueve casos Host Chat, añade ocho Host Follow y una certificación
  JSON Follow → decoder → converter → Host → completion.
- El adaptador se enlaza con el runner Host sólo para la prueba vertical; no existe una
  dependencia Adapter → Host en producción ni conexión WebSocket → Host.
- Fue publicada en `2416bf6` como
  `feat(host): complete follow vertical integration`.

### Fase 4D.3.1 — Refinamiento de certificación vertical Chat/Follow

- Corrige una asimetría exclusiva de certificación: Chat y Follow ya tenían el mismo
  nivel funcional A → B → C, pero sólo Follow recorría JSON → Host en una prueba.
- Añade el caso vertical equivalente Chat sin reducir las comprobaciones del caso
  Follow existente.
- Separa las pruebas que comienzan con inputs portables de las integraciones que
  comienzan con JSON TikFinity.
- `TikStudioEventHostTests` vuelve a enlazar únicamente Host y registra 17 casos;
  `TikStudioVerticalIntegrationTests` enlaza Host y Adapter y registra los dos casos
  verticales en orden Chat, Follow.
- WebSocket → Host continúa pendiente para ambos eventos. No se modificó producción.
- Fue publicada en `a63ad16` como
  `test: align chat and follow vertical certification`.
- El propietario certificó 118 PASS / 0 FAIL.

### Fase 4E.1 — Share A: converter y decisión familiar directa

- Inicia Share como tercer evento sin modificar `FTSShareInput`; la fuente autoritativa
  contiene únicamente el usuario común.
- Añade contratos y converter Share con estados explícitos, validación ordenada y
  reutilización de `FTSTikFinityDecodedUserConverter::TryConvert`.
- Añade `FTSSharePayload` propietario y una familia sin estado que produce sólo
  `FamilyKind = Share` y `Flow = Share` con defaults de admisión.
- Mantiene `ShareMilestone` reservado: no existe contador, umbral ni regla aprobada para
  inferirlo.
- Añade siete casos Adapter y dos casos de familia Pipeline. Share todavía no tiene
  repositorio, Coordinator, Host ni integración vertical.
- Fue publicada en `f2527b2` como
  `feat(share): add conversion and direct family decision`.
- El propietario certificó 127 PASS / 0 FAIL.

### Fase 4E.2 — Share B: Pipeline completo y lifecycle

- Añade `FTSSharePayloadRepository`, aliases tipados de dispatch/completion y las APIs
  públicas de admisión, inspección, despacho y finalización Share.
- Reutiliza `SubmitDecision`, `BeginProcessing` y `CompleteProcessing`; no crea otra
  cola, registro de bindings, notificación ready o autoridad `InFlight`.
- Amplía la pareja soportada con `Share / Share` y enruta lifecycle al repositorio Share
  por `FamilyKind`, preservando la validación completa previa a mutaciones.
- Mantiene `ShareMilestone` reservado y no añade contadores, umbrales ni acumulación.
- Añade doce casos Coordinator Share. Host e integración vertical permanecían pendientes.
- Fue publicada en `b890407` como `feat(share): complete pipeline lifecycle`.
- El propietario certificó 139 PASS / 0 FAIL: Core 10, Pipeline 56, Host 17, Adapter
  24, JSON Decoder 20, Checklist 10 y Vertical Integration 2.

### Fase 4E.3 — Share C: Host compartido e integración vertical

- Añade `PostShare` y `PostShareCompletion` al `FTSEventExecutionHost` existente.
- Conserva una única FIFO global de inputs y completions, un owner thread, un
  Coordinator, un Core y un `InFlight` para Chat, Follow y Share.
- Amplía de forma append-only los variants privado de comandos y público de dispatch.
- Enruta el ready mediante `PeekPendingReadyFamilyKind()` y un único Begin tipado.
- Añade ocho casos Host Share y la certificación JSON Share → Host.
- Fue publicada en `3321a2a` como
  `feat(host): complete share vertical integration`.
- El propietario certificó 148 PASS / 0 FAIL: Core 10, Pipeline 56, Host 25, Adapter
  24, JSON Decoder 20, Checklist 10 y Vertical Integration 3.

### Fase 4F.1 — Like A: conversión y decisión familiar directa

- Reutiliza `FTSTikFinityDecodedLikeMessage` y `FTSLikeInput` existentes.
- Añade un converter tipado con estados explícitos; ambos contadores son obligatorios,
  no negativos y representables en `int32_t`, sin relación semántica entre ellos.
- Reutiliza `FTSTikFinityDecodedUserConverter` para el snapshot común del usuario.
- Añade `FTSLikePayload` y una familia sin estado que produce únicamente
  `FamilyKind = Like` y `Flow = Like` con los defaults de admisión.
- Mantiene `LikeUser` reservado y no añade repositorio, Coordinator, Host, acumulación,
  umbral ni lifecycle Like.
- Añade dos casos Pipeline y ocho casos Adapter. Los cambios permanecen locales y sin
  commit; no se compiló ni se ejecutaron pruebas durante la implementación.

## 11. Reglas de trabajo para la siguiente sesión

- Leer este documento y comprobar el estado Git actual antes de asumir que sigue en
  `3321a2a` más los cambios locales de 4F.1.
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
- El vertical slice portable interno de Chat, el Pipeline completo de Follow y la
  organización 4D.2.1 con su corrección están publicados.
- La Fase 4D.3 publicó el Host compartido y la certificación vertical Follow. El
  refinamiento 4D.3.1 publicó la certificación simétrica Chat/Follow y quedó validado
  manualmente con 118 PASS / 0 FAIL.
- La Fase 4E.1 publicó Share A y fue certificada con 127 PASS / 0 FAIL. La Fase 4E.2
  publicó Share B en `b890407` y fue certificada con 139 PASS / 0 FAIL. La Fase 4E.3
  publicó Share C en `3321a2a` y fue certificada con 148 PASS / 0 FAIL. La Fase 4F.1
  completa localmente Like A; no anticipar Like B o Like C ni diseñar otra familia.
- La migración UE5 es trabajo futuro separado:
  `TikFinityPlugin → puente Blueprint/C++ → FTS*Input → Event Host`.
- No añadir automáticamente conexión WebSocket → Host, nuevas familias, repositorios,
  bindings, Simulator, Console, procesadores concretos, efectos ni UE5 sin
  especificación separada.
