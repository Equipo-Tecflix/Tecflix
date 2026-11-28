# Tecflix — Guía Persona 3 (Windows)

## 1. Alcance y requisitos

Streaming API C++17, FFmpeg, MPEG-DASH (360p/720p H.264 y AAC), Shaka Player, replicación V2 y observabilidad. Esta guía documenta operación y evidencias de desarrollo, no el paper académico. Trabajar desde la raíz del repositorio en la rama `feature/persona3-windows`. No modificar las APIs de otros integrantes sin coordinación.

Instalar Visual Studio 2022 Build Tools con carga de trabajo C++, CMake, FFmpeg, Node.js, k6, Nginx para Windows, OpenTelemetry Collector Contrib y Prometheus. Verificar `cmake --version`, `ffmpeg -version`, `node --version`, `k6 version`. Rutas de herramientas usadas aquí: `C:\tools\nginx`, `C:\tools\otelcol`, `C:\tools\prometheus`. Adaptarlas si difieren. En una red de pruebas privada solamente; sin metadata configurada el acceso público de la API es libre. No publicar tokens ni archivos de almacenamiento en Git.

## 2. Compilar y ejecutar V1

```powershell
.\scripts\build-streaming.ps1
.\scripts\run-streaming.ps1 -Node V1 -Port 8083 -Storage storage-V1
```

En otra terminal:

```powershell
.\scripts\demo-video.ps1 -BaseUrl http://127.0.0.1:8083 -Id demo-v1
curl.exe -i http://127.0.0.1:8083/videos/demo-v1/status
Start-Process .\streaming-api\scripts\player.html
```

Configurar principal `http://127.0.0.1:8083`, alternativa vacía, ID `demo-v1`. Este modo de ejecución del código actual no sustituye el tag histórico V1. No reutilizar IDs eliminados. `-NoTests` permite compilar sin GoogleTest, pero no valida las pruebas.

## 3. V2: seis servicios locales

Detener V1. Usar terminales independientes y mantenerlas abiertas. Los siguientes valores predeterminados se adoptaron tras los experimentos: caché 0 MiB, muestreo 0.10, bloque 1024 KiB y reintento de replicación 5 s. Para repetir un experimento, establecer las variables explícitamente antes de iniciar cada API.

| Servicio | Puerto | Almacenamiento/configuración |
|---|---:|---|
| API A | 8083 | storage-A |
| API B | 8084 | storage-B |
| Nginx A | 8080 | tecflix-A |
| Nginx B | 8081 | tecflix-B |
| Collector | 4318 / 9464 | deploy/observability/otel-collector.yaml |
| Prometheus | 9090 | deploy/observability/prometheus.yml |

```powershell
# Terminal 1
& "C:\tools\otelcol\otelcol-contrib.exe" --config="$PWD\deploy\observability\otel-collector.yaml"
# Terminal 2
& "C:\tools\prometheus\prometheus.exe" --config.file="$PWD\deploy\observability\prometheus.yml"
# Terminal 3
.\scripts\run-streaming.ps1 -Node A -Port 8083 -Peers http://127.0.0.1:8084 -Token demo-local -OtlpEndpoint http://127.0.0.1:4318
# Terminal 4
.\scripts\run-streaming.ps1 -Node B -Port 8084 -Peers http://127.0.0.1:8083 -Token demo-local -OtlpEndpoint http://127.0.0.1:4318
# Terminal 5
.\scripts\run-balancer.ps1 -NginxExe C:\tools\nginx\nginx.exe -Port 8080 -Instance tecflix-A
# Terminal 6
.\scripts\run-balancer.ps1 -NginxExe C:\tools\nginx\nginx.exe -Port 8081 -Instance tecflix-B
```

Desde una séptima terminal:

