# Organización de pruebas

`Tests/<Evento>/` contiene las suites propias de una familia concreta. Chat, Follow,
Share, Like, RoomUser y Gift tienen completos Adapter tipado, familia directa,
Pipeline, Host, lifecycle e integración vertical JSON → Host. MemberIdentity A está
publicado y MemberIdentity B completa localmente repositorio, admisión, dispatch,
completion y lifecycle dentro del Pipeline; Member C permanece pendiente.

`Tests/TSPipelineInfrastructureTests.cpp` cubre repositorios, bindings y piezas
transversales del Pipeline. `Tests/TikStudioEventQueueSystemTests.cpp` prueba el Core
genérico. `Tests/TikStudioTikFinityJsonDecoderTests.cpp` y
`Tests/TikStudioTikFinityChecklistTests.cpp` certifican la frontera transversal de los
siete eventos.

`TikStudioEventHostTests` comienza con inputs portables ya normalizados y certifica
exclusivamente el comportamiento del Event Host. `TikStudioVerticalIntegrationTests`
compone JSON TikFinity → converter → Event Host → Pipeline → Core. Adapter y Host sólo
se enlazan juntos en este runner de integración; el Host de producción no depende de
TikFinity.

Las pruebas verticales permanecen organizadas dentro del directorio de cada evento.
Cada evento que complete este recorrido debe añadir su equivalente vertical al runner
de integración, sin mezclarlo con sus pruebas propias del Host.

Share dispone de repositorio tipado, admisión coordinada, dispatch, completion, Host y
certificación vertical JSON → Host. Su familia produce exclusivamente
`ETSEventFlow::Share`; `ShareMilestone` no está implementado y requiere una
especificación posterior.

Like conserva `LikeCount` y `TotalLikeCount` como datos portables y produce únicamente
`ETSEventFlow::Like`. `LikeUser` permanece reservado y no está implementado; no existe
acumulación, umbral ni estado semántico para inferirlo.

RoomUser dispone de Adapter tipado, familia directa, Pipeline, Host, lifecycle y
certificación vertical JSON → Host. Preserva `TopViewers` por valor, en orden y sin
deduplicación, y produce exclusivamente `ETSEventFlow::RoomUser`.
`RoomUserMilestone` y `RoomUserTop1Change` permanecen reservados y no están
implementados.

Gift conserva `RepeatCount`, `GiftType`, `bRepeatEnd` y `GroupId` como datos crudos y
produce exclusivamente `ETSEventFlow::Gift`. Ninguno de esos metadatos activa
`GiftCombo`, que permanece reservado hasta que una fase posterior defina su semántica.
Su repositorio, admisión, dispatch, completion y lifecycle comparten el Coordinator y
las autoridades globales existentes. `PostGift` y `PostGiftCompletion` usan el FIFO,
owner thread, ready e `InFlight` compartidos; el dispatch variant incorpora la copia
propietaria Gift. La certificación vertical recorre JSON Gift → converter → Host. El
runner Pipeline registra 98 casos, Host 57 y Vertical Integration 6. El Host añade
cobertura simétrica de `Failed` para las seis familias, conserva el FIFO si falla el
mantenimiento previo al comando y certifica la separación entre `DueExpirations` y el
lifecycle de una completion.

Member conserva `ActionId` y el usuario portable completo y produce exclusivamente
`ETSEventFlow::MemberIdentity`. `MemberNormalized` permanece reservado: no existe
normalización, deduplicación, acumulación ni estado entre decisiones. El baseline
publicado `51cba3a` fue certificado por el propietario con 265 PASS / 0 FAIL. Los doce
casos Coordinator de MemberIdentity B elevan Pipeline a 112 y el total registrado a
277; no se compilaron ni ejecutaron durante la implementación.

Las suites futuras deben añadirse al directorio de su evento y registrarse
explícitamente desde un `main` pequeño. No se incluyen archivos `.cpp`, no se usa
autorregistro global y la infraestructura transversal no se asigna arbitrariamente a
una familia.
