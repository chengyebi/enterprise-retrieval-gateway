#!/usr/bin/env python3
"""Export repository demo ACL/documents into a static browser asset.

The GitHub Pages demo cannot run the C++ gateway, so this script converts the
existing simulated enterprise corpus into a compact JSON file consumable by the
React app. Raw embedding vectors are intentionally omitted to keep the static
asset small; the browser demo computes lightweight deterministic hash vectors
when it needs to simulate vector/cosine retrieval.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
USERS_PATH = ROOT / "datasets" / "demo_acl" / "users.json"
CHUNKS_PATH = ROOT / "datasets" / "demo_documents" / "chunks.jsonl"
QUERIES_PATH = ROOT / "datasets" / "evaluation" / "queries.jsonl"
OUTPUT_PATH = ROOT / "web" / "public" / "data" / "demo_data.json"


def as_string_list(value: Any) -> list[str]:
    if not isinstance(value, list):
        return []
    return [str(item) for item in value if item is not None]


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, start=1):
            raw = line.strip()
            if not raw:
                continue
            try:
                parsed = json.loads(raw)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_number} is not valid JSON") from exc
            if isinstance(parsed, dict):
                rows.append(parsed)
    return rows


def normalize_user(raw: dict[str, Any]) -> dict[str, Any]:
    tenant_id = str(raw.get("tenant_id") or raw.get("tenant") or "")
    project_ids = as_string_list(raw.get("project_ids") or raw.get("projects"))
    return {
        "user_id": str(raw.get("user_id") or ""),
        "tenant_id": tenant_id,
        "tenant": tenant_id,
        "department": str(raw.get("department") or ""),
        "groups": as_string_list(raw.get("groups")),
        "project_ids": project_ids,
        "projects": project_ids,
        "is_admin": bool(raw.get("is_admin", False)),
    }


def normalize_document(raw: dict[str, Any]) -> dict[str, Any]:
    content = str(raw.get("content") or raw.get("text") or raw.get("snippet") or "")
    tenant_id = str(raw.get("tenant_id") or raw.get("tenant") or "")
    return {
        "tenant_id": tenant_id,
        "tenant": tenant_id,
        "document_id": str(raw.get("document_id") or ""),
        "chunk_id": str(raw.get("chunk_id") or raw.get("document_id") or ""),
        "title": str(raw.get("title") or raw.get("document_id") or ""),
        "text": content,
        "snippet": content[:260],
        "department": str(raw.get("department") or "unknown"),
        "project_id": str(raw.get("project_id") or "unknown"),
        "document_type": str(raw.get("document_type") or "unknown"),
        "allowed_groups": as_string_list(raw.get("allowed_groups")),
        "document_version": int(raw.get("document_version") or 1),
        "updated_at": str(raw.get("updated_at") or ""),
        "embedding_model_version": str(raw.get("embedding_model_version") or "local-hash-v1"),
    }


def load_users() -> list[dict[str, Any]]:
    with USERS_PATH.open("r", encoding="utf-8") as handle:
        raw_users = json.load(handle)
    if not isinstance(raw_users, list):
        raise ValueError(f"{USERS_PATH} must contain a JSON array")
    return [normalize_user(user) for user in raw_users if isinstance(user, dict)]


def load_documents() -> list[dict[str, Any]]:
    return [normalize_document(row) for row in load_jsonl(CHUNKS_PATH)]


def load_queries() -> list[dict[str, Any]]:
    queries: list[dict[str, Any]] = []
    for row in load_jsonl(QUERIES_PATH):
        query = str(row.get("query") or "")
        if not query:
            continue
        queries.append(
            {
                "query_id": str(row.get("query_id") or ""),
                "query": query,
                "user_id": str(row.get("user_id") or ""),
                "top_k": int(row.get("top_k") or 10),
                "project_ids": as_string_list(row.get("project_ids")),
                "document_types": as_string_list(row.get("document_types")),
            }
        )
    return queries


def choose_examples(queries: list[dict[str, Any]]) -> list[str]:
    wanted = [
        "E1027 payment_timeout",
        "checkout API v3 dependency timeout",
        "downstream payment interface no response",
        "OpenSearch bulk 429",
        "ANN ACL filtering too few hits",
        "confidential financial report",
        "enterprise customer pricing",
        "promotion packet peer feedback",
    ]
    query_texts = {str(item["query"]) for item in queries}
    examples: list[str] = []
    for item in wanted:
        if item in query_texts or item == "E1027 payment_timeout":
            examples.append(item)
    for item in queries:
        query = str(item["query"])
        if query not in examples:
            examples.append(query)
        if len(examples) >= 8:
            break
    return examples[:8]


def main() -> int:
    users = load_users()
    documents = load_documents()
    queries = load_queries()
    projects = sorted({doc["project_id"] for doc in documents if doc["project_id"]})
    document_types = sorted({doc["document_type"] for doc in documents if doc["document_type"]})
    departments = sorted({doc["department"] for doc in documents if doc["department"]})

    output = {
        "schema_version": 1,
        "source_files": [
            str(USERS_PATH.relative_to(ROOT)),
            str(CHUNKS_PATH.relative_to(ROOT)),
            str(QUERIES_PATH.relative_to(ROOT)),
        ],
        "acl_model": {
            "non_admin_rule": "tenant_id, department, project_id, and allowed_groups intersection must match",
            "admin_rule": "admins can access all documents in the same tenant",
            "unknown_user": "fail_closed",
        },
        "users": users,
        "documents": documents,
        "projects": projects,
        "document_types": document_types,
        "departments": departments,
        "example_queries": choose_examples(queries),
        "evaluation_queries": queries[:120],
    }

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    with OUTPUT_PATH.open("w", encoding="utf-8") as handle:
        json.dump(output, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")
    print(f"wrote {OUTPUT_PATH.relative_to(ROOT)} with {len(documents)} documents and {len(users)} users")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
