#pragma once

#include <map>
#include <string>
#include <vector>

namespace erg {

class EmbeddingProvider {
public:
    explicit EmbeddingProvider(std::size_t dimensions = 64, std::string model_version = "local-hash-v1");

    std::vector<double> embed(const std::string& text);
    const std::string& modelVersion() const;
    std::size_t dimensions() const;
    std::size_t cacheHits() const;
    std::size_t cacheMisses() const;

private:
    std::size_t dimensions_;
    std::string model_version_;
    std::map<std::string, std::vector<double>> cache_;
    std::size_t cache_hits_{0};
    std::size_t cache_misses_{0};
};

double cosineSimilarity(const std::vector<double>& left, const std::vector<double>& right);

}  // namespace erg

