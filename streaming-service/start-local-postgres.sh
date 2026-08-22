#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PG_BIN="${PG_BIN:-/usr/lib/postgresql/16/bin}"
RUNTIME_DIR="${STREAM_EDGE_PGDATA:-$(cd "$SCRIPT_DIR/../.." && pwd)/.runtime/postgres-stream-edge}"
LOG_FILE="${STREAM_EDGE_PGLOG:-$(cd "$SCRIPT_DIR/../.." && pwd)/.runtime/postgres-stream-edge.log}"

DB_HOST="${DB_HOST:-127.0.0.1}"
DB_PORT="${DB_PORT:-55432}"
DB_NAME="${DB_NAME:-streaming_service}"
DB_USER="${DB_USER:-streaming_service}"
DB_PASSWORD="${DB_PASSWORD:-change_me}"

for tool in initdb pg_ctl psql createdb; do
  if [[ ! -x "$PG_BIN/$tool" ]]; then
    echo "Missing PostgreSQL tool: $PG_BIN/$tool" >&2
    exit 1
  fi
done

mkdir -p "$(dirname "$RUNTIME_DIR")" "$(dirname "$LOG_FILE")"

if [[ ! -f "$RUNTIME_DIR/PG_VERSION" ]]; then
  pwfile="$(mktemp)"
  trap 'rm -f "$pwfile"' EXIT
  printf '%s\n' "$DB_PASSWORD" > "$pwfile"

  "$PG_BIN/initdb" \
    -D "$RUNTIME_DIR" \
    --username="$DB_USER" \
    --auth-local=trust \
    --auth-host=scram-sha-256 \
    --pwfile="$pwfile"

  cat >> "$RUNTIME_DIR/postgresql.conf" <<EOF

# Diploma stream-edge local runtime database.
listen_addresses = '$DB_HOST'
port = $DB_PORT
unix_socket_directories = '$RUNTIME_DIR'
EOF
fi

if ! "$PG_BIN/pg_ctl" -D "$RUNTIME_DIR" status >/dev/null 2>&1; then
  "$PG_BIN/pg_ctl" -D "$RUNTIME_DIR" -l "$LOG_FILE" start
fi

if ! PGPASSWORD="$DB_PASSWORD" "$PG_BIN/psql" \
  -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d postgres \
  -tAc "select 1 from pg_database where datname = '$DB_NAME'" | grep -q 1; then
  PGPASSWORD="$DB_PASSWORD" "$PG_BIN/createdb" \
    -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" "$DB_NAME"
fi

cat <<EOF
Local stream-edge Postgres is ready.

Use these values in streaming-service/.env:
  DB_HOST=$DB_HOST
  DB_PORT=$DB_PORT
  DB_NAME=$DB_NAME
  DB_USER=$DB_USER
  DB_PASSWORD=$DB_PASSWORD
EOF
