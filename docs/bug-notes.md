# Bug Notes

## Header Changes Did Not Trigger Rebuild

### Symptom

Changing the planner threshold in a header did not affect `make demo`.

### Reproduction

Update `include/retrieval_gateway/search/filter_aware_query_planner.h`, then run `make demo`.

### Cause

The initial `Makefile` targets depended only on source files and did not include headers.

### Fix

Add `HEADERS := $(shell find include -type f -name '*.h')` and include `$(HEADERS)` in binary targets.

### How To Avoid Again

Use CMake or generated dependency files for a larger project. For this small repo, explicit header dependencies are enough.

### Test Added

`make test` and `make demo` were rerun after the change.

## Benchmark Error Rate Counted Empty Results

### Symptom

`scripts/run_benchmark.py` reported `error_rate` when a query returned no hits.

### Reproduction

Run benchmark against users with no authorized relevant documents.

### Cause

The script used "has at least one result" as success.

### Fix

Split `empty_rate` from true `error_rate`.

### How To Avoid Again

Keep retrieval quality metrics separate from transport/runtime errors.

### Test Added

Benchmark was regenerated with both columns.

