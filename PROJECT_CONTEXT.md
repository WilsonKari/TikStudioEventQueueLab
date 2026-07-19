# TikStudioEventQueueLab — contexto de transferencia

Última actualización: 2026-07-19.

Estado de referencia:
rama `main`, partiendo de HEAD `ba71fbc`
(`feat(host): complete gift vertical integration`).

El propietario certificó este baseline con 245 PASS / 0 FAIL. Los cambios del hardening
posterior a la auditoría permanecen locales y sin commit; durante su implementación no
se compiló ni se ejecutaron pruebas.

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
Like → FTSTikFinityLikeConverter             [publicado en 4F.1]
RoomUser → FTSTikFinityRoomUserConverter     [publicado en 4G.1]
Gift → FTSTikFinityGiftConverter             [publicado en 4H.1]
Member → converter tipado                    [pendiente]
        ↓
FTS*Input portable                            [Chat, Follow, Share, Like, RoomUser y Gift implementados]
        ↓
composición externa → Event Host             [Chat, Follow, Share, Like, RoomUser y Gift implementados]
```

El adaptador todavía no depende de Host o Pipeline. Las composiciones JSON
Chat/Follow/Share/Like/RoomUser/Gift → converter → Host existen sólo en el runner vertical;
no hay dependencia Adapter → Host en producción.

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
interpreta ni almacena. Chat, Follow, Share, Like, RoomUser y Gift disponen del recorrido
portable completo hasta Host y lifecycle. Member continúa sin implementación semántica.

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
Follow, Share, Like, RoomUser y Gift producen sus flujos directos; todavía no existe
lógica para los flujos derivados ni para Member. Los siete archivos de
`Core/Private/EventQueueSystem/Events/` sólo incluyen el header central y no contienen
implementación.

El recorrido previsto y el punto exacto alcanzado son:

```text
texto JSON TikFinity                                [implementado en Adapter]
→ decoder tipado de siete eventos                   [implementado]
→ FTSTikFinityMappedEvent                           [implementado]
→ contratos decodificados Chat, Follow, Share, Like, RoomUser y Gift [implementados]
→ FTSTikFinityChatConverter                         [implementado]
→ FTSChatInput portable                             [conversión implementada]
→ familia interpreta payload y elige flujo          [Chat, Follow, Share, Like, RoomUser y Gift]
→ decisión familiar / candidato de admisión tipado  [Chat, Follow, Share, Like, RoomUser y Gift]
→ coordinador inserta payload provisional            [seis familias, incluida Gift]
→ repositorio tipado de payloads                     [seis familias, incluida Gift]
→ FTSEnqueueRequest                                  [seis familias llaman Core.Enqueue]
→ validar flujo, enabled y TTL efectivo              [implementado en Enqueue]
→ comprobar capacidad por flujo                     [implementado por escaneo O(n)]
→ capturar tiempo / prioridad / expiración / ID      [implementado en Enqueue]
→ construir FTSEmissionEnvelope                     [implementado]
→ crear y almacenar record autoritativo Pending      [implementado]
→ indexar prioridad y expiración finita               [implementado]
→ Auto Pump tras Enqueue aceptado e idle               [implementado]
→ binding externo EmissionId → PayloadHandle          [seis familias conectadas]
→ lifecycle de Enqueue libera binding y payload       [generalizado en seis familias]
→ GetNextWakeTime consulta próximo vencimiento        [implementado]
→ ProcessDueExpirations elimina Pending vencidos      [implementado]
→ Pump selecciona y cambia Pending a InFlight          [implementado]
→ coordinador captura ready global de un solo uso       [seis familias]
→ Begin*Processing produce copia propietaria            [seis familias]
→ binding externo Bound → Processing                    [seis familias]
→ coordinador entrega despacho tipado propietario       [seis familias]
→ Confirm / Cancel elimina InFlight y emite terminal   [implementado]
→ Auto Pump tras Confirm exitoso                       [implementado]
→ Succeeded coordina Confirm                            [seis familias]
→ Cancelled / Failed coordinan CancelInFlight           [seis familias]
→ lifecycle terminal enruta y limpia payload tipado     [seis familias]
→ Confirm captura el siguiente ready multi-familia      [implementado]
→ Pump y expiración se exponen por el coordinador       [seis familias]
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
→ FTSTikFinityLikeConverter                                [publicado en 4F.1]
→ FTSLikePayload y candidato directo Flow Like             [publicados en 4F.1]
→ repositorio, binding y admisión Like                     [publicados en 4F.2]
→ dispatch y completion Like                               [publicados en 4F.2]
→ lifecycle mixto Chat/Follow/Share/Like                   [generalizado en 4F.2]
→ PostLike y PostLikeCompletion en Host compartido         [publicados en 4F.3]
→ Like en FIFO global y dispatch variant                   [publicado en 4F.3]
→ certificación JSON Like → Host                           [publicada en 4F.3]
→ FTSTikFinityRoomUserConverter                            [publicado en 4G.1]
→ FTSRoomUserPayload y candidato directo Flow RoomUser     [publicados en 4G.1]
→ repositorio, binding y admisión RoomUser                 [publicados en 4G.2]
→ dispatch y completion RoomUser                           [publicados en 4G.2]
→ lifecycle mixto Chat/Follow/Share/Like/RoomUser          [generalizado en 4G.2]
→ PostRoomUser y PostRoomUserCompletion en Host            [publicados en 4G.3]
→ RoomUser en FIFO global, owner y dispatch variant        [publicado en 4G.3]
→ certificación JSON RoomUser → Host                       [publicada en 4G.3]
→ FTSTikFinityGiftConverter                                [publicado en 4H.1]
→ FTSGiftPayload y candidato directo Flow Gift             [publicados en 4H.1]
→ repositorio, binding y admisión Gift                     [publicados en 4H.2]
→ dispatch y completion Gift                               [publicados en 4H.2]
→ lifecycle mixto de seis familias                         [generalizado en 4H.2]
→ PostGift y PostGiftCompletion en Host                    [publicados en 4H.3]
→ Gift en FIFO global, owner y dispatch variant            [publicado en 4H.3]
→ certificación JSON Gift → Host                           [publicada en 4H.3]
──────────────────────── PUNTO ACTUAL ────────────────────────
Chat    A → B → C                                          [completo]
Follow  A → B → C                                          [completo]
Share   A → B → C                                          [completo]
Like    A → B → C                                          [completo]
RoomUser A → B → C                                          [completo]
Gift A → B → C                                              [completo y publicado]
Member                                                      [pendiente]
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
certificó 148 PASS / 0 FAIL. La Fase 4F.1 fue publicada en `0081ee8`; el propietario
certificó 158 PASS / 0 FAIL. La Fase 4F.2 fue publicada en `46fdc41`; el propietario
certificó 170 PASS / 0 FAIL. La Fase 4F.3 fue publicada en `14bd28f`; el propietario
certificó 179 PASS / 0 FAIL. La Fase 4G.1 fue publicada en `e02f306`; el propietario
certificó 191 PASS / 0 FAIL. La Fase 4G.2 fue publicada en `14b9357`; su certificación
manual terminó con 203 PASS / 0 FAIL: Core 10, Pipeline 84, Host 33, Adapter 42, JSON
Decoder 20, Checklist 10 y Vertical Integration 4. La Fase 4G.3 fue publicada en
`f103f75`; su certificación manual terminó con 212 PASS / 0 FAIL: Core 10, Pipeline 84,
Host 41, Adapter 42, JSON Decoder 20, Checklist 10 y Vertical Integration 5. La Fase
4H.1 fue publicada en `0a75fb8`; el propietario certificó Core 10, Pipeline 86, Host
41, Adapter 52, JSON Decoder 20, Checklist 10 y Vertical Integration 5: 224 PASS / 0
FAIL. La Fase 4H.2 fue publicada en `427578b`. La Fase 4H.3 fue publicada en `ba71fbc`;
el propietario certificó Core 10, Pipeline 98, Host 49, Adapter 52, JSON Decoder 20,
Checklist 10 y Vertical Integration 6: 245 PASS / 0 FAIL.

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
los converters Chat, Follow, Share, Like, RoomUser y Gift; todavía no existe converter
para Member.

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
`FTSLikePayloadRepository` es un alias del repositorio genérico,
`FTSLikeProcessingDispatch` es un alias del dispatch genérico y
`FTSLikeProcessingCompletionResult` reutiliza el completion común. El Coordinator
admite Like sólo mediante la pareja `Like / Like`: `SubmitLike` coordina admisión y
binding, `BeginLikeProcessing` entrega un snapshot propietario y
`CompleteLikeProcessing` usa `Confirm` para `Succeeded` o `CancelInFlight` para
`Cancelled` y `Failed`. Like participa en el lifecycle compartido sin crear otro Core,
ready, `InFlight` o `BindingRegistry`.

