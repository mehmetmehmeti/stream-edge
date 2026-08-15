#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ENV_FILE="${ENV_FILE:-$SCRIPT_DIR/.env}"
APP_BINARY="$SCRIPT_DIR/build/streaming_service"

if [[ ! -f "$ENV_FILE" ]]; then
  cat <<EOF
Missing environment file: $ENV_FILE

Create it from the template first:
  cp "$SCRIPT_DIR/.env.example" "$SCRIPT_DIR/.env"
EOF
  exit 1
fi

if [[ ! -x "$APP_BINARY" ]]; then
  cat <<EOF
Missing executable: $APP_BINARY

Build the service first:
  cd "$SCRIPT_DIR"
  ./build-local.sh
EOF
  exit 1
fi

set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a

: "${DB_PORT:=5432}"

required_vars=(
  DB_HOST
  DB_NAME
  DB_USER
  DB_PASSWORD
)

for var_name in "${required_vars[@]}"; do
  if [[ -z "${!var_name:-}" ]]; then
    echo "Missing required variable in $ENV_FILE: $var_name" >&2
    exit 1
  fi
done

cd "$SCRIPT_DIR"
exec "$APP_BINARY" "$@"
