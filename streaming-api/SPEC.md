# SPEC – API de Streaming (Tecflix) · Persona 3

> Sección para fusionar en el `SPEC.md` raíz del repositorio. Es la entrada para los agentes de AI.

## 1. Responsabilidad
API REST en **C++17** que (a) recibe videos, (b) los transcodifica con **FFmpeg** a **MPEG-DASH**
(360p + 720p H.264, audio AAC, segmentos de 4 s) y (c) entrega manifiesto y segmentos a
**Shaka Player / dash.js**. En V2.0 corre en **dos nodos** con replicación y balanceador.

## 2. Contrato HTTP
| Método | Ruta | Descripción | Respuestas |
|---|---|---|---|
| POST | `/videos/{id}/upload` | Cuerpo crudo (`Content-Type: video/*`) o `multipart/form-data` (parte con filename). Inicia transcodificación asíncrona. | 202, 400 (vacío/id inválido), 403, 409 (ya existe), 413 (excede `TECFLIX_MAX_UPLOAD_BYTES`) |
| GET | `/videos/{id}/status` | `uploading\|queued\|processing\|ready\|failed` | 200, 403, 404 |
| GET | `/videos/{id}/manifest.mpd` | Manifiesto DASH (`application/dash+xml`) | 200, 403, 404 |
| GET | `/videos/{id}/{init-N\|chunk-N-NNNNN}.m4s` | Segmentos; soporta `Range` (206/416) | 200, 206, 403, 404, 416 |
| DELETE | `/videos/{id}` | Elimina el video en este nodo y en los peers | 200, 403, 404 |
| GET | `/health` | Salud del nodo (usado por el balanceador) | 200 |
| GET | `/metrics` | Métricas formato Prometheus | 200 |
| PUT/DELETE | `/internal/replicate/...` | Solo entre nodos, cabecera `X-Tecflix-Token` | 200, 400, 403, 404 |

`{id}` = `[A-Za-z0-9_-]{1,64}` (lo genera el API de metadata y se reutiliza aquí).
Solo se sirve un video con estado `ready`.

## 3. Autorización (acordar con Persona 2)
Si `TECFLIX_METADATA_URL` está definido, antes de cada operación el API consulta:
`GET {metadataUrl}/videos/{id}/access?permission=read|write` reenviando la cabecera `Authorization`.
**200 = permitido**; cualquier otra respuesta o timeout (2 s) = denegado.
Reglas: leer → video público o del propio usuario; escribir/borrar → solo el dueño.
Sin la variable, todo se permite (modo desarrollo).

## 4. Diseño (POO)
`ApiServer` (HTTP) → `StreamingService` (lógica) → interfaces inyectadas: `ITranscoder`
(`FfmpegTranscoder`), `IReplicator` (`NullReplicator`, `HttpReplicator`), `IAccessChecker`
(`AllowAll…`, `MetadataAccessChecker`), `IMetrics` (`PrometheusMetrics`), más `VideoStorage`
(layout en disco + validación de rutas) y `JobQueue` (pool acotado de transcodificaciones).
Las interfaces permiten pruebas unitarias con *fakes* (cobertura ≥ 90 %).

## 5. Seguridad / buenas prácticas
- FFmpeg se lanza con `posix_spawn` en Linux o `CreateProcessW` en Windows y argumentos escapados, **sin shell** (sin inyección de comandos).
- Todo id y nombre de archivo se valida (`VideoStorage`) → sin *path traversal*.
- Subidas en streaming a disco (no se carga el video en RAM); límite de tamaño configurable.
- Escrituras de réplicas atómicas (archivo temporal + `rename`).

## 6. Redundancia (V2.0)
- Dos nodos con la misma configuración, sin estado compartido en memoria.
- Tras transcodificar, el nodo origen envía a los peers primero los segmentos y **al final** `manifest.mpd`;
  el peer solo marca `ready` al recibir el manifiesto → nunca expone una copia incompleta.
