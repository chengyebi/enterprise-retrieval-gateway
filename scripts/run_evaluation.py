#!/usr/bin/env python3
"""Offline retrieval evaluation for the simulated enterprise corpus."""

import argparse
import hashlib
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_jsonl(path):
    rows = []
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


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


def embed(text, dimensions=64):
    vector = [0.0] * dimensions
    for token in tokenize(text):
        vector[stable_index("tok:" + token, dimensions)] += 2.0
    lower = text.lower()
    for index in range(max(0, len(lower) - 3)):
        gram = lower[index : index + 4]
        vector[stable_index("gram:" + gram, dimensions)] += 0.25
    norm = math.sqrt(sum(value * value for value in vector))
    if norm:
        vector = [value / norm for value in vector]
    return vector


def cosine(left, right):
    if not left or len(left) != len(right):
        return 0.0
    return sum(a * b for a, b in zip(left, right))


def keyword_score(query, doc):
    tokens = tokenize(query)
    haystack = (doc["title"] + " " + doc["content"]).lower()
    score = 0.0
    for token in tokens:
        count = haystack.count(token)
        if count:
            score += 2.0 + math.log1p(count)
            if token in doc["title"].lower():
                score += 1.5
    return score / (1.0 + len(tokenize(doc["content"])) / 300.0)


def is_authorized(user, doc):
    if user["tenant_id"] != doc["tenant_id"]:
        return False
    if user.get("is_admin"):
        return True
    return (
        user["department"] == doc["department"]
        and doc["project_id"] in user["project_ids"]
        and bool(set(user["groups"]).intersection(doc["allowed_groups"]))
    )


def matches_filters(query, doc):
    if query.get("project_ids") and doc["project_id"] not in query["project_ids"]:
        return False
    if query.get("document_types") and doc["document_type"] not in query["document_types"]:
        return False
    return True


def rrf(lexical, semantic, top_k, rank_constant=60.0):
    by_id = {}
    for rank, hit in enumerate(lexical, start=1):
        item = dict(hit)
        item["fusion_score"] = item.get("fusion_score", 0.0) + 1.0 / (rank_constant + rank)
        item["source"] = "keyword"
        by_id[item["chunk_id"]] = item
    for rank, hit in enumerate(semantic, start=1):
        if hit["chunk_id"] not in by_id:
            item = dict(hit)
            item["fusion_score"] = 1.0 / (rank_constant + rank)
            item["source"] = "vector"
            by_id[item["chunk_id"]] = item
        else:
            by_id[hit["chunk_id"]]["semantic_score"] = hit["semantic_score"]
            by_id[hit["chunk_id"]]["fusion_score"] += 1.0 / (rank_constant + rank)
            by_id[hit["chunk_id"]]["source"] = "hybrid"
    return sorted(by_id.values(), key=lambda row: (-row["fusion_score"], row["chunk_id"]))[:top_k]


def dedup(hits, top_k):
    seen = set()
    out = []
    for hit in hits:
        if hit["document_id"] in seen:
            continue
        seen.add(hit["document_id"])
        out.append(hit)
        if len(out) >= top_k:
            break
    return out


def retrieve(query, docs, users, strategy):
    user = users[query["user_id"]]
    apply_acl = strategy in {"bm25_only", "vector_only", "hybrid_acl", "planner_acl"}
    keyword = strategy in {"bm25_only", "hybrid", "hybrid_acl", "planner_acl"}
    vector = strategy in {"vector_only", "hybrid", "hybrid_acl", "planner_acl"}
    top_k = query.get("top_k", 10)

    candidates = [doc for doc in docs if matches_filters(query, doc)]
    if apply_acl:
        candidates = [doc for doc in candidates if is_authorized(user, doc)]

    candidate_limit = 40
    max_candidate_limit = 320
    exact_threshold = 5
    fallback = False
    if strategy == "planner_acl":
        candidate_limit = max(5, top_k)
        if len(candidates) <= exact_threshold:
            max_candidate_limit = candidate_limit

    query_vector = embed(query["query"])
    final_hits = []
    while True:
        lexical = []
        semantic = []
        if keyword:
            for doc in candidates:
                score = keyword_score(query["query"], doc)
                if score > 0:
                    lexical.append({"chunk_id": doc["chunk_id"], "document_id": doc["document_id"], "lexical_score": score, "semantic_score": 0.0})
            lexical.sort(key=lambda row: (-row["lexical_score"], row["chunk_id"]))
            lexical = lexical[:candidate_limit]
        if vector:
            for doc in candidates:
                score = cosine(query_vector, doc["_embedding"])
                if score > 0.02:
                    semantic.append({"chunk_id": doc["chunk_id"], "document_id": doc["document_id"], "lexical_score": 0.0, "semantic_score": score})
            semantic.sort(key=lambda row: (-row["semantic_score"], row["chunk_id"]))
            semantic = semantic[:candidate_limit]

        if keyword and vector:
            final_hits = dedup(rrf(lexical, semantic, candidate_limit), top_k)
        else:
            final_hits = dedup((lexical if keyword else semantic), top_k)

        if strategy != "planner_acl" or len(final_hits) >= top_k or candidate_limit >= max_candidate_limit:
            break
        candidate_limit = min(max_candidate_limit, candidate_limit * 2)
        fallback = True

    unauthorized = 0
    by_chunk = {doc["chunk_id"]: doc for doc in docs}
    for hit in final_hits:
        if not is_authorized(user, by_chunk[hit["chunk_id"]]):
            unauthorized += 1
    return final_hits, unauthorized, fallback


