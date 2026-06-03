#pragma once

#include <cstdint>
#include <string>

#include "retrieval_gateway/search/retrieval_gateway.h"

namespace erg {

class HttpServer {
public:
    explicit HttpServer(RetrievalGateway& gateway);
    int serve(uint16_t port);

private:
    std::string handleRequest(const std::string& request_text);
    RetrievalGateway& gateway_;
};

}  // namespace erg

