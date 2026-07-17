# TikFinity seven-event probe

Esta herramienta del laboratorio valida manualmente los frames reales que publica el
servidor WebSocket local de TikFinity. No está conectada al Event Host, no altera la
cola, no admite eventos y no confirma ni cancela emisiones. Tampoco sustituye a
`TikFinityPlugin`: la integración definitiva de Unreal seguirá usando ese plugin y un
puente Blueprint/C++.

## Construcción

Configure el proyecto activando la opción del probe:

```text
-DTIKSTUDIO_BUILD_TIKFINITY_PROBE=ON
```

Después compile únicamente el target:

```text
TikStudioTikFinitySevenEventProbe
```

El probe usa IXWebSocket sólo en este ejecutable opcional. El adapter y sus pruebas
automáticas no dependen del transporte WebSocket.

## Validación manual

1. Inicie TikFinity.
2. Habilite su servidor WebSocket.
3. Compruebe que utiliza `127.0.0.1:21213`.
4. Inicie o conéctese a un LIVE.
5. Ejecute el probe.
6. Provoque u observe `chat`, `gift`, `like`, `follow`, `share`, `roomUser` y
   `member`.
7. Revise el checklist hasta alcanzar 7/7.

Ejemplo:

```text
TikStudioTikFinitySevenEventProbe.exe --seconds 600 --stop-when-all-seen --require-all-seen
```

Argumentos:

- `--url <url>`: endpoint; default `ws://127.0.0.1:21213`.
- `--seconds <duración>`: entero entre 1 y 86400; default 120.
- `--stop-when-all-seen`: solicita detenerse al alcanzar 7/7.
- `--require-all-seen`: devuelve error si termina con menos de 7/7.
- `--verbose`: imprime todos los campos mapeados de cada frame válido.

Sin `--verbose` sólo se muestran progreso, primeras observaciones y errores. Los
campos opcionales pueden estar ausentes: un check confirma que el envelope, la forma
de `data`, los tipos presentes, los arrays y los objetos anidados son válidos. No
confirma que un converter semántico posterior vaya a aceptar el evento.