```powershell
foreach ($port in @(8080,8081,8083,8084)) {
    curl.exe -s -o NUL --max-time 5 -w "Puerto ${port}: %{http_code}`n" "http://127.0.0.1:$port/health"
}
.\scripts\demo-video.ps1 -BaseUrl http://127.0.0.1:8083 -Id demo-v2-nuevo
```

Esperar `ready` en ambos nodos. En el HTML utilizar principal 8080, alternativa 8081 y el ID creado. Para pruebas de continuidad de varios minutos utilizar un video suficientemente largo, por ejemplo `failover-test-01` si ya existe. El video de demostración corto no permite observar todas las etapas de una caída prolongada.

## 4. Pruebas de redundancia y recuperación

Con DevTools/Network, deshabilitar caché y conservar registro; verificar nuevos segmentos con HTTP 200, `X-Tecflix-Balancer` y `X-Tecflix-Node`. Antes de cada ensayo verificar ambos nodos y balanceadores. Para detener Nginx A:

```powershell
& "C:\tools\nginx\nginx.exe" -p "C:/tools/nginx/tecflix-A/" -c nginx.conf -s stop
```

Para B sustituir `tecflix-A` por `tecflix-B`. Detener la API correspondiente con Ctrl+C en su propia terminal. Para recuperar, repetir los comandos de arranque. No recargar el reproductor durante el ensayo; invertir principal y alternativa para probar el sentido contrario. Verificar que los servicios apagados dan HTTP 000 (sin respuesta HTTP) y los restantes 200. Guardar capturas de segmentos posteriores a la caída y del final del video; anotar pausas si ocurren.

Pruebas manuales locales realizadas: reproducción normal, caída individual de cada API, caída individual de cada Nginx, caída de cada pareja Nginx + API y recuperación. En la recuperación cada balanceador distribuyó 100 consultas secuenciales de salud en 50 A y 50 B. En un ensayo se observaron cambios adicionales del indicador y eventos de espera; no se determinó la causa exacta. No afirmar que todas las conmutaciones fueron sin pausas ni que el indicador equivale al número de fallos.

Replicación: con A apagado, cargar un nuevo ID en B, esperar `ready`, reiniciar A y verificar que aparezca la réplica automáticamente. Se comprobó persistencia de pendientes tras reiniciar el nodo origen, igualdad SHA256 de 17 archivos y propagación de borrados después de recuperar el peer. Observar `tecflix_replication_pending`, `tecflix_replication_retries_total`, `tecflix_replication_recovered_total` y `tecflix_replication_deletes_total`. No manipular `.replication` ni reutilizar IDs eliminados. Reparación manual de respaldo, no procedimiento habitual:

```powershell
.\scripts\repair-replica.ps1 -Storage storage-B -Id ID_EXISTENTE -Peer http://127.0.0.1:8083 -Token demo-local
```

Solo para un video listo que debe existir en el peer, nunca para revivir uno borrado. No se resuelven conflictos de escrituras concurrentes ni pérdida del único disco antes de replicar.

## 5. V2 en dos computadoras físicas (pendiente de validación)

Ejemplo: A=192.168.1.10, B=192.168.1.11. Sustituir por las IP reales (`ipconfig`), utilizar discos independientes, el mismo commit y un token aleatorio compartido. Abrir puertos TCP 8083 y 8080 únicamente para los equipos autorizados en la red privada; no exponer el puerto de replicación a Internet. Cada máquina usa 8083 para su API y 8080 para su Nginx. Verificar conectividad con `Test-NetConnection IP -Port 8083` y 8080. Configurar el token en cada sesión sin versionarlo.

```powershell
# Máquina A (token establecido previamente en TECFLIX_SHARED_TOKEN)
.\scripts\run-streaming.ps1 -Node A -Port 8083 -Peers http://192.168.1.11:8083 -Token $env:TECFLIX_SHARED_TOKEN -Storage storage-A
# Máquina B
.\scripts\run-streaming.ps1 -Node B -Port 8083 -Peers http://192.168.1.10:8083 -Token $env:TECFLIX_SHARED_TOKEN -Storage storage-B
# En cada máquina, otra terminal
.\scripts\run-balancer.ps1 -NginxExe C:\tools\nginx\nginx.exe -NodeA 192.168.1.10:8083 -NodeB 192.168.1.11:8083 -Port 8080 -Instance tecflix
```

En el cliente configurar ambas URLs de Nginx con sus IP. Cargar un video nuevo, comprobar réplica y hashes, apagar una máquina completa, confirmar acceso desde la otra y repetir inversamente. Verificar recuperación, carga durante caída y borrado pendiente. Ejecutar el cliente desde un equipo que no se apague. Para comparar rendimiento, preferir un tercer equipo para k6. Documentar hardware, red, sistema operativo, topología y versiones. El ejemplo no acredita por sí mismo despliegue físico ni alta disponibilidad integral.

## 6. Observabilidad

En local el collector recolecta ambos nodos (8083/8084), exporta métricas a 9464 y recibe trazas OTLP HTTP en 4318. Prometheus consulta 9464; interfaz en http://127.0.0.1:9090. Consultar nombres efectivos en `/metrics`, pues el collector puede normalizarlos. Validar etiquetas por nodo y actividad tras solicitudes. En dos máquinas ajustar los targets a IP:8083 de ambas. Si el collector se centraliza, configurar escucha OTLP en IP privada accesible (no 127.0.0.1), restringir firewall y pasar `-OtlpEndpoint http://IP-COLLECTOR:4318` a las APIs. El collector y Prometheus centrales siguen siendo puntos de fallo de observabilidad. Las trazas son HTTP independientes, sin propagación distribuida de contexto.

