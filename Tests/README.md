# Organización de pruebas

`Tests/<Evento>/` contiene las suites propias de una familia concreta. Chat, Follow,
Share y Like tienen completos Adapter tipado, familia directa, Pipeline, Host,
lifecycle e integración vertical JSON → Host. RoomUser tiene completos converter,
payload, familia directa, repositorio tipado, admisión, binding, dispatch, completion y
lifecycle mediante el Coordinator compartido; Host e integración vertical permanecen
pendientes. Gift y Member conservan sólo su directorio hasta que exista comportamiento
para probar.

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

RoomUser preserva `TopViewers` por valor, en orden y sin deduplicación, y produce
exclusivamente `ETSEventFlow::RoomUser`. `RoomUserMilestone` y
`RoomUserTop1Change` permanecen reservados y no están implementados.

Las suites futuras deben añadirse al directorio de su evento y registrarse
explícitamente desde un `main` pequeño. No se incluyen archivos `.cpp`, no se usa
autorregistro global y la infraestructura transversal no se asigna arbitrariamente a
una familia.
