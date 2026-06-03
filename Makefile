CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2
CPPFLAGS := -Iinclude

LIB_SRCS := \
	src/api/http_server.cpp \
	src/api/request_mapper.cpp \
	src/auth/access_policy_resolver.cpp \
	src/auth/acl_filter_builder.cpp \
	src/backend/in_memory_opensearch_client.cpp \
	src/common/demo_data.cpp \
	src/common/json_util.cpp \
	src/common/text.cpp \
	src/indexing/embedding_provider.cpp \
	src/indexing/incremental_indexer.cpp \
	src/metrics/query_metrics_recorder.cpp \
	src/search/filter_aware_query_planner.cpp \
	src/search/query_plan.cpp \
	src/search/retrieval_gateway.cpp \
	src/search/rrf_fusion.cpp \
	src/search/result_deduplicator.cpp

HEADERS := $(shell find include -type f -name '*.h')

.PHONY: all test clean demo

all: build/ergateway build/test_core

build:
	mkdir -p build

build/ergateway: build $(LIB_SRCS) src/main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LIB_SRCS) src/main.cpp -o $@

build/test_core: build $(LIB_SRCS) tests/unit/test_core.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LIB_SRCS) tests/unit/test_core.cpp -o $@

test: build/test_core
	./build/test_core

demo: build/ergateway
	./build/ergateway demo

clean:
	rm -rf build