- Replicación con reintentos iniciales y recuperación persistente de operaciones pendientes; fallos se cuentan en `tecflix_replications_total{result="failed"}`.
- Borrados se propagan a los peers.
- Al arrancar, `recover()` re-encola transcodificaciones interrumpidas y descarta subidas incompletas.
- Dos balanceadores Nginx independientes, cada uno con acceso a ambos nodos (`least_conn`, reintentos ante errores y 404). `scripts/run-balancer.ps1` permite seleccionar puerto e instancia; cada instancia conserva su configuración, PID y registros. `/internal/` se bloquea en el proxy.
- El reproductor de demostración `scripts/player.html` configura dos URLs y reintentos de manifiestos y segmentos; prioriza la última URL con respuesta exitosa. No requiere reiniciar el reproductor para cambiar de URL. `X-Tecflix-Balancer` y `X-Tecflix-Node` identifican la ruta atendida. El indicador puede alternar por respuestas concurrentes; no es un contador exacto de fallos.
- Conmutación de lectura probada localmente en ambas direcciones y con caída de API + balanceador. No garantiza ausencia de pausas de buffering. El frontend React debe incorporar el mecanismo, incluyendo autorización. Las escrituras requieren una política independiente de idempotencia y recuperación; no reintentar ciegamente POST.
- El acceso redundante evita depender de un único proceso Nginx para el reproductor de prueba. La tolerancia a pérdida de una máquina física y la eliminación de SPOF de todo Tecflix aún deben validarse.
- Mongo (replica set) y archivo binario de usuarios: coordinar con Personas 1 y 2.

## 7. Observabilidad
Métricas: `tecflix_http_requests_total`, `tecflix_http_request_duration_seconds` (histograma por ruta),
`tecflix_bytes_served_total`, `tecflix_transcode_duration_seconds`, `tecflix_transcodes_total`,
`tecflix_transcode_queue_size`, `tecflix_uploads_total`, `tecflix_replications_total`.
El OpenTelemetry Collector Contrib recibe métricas por su receptor Prometheus (scrape de `/metrics`) y trazas HTTP independientes por OTLP/HTTP JSON (`TECFLIX_OTLP_ENDPOINT`). Exportación de trazas en lotes, cola de 2048 entradas, sin bloquear el servidor. Configuraciones en `deploy/observability/`. En local el collector consulta A:8083 y B:8084; Prometheus consulta el exporter en 9464. Para despliegue físico ajustar destinos y alcance de escucha OTLP; restringir por firewall. El collector central no es redundante. No incluye propagación de contexto distribuido ni garantiza entrega de telemetría si el collector falla.

## 8. Configuración (variables de entorno)
`TECFLIX_PORT` (8083), `TECFLIX_NODE_ID`, `TECFLIX_STORAGE_DIR` (./storage), `TECFLIX_FFMPEG`,
`TECFLIX_MAX_JOBS` (2), `TECFLIX_SEGMENT_SECONDS` (4), `TECFLIX_MAX_UPLOAD_BYTES` (2 GiB),
`TECFLIX_CORS_ORIGIN` (*), `TECFLIX_METADATA_URL`, `TECFLIX_PEERS` (lista separada por comas),
`TECFLIX_INTERNAL_TOKEN`, `TECFLIX_OTLP_ENDPOINT` (opcional).

## 9. Verificación y V2.0
Scripts PowerShell en `scripts/`, pruebas k6 de carga constante y Playwright de reproducción real. La CI exige 90% de cobertura de las fuentes de la biblioteca en Linux; se excluye `main.cpp` y la rama Windows no se mide. Para tolerar la caída del balanceador, desplegar uno por máquina y configurar URLs principal y alternativa en el cliente Shaka (demo incluido). El workflow aprobado en el commit bb6fc7b ejecuta pruebas Windows y Linux, cobertura Linux y Playwright; las nuevas pruebas de caída de balanceadores son manuales y no están automatizadas en ese workflow. La integración del frontend final y la redundancia de las otras APIs requieren coordinación del equipo. Replicación inicial de videos con reintentos inmediatos, recuperación automática de pendientes y reparación manual de respaldo; no es almacenamiento distribuido con consenso ni tolera pérdida del origen antes de completar una réplica.

### V2: caché de segmentos en memoria (optimización experimental)

`TECFLIX_SEGMENT_CACHE_MIB` configura el límite de datos retenidos por nodo (0 por defecto; 0 deshabilita; valores superiores a 1024 vuelven a 0). Solo archivos `.m4s` de hasta 8 MiB y no vacíos son elegibles; máximo 4096 entradas, reemplazo LRU. El resto utiliza el proveedor original de archivos. La autorización, estado Ready y existencia del archivo se comprueban antes de servirlo. Las respuestas conservan HTTP Range. Los datos son inmutables y compartidos entre respuestas; el mutex no se mantiene durante el envío. Las mutaciones a través de VideoStorage invalidan las entradas del video (incluida la replicación). No se admite modificar archivos DASH externamente mientras se sirve el video. El límite de datos retenidos no incluye metadatos, asignaciones temporales ni referencias de respuestas en curso. Un fallo de caché lee el archivo bajo el mutex; medir contención con cargas de catálogo diverso.

