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

"${PROJECT_CMAKE}" -S "${SCRIPT_DIR}" -B "${SCRIPT_DIR}/build" "${GENERATOR_ARGS[@]}"
"${PROJECT_CMAKE}" --build "${SCRIPT_DIR}/build" -j"${BUILD_JOBS:-$(nproc)}"
