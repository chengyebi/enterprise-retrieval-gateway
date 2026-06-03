# Incremental Indexing

`IncrementalIndexer` supports three change types:

- `Upsert`
- `DeleteDocument`
- `UpdateAcl`

## Upsert

The indexer computes `content_hash` from title and content when missing, generates an embedding using the current `embedding_model_version`, and writes the chunk through bulk upsert.

Embedding generation is cached by content hash and model version, so unchanged content does not regenerate vectors.

## Delete

Delete removes all chunks with the same `document_id`. This prevents stale chunks from old document versions staying searchable.

## ACL Update

ACL updates rewrite `department`, `project_id`, and `allowed_groups` for all chunks in the document. The security test verifies that a backend user loses access immediately after the document is moved to finance ACLs.

## OpenSearch Bulk Path

`scripts/ingest_documents.py` sends newline-delimited `_bulk` requests and fails if any item contains an OpenSearch error. Failed item ids should be persisted for retry in a production deployment.