En 4F.3, `PostLike` y `PostLikeCompletion` incorporan Like al Host compartido. Input y
completion viajan por el mismo FIFO global y se ejecutan exclusivamente en el mismo
owner thread. `FTSLikeProcessingDispatch` forma parte del dispatch variant propietario;
el recorrido JSON Like → converter → Host quedó certificado sin crear otro
Host, Coordinator, Core, ready o `InFlight`. `LikeUser` permanece reservado.

### Quinta familia semántica: RoomUser

`FTSTikFinityRoomUserConverter` consume la estructura decodificada específica de
`roomUser`, distinta del usuario común. Exige `data.viewerCount`; valida como enteros no
negativos representables en `int32_t` el contador superior, el ranking opcional y los
campos numéricos de cada top viewer. Cada entrada de `topViewers` exige `coinCount`,
objeto `user` e identidad no vacía; nickname, avatar, flags y niveles opcionales usan
sus defaults portables sin inferir relaciones entre valores.

`FTSRoomUserPayload` posee el `FTSRoomUserInput` completo, incluido el vector de
`FTSRoomUserTopViewer`. La conversión y el snapshot conservan orden, multiplicidad y
valores exactos: no ordenan, deduplican, truncan ni derivan significado adicional.
`FTSRoomUserFamily::Decide` es sin estado y produce exclusivamente
`FamilyKind = RoomUser`, `Flow = RoomUser`, prioridad cero, sin override de TTL ni
protección especial. `RoomUserMilestone` y `RoomUserTop1Change` permanecen reservados;
4G.1 no añadió estado específico ni flujos derivados.

