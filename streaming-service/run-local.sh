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

while IFS= read -r line || [[ -n "$line" ]]; do
  [[ -z "${line//[[:space:]]/}" || "$line" =~ ^[[:space:]]*# ]] && continue
  if [[ "$line" =~ ^[[:space:]]*([A-Za-z_][A-Za-z0-9_]*)=(.*)$ ]]; then
    key="${BASH_REMATCH[1]}"
    if [[ -z "${!key+x}" ]]; then
      export "$line"
    fi
  fi
done < "$ENV_FILE"

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
