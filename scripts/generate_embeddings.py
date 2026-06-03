#!/usr/bin/env python3
"""Generate deterministic local hash embeddings for JSONL chunks."""

import argparse
import hashlib
import json
import math
from pathlib import Path


def tokenize(text):
    tokens = []
    current = []
    for char in text.lower():
        if char.isalnum() or char in "_-":
            current.append(char)
        elif current:
            tokens.append("".join(current))
            current = []
    if current:
        tokens.append("".join(current))
    return list(dict.fromkeys(tokens))


def stable_index(text, dimensions):
    digest = hashlib.sha256(text.encode("utf-8")).digest()
    return int.from_bytes(digest[:8], "big") % dimensions


def embed(text, dimensions):
    vector = [0.0] * dimensions
    for token in tokenize(text):
        vector[stable_index("tok:" + token, dimensions)] += 2.0
    lower = text.lower()
    for index in range(max(0, len(lower) - 3)):
        vector[stable_index("gram:" + lower[index : index + 4], dimensions)] += 0.25
    norm = math.sqrt(sum(value * value for value in vector))
    return [value / norm for value in vector] if norm else vector


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default="datasets/demo_documents/chunks.jsonl")
    parser.add_argument("--output", default="datasets/demo_documents/chunks_with_embeddings.jsonl")
    parser.add_argument("--dimensions", type=int, default=64)
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    input_path = root / args.input
    output_path = root / args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)

    count = 0
    with input_path.open("r", encoding="utf-8") as source, output_path.open("w", encoding="utf-8") as target:
        for line in source:
            if not line.strip():
                continue
            row = json.loads(line)
            row["embedding"] = embed(row["title"] + "\n" + row["content"], args.dimensions)
            row["embedding_model_version"] = "local-hash-v1"
            target.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
            count += 1
    print(f"wrote {count} embedded chunks to {output_path}")


if __name__ == "__main__":
    main()

