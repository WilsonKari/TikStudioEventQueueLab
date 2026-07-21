# Organización de pruebas

`Tests/<Evento>/` contiene las suites propias de una familia concreta. Las siete
familias tienen completos Adapter tipado, familia directa, Pipeline, Host, lifecycle e
integración vertical JSON → Host. Las fases históricas MemberIdentity A → B → C
están completas y publicadas; el baseline vigente es `b7809bc`.

`Tests/TSPipelineInfrastructureTests.cpp` cubre repositorios, bindings, preparación
transaccional, consistencia ready/processing/terminal y piezas transversales del
Pipeline, incluido el passthrough de settings.
`Tests/TSHostInfrastructureTests.cpp` cubre comandos transversales, lease/ack del frente
y FIFO del Host ante excepciones.
`Tests/TikStudioEventQueueSystemTests.cpp` prueba el Core genérico. Los casos locales de
hardening verifican prepare/commit, snapshots, desempates por `Sequence`, expiraciones,
índices stale y repetición determinista.
`Tests/TikStudioTikFinityJsonDecoderTests.cpp` y
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
`ETSEventFlow::Like`. `LikeMilestone` permanece reservado y no está implementado; no
existe acumulación, umbral ni estado semántico para inferirlo.

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
propietaria Gift. La certificación vertical recorre JSON Gift → converter → Host. Al
publicarse el hardening posterior a Gift, Pipeline registraba 98 casos, Host 57 y
Vertical Integration 6; esa cobertura conserva el FIFO si falla el mantenimiento y
certifica la separación entre `DueExpirations` y el lifecycle de una completion.

Member conserva `ActionId` y el usuario portable completo y produce exclusivamente
`ETSEventFlow::Member`. El flujo directo se denominaba `MemberIdentity` durante las
fases 4I.1–4I.3 y fue renombrado porque `Member` describe el evento base, mientras
“Identity” describía un detalle del payload. `MemberRate` permanece reservado: no
existen ventanas temporales, conteos agregados, tasas ni estado entre decisiones. El baseline
publicado `f59836f` fue certificado por el propietario con Core 10, Pipeline 112, Host
66, Adapter 62, JSON Decoder 20, Checklist 10 y Vertical Integration 7: 287 PASS / 0
FAIL.

Migración nominal: `LikeUser` fue renombrado a `LikeMilestone` y `MemberNormalized`
fue renombrado a `MemberRate`. Ambos continúan reservados y sin semántica operativa.
Estos dos renombres fueron publicados en `f59836f` y no modificaron pruebas ni conteos.

La actualización dinámica de settings fue publicada en `b7809bc` y certificada por el
propietario con Core 15, Pipeline 113, Host 68, Adapter 62, JSON Decoder 20, Checklist
10 y Vertical Integration 7: 295 PASS / 0 FAIL.

El hardening de invariantes compartidas permanece local y pendiente de certificación y
publicación. Añade cinco casos Core, cuatro Pipeline y uno Host: los conteos esperados
son Core 20, Pipeline 117, Host 69, Adapter 62, JSON Decoder 20, Checklist 10 y Vertical
Integration 7, para un total de 305. No se ejecutaron durante esta fase.

Las suites futuras deben añadirse al directorio de su evento y registrarse
explícitamente desde un `main` pequeño. No se incluyen archivos `.cpp`, no se usa
autorregistro global y la infraestructura transversal no se asigna arbitrariamente a
una familia.
