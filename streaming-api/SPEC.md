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
- FFmpeg se lanza con `posix_spawn` y vector de argumentos, **sin shell** (sin inyección de comandos).
- Todo id y nombre de archivo se valida (`VideoStorage`) → sin *path traversal*.
- Subidas en streaming a disco (no se carga el video en RAM); límite de tamaño configurable.
- Escrituras de réplicas atómicas (archivo temporal + `rename`).

## 6. Redundancia (V2.0)
- Dos nodos con la misma configuración, sin estado compartido en memoria.
- Tras transcodificar, el nodo origen envía a los peers primero los segmentos y **al final** `manifest.mpd`;
  el peer solo marca `ready` al recibir el manifiesto → nunca expone una copia incompleta.
- Replicación con 3 reintentos; fallos se cuentan en `tecflix_replications_total{result="failed"}`.
- Borrados se propagan a los peers.
- Al arrancar, `recover()` re-encola transcodificaciones interrumpidas y descarta subidas incompletas.
- Balanceador nginx (`deploy/nginx.conf`): `least_conn`, `max_fails`, `proxy_next_upstream` (incluye 404
  por si la réplica aún no llegó). **Limitación conocida:** nginx es un punto único de falla; se mitiga con
  keepalived/IP virtual o se documenta como riesgo residual en el paper.
- Mongo (replica set) y archivo binario de usuarios: coordinar con Personas 1 y 2.

## 7. Observabilidad
Métricas: `tecflix_http_requests_total`, `tecflix_http_request_duration_seconds` (histograma por ruta),
`tecflix_bytes_served_total`, `tecflix_transcode_duration_seconds`, `tecflix_transcodes_total`,
`tecflix_transcode_queue_size`, `tecflix_uploads_total`, `tecflix_replications_total`.
**Pendiente (requisito del curso):** implementar `OtelMetrics : IMetrics` con `opentelemetry-cpp`
(exportador OTLP o Prometheus) y trazas por petición. El resto del código no cambia.

## 8. Configuración (variables de entorno)
`TECFLIX_PORT` (8083), `TECFLIX_NODE_ID`, `TECFLIX_STORAGE_DIR` (./storage), `TECFLIX_FFMPEG`,
`TECFLIX_MAX_JOBS` (2), `TECFLIX_SEGMENT_SECONDS` (4), `TECFLIX_MAX_UPLOAD_BYTES` (2 GiB),
`TECFLIX_CORS_ORIGIN` (*), `TECFLIX_METADATA_URL`, `TECFLIX_PEERS` (lista separada por comas),
`TECFLIX_INTERNAL_TOKEN`.
