#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cmake_bin="${repo_root}/.cmake-venv/bin/cmake"
build_dir="${repo_root}/streaming-service/build-holoscan41-cuda12"
required_cmake_version="3.30.4"

version_ge() {
  printf '%s\n%s\n' "$2" "$1" | sort -V -C
}

ensure_cmake() {
  local system_cmake=""
  local system_version=""

  if command -v cmake >/dev/null 2>&1; then
    system_cmake="$(command -v cmake)"
    system_version="$("${system_cmake}" --version | awk 'NR==1 { print $3 }')"
  fi

  if [[ -n "${system_version}" ]] && version_ge "${system_version}" "${required_cmake_version}"; then
    cmake_bin="${system_cmake}"
    return
  fi

  if [[ ! -x "${cmake_bin}" ]]; then
    python3 -m venv "${repo_root}/.cmake-venv"
    "${repo_root}/.cmake-venv/bin/pip" install "cmake==${required_cmake_version}"
  fi
}

ensure_cuda12_active() {
  local active_cuda=""

  if [[ -e /usr/local/cuda ]]; then
    active_cuda="$(readlink -f /usr/local/cuda)"
  fi

  if [[ "${active_cuda}" != "/usr/local/cuda-12.6" ]]; then
    cat <<EOF
Expected /usr/local/cuda to point to CUDA 12.6 for holoscan-cuda-12.

Current /usr/local/cuda target:
  ${active_cuda:-missing}

Run this once, then rerun this script:
  sudo update-alternatives --set cuda /usr/local/cuda-12.6
EOF
    exit 1
  fi
}

ensure_cmake
ensure_cuda12_active

"${cmake_bin}" \
  -S "${repo_root}/streaming-service" \
  -B "${build_dir}" \
  -DCMAKE_BUILD_TYPE=Debug

"${cmake_bin}" --build "${build_dir}"
