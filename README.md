# Tecflix – Streaming API, Redundancia y Pruebas de carga (Persona 3)

## Dónde va cada cosa en el repo
```
streaming-api/        código C++ (CMake, tests, SPEC.md)   ← copiar a la raíz del repo
load-tests/           scripts k6
deploy/nginx.conf     balanceador de los dos nodos
.github/workflows/    CI del streaming-api
docs/                 bitácora para el paper
```
Crea tu rama (`feature/streaming-api`); no toques `include/`, `src/` ni el `CMakeLists.txt` de la raíz
(son del API de metadata). Todo lo tuyo vive en `streaming-api/`.

## Compilar y probar
```bash
sudo apt install cmake g++ ffmpeg gcovr          # dependencias
cd streaming-api
cmake -B build -DTECFLIX_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j
./build/tecflix_tests                            # 44 pruebas
gcovr -r . build --filter 'src/' --exclude 'src/main.cpp' --print-summary   # cobertura
```
## Ejecutar y probar a mano
```bash
cmake -B build-rel -DTECFLIX_BUILD_TESTS=OFF && cmake --build build-rel -j
BIN=./build-rel/tecflix_streaming ./scripts/run-two-nodes.sh      # dos nodos (8083 y 8084)

curl -X POST --data-binary @video.mp4 -H 'Content-Type: video/mp4' localhost:8083/videos/demo1/upload
curl localhost:8083/videos/demo1/status          # esperar "ready"
curl localhost:8084/videos/demo1/manifest.mpd    # ¡lo sirve el OTRO nodo!
```
Abre `scripts/player.html` en el navegador (Shaka) y reproduce.
