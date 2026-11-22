# Bitácora para el paper (V2.0) – llenar MIENTRAS trabajas

> El paper debe escribirlo usted, sin AI. Aquí solo se guardan datos y decisiones. Guarde también capturas
> y los archivos CSV/JSON crudos de k6 y de /metrics en `docs/resultados/` con fecha en el nombre.

## 1. Entorno de pruebas (siempre el mismo)
- Máquinas: CPU / núcleos / RAM / disco / red / SO.  Video de prueba: duración, resolución, tamaño.
- Versiones: FFmpeg, nginx, k6, commit de git de cada medición.

## 2. Línea base (ANTES de optimizar)
| Métrica | Valor |
|---|---|
| Latencia p50/p95/p99 de segmentos | |
| Req/s máximos antes de degradar | |
| Tiempo de transcodificación (video de prueba) | |
| CPU / RAM durante transcodificación | |
Guardar: dashboard/`/metrics` + resultado de k6.

## 3. Optimizaciones (mínimo 5) – una fila por cambio
| # | Cambio | Hipótesis (por qué ayuda) | Métrica antes | Métrica después | Commit |
|---|---|---|---|---|---|
| 1 | | | | | |
Ideas: caché de nginx para segmentos · `Cache-Control`/ETag · `sendfile`/mmap en vez de leer por chunks ·
preset x264 / duración de segmento · `TECFLIX_MAX_JOBS` según núcleos · keep-alive/HTTP2 · caché de permisos
del API de metadata (evita 1 llamada HTTP por segmento) · hilos del servidor HTTP.
**Regla: cambie UNA cosa por vez y mida.**

## 4. Redundancia – decisiones de diseño (¿por qué así?)
- Balanceador elegido y alternativas descartadas:
- Estrategia de replicación de archivos y alternativas (NFS, MinIO, rsync):
- Qué pasa si cae un nodo (durante reproducción / durante subida / durante transcodificación):
- Riesgos residuales (nginx como SPOF, ventana de replicación, subida en curso al caer el nodo):
- Prueba de falla: fecha, procedimiento, resultado.

## 5. Pruebas de carga
- Herramienta y por qué; scripts usados (ruta en el repo).
- Escenarios y justificación (reproducción, subida/transcodificación).
- Diseño de la rampa (VUs, duración de cada escalón) y criterio de "punto de quiebre"
  (p95 > X ms, errores > 1 %, rechazos).
| Config | VUs en el punto de quiebre | req/s | p95 | Error % | Recurso limitante (CPU/red/disco) |
|---|---|---|---|---|---|
| 1 servidor | | | | | |
| 2 servidores | | | | | |
- Observaciones: ¿escala linealmente? ¿qué se saturó primero?
