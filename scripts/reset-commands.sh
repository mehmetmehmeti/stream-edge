#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
if [[ "${1:-}" != --yes ]]; then
  echo "Stops the diploma stack, backs up databases, clears commands, and closes stale stream states."
  echo "Usage: bash $0 --yes"
  exit 1
fi
bash "$ROOT/diploma-2026-stream-edge/scripts/stack.sh" stop
bash "$ROOT/diploma-2026-stream-edge/streaming-service/start-local-postgres.sh"
export PGPASSWORD="${PGPASSWORD:-change_me}"
PG=( -h 127.0.0.1 -p 55432 -U streaming_service )
STAMP="$(date +%Y%m%d-%H%M%S)"
for database in diploma_edge diploma_cloud streaming_service; do
  /usr/lib/postgresql/16/bin/pg_dump "${PG[@]}" -Fc -f "$ROOT/.runtime/$database-$STAMP.dump" "$database"
done
for database in diploma_edge diploma_cloud; do
  psql "${PG[@]}" -v ON_ERROR_STOP=1 -d "$database" <<'SQL'
BEGIN;
DELETE FROM sync_outbox WHERE entity_type = 'command';
DELETE FROM sync_inbox WHERE entity_type = 'command';
DELETE FROM commands;
UPDATE sync_outbox SET status='APPLIED',acked_at=now() WHERE entity_type IN ('stream','device','video') AND status <> 'APPLIED';
UPDATE streams SET status='ENDED',ended_at=now(),last_error=NULL,version=version+1,updated_at=now() WHERE status IN ('LIVE','STARTING','STOPPING');
UPDATE videos SET status='FAILED',ended_at=now(),version=version+1,updated_at=now() WHERE status='RECORDING';
UPDATE devices SET streaming=false,recording=false,status='OFFLINE',last_error=NULL,version=version+1,updated_at=now();
COMMIT;
SQL
done
psql "${PG[@]}" -v ON_ERROR_STOP=1 -d streaming_service -c 'DELETE FROM outbox_commands;'
echo "Commands cleared. Users and recording files preserved. Backups: $ROOT/.runtime/*-$STAMP.dump"