def recall_at(results, relevant, k):
    if not relevant:
        return 1.0
    got = {row["chunk_id"] for row in results[:k]}
    return len(got.intersection(relevant)) / len(relevant)


def mrr(results, relevant):
    if not relevant:
        return 1.0
    for rank, row in enumerate(results, start=1):
        if row["chunk_id"] in relevant:
            return 1.0 / rank
    return 0.0


def ndcg_at(results, judgments, k):
    gains = [judgments.get(row["chunk_id"], 0) for row in results[:k]]
    dcg = sum((2**gain - 1) / math.log2(index + 2) for index, gain in enumerate(gains))
    ideal = sorted(judgments.values(), reverse=True)[:k]
    idcg = sum((2**gain - 1) / math.log2(index + 2) for index, gain in enumerate(ideal))
    return dcg / idcg if idcg else 1.0


def markdown_table(rows):
    headers = ["strategy", "recall@5", "recall@10", "mrr", "ndcg@10", "empty_rate", "unauthorized", "fallback_rate"]
    lines = ["|" + "|".join(headers) + "|", "|" + "|".join(["---"] * len(headers)) + "|"]
    for row in rows:
        lines.append(
            "|{strategy}|{recall5:.3f}|{recall10:.3f}|{mrr:.3f}|{ndcg10:.3f}|{empty_rate:.3f}|{unauthorized}|{fallback_rate:.3f}|".format(**row)
        )
    return "\n".join(lines)


def evaluate():
    docs = read_jsonl(ROOT / "datasets/demo_documents/chunks.jsonl")
    users = {row["user_id"]: row for row in json.loads((ROOT / "datasets/demo_acl/users.json").read_text(encoding="utf-8"))}
    queries = read_jsonl(ROOT / "datasets/evaluation/queries.jsonl")
    judgments_rows = read_jsonl(ROOT / "datasets/evaluation/relevance_judgments.jsonl")
    judgments = defaultdict(dict)
    relevant = defaultdict(set)
    for row in judgments_rows:
        if row.get("authorized"):
            judgments[row["query_id"]][row["chunk_id"]] = row["relevance"]
            relevant[row["query_id"]].add(row["chunk_id"])
    for doc in docs:
        doc["_embedding"] = embed(doc["title"] + "\n" + doc["content"])

    strategies = ["bm25_only", "vector_only", "hybrid", "hybrid_acl", "planner_acl"]
    rows = []
    for strategy in strategies:
        recall5 = []
        recall10 = []
        mrr_values = []
        ndcg10 = []
        empty = 0
        unauthorized_total = 0
        fallback_count = 0
        for query in queries:
            results, unauthorized, fallback = retrieve(query, docs, users, strategy)
            rel = relevant[query["query_id"]]
            recall5.append(recall_at(results, rel, 5))
            recall10.append(recall_at(results, rel, 10))
            mrr_values.append(mrr(results, rel))
            ndcg10.append(ndcg_at(results, judgments[query["query_id"]], 10))
            empty += 1 if not results else 0
            unauthorized_total += unauthorized
            fallback_count += 1 if fallback else 0
        rows.append(
            {
                "strategy": strategy,
                "recall5": statistics.mean(recall5),
                "recall10": statistics.mean(recall10),
                "mrr": statistics.mean(mrr_values),
                "ndcg10": statistics.mean(ndcg10),
                "empty_rate": empty / len(queries),
                "unauthorized": unauthorized_total,
                "fallback_rate": fallback_count / len(queries),
            }
        )
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write-doc", default="")
    args = parser.parse_args()
    rows = evaluate()
    table = markdown_table(rows)
    print(table)
    if args.write_doc:
        content = "# Offline Evaluation\n\n" + table + "\n\nSimulated labels are generated by `scripts/build_demo_dataset.py` with deterministic query templates and authorization checks.\n"
        Path(args.write_doc).write_text(content, encoding="utf-8")


if __name__ == "__main__":
    main()

