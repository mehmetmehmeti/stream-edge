#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CMAKE_VERSION="${CMAKE_VERSION:-3.30.4}"
TOOLS_DIR="${TOOLS_DIR:-$SCRIPT_DIR/.tools}"

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

ARCHIVE_NAME="cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}.tar.gz"
INSTALL_DIR="${TOOLS_DIR}/cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}"
ARCHIVE_PATH="${TOOLS_DIR}/${ARCHIVE_NAME}"
DOWNLOAD_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/${ARCHIVE_NAME}"

if [[ -x "${INSTALL_DIR}/bin/cmake" ]]; then
  echo "Project-local CMake is already installed at:"
  echo "  ${INSTALL_DIR}/bin/cmake"
  exit 0
fi

mkdir -p "${TOOLS_DIR}"

echo "Downloading ${DOWNLOAD_URL}"
curl -L --fail --output "${ARCHIVE_PATH}" "${DOWNLOAD_URL}"

EXTRACT_DIR="$(mktemp -d "${TOOLS_DIR}/cmake-extract.XXXXXX")"
trap 'rm -rf "${EXTRACT_DIR}"' EXIT

tar -xzf "${ARCHIVE_PATH}" -C "${EXTRACT_DIR}"
rm -rf "${INSTALL_DIR}"
mv "${EXTRACT_DIR}/cmake-${CMAKE_VERSION}-linux-${CMAKE_ARCH}" "${INSTALL_DIR}"
rm -f "${ARCHIVE_PATH}"

echo
echo "Project-local CMake installed:"
echo "  ${INSTALL_DIR}/bin/cmake"
echo
echo "Build this project with:"
echo "  ./build-local.sh"
