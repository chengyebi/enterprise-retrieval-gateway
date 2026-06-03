#!/usr/bin/env python3
"""Generate deterministic enterprise documents and simulated human labels."""

import argparse
import hashlib
import json
import random
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RNG = random.Random(20260603)


USERS = [
    {
        "user_id": "backend-user-01",
        "tenant_id": "tenant-acme",
        "department": "engineering",
        "groups": ["backend", "employee"],
        "project_ids": ["payment", "search", "security"],
        "is_admin": False,
    },
    {
        "user_id": "sre-user-01",
        "tenant_id": "tenant-acme",
        "department": "engineering",
        "groups": ["sre", "employee"],
        "project_ids": ["payment", "search"],
        "is_admin": False,
    },
    {
        "user_id": "finance-user-01",
        "tenant_id": "tenant-acme",
        "department": "finance",
        "groups": ["finance", "employee"],
        "project_ids": ["finance-core", "enterprise-sales"],
        "is_admin": False,
    },
    {
        "user_id": "sales-user-01",
        "tenant_id": "tenant-acme",
        "department": "sales",
        "groups": ["sales", "employee"],
        "project_ids": ["enterprise-sales"],
        "is_admin": False,
    },
    {
        "user_id": "hr-user-01",
        "tenant_id": "tenant-acme",
        "department": "hr",
        "groups": ["hr", "employee"],
        "project_ids": ["people"],
        "is_admin": False,
    },
    {
        "user_id": "admin-user",
        "tenant_id": "tenant-acme",
        "department": "engineering",
        "groups": ["admin", "security"],
        "project_ids": ["payment", "search", "security", "finance-core", "enterprise-sales", "people"],
        "is_admin": True,
    },
    {
        "user_id": "no-access-user",
        "tenant_id": "tenant-acme",
        "department": "engineering",
        "groups": ["intern"],
        "project_ids": ["sandbox"],
        "is_admin": False,
    },
]


TEMPLATES = [
    {
        "prefix": "incident-payment",
        "department": "engineering",
        "project_id": "payment",
        "groups": ["backend", "sre"],
        "document_type": "incident_report",
        "title": "Payment service E1027 incident review {i}",
        "content": "Payment service raised E1027 after checkout API v3 timed out. Downstream transaction gateway had long latency and payment_timeout alerts fired.",
        "queries": ["E1027 payment timeout", "checkout API v3 dependency timeout", "payment service incident"],
    },
    {
        "prefix": "runbook-payment",
        "department": "engineering",
        "project_id": "payment",
        "groups": ["backend", "sre"],
        "document_type": "runbook",
        "title": "Payment timeout runbook {i}",
        "content": "When downstream payment interface has no response for a long time, inspect transaction gateway saturation, retry queues, and circuit breaker state.",
        "queries": ["downstream payment interface no response", "transaction gateway retry queue", "payment circuit breaker"],
    },
    {
        "prefix": "api-payment-v3",
        "department": "engineering",
        "project_id": "payment",
        "groups": ["backend"],
        "document_type": "api_doc",
        "title": "Payment API v3 contract {i}",
        "content": "Checkout API v3 accepts order_id, merchant_id, amount, idempotency_key. E1027 means dependency timeout and requires retry-safe handling.",
        "queries": ["API v3 idempotency key", "E1027 dependency timeout", "checkout contract fields"],
    },
    {
        "prefix": "incident-search",
        "department": "engineering",
        "project_id": "search",
        "groups": ["backend", "sre"],
        "document_type": "incident_report",
        "title": "Search index lag incident {i}",
        "content": "OpenSearch bulk upsert returned 429 and retrieval index freshness lagged. Failed document ids must be retried in order.",
        "queries": ["OpenSearch bulk 429", "index freshness lag", "bulk upsert retry"],
    },
    {
        "prefix": "design-vector-filter",
        "department": "engineering",
        "project_id": "search",
        "groups": ["backend", "sre"],
        "document_type": "design_doc",
        "title": "Vector retrieval with strict filters {i}",
        "content": "ANN can return too few authorized hits after ACL filtering. The planner expands candidate limits or switches to exact search for small candidate sets.",
        "queries": ["ANN ACL filtering too few hits", "planner expands candidate limit", "exact search small candidate set"],
    },
    {
        "prefix": "security-acl",
        "department": "engineering",
        "project_id": "security",
        "groups": ["backend", "security"],
        "document_type": "policy",
        "title": "Server-side ACL enforcement {i}",
        "content": "The gateway builds ACL filters on the server side. unrestricted=true is ignored and resolver failures default to deny.",
        "queries": ["server side ACL filter", "resolver failure default deny", "unrestricted parameter ignored"],
    },
    {
        "prefix": "finance-report",
        "department": "finance",
        "project_id": "finance-core",
        "groups": ["finance", "executive"],
        "document_type": "financial_report",
        "title": "Confidential financial report {i}",
        "content": "Quarterly revenue, gross margin, contract exposure, and cash runway are confidential finance data.",
        "queries": ["confidential financial report", "quarterly revenue gross margin", "cash runway"],
    },
    {
        "prefix": "finance-expense",
        "department": "finance",
        "project_id": "finance-core",
        "groups": ["finance"],
        "document_type": "policy",
        "title": "Expense reimbursement policy {i}",
        "content": "Travel reimbursement requires invoice id, cost center, manager approval, and submission within 30 days.",
        "queries": ["travel reimbursement invoice", "expense policy cost center", "manager approval reimbursement"],
    },
    {
        "prefix": "sales-contract",
        "department": "sales",
        "project_id": "enterprise-sales",
        "groups": ["sales", "finance"],
        "document_type": "contract",
        "title": "Enterprise customer contract {i}",
        "content": "Customer pricing, SLA credits, renewal options, and discount boundaries are restricted to sales and finance.",
        "queries": ["enterprise customer pricing", "SLA credits renewal", "discount boundaries"],
    },
    {
        "prefix": "hr-policy",
        "department": "hr",
        "project_id": "people",
        "groups": ["hr", "employee"],
        "document_type": "policy",
        "title": "People policy {i}",
        "content": "Leave policy, promotion packets, peer feedback, manager assessment, and calibration notes are managed by HR.",
        "queries": ["leave policy manager approval", "promotion packet peer feedback", "calibration notes"],
    },
]