En 4G.2, `FTSRoomUserPayloadRepository`, `FTSRoomUserProcessingDispatch` y
`FTSRoomUserProcessingCompletionResult` reutilizan las plantillas genéricas. El
Coordinator admite únicamente la pareja `RoomUser / RoomUser`, conserva el snapshot
completo durante admisión y procesamiento, y enruta sus terminales por las mismas
rutas Pending, Confirm y Cancel que las otras familias. RoomUser comparte el único
ready global, `InFlight`, Core y BindingRegistry.

En 4G.3, `PostRoomUser` y `PostRoomUserCompletion` incorporan RoomUser al Host
compartido. Input y completion usan la única bandeja FIFO y se ejecutan en el mismo
owner thread sobre el mismo Coordinator y Core. `FTSRoomUserProcessingDispatch` se
añade al dispatch variant existente, y la certificación JSON RoomUser → converter →
Host comprueba el snapshot profundo y el lifecycle terminal sin añadir rutas derivadas.

### Sexta familia semántica: Gift

Gift A es una integración directa sin semántica derivada específica de la familia.
`FTSTikFinityGiftConverter` exige envelope Gift, data, usuario e identidad, además de
`GiftId`, `GiftName` no vacío y `DiamondCount`. Los campos numéricos Gift y del usuario
deben ser no negativos y representables como `int32_t`; el converter reutiliza
`FTSTikFinityDecodedUserConverter` y conserva strings sin trim ni normalización.

`FTSGiftPayload` posee el `FTSGiftInput` completo por valor. `FTSGiftFamily::Decide`
produce exclusivamente `FamilyKind = Gift` y `Flow = Gift`, con prioridad cero, sin
override de TTL ni protección especial. `RepeatCount`, `GiftType`, `bRepeatEnd` y
`GroupId` se preservan como datos crudos: no agrupan, acumulan ni activan `GiftCombo`.
`GiftCombo` permanece reservado hasta una fase específica que defina estado,
agrupación y cierre.

Gift B añade `FTSGiftPayloadRepository`, `FTSGiftProcessingDispatch` y
`FTSGiftProcessingCompletionResult`, más `SubmitGift`, `BeginGiftProcessing`,
`CompleteGiftProcessing`, visita por `EmissionId` y conteo tipado en el Coordinator.
Sólo admite la pareja `Gift / Gift`; conserva los metadatos de repetición dentro del
snapshot y generaliza las rutas Pending, Confirm y Cancel sin introducir estado de
combo. Gift comparte el único Core, BindingRegistry, ready e `InFlight`.

Gift C añade `PostGift` y `PostGiftCompletion` al Host compartido, incorpora input y
completion al único FIFO y ejecuta `SubmitGift`/`CompleteGiftProcessing` sólo desde el
owner thread. `FTSGiftProcessingDispatch` es la sexta alternativa propietaria del
dispatch variant. La certificación vertical recorre JSON Gift → decoder → converter →
Host → Pipeline → Core y confirma el terminal sin interpretar metadatos de repetición.
El mantenimiento de expiraciones precede al comando del ciclo para liberar capacidad
vencida antes de una nueva admisión. El Host sólo acepta `Flow = Gift`; `GiftCombo`
continúa reservado.

### Repositorios tipados de payloads

`TTSPayloadRepository<TPayload>` es un contenedor header-only reutilizable. Cada
instancia posee exclusivamente payloads de su tipo y asigna `FTSPayloadHandle` no cero
de forma monotónica, sin reutilizarlos durante la vida de la instancia. Su API ofrece:

