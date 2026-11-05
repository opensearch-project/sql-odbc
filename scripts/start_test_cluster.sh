#!/bin/bash
# Start a throwaway, security-enabled OpenSearch in Docker and load the
# OpenSearch Dashboards sample datasets the driver tests query
# (opensearch_dashboards_sample_data_{flights,ecommerce,logs}).
#
# Required:
#   OPENSEARCH_INITIAL_ADMIN_PASSWORD  admin password for the new cluster
#                                      (generate one per run; never commit it)
# Optional:
#   OPENSEARCH_VERSION   image tag for opensearch / opensearch-dashboards (default: 3)
#   OPENSEARCH_PORT      host port for OpenSearch HTTPS (default: 9200)
#   DASHBOARDS_PORT      host port for Dashboards, only while loading data (default: 5601)
#   CLUSTER_NAME         container/network name prefix (default: odbc-test)
#
# Both containers publish on 127.0.0.1 only. Stop with:
#   docker rm -f "$CLUSTER_NAME-opensearch"; docker network rm "$CLUSTER_NAME"
set -euo pipefail

: "${OPENSEARCH_INITIAL_ADMIN_PASSWORD:?set OPENSEARCH_INITIAL_ADMIN_PASSWORD (generate one per run)}"
VERSION="${OPENSEARCH_VERSION:-3}"
OS_PORT="${OPENSEARCH_PORT:-9200}"
OSD_PORT="${DASHBOARDS_PORT:-5601}"
NAME="${CLUSTER_NAME:-odbc-test}"
OS="$NAME-opensearch"
OSD="$NAME-dashboards"
AUTH="admin:$OPENSEARCH_INITIAL_ADMIN_PASSWORD"

wait_for() {  # wait_for <description> <command...>
    local what="$1"; shift
    for _ in $(seq 1 90); do
        if "$@" > /dev/null 2>&1; then echo "$what is up"; return 0; fi
        sleep 2
    done
    echo "timed out waiting for $what" >&2
    return 1
}

docker network inspect "$NAME" > /dev/null 2>&1 || docker network create "$NAME" > /dev/null

docker run --detach --name "$OS" --network "$NAME" --network-alias opensearch \
    --publish "127.0.0.1:$OS_PORT:9200" \
    --env discovery.type=single-node \
    --env "OPENSEARCH_JAVA_OPTS=-Xms1g -Xmx1g" \
    --env OPENSEARCH_INITIAL_ADMIN_PASSWORD \
    "opensearchproject/opensearch:$VERSION" > /dev/null

wait_for "OpenSearch" curl -sfk -u "$AUTH" "https://127.0.0.1:$OS_PORT/_cluster/health?wait_for_status=yellow&timeout=2s"
curl -sk -u "$AUTH" "https://127.0.0.1:$OS_PORT" | grep '"number"'

# Dashboards is only needed for its sample-data API; it connects to OpenSearch
# over the private Docker network and is removed once the data is loaded.
docker run --detach --name "$OSD" --network "$NAME" \
    --publish "127.0.0.1:$OSD_PORT:5601" \
    --env 'OPENSEARCH_HOSTS=["https://opensearch:9200"]' \
    "opensearchproject/opensearch-dashboards:$VERSION" > /dev/null
trap 'docker rm -f "$OSD" > /dev/null 2>&1 || true' EXIT

wait_for "OpenSearch Dashboards" curl -sf -u "$AUTH" "http://127.0.0.1:$OSD_PORT/api/status"

for dataset in flights ecommerce logs; do
    curl -sf -u "$AUTH" -X POST -H 'osd-xsrf: true' \
        "http://127.0.0.1:$OSD_PORT/api/sample_data/$dataset" > /dev/null
    echo "loaded sample data: $dataset"
done

curl -sfk -u "$AUTH" "https://127.0.0.1:$OS_PORT/_cat/indices/opensearch_dashboards_sample_data_*?v&h=index,docs.count"
