#pragma once

#include <chrono>
#include <cstdint>

namespace erg {

class Stopwatch {
public:
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}

    int64_t elapsedMs() const {
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

}  // namespace erg

