/* This file is part of:
 * Operon - Large Scale Genetic Programming Framework
 *
 * Licensed under the ISC License <https://opensource.org/licenses/ISC> 
 * Copyright (C) 2019 Bogdan Burlacu 
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE. 
 */

#ifndef DIVERSITY_HPP
#define DIVERSITY_HPP

#include <Eigen/Core>

#include "core/operator.hpp"
#include "core/stats.hpp"
#include <execution>
#include <unordered_set>
#include <mutex>

namespace Operon {
namespace detail {
    using hash_vector_t = std::vector<operon::hash_t, Eigen::aligned_allocator<operon::hash_t>>;

    constexpr int shift_one { _MM_SHUFFLE(0, 3, 2, 1) };
    constexpr int shift_two { _MM_SHUFFLE(1, 0, 3, 2) };
    constexpr int shift_thr { _MM_SHUFFLE(2, 1, 0, 3) };

    static inline bool _mm256_is_zero(__m256i m) noexcept { return _mm256_testz_si256(m, m); }

    static inline bool probe_nullintersect_fast(operon::hash_t const* lhs, operon::hash_t const* rhs) noexcept {
        __m256i a { _mm256_load_si256((__m256i*)lhs) };
        __m256i b { _mm256_load_si256((__m256i*)rhs) };

        __m256i r0 { _mm256_cmpeq_epi64(a, b) };
        if (!_mm256_is_zero(r0)) return false;

        __m256i r1 { _mm256_cmpeq_epi64(a, _mm256_shuffle_epi32(b, shift_one)) };
        if (!_mm256_is_zero(r1)) return false;

        __m256i r2 { _mm256_cmpeq_epi64(a, _mm256_shuffle_epi32(b, shift_two)) };
        if (!_mm256_is_zero(r2)) return false;

        __m256i r3 { _mm256_cmpeq_epi64(a, _mm256_shuffle_epi32(b, shift_thr)) };
        return _mm256_is_zero(r3);
    }

    static inline bool is_set(hash_vector_t const& vec) {
        return std::adjacent_find(vec.begin(), vec.end(), std::equal_to{}) == vec.end();
    }
}

template <typename T, Operon::HashMode H = Operon::HashMode::Strict, typename ExecutionPolicy = std::execution::parallel_unsequenced_policy>
class PopulationDiversityAnalyzer final : PopulationAnalyzerBase<T> {
public:
    double operator()(operon::rand_t&) const
    {
        return diversity;
    }

    void Prepare(gsl::span<const T> pop)
    {
        // warning: this will fail if the population size is too large
        // (not to mention this whole analyzer will be SLOW)
        // for large population sizes it is recommended to use the sampling analyzer
        hashes.clear();
        hashes.resize(pop.size());

        std::vector<gsl::index> indices(pop.size());
        std::iota(indices.begin(), indices.end(), 0);

        ExecutionPolicy ep;

        // hybrid (strict) hashing
        std::for_each(ep, indices.begin(), indices.end(), [&](gsl::index i) {
            auto tree = pop[i].Genotype; // make a copy because the tree will be sorted
            hashes[i] = HashTree(tree, H);
        });

        // calculate intersection
        auto calculateDistance = [&](size_t i, size_t j) {
            double h = Intersect2(hashes[i], hashes[j]); 
            size_t s = hashes[i].size() + hashes[j].size();
            return (s-h) / s;
        };

        std::vector<std::pair<size_t, size_t>> pairs(hashes.size());
        std::vector<double> distances(hashes.size());
        size_t k = 0;
        size_t c = 0;
        size_t n = (hashes.size()-1) * hashes.size() / 2;

        MeanVarianceCalculator calc;
        for (size_t i = 0; i < hashes.size() - 1; ++i) {
            for(size_t j = i+1; j < hashes.size(); ++j) {
                pairs[k++] = { i, j }; ++c;

                if (k == distances.size()) {
                    k = 0;
                }

                if (k == distances.size() || c == n) {
                    k = 0;
                    std::for_each(ep, indices.begin(), indices.end(), [&](gsl::index idx) {
                        auto [a, b] = pairs[idx];
                        distances[idx] = calculateDistance(a, b);
                    });
                    calc.Add(gsl::span(distances.data(), distances.size()));
                }
            }
        }
        diversity = calc.Mean();
    }

    static size_t Intersect1(detail::hash_vector_t const& lhs, detail::hash_vector_t const& rhs) noexcept
    {
        size_t count = 0;
        size_t i = 0;
        size_t j = 0;
        size_t ls = lhs.size();
        size_t rs = rhs.size();

        operon::hash_t const* p = lhs.data();
        operon::hash_t const* q = rhs.data();

        auto lt = (ls / 4) * 4;
        auto rt = (rs / 4) * 4;

        while (i < lt && j < rt) {
            if (detail::probe_nullintersect_fast(&p[i], &q[j])) {
                auto a = p[i + 3];
                auto b = q[j + 3];
                i += (a < b) * 4;
                j += (b < a) * 4;
            } else {
                break;
            }
        }

        auto lm = lhs.back();
        auto rm = rhs.back();

        while (i < ls && j < rs) {
            auto a = lhs[i];
            auto b = rhs[j];

            count += a == b;
            i += a <= b;
            j += a >= b;

            if (a > rm || b > lm) {
                break;
            }
        }
        return count;
    }

    static size_t Intersect2(detail::hash_vector_t const& lhs, detail::hash_vector_t const& rhs) noexcept
    {
        size_t count = 0;
        size_t i = 0;
        size_t j = 0;
        size_t ls = lhs.size();
        size_t rs = rhs.size();

        auto lm = lhs.back();
        auto rm = rhs.back();

        while (i != ls && j != rs) {
            auto a = lhs[i];
            auto b = rhs[j];

            count += a == b;
            i += a <= b;
            j += a >= b;

            if (a > rm || b > lm) {
                break;
            }
        }
        return count;
    }

    static inline detail::hash_vector_t HashTree(Tree& tree, Operon::HashMode mode)
    {
        tree.Sort(mode);
        const auto& nodes = tree.Nodes();
        detail::hash_vector_t hashes(nodes.size());
        std::transform(nodes.begin(), nodes.end(), hashes.begin(), [](const auto& n) { return n.CalculatedHashValue; });
        std::sort(hashes.begin(), hashes.end());
        return hashes;
    }

    private:
        std::atomic<double> diversity;
        std::vector<detail::hash_vector_t> hashes; 
    };
} // namespace Operon

#endif
