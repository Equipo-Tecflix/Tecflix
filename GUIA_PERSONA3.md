# Tecflix – Persona 3 | Windows

## Incorporar el parche
1. Descomprimir `Tecflix-persona3-cambios.zip` en la **raíz** del repositorio (donde está `streaming-api/`). Permitir sobrescribir los archivos listados en `CAMBIOS_PERSONA3.txt` si siguen iguales a la versión base. Si alguien editó esos archivos, comparar primero con Git. No copiar una carpeta exterior adicional.
2. Crear una rama con `git switch -c feature/persona3-windows` antes de copiar. No tocar los archivos raíz `src/`, `include/`, `CMakeLists.txt` ni la metadata. El `SPEC.md` raíz contiene solo la sección propia: si el equipo ya creó uno, fusionar manualmente.
3. Abrir PowerShell en la raíz del repositorio. Si Windows bloquea los scripts descargados, usar `Get-ChildItem scripts/*.ps1 | Unblock-File` y, si se requiere, `Set-ExecutionPolicy -Scope Process Bypass` (solo esta sesión).

## Instalar una vez
En PowerShell con permisos apropiados, instalar herramientas con winget (si ya están instaladas, omitirlas):

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install --id Kitware.CMake -e
winget install --id Gyan.FFmpeg -e
winget install --id OpenJS.NodeJS.LTS -e
winget install --id k6.k6 -e
```
Cerrar y reabrir PowerShell; verificar `cmake --version`, `ffmpeg -version`, `node --version`, `k6 version`. Si winget no encuentra un paquete, instalarlo desde la página oficial. Para el balanceador descargar el ZIP oficial de nginx para Windows y extraerlo (https://nginx.org/en/download.html).

## V1.0: compilar, ejecutar, reproducir
```powershell
.\scripts\build-streaming.ps1
.\scripts\run-streaming.ps1
```
Dejar la terminal del servidor abierta. Si GoogleTest no se puede descargar, compilar temporalmente con `-NoTests` (no cuenta como pruebas superadas).
En otra terminal:
```powershell
.\scripts\demo-video.ps1
Invoke-RestMethod http://localhost:8083/health
Invoke-RestMethod http://localhost:8083/videos/demo1/status
Start-Process .\streaming-api\scripts\player.html
```
En el reproductor usar API `http://localhost:8083` y video `demo1`. El HTML descarga Shaka desde CDN y es una demo, no sustituye el frontend React. Usar `-Id demo2` para otra carga, ya que los IDs no se sobrescriben.

## V2.0: simulación local (dos terminales distintas)
Detener el servidor V1 antes de empezar. Para una prueba limpia usar carpetas de almacenamiento nuevas y otro ID, evitando confundir archivos previos. Ambas instancias usan el mismo token de demostración.
```powershell
# Terminal A
.\scripts\run-streaming.ps1 -Node A -Port 8083 -Peers http://127.0.0.1:8084 -Token 'demo-local' -Storage storage-A
# Terminal B
.\scripts\run-streaming.ps1 -Node B -Port 8084 -Peers http://127.0.0.1:8083 -Token 'demo-local' -Storage storage-B
# Terminal C
.\scripts\demo-video.ps1 -BaseUrl http://localhost:8083 -Id demo-v2
Invoke-RestMethod http://localhost:8084/videos/demo-v2/status
.\scripts\run-balancer.ps1 -NginxExe C:\nginx\nginx.exe
```
Esperar a que ambos estén `ready`; probar Shaka con API `http://localhost:8080`. Detener A y consultar `http://localhost:8080/videos/demo-v2/manifest.mpd`. Para demostrar tolerancia a la caída de un balanceador, ejecutar uno por máquina y usar las dos URLs en el HTML (campo API alternativa). Esto cubre la lectura DASH; las operaciones de escritura y el frontend final necesitan su propio contrato de conmutación.

## V2.0: dos máquinas reales (exigencia del enunciado)
En ambas máquinas compilar o descargar `streaming-windows` del pipeline. Ejemplo de IPs: A `192.168.1.10`, B `192.168.1.11` (reemplazar con las de `ipconfig`). Abrir TCP 8083 y 8080 únicamente para la red privada del laboratorio. Usar un token largo, aleatorio, compartido; no publicar en Git. A y B deben tener discos independientes.
```powershell
# Máquina A
.\scripts\run-streaming.ps1 -Node A -Port 8083 -Peers http://192.168.1.11:8083 -Token 'REEMPLAZAR'
# Máquina B
.\scripts\run-streaming.ps1 -Node B -Port 8083 -Peers http://192.168.1.10:8083 -Token 'REEMPLAZAR'
# En cada máquina, otro PowerShell
.\scripts\run-balancer.ps1 -NginxExe C:\nginx\nginx.exe -NodeA 192.168.1.10:8083 -NodeB 192.168.1.11:8083
```
Cargar al nodo A, verificar la réplica B, apagar A y usar B. Para comparar rendimiento, la máquina que ejecuta k6 debería ser una tercera máquina, o mantener sus recursos aislados. Nginx para Windows tiene limitaciones de rendimiento frente a Linux y puede convertirse en cuello de botella; registrarlo como variable experimental. No afirmar tolerancia a fallos de usuarios/metadata hasta que se implementen sus réplicas.

