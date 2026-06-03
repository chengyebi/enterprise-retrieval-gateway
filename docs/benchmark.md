# Benchmark

Local benchmark over the generated 3000-chunk corpus using the deterministic planner strategy.

|concurrency|requests|qps|p50_ms|p95_ms|p99_ms|empty_rate|error_rate|fallback_rate|
|---|---|---|---|---|---|---|---|---|
|1|120|68.3|8.846|43.731|45.506|0.175|0.000|0.000|
|4|120|71.7|33.654|161.144|186.767|0.192|0.000|0.000|
|8|120|62.3|91.196|344.912|415.160|0.117|0.000|0.000|

Benchmark runs the local deterministic retrieval model over the generated enterprise corpus.

`empty_rate` is a retrieval-quality observation, not a runtime error. `error_rate` counts execution failures and is zero in this run.
