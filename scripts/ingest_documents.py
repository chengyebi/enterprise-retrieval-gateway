#!/usr/bin/env python3
"""Bulk ingest generated document chunks into OpenSearch."""

import argparse
import json
import urllib.request
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://localhost:9200")
    parser.add_argument("--index", default="enterprise_docs")
    parser.add_argument("--input", default="datasets/demo_documents/chunks_with_embeddings.jsonl")
    parser.add_argument("--batch-size", type=int, default=500)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    input_path = root / args.input
    rows = []
    with input_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if line.strip():
                rows.append(json.loads(line))

    indexed = 0
    for start in range(0, len(rows), args.batch_size):
        batch = rows[start : start + args.batch_size]
        payload_lines = []
        for row in batch:
            action = {"index": {"_index": args.index, "_id": row["chunk_id"]}}
            payload_lines.append(json.dumps(action, ensure_ascii=False))
            payload_lines.append(json.dumps(row, ensure_ascii=False))
        payload = ("\n".join(payload_lines) + "\n").encode("utf-8")
        req = urllib.request.Request(
            f"{args.url}/_bulk",
            data=payload,
            method="POST",
            headers={"Content-Type": "application/x-ndjson"},
        )
        with urllib.request.urlopen(req, timeout=60) as response:
            result = json.loads(response.read().decode("utf-8"))
        if result.get("errors"):
            failed = [item for item in result.get("items", []) if item.get("index", {}).get("error")]
            raise RuntimeError(f"bulk indexing failed for {len(failed)} items")
        indexed += len(batch)
        print(f"indexed {indexed}/{len(rows)}")


if __name__ == "__main__":
    main()