Si un peer estuvo apagado durante la carga y los reintentos se agotaron, reparar desde el nodo que conserva el video READY:
```powershell
.\scripts\repair-replica.ps1 -Storage storage-A -Id demo-v2 -Peer http://localhost:8084 -Token 'demo-local'
```
La reparación es manual: no hay outbox persistente ni reconciliación automática de borrados. No realizar modificaciones concurrentes del mismo ID en distintos nodos. No borrar un video mientras se procesa o replica. No reutilizar IDs después de borrar.

## Playwright
Con servidor encendido y FFmpeg en PATH:
```powershell
cd functional-tests
npm install
npx playwright install chromium
npm test
# Con dos nodos
$env:PEER_URL='http://localhost:8084'
npm test
cd ..
```
Genera una muestra independiente, sube, espera ready, verifica MPD y Range, reproduce con Shaka, verifica avance del video, comprueba peer si se configuró y limpia el ID de prueba. Usar modo desarrollo exclusivamente en red de confianza.

## k6: comparación reproducible
Antes de medir, cargar la misma muestra de 16 segundos en ambas configuraciones. El video debe tener al menos tres segmentos para la pista 720p (TRACK=1). El modo V1 debe usar una instancia sin peers y V2 dos nodos con copia completa. Ejemplo con ambas topologías listas e independientes:
```powershell
.\scripts\compare-load.ps1 -Single http://localhost:8083 -Dual http://localhost:8080 -Video demo1
```
Si se usan las mismas máquinas secuencialmente, primero ejecutar k6 contra V1, luego contra V2, conservando las mismas opciones `FIXED_VUS`, `DURATION`, `VIDEO_ID`, `TRACK` y `SEGMENTS`. Ejemplo:
```powershell
k6 run -e BASE_URL=http://localhost:8083 -e VIDEO_ID=demo1 -e FIXED_VUS=50 -e DURATION=60s --summary-export v1-50.json load-tests/streaming.js
k6 run -e BASE_URL=http://localhost:8080 -e VIDEO_ID=demo1 -e FIXED_VUS=50 -e DURATION=60s --summary-export v2-50.json load-tests/streaming.js
```
El script comparativo produce CSV y JSON por nivel. Umbrales de ensayo: errores <1%, latencia HTTP p95 <800 ms, segmentos p95 <500 ms. Son criterios de diseño elegidos para el ensayo, no requisitos del profesor. Un VU solicita el MPD, init y tres segmentos con pausas de 0.5 s; esto es un escenario de estrés y no simula exactamente el bitrate adaptativo de un navegador. Repetir cada escenario y documentar hardware, ancho de banda, cantidad de nodos, versiones y resultados reales. No usar modo upload sobre servidores de terceros ni sobre el entorno compartido; deja archivos que deben limpiarse tras terminar el procesamiento.

## OpenTelemetry
Obtener el ejecutable oficial `otelcol-contrib.exe` para Windows y Prometheus para Windows. Ejecutar en una terminal:
```powershell
.\otelcol-contrib.exe --config .\deploy\observability\otel-collector.yaml
```
(ajustar ruta al ejecutable). En otra, ejecutar el servidor con `-OtlpEndpoint http://127.0.0.1:4318`. El collector imprime trazas y transforma las métricas de `/metrics` a su endpoint Prometheus 9464. Añadir las IP de los dos nodos en el YAML. En otra terminal ejecutar `prometheus.exe --config.file=deploy/observability/prometheus.yml`; abrir `http://localhost:9090`. Consultar los nombres de métricas reales en `http://127.0.0.1:9464/metrics` (el collector puede normalizar nombres). El exporter de trazas es OTLP HTTP JSON directo, sin dependencia del SDK C++ y sin propagación de contexto entre servicios; sus spans no están conectados a los de metadata. Si se desea un visor gráfico de trazas configurar un backend OTLP adicional. El collector completo aún no se probó aquí.

## GitHub Actions y coordinación
`.github/workflows/streaming.yml` construye en Windows y Linux, ejecuta GoogleTest, exige cobertura de líneas del 90% de la biblioteca (sin `main.cpp`), ejecuta Playwright Linux, y sube artefactos. El resultado de cobertura no está medido en esta entrega. Si falla, corregir los tests o agregar casos reales, no bajar el umbral. La compilación nativa Windows se valida en este pipeline. Acordar con metadata `GET /videos/{id}/access?permission=read|write` y encabezado Authorization; mientras no exista ese contrato el modo desarrollo permite todo. Para integrar, pasar `-MetadataUrl http://IP:PUERTO` y hacer que Shaka reenvíe el token al pedir el MPD y segmentos. El ZIP original no incluye React ni una API de metadata funcional con este endpoint. No usar modo de acceso libre fuera de la red de pruebas.

## Optimización experimental pendiente
Comparar respecto al commit original: limitación de threads FFmpeg, uso de conexiones persistentes en replicación, política de tamaño de segmentos, tamaño del pool de workers y reducción de asignaciones durante envío de segmentos; la versión base ya usa procesamiento asíncrono, streaming a disco y cache HTTP. Las cinco mejoras deben medirse y documentarse con datos, no basta proponerlas. Este parche no demuestra cinco mejoras cuantitativas.

No se redactó el paper, según la restricción de la asignación. La V1 y V2 deben registrarse como tags o ramas separadas en el repositorio del equipo.
