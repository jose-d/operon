// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright 2019-2023 Heal Research

#ifndef OPERON_AUTODIFF_FORWARD_HPP
#define OPERON_AUTODIFF_FORWARD_HPP

#include "operon/core/dataset.hpp"
#include "operon/core/tree.hpp"
#include "dual.hpp"

namespace Operon::Autodiff::Forward {
template<typename Interpreter>
class DerivativeCalculator {
    Interpreter const& interpreter_;

public:
    explicit DerivativeCalculator(Interpreter const& interpreter)
        : interpreter_(interpreter) { }

    template<int StorageOrder = Eigen::ColMajor>
    auto operator()(Operon::Tree const& tree, Operon::Dataset const& dataset, Operon::Span<Operon::Scalar const> coeff, Operon::Range const range) {
        Eigen::Array<Operon::Scalar, -1, -1, StorageOrder> jacobian(static_cast<int>(range.Size()), std::ssize(coeff));
        this->operator()<StorageOrder>(tree, dataset, coeff, range, jacobian.data());
        return jacobian;
    }

    template<int StorageOrder = Eigen::ColMajor>
    auto operator()(Operon::Tree const& tree, Operon::Dataset const& dataset, Operon::Span<Operon::Scalar const> coeff, Operon::Range const range, Operon::Scalar* jacobian)
    {
        auto const& nodes = tree.Nodes();
        std::vector<Dual> inputs(coeff.size());
        std::vector<Dual> outputs(range.Size());
        Eigen::Map<Eigen::Array<Operon::Scalar, -1, -1, StorageOrder>> jac(jacobian, std::ssize(outputs), std::ssize(inputs));
        jac.setConstant(0);

        for (auto i{0UL}; i < inputs.size(); ++i) {
            inputs[i].a = coeff[i];
            inputs[i].v.setZero();
        }

        auto constexpr d{ Dual::DIMENSION };
        for (auto s = 0U; s < inputs.size(); s += d) {
            auto r = std::min(static_cast<uint32_t>(inputs.size()), s + d); // remaining parameters

            for (auto i = s; i < r; ++i) {
                inputs[i].v[i - s] = 1.0;
            }

            interpreter_.template operator()<Dual>(tree, dataset, range, outputs, inputs);

            // fill in the jacobian trying to exploit its layout for efficiency
            if constexpr (StorageOrder == Eigen::ColMajor) {
                for (auto i = s; i < r; ++i) {
                    std::transform(outputs.cbegin(), outputs.cend(), jac.col(i).data(), [&](auto const& jet) { return jet.v[i - s]; });
                }
            } else {
                for (auto i = 0; i < outputs.size(); ++i) {
                    std::copy_n(outputs[i].v.data(), r - s, jac.row(i).data() + s);
                }
            }
        }
    }

    [[nodiscard]] auto GetInterpreter() const -> Interpreter const& { return interpreter_; }
};
} // namespace Operon::Autodiff::Forward

#endif
