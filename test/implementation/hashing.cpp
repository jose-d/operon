// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2019-2021 Heal Research

#include <doctest/doctest.h>
#include <unordered_set>

#include "core/tree.hpp"
#include "core/distance.hpp"
#include "core/operator.hpp"
#include "core/format.hpp"
#include "hash/hash.hpp"
#include "operators/creator.hpp"

#include "vstat.hpp"

namespace Operon {
namespace Test {

void calculateDistance(std::vector<Tree>& trees, Operon::HashFunction f, const std::string& name) {
    std::vector<Operon::Vector<Operon::Hash>> treeHashes;
    treeHashes.reserve(trees.size());

    for (auto& t : trees) {
        Operon::Vector<Operon::Hash> hh(t.Length());
        t.Hash(f, Operon::HashMode::Strict);
        std::transform(t.Nodes().begin(), t.Nodes().end(), hh.begin(), [](auto& n) { return n.CalculatedHashValue; });
        std::sort(hh.begin(), hh.end());
        treeHashes.push_back(hh);
    }

    univariate_accumulator<double> acc(0.0);

    for (size_t i = 0; i < treeHashes.size() - 1; ++i) {
        for (size_t j = i + 1; j < treeHashes.size(); ++j) {
            auto d = Operon::Distance::Jaccard(treeHashes[i], treeHashes[j]);
            acc(d);
        }
    }
    univariate_statistics stats(acc);
    fmt::print("Average distance ({}): {}\n", name, stats.mean);
}

void calculateDistanceWithSort(std::vector<Tree>& trees, const std::string& name) {
    std::vector<Operon::Vector<Operon::Hash>> treeHashes;
    treeHashes.reserve(trees.size());

    for (auto& t : trees) {
        Operon::Vector<Operon::Hash> hh(t.Length());
        t.Sort();
        std::transform(t.Nodes().begin(), t.Nodes().end(), hh.begin(), [](auto& n) { return n.CalculatedHashValue; });
        std::sort(hh.begin(), hh.end());
        treeHashes.push_back(hh);
    }

    univariate_accumulator<double> acc(0.0);

    for (size_t i = 0; i < treeHashes.size() - 1; ++i) {
        for (size_t j = i + 1; j < treeHashes.size(); ++j) {
            auto d = Operon::Distance::Jaccard(treeHashes[i], treeHashes[j]);
            acc(d);
        }
    }

    univariate_statistics stats(acc);
    fmt::print("Average distance (sort) ({}): {}\n", name, stats.mean);
}

TEST_CASE("Hash-based distance") {
    size_t n = 5000;

    size_t maxLength = 100;
    size_t minDepth  = 1;
    size_t maxDepth = 1000;

    Operon::RandomGenerator rd(1234);
    auto ds = Dataset("../data/Poly-10.csv", true);

    auto target = "Y";
    auto variables = ds.Variables();
    std::vector<Variable> inputs;
    std::copy_if(variables.begin(), variables.end(), std::back_inserter(inputs), [&](const auto& v) { return v.Name != target; });

    std::uniform_int_distribution<size_t> sizeDistribution(1, maxLength);

    PrimitiveSet grammar;
    grammar.SetConfig(PrimitiveSet::Arithmetic);

    std::vector<size_t> indices(n);
    std::vector<Operon::Hash> seeds(n);
    std::vector<Tree> trees(n);
    std::vector<Operon::Vector<Operon::Hash>> treeHashes(n);

    auto btc = BalancedTreeCreator { grammar, inputs };

    std::iota(indices.begin(), indices.end(), 0);
    std::generate(seeds.begin(), seeds.end(), [&](){ return rd(); });
    std::transform(indices.begin(), indices.end(), trees.begin(), [&](auto i) {
        Operon::RandomGenerator rand(seeds[i]);
        auto tree = btc(rand, sizeDistribution(rand), minDepth, maxDepth);
        return tree;
    });

    std::vector<std::pair<Operon::HashFunction, std::string>> hashFunctions {
        { Operon::HashFunction::XXHash,    "XXHash" },
        { Operon::HashFunction::MetroHash, "MetroHash" },
        { Operon::HashFunction::FNV1Hash,  "FNV1Hash" },
    };

    for (const auto& [f, name] : hashFunctions) {
        calculateDistance(trees, f, name);
    }
}

TEST_CASE("Hash collisions") {
    size_t n = 100000;
    size_t maxLength = 200;
    size_t minDepth = 0;
    size_t maxDepth = 100;

    Operon::RandomGenerator rd(1234);
    auto ds = Dataset("../data/Poly-10.csv", true);

    auto target = "Y";
    auto variables = ds.Variables();
    std::vector<Variable> inputs;
    std::copy_if(variables.begin(), variables.end(), std::back_inserter(inputs), [&](const auto& v) { return v.Name != target; });

    std::uniform_int_distribution<size_t> sizeDistribution(1, maxLength);

    PrimitiveSet grammar;
    grammar.SetConfig(PrimitiveSet::Arithmetic);

    std::vector<size_t> indices(n);
    std::vector<Operon::Hash> seeds(n);
    std::vector<Tree> trees(n);

    auto btc = BalancedTreeCreator { grammar, inputs };

    std::iota(indices.begin(), indices.end(), 0);
    std::generate(seeds.begin(), seeds.end(), [&](){ return rd(); });
    std::transform(indices.begin(), indices.end(), trees.begin(), [&](auto i) {
        Operon::RandomGenerator rand(seeds[i]);
        auto tree = btc(rand, sizeDistribution(rand), minDepth, maxDepth);
        tree.Hash<Operon::HashFunction::FNV1Hash>(Operon::HashMode::Strict);
        return tree;
    });

    std::unordered_set<uint64_t> set64;
    std::unordered_set<uint32_t> set32;
    auto totalNodes = std::transform_reduce(trees.begin(), trees.end(), size_t { 0 }, std::plus<size_t> {}, [](auto& tree) { return tree.Length(); });

    for(auto& tree : trees) {
        for(auto& node : tree.Nodes()) {
            auto h = node.CalculatedHashValue;
            set64.insert(h);
            set32.insert(static_cast<uint32_t>(h & 0xFFFFFFFFLL));
        }
        tree.Nodes().clear();
    }
    double s64 = (double)set64.size();
    double s32 = (double)set32.size();
    fmt::print("total nodes: {}, {:.3f}% unique, unique 64-bit hashes: {}, unique 32-bit hashes: {}, collision rate: {:.3f}%\n", totalNodes, s64/(double)totalNodes * 100, s64, s32, (1 - s32/s64) * 100);
}
} // namespace Test
} // namespace Operon

