#pragma once

#include <cstdint>
#include <string>

#include "retrieval_gateway/auth/supabase_auth.h"
#include "retrieval_gateway/search/retrieval_gateway.h"

namespace erg {

class HttpServer {
public:
    HttpServer(RetrievalGateway& gateway, SupabaseAuthManager supabase_auth = SupabaseAuthManager());
    int serve(uint16_t port);
    std::string handleRequest(const std::string& request_text);

private:
    RetrievalGateway& gateway_;
    SupabaseAuthManager supabase_auth_;
};

}  // namespace erg
