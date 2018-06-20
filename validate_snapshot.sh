#!/bin/bash
set -o errexit -o nounset -o pipefail
which shellcheck > /dev/null && shellcheck "$0"

NETWORK="$1"
SNAPSHOT_FILE="$2"

(
	cd "$(dirname "$SNAPSHOT_FILE")"
	sha256sum "$(basename "$SNAPSHOT_FILE")"
)

RESTORE_TIME="$(date --utc +%Y-%m-%dT%H-%M-%SZ)"
RESTORE_DB_NAME="snapshot_validator_tmp_${RESTORE_TIME}_pid$$"

echo "Creating temporary database $RESTORE_DB_NAME …"
createdb "$RESTORE_DB_NAME"

echo "Restoring snapshot to database …"
gunzip -fcq "$SNAPSHOT_FILE" | psql --quiet --dbname "$RESTORE_DB_NAME"

echo "Restoring validating snapshot database …"
validate-snapshot-database "$NETWORK" "$RESTORE_DB_NAME"

echo "Removing temporary database $RESTORE_DB_NAME …"
dropdb "$RESTORE_DB_NAME"
