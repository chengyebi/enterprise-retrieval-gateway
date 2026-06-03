#!/usr/bin/env python3
"""Deterministic local benchmark for retrieval planning and ACL filtering."""

import argparse
import random
import statistics
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import run_evaluation


ROOT = Path(__file__).resolve().parents[1]


def percentile(values, pct):
    if not values:
        return 0.0
    values = sorted(values)
    index = min(len(values) - 1, int((pct / 100.0) * (len(values) - 1)))
    return values[index]


def run_once(docs, users, query):
    start = time.perf_counter()
    results, _unauthorized, fallback = run_evaluation.retrieve(query, docs, users, "planner_acl")
    latency_ms = (time.perf_counter() - start) * 1000.0
    return latency_ms, bool(results), fallback


def benchmark(requests, concurrency):
    docs = run_evaluation.read_jsonl(ROOT / "datasets/demo_documents/chunks.jsonl")
    users = {row["user_id"]: row for row in __import__("json").loads((ROOT / "datasets/demo_acl/users.json").read_text(encoding="utf-8"))}
    queries = run_evaluation.read_jsonl(ROOT / "datasets/evaluation/queries.jsonl")
    for doc in docs:
        doc["_embedding"] = run_evaluation.embed(doc["title"] + "\n" + doc["content"])

    rng = random.Random(20260603 + concurrency)
    work = [rng.choice(queries) for _ in range(requests)]

    start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=concurrency) as pool:
        results = list(pool.map(lambda query: run_once(docs, users, query), work))
    elapsed = time.perf_counter() - start
    latencies = [row[0] for row in results]
    success = sum(1 for row in results if row[1])
    fallback = sum(1 for row in results if row[2])
    return {
        "concurrency": concurrency,
        "requests": requests,
        "qps": requests / elapsed if elapsed else 0.0,
        "p50": statistics.median(latencies),
        "p95": percentile(latencies, 95),
        "p99": percentile(latencies, 99),
        "empty_rate": 1.0 - success / requests,
        "error_rate": 0.0,
        "fallback_rate": fallback / requests,
    }


def render(rows):
    headers = ["concurrency", "requests", "qps", "p50_ms", "p95_ms", "p99_ms", "empty_rate", "error_rate", "fallback_rate"]
    lines = ["|" + "|".join(headers) + "|", "|" + "|".join(["---"] * len(headers)) + "|"]
    for row in rows:
        lines.append(
            "|{concurrency}|{requests}|{qps:.1f}|{p50:.3f}|{p95:.3f}|{p99:.3f}|{empty_rate:.3f}|{error_rate:.3f}|{fallback_rate:.3f}|".format(**row)
        )
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--requests", type=int, default=200)
    parser.add_argument("--concurrency", type=int, nargs="*", default=[1, 4, 8])
    parser.add_argument("--write-doc", default="")
    args = parser.parse_args()

    rows = [benchmark(args.requests, concurrency) for concurrency in args.concurrency]
    table = render(rows)
    print(table)
    if args.write_doc:
        content = "# Benchmark\n\n" + table + "\n\nBenchmark runs the local deterministic retrieval model over the generated enterprise corpus.\n"
        Path(args.write_doc).write_text(content, encoding="utf-8")


if __name__ == "__main__":
    main()
