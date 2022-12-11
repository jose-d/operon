// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2019-2023 Heal Research

#ifndef OPERON_INTERPRETER_HPP
#define OPERON_INTERPRETER_HPP

#include <algorithm>
#include <optional>
#include <utility>

#include "operon/core/dataset.hpp"
#include "operon/core/dual.hpp"
#include "operon/core/tree.hpp"
#include "operon/core/types.hpp"
#include "dispatch_table.hpp"

namespace Operon {

template<typename... Ts>
struct GenericInterpreter {
    using DTable = DispatchTable<Ts...>;

    explicit GenericInterpreter(DTable ft)
        : ftable_(std::move(ft))
    {
    }

    GenericInterpreter() : GenericInterpreter(DTable{}) { }

    // evaluate a tree and return a vector of values
    template <typename T>
    auto Evaluate(Tree const& tree, Dataset const& dataset, Range const range, T const* const parameters = nullptr) const noexcept -> Operon::Vector<T>
    {
        Operon::Vector<T> result(range.Size());
        Evaluate<T>(tree, dataset, range, Operon::Span<T>(result), parameters);
        return result;
    }

    template <typename T>
    auto Evaluate(Tree const& tree, Dataset const& dataset, Range const range, size_t const batchSize, T const* const parameters = nullptr) const noexcept -> Operon::Vector<T>
    {
        Operon::Vector<T> result(range.Size());
        Operon::Span<T> view(result);

        size_t n = range.Size() / batchSize;
        size_t m = range.Size() % batchSize;
        std::vector<size_t> indices(n + (m != 0));
        std::iota(indices.begin(), indices.end(), 0UL);
        std::for_each(indices.begin(), indices.end(), [&](auto idx) {
            auto start = range.Start() + idx * batchSize;
            auto end = std::min(start + batchSize, range.End());
            auto subview = view.subspan(idx * batchSize, end - start);
            Evaluate(tree, dataset, Range { start, end }, subview, parameters);
        });
        return result;
    }

    template <typename T>
    void Evaluate(Tree const& tree, Dataset const& dataset, Range const range, Operon::Span<T> result, T const* const parameters = nullptr) const noexcept
    {
        using Callable = typename DTable::template Callable<T>;
        const auto& nodes = tree.Nodes();
        EXPECT(!nodes.empty());

        constexpr int S = static_cast<Eigen::Index>(detail::BatchSize<T>::Value);
        Operon::Vector<detail::Array<T>> m(nodes.size());
        Eigen::Map<Eigen::Array<T, -1, 1>> res(result.data(), result.size(), 1);

        using NodeMeta = std::tuple<T, Eigen::Map<Eigen::Array<Operon::Scalar, -1, 1> const>, std::optional<Callable const>>;
        Operon::Vector<NodeMeta> meta; meta.reserve(nodes.size());

        size_t idx = 0;
        int numRows = static_cast<int>(range.Size());
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto const& n = nodes[i];

            auto const* ptr = n.IsVariable() ? dataset.GetValues(n.HashValue).subspan(range.Start(), numRows).data() : nullptr;
            auto const param = (parameters && n.Optimize) ? parameters[idx++] : T{n.Value};
            meta.push_back({
                param,
                std::tuple_element_t<1, NodeMeta>(ptr, numRows),
                ftable_.template TryGet<T>(n.HashValue)
            });
            if (n.IsConstant()) { m[i].setConstant(param); }
        }

        for (int row = 0; row < numRows; row += S) {
            auto remainingRows = std::min(S, numRows - row);
            Operon::Range rg(range.Start() + row, range.Start() + row + remainingRows);

            for (size_t i = 0; i < nodes.size(); ++i) {
                auto const& [ param, values, func ] = meta[i];
                if (nodes[i].IsVariable()) {
                    m[i].segment(0, remainingRows) = param * values.segment(row, remainingRows).template cast<T>();
                } else if (func) {
                    std::invoke(*func, m, nodes, i, rg);
                }
            }
            // the final result is found in the last section of the buffer corresponding to the root node
            res.segment(row, remainingRows) = m.back().segment(0, remainingRows);
        }
    }

    auto GetDispatchTable() -> DTable& { return ftable_; }
    [[nodiscard]] auto GetDispatchTable() const -> DTable const& { return ftable_; }

private:
    DTable ftable_;
};

using Interpreter = GenericInterpreter<Operon::Scalar, Operon::Dual>;

// convenience method to interpret many trees in parallel (mostly useful from the python wrapper)
auto OPERON_EXPORT EvaluateTrees(std::vector<Operon::Tree> const& trees, Operon::Dataset const& dataset, Operon::Range range, size_t nthread = 0) -> std::vector<std::vector<Operon::Scalar>> ;
auto OPERON_EXPORT EvaluateTrees(std::vector<Operon::Tree> const& trees, Operon::Dataset const& dataset, Operon::Range range, std::span<Operon::Scalar> result, size_t nthread) -> void;

} // namespace Operon


#endif