def content_hash(text: str) -> str:
    return "sha256:" + hashlib.sha256(text.encode("utf-8")).hexdigest()


def is_authorized(user, doc) -> bool:
    if user["tenant_id"] != doc["tenant_id"]:
        return False
    if user["is_admin"]:
        return True
    return (
        user["department"] == doc["department"]
        and doc["project_id"] in user["project_ids"]
        and bool(set(user["groups"]).intersection(doc["allowed_groups"]))
    )


def build_documents(count: int):
    docs = []
    for i in range(count):
        template = TEMPLATES[i % len(TEMPLATES)]
        doc_id = f"{template['prefix']}-{i:04d}"
        suffix = f" Sample {i} contains ticket T{i % 97:03d}, version v{1 + i % 5}, and owner team {template['project_id']}."
        text = template["content"] + suffix
        doc = {
            "tenant_id": "tenant-acme",
            "document_id": doc_id,
            "chunk_id": f"{doc_id}#1",
            "title": template["title"].format(i=i),
            "content": text,
            "department": template["department"],
            "project_id": template["project_id"],
            "allowed_groups": template["groups"],
            "document_type": template["document_type"],
            "document_version": 1 + i % 9,
            "content_hash": content_hash(text),
            "updated_at": "2026-06-03T10:00:00Z",
            "embedding_model_version": "local-hash-v1",
        }
        docs.append(doc)
    return docs


def build_queries(docs, query_count: int):
    queries = []
    judgments = []
    by_template = {}
    for doc in docs:
        prefix = doc["document_id"].rsplit("-", 1)[0]
        by_template.setdefault(prefix, []).append(doc)

    for idx in range(query_count):
        template = TEMPLATES[idx % len(TEMPLATES)]
        user = USERS[idx % len(USERS)]
        query = RNG.choice(template["queries"])
        candidates = by_template[template["prefix"]]
        relevant_authorized = [doc["chunk_id"] for doc in candidates if is_authorized(user, doc)]
        relevant_unauthorized = [doc["chunk_id"] for doc in candidates if not is_authorized(user, doc)]

        # Simulated human labels: prefer the first few authorized chunks, but keep
        # unauthorized examples to test leakage detection.
        relevant = relevant_authorized[:5]
        query_id = f"eval-{idx + 1:03d}"
        queries.append(
            {
                "query_id": query_id,
                "user_id": user["user_id"],
                "query": query,
                "top_k": 10,
                "project_ids": [],
                "document_types": [],
                "relevant_chunk_ids": relevant,
                "unauthorized_relevant_chunk_ids": relevant_unauthorized[:5],
            }
        )
        for rank, chunk_id in enumerate(relevant, start=1):
            judgments.append(
                {
                    "query_id": query_id,
                    "chunk_id": chunk_id,
                    "relevance": max(1, 4 - min(rank, 3)),
                    "authorized": True,
                }
            )
        for chunk_id in relevant_unauthorized[:2]:
            judgments.append(
                {
                    "query_id": query_id,
                    "chunk_id": chunk_id,
                    "relevance": 3,
                    "authorized": False,
                }
            )
    return queries, judgments


def write_jsonl(path: Path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--documents", type=int, default=3000)
    parser.add_argument("--queries", type=int, default=120)
    args = parser.parse_args()

    docs = build_documents(args.documents)
    queries, judgments = build_queries(docs, args.queries)

    write_jsonl(ROOT / "datasets/demo_documents/chunks.jsonl", docs)
    write_jsonl(ROOT / "datasets/evaluation/queries.jsonl", queries)
    write_jsonl(ROOT / "datasets/evaluation/relevance_judgments.jsonl", judgments)

    acl_path = ROOT / "datasets/demo_acl/users.json"
    acl_path.parent.mkdir(parents=True, exist_ok=True)
    acl_path.write_text(json.dumps(USERS, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"generated {len(docs)} chunks, {len(queries)} queries, {len(judgments)} judgments")


if __name__ == "__main__":
    main()

