#!/usr/bin/env python3
"""Create the OpenSearch index and optional search pipeline."""

import argparse
import json
import urllib.error
import urllib.request
from pathlib import Path


def request(method, url, body=None):
    data = json.dumps(body).encode("utf-8") if body is not None else None
    req = urllib.request.Request(url, data=data, method=method, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=20) as response:
        return response.status, response.read().decode("utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default="http://localhost:9200")
    parser.add_argument("--index", default="enterprise_docs")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    mapping = json.loads((root / "config/opensearch-index.json").read_text(encoding="utf-8"))
    pipeline = json.loads((root / "config/opensearch-pipeline.json").read_text(encoding="utf-8"))

    try:
        request("DELETE", f"{args.url}/{args.index}")
    except urllib.error.HTTPError as error:
        if error.code != 404:
            raise

    status, _ = request("PUT", f"{args.url}/{args.index}", mapping)
    print(f"created index {args.index}: HTTP {status}")

    status, _ = request("PUT", f"{args.url}/_search/pipeline/rrf-hybrid-pipeline", pipeline)
    print(f"created search pipeline rrf-hybrid-pipeline: HTTP {status}")


if __name__ == "__main__":
    main()

