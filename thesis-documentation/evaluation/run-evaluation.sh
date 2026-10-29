#!/usr/bin/env bash
set -euo pipefail
DIRECTORY="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$DIRECTORY/../.." && pwd)"
OUTPUT="${THESIS_EVALUATION_OUTPUT:-$ROOT/.runtime/evaluation/$(date -u +%Y%m%dT%H%M%SZ)}"
export THESIS_TRANSFER_TIMING_FILE="$OUTPUT/transfers.jsonl"
export ANNOUNCED_ADDRESS=127.0.0.1
export CORS_ALLOWED_ORIGINS=http://localhost:3001,http://127.0.0.1:3001
export NEXT_PUBLIC_API_URL=http://localhost:3000
export NEXT_PUBLIC_MEDIA_SERVER_URL=http://localhost:3010
export MEDIA_SERVER_PUBLIC_URL=http://127.0.0.1:3010

if [[ "${1:-}" != --camera && "${1:-}" != --preflight ]]; then
  printf '%s\n' 'This test uses the real camera and creates local MP4 recordings.' \
    'Run: bash thesis-documentation/evaluation/run-evaluation.sh --camera' \
    'It starts the thesis stack, measures three alternating off/on recording pairs,' \
    'then records at least 10 minutes. Allow about 18 minutes plus setup/transfer.' \
    'Close existing thesis processes first. No command queue is deleted.'
  printf '%s\n' 'Use --preflight instead to validate browser login without requesting a stream.'
  exit 0
fi
MODE="$1"
shift
for port in 3000 3001 3002 3010 8081; do
  if [[ -n "$(ss -H -ltn "sport = :$port")" ]]; then
    printf 'Port %s is already in use. Stop the thesis stack yourself first.\n' "$port" >&2
    exit 1
  fi
done
mkdir -p "$OUTPUT"
node -e "require.resolve('playwright', {paths: [process.argv[1]]})" "$DIRECTORY" >/dev/null
stack_started=0
cleanup() {
  if [[ "$stack_started" == 1 ]]; then bash "$ROOT/stream-edge/scripts/stack.sh" stop; fi
}
trap cleanup EXIT
stack_started=1
bash "$ROOT/stream-edge/scripts/stack.sh" start --no-qt
node "$DIRECTORY/verify-benchmark.cjs" "$MODE" --headed --create-test-user --root "$ROOT" --output "$OUTPUT" "$@"
if [[ "$MODE" == --camera ]]; then python3 "$DIRECTORY/analyze_results.py" "$OUTPUT"; fi
printf 'Evidence: %s\n' "$OUTPUT"
