# Tecflix — Streaming API (Persona 3)

Servicio multimedia en C++17 para carga, transcodificación FFmpeg y reproducción MPEG-DASH con Shaka Player. Esta rama (`feature/persona3-windows`) contiene la implementación V2 de streaming, validada localmente en Windows y mediante CI en Windows/Linux.

## Inicio rápido (Windows)

Requisitos: Visual Studio 2022 Build Tools (C++), CMake, FFmpeg y PowerShell. Para pruebas adicionales: Node.js, k6, Nginx, OpenTelemetry Collector Contrib y Prometheus.

Desde la raíz del repositorio:

```powershell
.\scripts\build-streaming.ps1
.\scripts\run-streaming.ps1 -Node V1 -Port 8083 -Storage storage-V1
```

En otra terminal:

```powershell
.\scripts\demo-video.ps1 -BaseUrl http://127.0.0.1:8083 -Id demo-v1
Start-Process .\streaming-api\scripts\player.html
```

Configurar URL principal `http://127.0.0.1:8083`, alternativa vacía e ID `demo-v1`. Esperar estado `ready`. Este modo de un nodo no sustituye la versión histórica V1.

## Compilación Linux

```bash
sudo apt install cmake g++ ffmpeg gcovr
cmake -S streaming-api -B build -DTECFLIX_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
(cd streaming-api && gcovr --root . --filter 'src/' --exclude 'src/main.cpp' --print-summary ../build)
```

## Arquitectura V2

Dos APIs con almacenamiento independiente y replicación bidireccional; recuperación persistente de transferencias y borrados pendientes; dos Nginx que pueden enrutar hacia cualquiera de las APIs; reproductor de demostración con URLs alternativas. OpenTelemetry Collector recibe trazas OTLP HTTP y recolecta métricas para Prometheus.

Para iniciar los seis servicios, probar fallos o configurar dos equipos físicos, seguir [GUIA_PERSONA3.md](GUIA_PERSONA3.md). No exponer `/internal/` ni publicar tokens. Sin metadata configurada, la autorización está desactivada (modo desarrollo).

## Evidencias de validación

| Prueba | Resultado registrado |
|---|---|
| GoogleTest Windows / Linux | 56/56 y 57/57 |
| Cobertura histórica Windows | 93.95% de líneas instrumentadas (sin main.cpp) |
| CI Linux | Umbral de 90% aprobado; consultar artefacto para cifra exacta |
| Playwright | Carga, DASH, rango HTTP, reproducción y réplica |
| Redundancia local | Caídas de APIs, balanceadores y parejas de servicios; recuperación |
| k6 | 19 ensayos comparativos locales |

| Optimización | VUs | Antes → después (req/s) |
|---|---:|---:|
| Pool 8 → 32 hilos | 10 | 22.95 → 32.70 |
| Keep-Alive | 100 | 104.09 → 304.99 |
| Backlog 5 → 512 | 200 | 393.18 → 477.07 |
| Muestreo de trazas 100% → 10% | 200 | 407.27 → 485.14 |
| Bloque 64 → 1024 KiB | 200 | 430.48 → 543.81 |

Comparaciones separadas, no acumulables. Con Nginx, 50 VUs fue el mayor nivel probado que cumplió todos los umbrales tanto con una como con dos instancias locales. Acceso directo: 559.18 req/s a 200 VUs sin fallos HTTP. No se ha demostrado mejora de escalabilidad física.

## Documentación y estructura

- [GUIA_PERSONA3.md](GUIA_PERSONA3.md): instalación, operación, pruebas y resultados.
- [SPEC.md](SPEC.md): especificación general de la contribución.
- [streaming-api/SPEC.md](streaming-api/SPEC.md): contratos HTTP y configuración.
- `streaming-api/`: fuentes C++, CMake y pruebas unitarias.
- `scripts/`: scripts Windows de compilación y despliegue.
- `functional-tests/`: pruebas Playwright.
- `load-tests/`: escenarios k6 y resultados.
- `deploy/observability/`: Collector y Prometheus.
- `.github/workflows/streaming.yml`: pipeline Windows/Linux.
- `CAMBIOS_PERSONA3.txt`: historial de cambios.

## Pendientes de integración

Integrar autorización con metadata y failover en React; validar dos computadoras físicas, escalabilidad y redundancia integral; acordar tags V1/V2. La replicación no ofrece consenso ni garantiza recuperar datos perdidos antes de la primera réplica. La CI no automatiza aún la caída de los balanceadores. Coordinar cambios del README general y de las otras APIs con el equipo antes de fusionar a `develop`.