## 7. Pruebas unitarias, cobertura y CI

```powershell
.\scripts\build-streaming.ps1
ctest --test-dir build-streaming -C Release --output-on-failure
```

Windows: 56 pruebas; Linux: 57 (casos de transcodificación específicos). Cobertura histórica Windows: 839/893 líneas, 93.95%, fuentes de biblioteca instrumentadas, sin main.cpp; no equivale a cobertura de todo el repositorio. GitHub Actions aprobado en commit `bb6fc7b` (27-11-2026): build y tests Windows/Linux, mínimo 90% de líneas en Linux, prueba funcional y artefactos. El valor exacto Linux debe obtenerse del reporte de CI. El primer pipeline falló en `Api.HealthAndMetrics`; se corrigió la prueba con espera acotada de métricas, sin desactivar aserciones. El pipeline no automatiza todavía las pruebas nuevas de caída de balanceadores.

```powershell
# Con A y B encendidos
cd functional-tests
npm ci
npx playwright install chromium
$env:BASE_URL='http://127.0.0.1:8083'
$env:PEER_URL='http://127.0.0.1:8084'
npm test
cd ..
```

Playwright genera video real, carga, espera ready, verifica MPD y rango HTTP, reproduce Shaka, comprueba avance y réplica, y solicita limpieza. No sustituye pruebas manuales de caída del proxy.

## 8. Rendimiento: metodología y resultados

k6 simula solicitudes de MPD, inicialización y tres segmentos 720p, con pausa de 0.5 s tras cada segmento. No es un modelo completo de reproducción adaptativa. Umbrales elegidos: errores <1%, HTTP p95 <800 ms, segmentos p95 <500 ms. Mantener mismo video, pista, duración, telemetría y configuración al comparar. Para medir una instancia detrás de Nginx, retirar el segundo upstream de su configuración (no dejarlo apagado pero configurado). En dos instancias, ambos upstreams activos. El script compare-load.ps1 por defecto compara acceso directo y proxy; ajustar URLs/topologías para comparaciones controladas.

```powershell
k6 run -e BASE_URL=http://127.0.0.1:8080 -e VIDEO_ID=replica-test-01 -e MODE=play -e TRACK=1 -e SEGMENTS=3 -e FIXED_VUS=100 -e DURATION=60s --summary-export resultado.json load-tests/streaming.js
```

### Cinco optimizaciones adoptadas

| Cambio | VUs de comparación | Antes req/s | Después req/s | Variación aproximada |
|---|---:|---:|---:|---:|
| Pool HTTP 8 → 32 hilos | 10 | 22.95 | 32.70 | +42.5% |
| Límite keep-alive 1000 → 1 y timeout 5 → 1 s | 100 | 104.09 | 304.99 | +193% |
| Backlog TCP 5 → 512 | 200 | 393.18 | 477.07 | +21.3% |
| Muestreo de trazas de segmentos 100% → 10% | 200 | 407.27 | 485.14 | +19.1% |
| Bloque de lectura 64 → 1024 KiB (repetición) | 200 | 430.48 | 543.81 | +26.3% |

Son comparaciones locales separadas realizadas durante el desarrollo, no mejoras acumulables ni garantía de rendimiento físico. La caché LRU de 64 MiB no mejoró los resultados generales, por eso queda desactivada. Los valores históricos de las primeras optimizaciones proceden del seguimiento de pruebas; conservar los reportes originales como evidencia. No atribuir las diferencias exclusivamente a un parámetro si cambiaron otras condiciones.