- `Insert(TPayload)` por valor, con resultado opcional ante agotamiento;
- `Visit(Handle, Callback)` con acceso `const` limitado a la llamada;
- `Erase(Handle)`, que sólo tiene éxito una vez por entrada;
- `Size()` y `Empty()`.

`FTSChatPayloadRepository`, `FTSFollowPayloadRepository`,
`FTSSharePayloadRepository`, `FTSLikePayloadRepository`,
`FTSRoomUserPayloadRepository` y `FTSGiftPayloadRepository` son aliases tipados de
instancias independientes. Ninguno conoce identidades de emisión, flujos, familias,
bindings, lifecycle events ni procesadores. El handle sólo identifica una entrada de
su instancia; varios repositorios pueden asignar el mismo valor numérico porque
`FamilyKind` enruta la autoridad correcta. Los seis conservan autoridad exclusiva y
no pueden copiarse ni moverse.

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
bindings Chat, Follow, Share, Like, RoomUser y Gift después de cada admisión aceptada. Como autoridad
estable de los bindings por `EmissionId`, no puede copiarse, asignarse ni moverse.

### Coordinador de admisión Chat, Follow, Share, Like, RoomUser y Gift

`FTSEventPipelineCoordinator` posee de forma privada y exclusiva el core, los
repositorios Chat/Follow/Share/Like/RoomUser/Gift y el registro de bindings. Es no
copiable y no movible, y no expone referencias mutables a ninguna autoridad.

`SubmitChat`, `SubmitFollow`, `SubmitShare`, `SubmitLike`, `SubmitRoomUser` y
`SubmitGift` conservan este orden mediante una guarda provisional templada:

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

El handler privado de lifecycle acepta tandas mixtas
Chat/Follow/Share/Like/RoomUser/Gift. Valida primero toda la tanda, incluida la pareja
familia/flujo y la existencia del payload en su repositorio; después aplica en orden
`TerminalPendingHandling`, borrado tipado y borrado del binding. Una familia todavía no
integrada produce `std::logic_error`.

La inspección pública permite visitar bindings y payloads
Chat/Follow/Share/Like/RoomUser/Gift mediante `EmissionId`, además de consultar sus
conteos, sin exponer ownership ni referencias mutables.

### Despacho autorizado de Chat, Follow, Share, Like, RoomUser y Gift

El coordinador conserva como máximo una copia privada de `FTSEmissionEnvelope` en
`PendingReadyEmission`, compartida por las seis familias porque el core sólo
posee un `InFlight`. Es una notificación de despacho pendiente, no una réplica del
estado autoritativo.

`CaptureCorePumpOutcome` ignora `NotRequested`, `QueueEmpty` y `Busy` sin eliminar una
notificación previa. Para `EmissionReady`, valida identidad, binding, pareja
familia/flujo soportada y estado `Bound`; nunca sobrescribe otro ready pendiente. El
`AutoPumpOutcome` que permanece dentro de `FTSEnqueueResult` es diagnóstico y no autoriza
un despacho creado por código externo.

`BeginChatProcessing()`, `BeginFollowProcessing()`, `BeginShareProcessing()`,
`BeginLikeProcessing()`, `BeginRoomUserProcessing()` y `BeginGiftProcessing()` sólo
autorizan su propia familia. Si el ready pertenece a otra, devuelven `NoEmissionReady`
y preservan ready, binding y payload.
`PeekPendingReadyFamilyKind()` inspecciona el enrutamiento sin consumirlo ni autorizar
procesamiento. Los dispatches siguen siendo copias propietarias tipadas.

Las comprobaciones estáticas exigen que despacho y resultado puedan moverse sin lanzar.
Si cualquier copia o validación falla antes de la transición, binding, payload y ready
permanecen intactos y la operación puede reintentarse. Después de una transición
exitosa sólo quedan operaciones no lanzables. El payload original y el binding continúan
almacenados mientras la emisión permanece `Processing`.

### Finalización y lifecycle completo de Chat, Follow, Share, Like, RoomUser y Gift

`CompleteChatProcessing`, `CompleteFollowProcessing`, `CompleteShareProcessing`,
`CompleteLikeProcessing`, `CompleteRoomUserProcessing` y `CompleteGiftProcessing`
conservan wrappers públicos tipados sobre un helper común.
Validan identidad, ausencia de cualquier ready pendiente, familia, flujo, handle,
estado y payload antes de solicitar una transición terminal al core. `Succeeded` llama
a `Confirm`; `Cancelled` y `Failed` llaman a `CancelInFlight`, sin retry implícito.

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

