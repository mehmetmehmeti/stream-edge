#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUNTIME="$ROOT/.runtime"
mkdir -p "$RUNTIME"

stop_stack() {
  curl --max-time 10 -sf -X POST http://127.0.0.1:8081/record/stop -H 'Content-Type: application/json' -d '{"metadata_managed_by_client":true}' >/dev/null || true
  curl --max-time 10 -sf -X POST http://127.0.0.1:8081/stream/stop -H 'Content-Type: application/json' -d '{}' >/dev/null || true
  # Limit cleanup to processes owned by these diploma repositories.
  for proc in /proc/[0-9]*; do
    cwd="$(readlink "$proc/cwd" 2>/dev/null || true)"
    case "$cwd" in
      "$ROOT"/diploma-*)
        command="$(tr '\0' ' ' < "$proc/cmdline" 2>/dev/null || true)"
        case "$command" in
          *dist/src/main.js*|*next\ dev*|*next-server*|*node_modules/next/dist/bin/next*|*node\ server.mjs*|*build/streaming_service*|*appDiplomaFrontendEdge*)
            kill -TERM "${proc##*/}" 2>/dev/null || true ;;
        esac ;;
    esac
  done
}

start_one() {
  local name="$1" repo="$2"
  shift 2
  (cd "$ROOT/$repo"; exec setsid "$@" </dev/null >>"$RUNTIME/$name.log" 2>&1) &
  echo "$!" >"$RUNTIME/$name.pid"
}

wait_http() {
  local url="$1"
  for attempt in {1..60}; do
    if curl --max-time 1 -s -o /dev/null "$url"; then return; fi
    sleep 0.5
  done
  echo "Service did not start: $url. Check $RUNTIME/*.log" >&2
  exit 1
}

case "${1:-start}" in
  stop) stop_stack ;;
  build)
    (cd "$ROOT/diploma-2026-backend-cloud"; npm run prisma:generate)
    (cd "$ROOT/diploma-2026-backend-edge"; npm run prisma:generate)
    (cd "$ROOT/diploma-2026-backend-cloud"; npm run build)
    (cd "$ROOT/diploma-2026-backend-edge"; npm run build)
    (cd "$ROOT/diploma-2026-frontend-cloud"; npm run typecheck)
    BUILD_JOBS=4 bash "$ROOT/diploma-2026-stream-edge/streaming-service/build-local.sh"
    cmake --build "$ROOT/diploma-2026-frontend-edge/build-local" -j4
    bash "$ROOT/diploma-media-server/scripts/build-whip-client.sh"
    ;;
  start)
    for port in 3000 3001 3002 3010 8081; do
      if ss -H -ltn "sport = :$port" | read -r _; then
        echo "Port $port is in use. Run: bash $0 stop" >&2; exit 1
      fi
    done
    bash "$ROOT/diploma-2026-stream-edge/streaming-service/start-local-postgres.sh"
    start_one media diploma-media-server env HTTP_PORT=3010 ANNOUNCED_ADDRESS="${ANNOUNCED_ADDRESS:-127.0.0.1}" node server.mjs
    start_one stream diploma-2026-stream-edge/streaming-service ./build/streaming_service
    start_one cloud diploma-2026-backend-cloud node dist/src/main.js
    wait_http http://127.0.0.1:3000/auth/me
    start_one edge diploma-2026-backend-edge node dist/src/main.js
    start_one web diploma-2026-frontend-cloud node node_modules/next/dist/bin/next dev --port 3001
    wait_http http://127.0.0.1:3002/auth/me
    wait_http http://127.0.0.1:8081/stream/status
    wait_http http://127.0.0.1:3010/api/health
    wait_http http://127.0.0.1:3001
    if [[ "${2:-}" != --no-qt ]]; then
      start_one qt diploma-2026-frontend-edge ./build-local/appDiplomaFrontendEdge
    fi
    echo "Cloud: http://localhost:3001"
    echo "Logs: $RUNTIME/{media,stream,cloud,edge,web,qt}.log"
    ;;
  *) echo "Usage: bash $0 {build|start [--no-qt]|stop}" >&2; exit 1 ;;
esac