### Comparación local de topologías

19 reportes JSON: ocho con un nodo detrás de Nginx, ocho con dos y tres de acceso directo. Todos en una computadora Windows, con el mismo video y carga nominal de 60 s por ensayo. Los valores siguientes se extraen de los JSON originales; req/s incluye solicitudes fallidas.

| Modo | VUs | req/s | req/s sin fallo HTTP | HTTP p95 ms | Segmentos p95 ms | Errores % | Umbrales |
|---|---:|---:|---:|---:|---:|---:|---|
| single | 10 | 32.33 | 32.33 | 18.45 | 19.82 | 0.00 | Cumple |
| single | 25 | 79.76 | 79.76 | 24.27 | 27.78 | 0.00 | Cumple |
| single | 50 | 141.97 | 141.97 | 173.70 | 214.88 | 0.00 | Cumple |
| single | 100 | 137.22 | 137.22 | 772.65 | 816.30 | 0.00 | No cumple |
| single | 150 | 140.38 | 140.38 | 1332.96 | 1409.12 | 0.00 | No cumple |
| single | 200 | 139.51 | 139.51 | 1864.46 | 1895.10 | 0.00 | No cumple |
| single | 300 | 135.77 | 135.77 | 3382.59 | 3600.88 | 0.00 | No cumple |
| single | 400 | 130.09 | 130.09 | 4846.67 | 4950.21 | 0.00 | No cumple |
| dual | 10 | 31.92 | 31.92 | 25.47 | 29.07 | 0.00 | Cumple |
| dual | 25 | 76.48 | 76.48 | 58.47 | 79.74 | 0.00 | Cumple |
| dual | 50 | 136.07 | 136.07 | 255.36 | 293.26 | 0.00 | Cumple |
| dual | 100 | 85.90 | 85.90 | 1926.18 | 2065.56 | 0.00 | No cumple |
| dual | 150 | 87.56 | 87.56 | 3642.20 | 4133.92 | 0.00 | No cumple |
| dual | 200 | 87.46 | 74.80 | 5044.54 | 5103.67 | 14.47 | No cumple |
| dual | 300 | 121.04 | 100.46 | 4666.15 | 4863.07 | 17.01 | No cumple |
| dual | 400 | 136.51 | 109.43 | 6301.88 | 8626.28 | 19.83 | No cumple |
| direct | 50 | 159.68 | 159.68 | 14.58 | 17.08 | 0.00 | Cumple |
| direct | 100 | 309.20 | 309.20 | 30.24 | 23.74 | 0.00 | Cumple |
| direct | 200 | 559.18 | 559.18 | 97.72 | 112.34 | 0.00 | Cumple |

Con Nginx, ambas topologías cumplen hasta 50 VUs de los niveles ensayados; el límite exacto está entre 50 y 100. La configuración dual no supera el rendimiento efectivo de la simple en los ensayos. Acceso directo: 559.18 req/s a 200 VUs, sin errores y con umbrales cumplidos. El proxy está asociado con una caída de rendimiento en este entorno, pero no se aisló el recurso limitante. CPU, disco, memoria, k6 y observabilidad comparten host; no extrapolar al despliegue distribuido. Repetir en máquinas independientes, conservar resultados negativos y registrar condiciones. Evidencias: `load-tests/results/local-scaling/`, reportes de optimizaciones y capturas guardadas por el responsable.

## 9. Integración y pendientes

Acordar `GET /videos/{id}/access?permission=read|write` con metadata; reenviar Authorization desde el frontend en MPD y segmentos. `-MetadataUrl` activa la comprobación; sin URL se permite todo en modo desarrollo. No exponer endpoints internos; usar TLS y controles de red para despliegue real. Integrar dos URLs de lectura en React; no reintentar escrituras a ciegas ante respuestas ambiguas. Pendientes: pruebas físicas de failover/escalabilidad, redundancia de usuarios/metadata/observabilidad según alcance del equipo, frontend integrado, validación de escrituras y versiones V1/V2 separadas. No crear tags de entrega sin coordinación con el equipo. El paper académico queda a cargo de sus autores.
