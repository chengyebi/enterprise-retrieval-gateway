CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2
CPPFLAGS := -Iinclude
LDLIBS ?=
OPENSSL_PREFIX ?= $(shell brew --prefix openssl@3 2>/dev/null)

ifneq ($(wildcard $(OPENSSL_PREFIX)/include/openssl/crypto.h),)
CPPFLAGS += -I$(OPENSSL_PREFIX)/include
LDLIBS += -L$(OPENSSL_PREFIX)/lib
endif

ifeq ($(OS),Windows_NT)
LDLIBS += -lws2_32
else
LDLIBS += -lcrypto
endif

LIB_SRCS := \
	src/api/http_server.cpp \
	src/api/request_mapper.cpp \
	src/auth/access_policy_resolver.cpp \
	src/auth/acl_filter_builder.cpp \
	src/auth/supabase_auth.cpp \
	src/backend/in_memory_opensearch_client.cpp \
	src/backend/opensearch_http_client.cpp \
	src/common/demo_data.cpp \
	src/common/json_util.cpp \
	src/common/parse_util.cpp \
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

.PHONY: all test test-cli test-opensearch clean demo

all: build/ergateway build/test_core build/test_opensearch_backend

build:
	mkdir -p build

build/ergateway: build $(LIB_SRCS) src/main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LIB_SRCS) src/main.cpp $(LDLIBS) -o $@

build/test_core: build $(LIB_SRCS) tests/unit/test_core.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LIB_SRCS) tests/unit/test_core.cpp $(LDLIBS) -o $@

build/test_opensearch_backend: build $(LIB_SRCS) tests/integration/test_opensearch_backend.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LIB_SRCS) tests/integration/test_opensearch_backend.cpp $(LDLIBS) -o $@

test: build/test_core
	./build/test_core

test-cli: build/ergateway
	sh tests/cli/test_cli.sh ./build/ergateway

test-opensearch: build/test_opensearch_backend
	./build/test_opensearch_backend

demo: build/ergateway
	./build/ergateway demo

clean:
	rm -rf build
