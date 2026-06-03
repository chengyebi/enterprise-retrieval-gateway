#include "retrieval_gateway/indexing/embedding_provider.h"

#include <cmath>
#include <functional>
#include <utility>

#include "retrieval_gateway/common/text.h"

namespace erg {

EmbeddingProvider::EmbeddingProvider(std::size_t dimensions, std::string model_version)
    : dimensions_(dimensions), model_version_(std::move(model_version)) {}

std::vector<double> EmbeddingProvider::embed(const std::string& text) {
    const std::string cache_key = model_version_ + ":" + stableHashHex(text);
    const auto it = cache_.find(cache_key);
    if (it != cache_.end()) {
        ++cache_hits_;
        return it->second;
    }

    std::vector<double> values(dimensions_, 0.0);
    const auto tokens = tokenize(text);
    const auto grams = byteNgrams(text, 4);
    std::hash<std::string> hasher;

    for (const auto& token : tokens) {
        const std::size_t index = hasher("tok:" + token) % dimensions_;
        values[index] += 2.0;
    }
    for (const auto& gram : grams) {
        const std::size_t index = hasher("gram:" + gram) % dimensions_;
        values[index] += 0.25;
    }

    double norm = 0.0;
    for (double value : values) {
        norm += value * value;
    }
    norm = std::sqrt(norm);
    if (norm > 0.0) {
        for (double& value : values) {
            value /= norm;
        }
    }

    ++cache_misses_;
    cache_[cache_key] = values;
    return values;
}

const std::string& EmbeddingProvider::modelVersion() const {
    return model_version_;
}

std::size_t EmbeddingProvider::dimensions() const {
    return dimensions_;
}

std::size_t EmbeddingProvider::cacheHits() const {
    return cache_hits_;
}

std::size_t EmbeddingProvider::cacheMisses() const {
    return cache_misses_;
}

double cosineSimilarity(const std::vector<double>& left, const std::vector<double>& right) {
    if (left.empty() || left.size() != right.size()) {
        return 0.0;
    }
    double dot = 0.0;
    double left_norm = 0.0;
    double right_norm = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        dot += left[i] * right[i];
        left_norm += left[i] * left[i];
        right_norm += right[i] * right[i];
    }
    if (left_norm == 0.0 || right_norm == 0.0) {
        return 0.0;
    }
    return dot / (std::sqrt(left_norm) * std::sqrt(right_norm));
}

}  // namespace erg
