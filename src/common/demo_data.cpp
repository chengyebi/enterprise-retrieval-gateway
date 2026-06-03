#include "retrieval_gateway/common/demo_data.h"

namespace erg {

namespace {

DocumentChunk chunk(const std::string& document_id,
                    const std::string& chunk_id,
                    const std::string& title,
                    const std::string& content,
                    const std::string& department,
                    const std::string& project_id,
                    std::vector<std::string> groups,
                    const std::string& document_type,
                    int version = 1) {
    DocumentChunk value;
    value.tenant_id = "tenant-acme";
    value.document_id = document_id;
    value.chunk_id = chunk_id;
    value.title = title;
    value.content = content;
    value.department = department;
    value.project_id = project_id;
    value.allowed_groups = std::move(groups);
    value.document_type = document_type;
    value.document_version = version;
    value.updated_at = "2026-06-03T10:00:00Z";
    return value;
}

}  // namespace

std::vector<DocumentChunk> buildDemoChunks() {
    return {
        chunk("incident-2026-041", "incident-2026-041#1", "Payment service E1027 incident review",
              "Payment service raised error code E1027 after checkout API v3 timed out. Downstream transaction gateway did not respond within 800 ms.",
              "engineering", "payment", {"backend", "sre"}, "incident_report", 3),
        chunk("incident-2026-041", "incident-2026-041#2", "Payment service E1027 remediation",
              "For E1027, first check payment_timeout metrics, then inspect transaction-gateway saturation, retry queue depth, and circuit breaker state.",
              "engineering", "payment", {"backend", "sre"}, "incident_report", 3),
        chunk("runbook-payment", "runbook-payment#1", "Payment timeout runbook",
              "When a downstream payment interface has no response for a long time, confirm API v3 health, dependency latency, and rollback flags.",
              "engineering", "payment", {"backend", "sre"}, "runbook", 7),
        chunk("runbook-payment", "runbook-payment#2", "Payment API rollback",
              "payment_timeout alerts require checking idempotency keys and disabling experimental routing before restarting the worker.",
              "engineering", "payment", {"backend", "sre"}, "runbook", 7),
        chunk("api-payment-v3", "api-payment-v3#1", "Payment API v3 contract",
              "The checkout API v3 accepts order_id, merchant_id, amount, and idempotency_key. Error E1027 means dependency timeout.",
              "engineering", "payment", {"backend"}, "api_doc", 5),
        chunk("incident-2026-055", "incident-2026-055#1", "Search index lag incident",
              "The retrieval index was stale for 17 minutes because bulk upsert workers exhausted retry tokens after OpenSearch returned 429.",
              "engineering", "search", {"backend", "sre"}, "incident_report", 1),
        chunk("runbook-search", "runbook-search#1", "OpenSearch bulk recovery",
              "If OpenSearch bulk requests fail partially, collect failed document ids, preserve retry order, and keep delete operations idempotent.",
              "engineering", "search", {"backend", "sre"}, "runbook", 2),
        chunk("design-acl-cache", "design-acl-cache#1", "ACL cache isolation design",
              "Query cache keys must include tenant id, department, project ids, group ids, and admin state. Admin results must never be reused by normal users.",
              "engineering", "search", {"backend", "security"}, "design_doc", 1),
        chunk("security-acl", "security-acl#1", "Document ACL enforcement",
              "The gateway builds ACL filters on the server side. Requests cannot set unrestricted=true and resolver failures must default to deny.",
              "engineering", "security", {"security", "backend"}, "policy", 2),
        chunk("finance-expense", "finance-expense#1", "Expense reimbursement policy",
              "Employees can submit travel reimbursement within 30 days. Finance approvers require invoice id, cost center, and manager approval.",
              "finance", "finance-core", {"finance"}, "policy", 4),
        chunk("finance-report-q2", "finance-report-q2#1", "Q2 confidential financial report",
              "Quarterly revenue, gross margin, enterprise contract exposure, and cash runway are confidential to the finance group.",
              "finance", "finance-core", {"finance", "executive"}, "financial_report", 8),
        chunk("contract-acme-bank", "contract-acme-bank#1", "ACME Bank enterprise contract",
              "Customer pricing, SLA credits, payment terms, and renewal options are restricted to sales and finance.",
              "sales", "enterprise-sales", {"sales", "finance"}, "contract", 6),
        chunk("hr-leave", "hr-leave#1", "Leave policy",
              "Annual leave requires manager approval. Sick leave over three days requires a certificate and HR confirmation.",
              "hr", "people", {"employee", "hr"}, "policy", 2),
        chunk("hr-promotion", "hr-promotion#1", "Promotion review guide",
              "Promotion packets include impact summary, peer feedback, manager assessment, and calibration notes.",
              "hr", "people", {"hr", "manager"}, "policy", 3),
        chunk("sales-pricing", "sales-pricing#1", "Product quotation guide",
              "Enterprise quotation uses seat count, support tier, renewal term, and approved discount boundaries.",
              "sales", "enterprise-sales", {"sales"}, "pricing", 5),
        chunk("customer-ticket-778", "customer-ticket-778#1", "Customer ticket 778",
              "A customer reported checkout timeout after API v3 migration. The workaround was to pin traffic away from transaction-gateway shard 4.",
              "engineering", "payment", {"backend", "support"}, "customer_ticket", 1),
        chunk("oncall-payment", "oncall-payment#1", "Payment on-call handbook",
              "For E1027 during on-call, page payment SRE, inspect dependency dashboards, and attach query trace evidence to the incident channel.",
              "engineering", "payment", {"sre"}, "runbook", 4),
        chunk("design-vector-filter", "design-vector-filter#1", "Vector retrieval with strict filters",
              "ANN search can return too few authorized hits after ACL filtering. The planner expands candidate limits or switches to exact search for small candidate sets.",
              "engineering", "search", {"backend", "sre"}, "design_doc", 1),
        chunk("bug-bulk-delete", "bug-bulk-delete#1", "Bulk delete bug note",
              "A deleted document stayed searchable because old chunks used a stale document_version. The fix deletes by document_id before reindexing.",
              "engineering", "search", {"backend"}, "bug_note", 1),
        chunk("runbook-observability", "runbook-observability#1", "Slow query trace playbook",
              "Slow retrieval traces should identify ACL resolve latency, backend latency, fusion latency, fallback expansion, and returned hit count.",
              "engineering", "search", {"backend", "sre"}, "runbook", 1),
    };
}

std::vector<AccessContext> buildDemoUsers() {
    return {
        {"backend-user-01", "tenant-acme", "engineering", {"backend", "employee"}, {"payment", "search", "security"}, false},
        {"sre-user-01", "tenant-acme", "engineering", {"sre", "employee"}, {"payment", "search"}, false},
        {"finance-user-01", "tenant-acme", "finance", {"finance", "employee"}, {"finance-core", "enterprise-sales"}, false},
        {"hr-user-01", "tenant-acme", "hr", {"hr", "employee"}, {"people"}, false},
        {"sales-user-01", "tenant-acme", "sales", {"sales", "employee"}, {"enterprise-sales"}, false},
        {"cross-project-user", "tenant-acme", "engineering", {"backend", "support", "employee"}, {"payment", "search"}, false},
        {"admin-user", "tenant-acme", "engineering", {"admin", "security"}, {"payment", "search", "security", "finance-core", "enterprise-sales", "people"}, true},
        {"no-access-user", "tenant-acme", "engineering", {"intern"}, {"sandbox"}, false},
    };
}

}  // namespace erg

