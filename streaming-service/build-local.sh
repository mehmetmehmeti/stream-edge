#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CMAKE_VERSION="${CMAKE_VERSION:-3.30.4}"

case "$(uname -m)" in
  x86_64)
    CMAKE_ARCH="x86_64"
    ;;
  aarch64|arm64)
    CMAKE_ARCH="aarch64"
    ;;
  *)
    echo "Unsupported architecture: $(uname -m)" >&2
    exit 1
    ;;
esac

PROJECT_CMAKE="${SCRIPT_DIR}/.tools/cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}/bin/cmake"

if [[ ! -x "${PROJECT_CMAKE}" ]]; then
  cat <<EOF
Missing project-local CMake: ${PROJECT_CMAKE}

Install it without touching the system CMake:
  cd "${SCRIPT_DIR}"
  ./install-project-cmake.sh
EOF
  exit 1
fi

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GENERATOR_ARGS=(-G Ninja)
fi

PREFIX_PATH="${CMAKE_PREFIX_PATH:-}"
if [[ -n "${STREAM_EDGE_EXTRA_PREFIX_PATH:-}" ]]; then
  if [[ -n "$PREFIX_PATH" ]]; then
    PREFIX_PATH="${STREAM_EDGE_EXTRA_PREFIX_PATH}:${PREFIX_PATH}"
  else
    PREFIX_PATH="${STREAM_EDGE_EXTRA_PREFIX_PATH}"
  fi
fi
PREFIX_PATH="${PREFIX_PATH//:/$';'}"

CMAKE_ARGS=()
if [[ -n "$PREFIX_PATH" ]]; then
  CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=${PREFIX_PATH}")
fi
if [[ -z "${CMAKE_BUILD_TYPE:-}" ]]; then
  CMAKE_ARGS+=("-DCMAKE_BUILD_TYPE=Release")
else
  CMAKE_ARGS+=("-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
fi

"${PROJECT_CMAKE}" -S "${SCRIPT_DIR}" -B "${SCRIPT_DIR}/build" \
  "${GENERATOR_ARGS[@]}" "${CMAKE_ARGS[@]}"
"${PROJECT_CMAKE}" --build "${SCRIPT_DIR}/build" -j"${BUILD_JOBS:-$(nproc)}"
