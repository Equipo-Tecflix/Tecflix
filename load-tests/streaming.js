// Prueba de carga del API de streaming con k6  (https://k6.io)
//
//   k6 run -e BASE_URL=http://192.168.1.10:8083 -e VIDEO_ID=demo1 load-tests/streaming.js   # 1 servidor
//   k6 run -e BASE_URL=http://192.168.1.50:8080 -e VIDEO_ID=demo1 load-tests/streaming.js   # 2 servidores (nginx)
//   k6 run -e MODE=upload -e VIDEO_FILE=./sample.mp4 ...                                    # carga de transcodificación
//
// Cada usuario virtual (VU) simula un espectador: pide el manifiesto y luego los segmentos en orden.
import http from 'k6/http';
import { check, sleep } from 'k6';
import { Counter, Trend } from 'k6/metrics';

const BASE = __ENV.BASE_URL || 'http://localhost:8083';
const VIDEO = __ENV.VIDEO_ID || 'demo1';
const MODE = __ENV.MODE || 'play';
const SEGMENTS = parseInt(__ENV.SEGMENTS || '3');          // segmentos disponibles por pista
const TRACK = __ENV.TRACK || '1';                           // 0=360p 1=720p 2=audio
const PEAK = parseInt(__ENV.PEAK_VUS || '200');

const segmentLatency = new Trend('segment_latency', true);
const rejected = new Counter('rejected_requests');          // 429/5xx/timeouts = el sistema "truena"

// Rampa escalonada: cada escalón mantiene la carga para observar el punto de quiebre.
const steps = [10, 25, 50, 100, 150, 200, 300, 400].filter(v => v <= PEAK);
const stages = [];
steps.forEach(v => { stages.push({ duration: '30s', target: v }); stages.push({ duration: '60s', target: v }); });
stages.push({ duration: '30s', target: 0 });

export const options = {
  scenarios: { load: __ENV.FIXED_VUS ? { executor: 'constant-vus', vus: Number(__ENV.FIXED_VUS), duration: __ENV.DURATION || '60s' } : { executor: 'ramping-vus', startVUs: 1, stages, gracefulRampDown: '15s' } },
  thresholds: {
    http_req_failed: ['rate<0.01'],
    http_req_duration: ['p(95)<800'],
    segment_latency: ['p(95)<500'],
  },
  summaryTrendStats: ['avg', 'min', 'med', 'p(90)', 'p(95)', 'p(99)', 'max'],
};

const uploadBody = MODE === 'upload' ? open(__ENV.VIDEO_FILE || './sample.mp4', 'b') : null;

export default function () {
  if (MODE === 'upload') {
    const id = `load-${__VU}-${__ITER}-${Date.now()}`;
    const res = http.post(`${BASE}/videos/${id}/upload`, uploadBody, { headers: { 'Content-Type': 'video/mp4' } });
    check(res, { 'upload aceptado (202)': r => r.status === 202 });
    if (res.status >= 500 || res.status === 429 || res.status === 0) rejected.add(1);
    sleep(5);
    // No borrar mientras FFmpeg procesa: limpiar los IDs load-* al finalizar manualmente.
    return;
  }

  const mpd = http.get(`${BASE}/videos/${VIDEO}/manifest.mpd`);
  check(mpd, { 'manifest 200': r => r.status === 200 });
  if (mpd.status !== 200) rejected.add(1);

  if (mpd.status !== 200) { sleep(1); return; }
  const init = http.get(`${BASE}/videos/${VIDEO}/init-${TRACK}.m4s`);
  check(init, { 'init 200': r => r.status === 200 });
  for (let n = 1; n <= SEGMENTS; n++) {
    const seg = http.get(`${BASE}/videos/${VIDEO}/chunk-${TRACK}-${String(n).padStart(5, '0')}.m4s`);
    segmentLatency.add(seg.timings.duration);
    check(seg, { 'segmento 200': r => r.status === 200 });
    if (seg.status !== 200) rejected.add(1);
    sleep(0.5);   // un reproductor real no pide todo de golpe
  }
}