La auditoría posterior a 4H.3 identificó como riesgo pendiente las excepciones que
ocurran después de que el Core ya comprometió una admisión o terminal, pero antes de
sincronizar `BindingRegistry`, repositorio y ready. Resolver esa garantía requiere una
fase de diseño independiente; este hardening no añade rollback ni modifica
`SubmitDecision`, `CompleteProcessing` o lifecycle.

### Host portable compartido de ejecución de seis familias

`FTSEventExecutionHost` es una biblioteca separada que posee un único coordinador y Core
mediante un PImpl no copiable ni movible. Su API pública permite `PostChat`,
`PostFollow`, `PostShare`, `PostLike`, `PostRoomUser`, `PostGift` y sus completions
tipadas desde cualquier hilo, pero reserva `RunOneCycle` al hilo que construyó la
instancia. El Host no expone mutexes, bandeja, thread ID ni el coordinador.

Una sola bandeja privada conserva inputs y finalizaciones de las seis familias bajo el
mismo mutex. Así, el FIFO global queda definido por el orden efectivo de inserción entre
familias. El mutex nunca permanece bloqueado durante una llamada a Pipeline. El booleano
de cada `Post*` sólo indica la transición de bandeja vacía a ocupada para que la
composición externa solicite un ciclo.

Cada `RunOneCycle` valida primero el owner thread, procesa expiraciones con la bandeja
intacta y sólo después retira como máximo un comando del frente. Así, un fallo del
`NowProvider` o del mantenimiento no consume el comando y una expiración puede liberar
capacidad antes de la admisión. Después procesa el comando, consume cualquier ready ya
capturado, llama Pump una vez si no hubo dispatch, consulta el próximo wake y comunica
si quedan comandos. El variant propietario cubre Chat, Follow, Share, Like, RoomUser y
Gift, y la política continúa siendo work-conserving.

El Host no crea threads, timers, callbacks, procesadores ni efectos. Una excepción al
procesar un comando consume únicamente ese comando y se propaga; los comandos posteriores
permanecen en la bandeja. La composición propietaria debe solicitar explícitamente otro
ciclo después de capturar el fallo si desea continuar. Antes de destruir el Host, todos
los productores deben detenerse y finalizar; el destructor no coordina publicaciones
concurrentes ni implementa un protocolo de shutdown.

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
- `TikStudioEventPipeline` (STATIC): contratos portables y familias Chat, Follow, Share,
  Like, RoomUser y Gift; el coordinador de admisión, despacho y finalización cubre
  Chat/Follow/Share/Like/RoomUser/Gift;
  publica `Pipeline/Public` y enlaza públicamente sólo con Core.
- `TikStudioEventHost` (STATIC): PImpl, bandeja thread-safe compartida y ciclo
  propietario de Chat/Follow/Share/Like/RoomUser/Gift; publica `Host/Public`, enlaza
  públicamente con Pipeline y privadamente con `Threads::Threads`.
- `TikStudioEventSimulator` (STATIC): enlaza con Core; actualmente placeholder.
- `TikStudioTikFinityAdapter` (STATIC): publica contratos, decoder, formatter,
  checklist y converters Chat/Follow/Share/Like/RoomUser/Gift; enlaza públicamente sólo con
  Core y privadamente con `nlohmann_json::nlohmann_json`. El cliente de transporte
  sigue siendo placeholder.
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
  certificar los recorridos JSON Chat/Follow/Share/Like/RoomUser/Gift → converter →
  Host → Pipeline → Core; está registrado en CTest mediante `add_test`.
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
automático. En el baseline publicado de 4H.2, Pipeline, Host, Adapter y Vertical
registran respectivamente 98, 41, 52 y 5 casos. Los cambios locales de 4H.3 elevan Host
a 49 y Vertical a 6, sin modificar Pipeline ni Adapter.

La estructura familiar queda así:

