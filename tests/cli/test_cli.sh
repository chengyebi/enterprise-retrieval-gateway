#!/bin/sh
set -eu

BIN="${1:-./build/ergateway}"

fail() {
    echo "FAIL: $1" >&2
    exit 1
}

require_error() {
    label="$1"
    expected="$2"
    shift 2

    out_file="$(mktemp)"
    err_file="$(mktemp)"
    if "$@" >"$out_file" 2>"$err_file"; then
        rm -f "$out_file" "$err_file"
        fail "$label should fail"
    fi
    if ! grep -Fq "$expected" "$err_file"; then
        echo "stderr:" >&2
        cat "$err_file" >&2
        rm -f "$out_file" "$err_file"
        fail "$label should mention $expected"
    fi
    rm -f "$out_file" "$err_file"
}

for value in 0 -1 51 abc; do
    require_error "invalid top-k $value" \
        "error: --top-k must be an integer between 1 and 50" \
        "$BIN" search --backend memory --user backend-user-01 --query E1027 --top-k "$value"
done

for value in 0 65536 abc; do
    require_error "invalid port $value" \
        "error: --port must be an integer between 1 and 65535" \
        "$BIN" serve --backend memory --port "$value"
done

valid_output="$("$BIN" search --backend memory --user backend-user-01 --query E1027 --top-k 5 --project payment)"
printf '%s' "$valid_output" | grep -Fq '"ok":true' || fail "valid search should succeed"
printf '%s' "$valid_output" | grep -Fq '"hits":[' || fail "valid search should return hits field"

echo "cli tests passed"
