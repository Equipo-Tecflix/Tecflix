# SPEC — Tecflix

## Sección Persona 3: Streaming API y V2.0
La especificación detallada y los contratos HTTP de esta sección están en [streaming-api/SPEC.md](streaming-api/SPEC.md).

Mantener las APIs existentes y no alterar `src/`, `include/` ni el CMake raíz sin acuerdo con el equipo. El servicio multimedia debe compilar con C++17 en Windows y Linux, aceptar un ID único de metadata, almacenar segmentos DASH generados por FFmpeg y ofrecer compatibilidad Shaka. El procesamiento se realiza en segundo plano. Las instancias V2 deben usar discos locales separados, un token de replicación y URLs de peers configurables. No exponer `/internal/` a clientes. Las métricas deben ser accesibles a OpenTelemetry Collector, las trazas se exportan por OTLP HTTP opcional. CI debe compilar, ejecutar pruebas y aplicar el umbral de cobertura del 90% de la biblioteca. Pruebas funcionales deben usar video real y k6 debe permitir comparar la misma carga en ambas topologías. No inventar resultados de performance.

Las especificaciones de usuarios, metadata, frontend y dashboard global deben ser agregadas por sus responsables; este archivo no sustituye sus contratos.
