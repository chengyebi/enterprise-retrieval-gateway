#!/usr/bin/env sh
set -eu

curl -sS http://localhost:8080/health
printf '\n'

curl -sS -X POST http://localhost:8080/v1/search \
  -H 'Content-Type: application/json' \
  --data @tools/request_examples/search_backend_user.json
printf '\n'

curl -sS http://localhost:8080/metrics
printf '\n'

