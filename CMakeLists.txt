cmake_minimum_required(VERSION 3.16)
project(EnterpriseRetrievalGateway VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(retrieval_gateway
    src/api/http_server.cpp
    src/api/request_mapper.cpp
    src/auth/access_policy_resolver.cpp
    src/auth/acl_filter_builder.cpp
    src/auth/supabase_auth.cpp
    src/backend/in_memory_opensearch_client.cpp
    src/backend/opensearch_http_client.cpp
    src/common/demo_data.cpp
    src/common/json_util.cpp
    src/common/text.cpp
    src/indexing/embedding_provider.cpp
    src/indexing/incremental_indexer.cpp
    src/metrics/query_metrics_recorder.cpp
    src/search/filter_aware_query_planner.cpp
    src/search/query_plan.cpp
    src/search/retrieval_gateway.cpp
    src/search/rrf_fusion.cpp
    src/search/result_deduplicator.cpp
)

target_include_directories(retrieval_gateway PUBLIC include)
find_package(OpenSSL REQUIRED)
target_link_libraries(retrieval_gateway PUBLIC OpenSSL::Crypto)
if(WIN32)
    target_link_libraries(retrieval_gateway PUBLIC ws2_32)
endif()

add_executable(ergateway src/main.cpp)
target_link_libraries(ergateway PRIVATE retrieval_gateway)

add_executable(test_core tests/unit/test_core.cpp)
target_link_libraries(test_core PRIVATE retrieval_gateway)

add_executable(test_opensearch_backend tests/integration/test_opensearch_backend.cpp)
target_link_libraries(test_opensearch_backend PRIVATE retrieval_gateway)

enable_testing()
add_test(NAME core COMMAND test_core)
