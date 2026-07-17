# Organización de pruebas

`Tests/<Evento>/` contiene las suites propias de una familia concreta. Chat y Follow
tienen cobertura real; Gift, Like, Member, RoomUser y Share conservan únicamente su
directorio hasta que exista comportamiento para probar.

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

Las suites futuras deben añadirse al directorio de su evento y registrarse
explícitamente desde un `main` pequeño. No se incluyen archivos `.cpp`, no se usa
autorregistro global y la infraestructura transversal no se asigna arbitrariamente a
una familia.