```text
Tests/Chat/  → Pipeline, Host, Adapter y certificación vertical Chat
Tests/Follow/ → Pipeline, Host, Adapter y certificación vertical Follow
Tests/Share/ → Pipeline, Host, Adapter y certificación vertical Share
Tests/Like/ → Pipeline, Host, Adapter y certificación vertical Like
Tests/RoomUser/ → Pipeline, Host, Adapter y certificación vertical RoomUser
Tests/Gift/ → Pipeline, Host, Adapter y certificación vertical Gift
Tests/Member/ → sólo .gitkeep
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
fue publicada en `3321a2a` y certificada con 148 PASS / 0 FAIL. La Fase 4F.2 añadió
doce casos Coordinator Like y fue publicada en `46fdc41`; el propietario certificó 170
PASS / 0 FAIL. La Fase 4F.3 añadió ocho casos Host Like y una certificación JSON Like →
Host; fue publicada en `14bd28f` y certificada con 179 PASS / 0 FAIL: Core 10, Pipeline
70, Host 33, Adapter 32, JSON Decoder 20, Checklist 10 y Vertical Integration 4. La
Fase 4G.1 añadió diez casos Adapter RoomUser y dos casos de familia Pipeline RoomUser;
fue publicada en `e02f306` y certificada con 191 PASS / 0 FAIL: Core 10, Pipeline 72,
Host 33, Adapter 42, JSON Decoder 20, Checklist 10 y Vertical Integration 4. La Fase
4G.2 añadió doce escenarios Coordinator RoomUser, fue publicada en `14b9357` y fue
certificada con 203 PASS / 0 FAIL: Core 10, Pipeline 84, Host 33, Adapter 42, JSON
Decoder 20, Checklist 10 y Vertical Integration 4. La Fase 4G.3 añadió ocho escenarios
Host y una certificación JSON RoomUser → Host; fue publicada en `f103f75` y certificada
con 212 PASS / 0 FAIL: Core 10, Pipeline 84, Host 41, Adapter 42, JSON Decoder 20,
Checklist 10 y Vertical Integration 5. La Fase 4H.1 añadió diez escenarios Adapter
Gift y dos casos de familia Pipeline Gift; fue publicada en `0a75fb8` y certificada con
224 PASS / 0 FAIL: Core 10, Pipeline 86, Host 41, Adapter 52, JSON Decoder 20,
Checklist 10 y Vertical Integration 5. La Fase 4H.2 añadió doce escenarios Coordinator
Gift y fue publicada en `427578b`; Pipeline registra 98 casos. La Fase 4H.3 añadió ocho
escenarios Host Gift y una certificación JSON Gift → Host, fue publicada en `ba71fbc` y
el propietario certificó: Core 10, Pipeline 98, Host 49, Adapter 52, JSON Decoder 20,
Checklist 10 y Vertical Integration 6; 245 PASS / 0 FAIL. El hardening local posterior
añade ocho casos Host: `Failed` para las seis familias, retención FIFO ante mantenimiento
lanzable y partición de expiraciones anterior a una completion. Sin ejecución local,
los runners registran Core 10, Pipeline 98, Host 57, Adapter 52, JSON Decoder 20,
Checklist 10 y Vertical Integration 6; 253 casos.

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
- Añade dos casos Pipeline y ocho casos Adapter.
- Fue publicada en `0081ee8` como
  `feat(like): add conversion and direct family decision`.
- El propietario certificó 158 PASS / 0 FAIL: Core 10, Pipeline 58, Host 25, Adapter
  32, JSON Decoder 20, Checklist 10 y Vertical Integration 3.

### Fase 4F.2 — Like B: lifecycle completo en el Pipeline compartido

- Añade aliases tipados para repositorio, dispatch y resultado de completion Like.
- Integra `SubmitLike`, `BeginLikeProcessing`, `CompleteLikeProcessing`, visita por
  `EmissionId` y conteo de payloads en el Coordinator existente.
- Generaliza el routing de payload y las tres rutas de lifecycle para la pareja
  exclusiva `Like / Like`, sin crear otro Core, ready, `InFlight` o `BindingRegistry`.
- Añade doce escenarios Coordinator Like; Pipeline registra 70 casos.
- Mantiene `LikeUser` reservado y no añade Host ni integración vertical Like.
- Fue publicada en `46fdc41` como `feat(like): complete pipeline lifecycle`.
- El propietario certificó 170 PASS / 0 FAIL: Core 10, Pipeline 70, Host 25, Adapter
  32, JSON Decoder 20, Checklist 10 y Vertical Integration 3.

### Fase 4F.3 — Like C: Host compartido e integración vertical

- Añade `PostLike` y `PostLikeCompletion` al `FTSEventExecutionHost` existente.
- Incorpora input y completion Like al único FIFO global y al visitor del owner thread.
- Amplía el dispatch variant con `FTSLikeProcessingDispatch` y enruta Like mediante el
  mismo Coordinator, Core, ready e `InFlight`.
- Añade ocho casos Host Like y una certificación JSON Like → converter → Host →
  completion. Host registra 33 casos y Vertical Integration 4.
- Mantiene `LikeUser` reservado y no añade acumulación, thresholds ni emisiones
  derivadas.
- Fue publicada en `14bd28f` como `feat(host): complete like vertical integration`.
- El propietario certificó 179 PASS / 0 FAIL: Core 10, Pipeline 70, Host 33, Adapter
  32, JSON Decoder 20, Checklist 10 y Vertical Integration 4.

### Fase 4G.1 — RoomUser A: conversión y decisión familiar directa

- Añade contratos y converter RoomUser específicos para `data.topViewers[].user`, sin
  forzar esa estructura a través del usuario común.
- Valida `ViewerCount`, `TopGifterRank` y los campos numéricos de cada top viewer como
  enteros no negativos representables en `int32_t`.
- Conserva el input portable completo por valor, incluido el vector ordenado con su
  multiplicidad original.
- Añade `FTSRoomUserPayload` y una familia sin estado que produce exclusivamente
  `FamilyKind = RoomUser` y `Flow = RoomUser` con los defaults de admisión.
- Mantiene `RoomUserMilestone` y `RoomUserTop1Change` reservados; no añade repositorio,
  Coordinator, Host, lifecycle ni integración vertical RoomUser.
- Añade diez escenarios Adapter y dos escenarios Pipeline; Pipeline registra 72 casos
  y el total certificado alcanza 191.
- Fue publicada en `e02f306` como
  `feat(room-user): add conversion and direct family decision`.
- El propietario certificó 191 PASS / 0 FAIL: Core 10, Pipeline 72, Host 33, Adapter
  42, JSON Decoder 20, Checklist 10 y Vertical Integration 4.

### Fase 4G.2 — RoomUser B: lifecycle completo en el Pipeline compartido

- Añade aliases tipados para repositorio, dispatch y resultado de completion RoomUser.
- Integra `SubmitRoomUser`, `BeginRoomUserProcessing`, `CompleteRoomUserProcessing`,
  visita por `EmissionId` y conteo de payloads en el Coordinator existente.
- Generaliza validación y limpieza de lifecycle en las rutas Pending, Confirm y Cancel
  para la pareja exclusiva `RoomUser / RoomUser`.
- Comparte con las otras familias el único Core, BindingRegistry, ready e `InFlight`.
- Añade doce escenarios Coordinator RoomUser; Pipeline registra 84 casos.
- Mantiene reservados `RoomUserMilestone` y `RoomUserTop1Change`, sin añadir Host ni
  integración vertical RoomUser.
- Fue publicada en `14b9357` como `feat(room-user): complete pipeline lifecycle`.
- El propietario certificó 203 PASS / 0 FAIL: Core 10, Pipeline 84, Host 33, Adapter
  42, JSON Decoder 20, Checklist 10 y Vertical Integration 4.

### Fase 4G.3 — RoomUser C: Host compartido e integración vertical

- Añade `PostRoomUser` y `PostRoomUserCompletion` al `FTSEventExecutionHost` existente.
- Incorpora input y completion RoomUser a la única FIFO global y al visitor del owner
  thread, sin crear otro Host, Coordinator, Core, ready o `InFlight`.
- Amplía de forma append-only el enum de comandos, el variant privado de comandos y el
  dispatch variant público con la ruta exclusiva `RoomUser / RoomUser`.
- Enruta el ready mediante `PeekPendingReadyFamilyKind()` y
  `BeginRoomUserProcessing()`, y valida la familia de completions en el Coordinator.
- Añade ocho casos Host RoomUser y una certificación JSON RoomUser → converter → Host
  → completion. El resultado esperado queda en Host 41, Vertical Integration 5 y 212
  casos totales.
- Mantiene `RoomUserMilestone` y `RoomUserTop1Change` reservados; no añade comparación
  de snapshots, historial, acumulación, deduplicación ni lógica derivada.
- Fue publicada en `f103f75` como
  `feat(host): complete room-user vertical integration`.
- El propietario certificó 212 PASS / 0 FAIL: Core 10, Pipeline 84, Host 41, Adapter
  42, JSON Decoder 20, Checklist 10 y Vertical Integration 5.

### Fase 4H.1 — Gift A: conversión y decisión familiar directa

- Añade contratos y converter Gift con precedencia explícita para envelope, data,
  usuario, identidad, campos obligatorios y representación numérica.
- Reutiliza el converter del usuario común y preserva strings, booleanos y metadatos de
  repetición sin trim, normalización ni inferencias.
- Añade `FTSGiftPayload` y una familia sin estado que produce exclusivamente
  `FamilyKind = Gift` y `Flow = Gift` con los defaults de admisión.
- Conserva `RepeatCount`, `GiftType`, `bRepeatEnd` y `GroupId` como datos crudos;
  `GiftCombo` continúa reservado y no existe estado de combo.
- Añade diez escenarios Adapter Gift y dos escenarios Pipeline Gift. La certificación
  publicada dejó Adapter 52, Pipeline 86 y 224 casos totales.
- No añade repositorio, Coordinator, dispatch, completion, lifecycle, Host ni
  integración vertical Gift; Member permanece intacto.
- Fue publicada en `0a75fb8` como
  `feat(gift): add conversion and direct family decision`.
- El propietario certificó 224 PASS / 0 FAIL: Core 10, Pipeline 86, Host 41, Adapter
  52, JSON Decoder 20, Checklist 10 y Vertical Integration 5.

### Fase 4H.2 — Gift B: Pipeline completo y lifecycle

- Añade aliases tipados para repositorio, dispatch y resultado de completion Gift.
- Integra `SubmitGift`, `BeginGiftProcessing`, `CompleteGiftProcessing`, visita por
  `EmissionId` y conteo de payloads en el Coordinator existente.
- Generaliza validación y limpieza de lifecycle Pending, Confirm y Cancel para la
  pareja exclusiva `Gift / Gift`, compartiendo Core, BindingRegistry, ready e
  `InFlight` con las otras cinco familias.
- Preserva `RepeatCount`, `GiftType`, `bRepeatEnd` y `GroupId` como snapshot crudo; no
  admite ni infiere `GiftCombo`.
- Añade doce escenarios Coordinator Gift; Pipeline registra 98 casos y el total
  esperado queda en 236.
- Host e integración vertical Gift permanecen pendientes; Member permanece intacto.
- Fue publicada en `427578b` como `feat(gift): complete pipeline lifecycle`.

### Fase 4H.3 — Gift C: Host compartido e integración vertical

- Añade `PostGift` y `PostGiftCompletion` al `FTSEventExecutionHost` existente.
- Incorpora input y completion Gift a la única bandeja FIFO y al visitor del owner
  thread, sin crear otro Host, Coordinator, Core, ready o `InFlight`.
- Amplía append-only el enum de comandos, el variant privado y el dispatch variant
  público con la ruta exclusiva `Gift / Gift`.
- Añade ocho casos Host Gift y una certificación JSON Gift → decoder → converter → Host
  → completion; Host registra 49 casos y Vertical Integration 6.
- Conserva `RepeatCount`, `GiftType`, `bRepeatEnd` y `GroupId` sin interpretación;
  `GiftCombo` continúa reservado y Member permanece pendiente.
- Fue publicada en `ba71fbc` como `feat(host): complete gift vertical integration`; el
  propietario certificó 245 PASS / 0 FAIL.

### Hardening posterior a la auditoría

- Ejecuta mantenimiento antes de retirar el comando del FIFO, de modo que una excepción
  del reloj o de expiración preserve todo el trabajo pendiente sin cambiar la
  precedencia expiración → admisión.
- Añade cobertura Host de `Failed` para las seis familias, sin retry implícito y con
  recuperación posterior, además de una prueba focalizada de partición de expiraciones.
- Documenta que todos los productores deben detenerse y finalizar antes de destruir el
  Host; no añade API ni sincronización de shutdown.
- Mantiene pendiente para una fase independiente la garantía de excepción entre Core,
  bindings y repositorios después de un commit autoritativo.
- Los cambios permanecen locales y sin commit; no se compiló ni se ejecutaron pruebas.

## 11. Reglas de trabajo para la siguiente sesión

- Leer este documento y comprobar el estado Git actual antes de asumir que sigue en
  `ba71fbc` más los cambios locales del hardening posterior a la auditoría.
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
  publicó Like A en `0081ee8` y fue certificada con 158 PASS / 0 FAIL. La Fase 4F.2
  publicó Like B en `46fdc41` y fue certificada con 170 PASS / 0 FAIL. La Fase 4F.3
  publicó Like C en `14bd28f` y fue certificada con 179 PASS / 0 FAIL. La Fase 4G.1
  publicó RoomUser A en `e02f306` y fue certificada con 191 PASS / 0 FAIL. La Fase
  4G.2 publicó RoomUser B en `14b9357` y fue certificada con 203 PASS / 0 FAIL. La Fase
  4G.3 publicó RoomUser C en `f103f75` y fue certificada con 212 PASS / 0 FAIL. La Fase
  4H.1 publicó Gift A en `0a75fb8` y fue certificada con 224 PASS / 0 FAIL. La Fase
  4H.2 publicó Gift B en `427578b`. La Fase 4H.3 publicó Gift C en `ba71fbc` y fue
  certificada con 245 PASS / 0 FAIL; no continuar automáticamente con Member.
- La migración UE5 es trabajo futuro separado:
  `TikFinityPlugin → puente Blueprint/C++ → FTS*Input → Event Host`.
- No añadir automáticamente conexión WebSocket → Host, nuevas familias, repositorios,
  bindings, Simulator, Console, procesadores concretos, efectos ni UE5 sin
  especificación separada.