Métrica: `tecflix_segment_cache_requests_total{result="hit|miss|bypass"}` (miss = carga exitosa, bypass = ruta de archivo original). Comparar el mismo binario con 0 y 64 MiB, misma carga, telemetría y condiciones. Las pruebas locales con 100, 150 y 200 VUs no mostraron mejora general; se deja deshabilitada por defecto.

### V2: muestreo de trazas HTTP
`TECFLIX_TRACE_SAMPLE_RATE` (0 a 1, predeterminado 0.10; valores inválidos vuelven a 0.10) controla únicamente GET de segmentos `.m4s` públicos con estado 200/206. Se decide antes de generar IDs y JSON. Las demás operaciones trazadas conservan el 100% de selección; `/health` y `/metrics` siguen excluidas. Las métricas HTTP no se muestrean. `tecflix_trace_spans_total{result="generated|sampled_out|queue_dropped|export_failed|exported"}` distingue entradas encoladas, omitidas, descartadas por cola llena y resultados HTTP de exportación. Los contadores son acumulativos, no categorías mutuamente excluyentes; exported significa respuesta HTTP 2xx, no garantía de persistencia ni de aceptación de todos los spans OTLP. No se garantizan trazas de errores de transporte ni de errores HTTP si falla la cola/exportación. Comparar 1.0, 0.10 y 0.01 con Collector activo, caché 0 y misma carga; las pruebas locales mostraron mejoras con muestreo reducido a 150 y 200 VUs. Se elige 0.10 como equilibrio provisional; falta repetir en máquinas independientes.

### Tamaño de bloque de lectura (experimento V2.0)

`TECFLIX_FILE_CHUNK_KIB` configura el máximo de bytes por lectura del proveedor de archivos, en KiB (1024 por defecto, intervalo 64–1024; valores inválidos vuelven a 1024). El proveedor respeta longitud restante y offset de HTTP Range. No altera la ruta de caché RAM ni el tamaño de paquetes TCP. Comparar el mismo binario con caché deshabilitada, muestreo 0.10 y bloques de 64, 128, 256, 512 y 1024 KiB. El mayor bloque aumenta memoria temporal por solicitud activa; no se presupone mejora.

### Recuperación de replicaciones V2 (dos nodos, IDs inmutables)

Las operaciones pendientes se conservan en `<storage>/.replication` mediante marcadores persistentes de operaciones pendientes (por ejemplo `.push`; no depender de los nombres internos para operar el servicio); un worker espera `TECFLIX_REPLICATION_RETRY_SECONDS` entre rondas de reintentos (5 por defecto, 1–300); la duración de cada ronda depende de los timeouts y de los archivos. Se reenvía el conjunto DASH con manifiesto al final. Las eliminaciones conservan un marcador `.tombstone` incluso si el ID no existía en el receptor; no se permite reutilizar IDs eliminados, para impedir que una transferencia atrasada los recree. No borrar estos marcadores mientras existan réplicas atrasadas. Las métricas `tecflix_replication_pending`, `tecflix_replication_retries_total`, `tecflix_replication_recovered_total` y `tecflix_replication_deletes_total` permiten supervisar la recuperación. Se mantienen las trazas HTTP existentes; no se añaden spans salientes específicos. El registro persiste ante reinicio ordinario del proceso; no garantiza durabilidad ante pérdida de disco o energía. No reconcilia videos históricos sin marcador ni resuelve escrituras concurrentes del mismo ID en ambos nodos, cambios de topología o particiones con conflictos de metadata.

### Configuración de rendimiento adoptada

Pool HTTP de 32 hilos por instancia, keep-alive limitado a una solicitud por conexión y timeout de 1 s, backlog TCP 512, muestreo de trazas de segmentos 0.10 y bloque de lectura 1024 KiB. Caché RAM desactivada por defecto. Estos valores son decisiones experimentales del entorno local, no garantías de escalabilidad. Ver [GUIA_PERSONA3.md](../GUIA_PERSONA3.md) y evidencias de carga.
