#!/usr/bin/env bash
set -euo pipefail

cleanup() {
    if [[ -n "${frontend_pid:-}" ]] && kill -0 "${frontend_pid}" 2>/dev/null; then
        kill "${frontend_pid}" 2>/dev/null || true
        wait "${frontend_pid}" 2>/dev/null || true
    fi
}

trap cleanup EXIT INT TERM

/home/qtrunner/appFrontendEdge &
frontend_pid=$!

wait "${frontend_pid}"
exit_code=$?

cleanup
exit "${exit_code}"
