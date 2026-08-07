#pragma once

// Shared Evaluate()-based numerical Jacobian check for projection factors.
//
// Legacy check() methods re-implemented residual math by hand and often omitted
// sqrt_weight on the finite-difference side. Always obtain both analytic and
// numeric residuals from Factor::Evaluate so √w is applied identically.

#include <pht_slam_common/utility.hpp>

#include <Eigen/Dense>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

namespace projection_factor_check
{

struct Block
{
    int ambient = 0;
    int local = 0;
    bool is_pose = false;
};

inline Block pose() { return Block{7, 6, true}; }
inline Block scalar() { return Block{1, 1, false}; }

inline void applyDelta(const Block &block, const double *in, const double *delta, double *out)
{
    if (!block.is_pose)
    {
        for (int i = 0; i < block.ambient; i++)
            out[i] = in[i] + delta[i];
        return;
    }

    Eigen::Map<const Eigen::Vector3d> p(in);
    const Eigen::Quaterniond q(in[6], in[3], in[4], in[5]);
    Eigen::Map<const Eigen::Vector3d> dp(delta);
    const Eigen::Quaterniond dq = Utility::deltaQ(Eigen::Map<const Eigen::Vector3d>(delta + 3));

    const Eigen::Vector3d p_out = p + dp;
    const Eigen::Quaterniond q_out = (q * dq).normalized();
    out[0] = p_out.x();
    out[1] = p_out.y();
    out[2] = p_out.z();
    out[3] = q_out.x();
    out[4] = q_out.y();
    out[5] = q_out.z();
    out[6] = q_out.w();
}

// Print analytic residual/Jacobians from Evaluate, then central-difference
// Jacobians also obtained exclusively via Evaluate (no duplicated residual math).
template <typename Factor>
inline void checkViaEvaluate(const Factor &factor,
                             const std::vector<Block> &blocks,
                             double **parameters,
                             double eps = 1e-6)
{
    constexpr int kResidual = 2;

    std::vector<std::vector<double>> ambient;
    std::vector<double *> params;
    ambient.reserve(blocks.size());
    for (size_t b = 0; b < blocks.size(); b++)
    {
        ambient.emplace_back(parameters[b], parameters[b] + blocks[b].ambient);
        params.push_back(ambient.back().data());
    }

    Eigen::Vector2d residual = Eigen::Vector2d::Zero();
    std::vector<std::vector<double>> row_major;
    std::vector<double *> jac_ptrs;
    for (const auto &block : blocks)
    {
        row_major.emplace_back(static_cast<size_t>(kResidual * block.ambient), 0.0);
        jac_ptrs.push_back(row_major.back().data());
    }
    factor.Evaluate(params.data(), residual.data(), jac_ptrs.data());

    puts("check begins (Evaluate-based)");
    puts("my");
    std::cout << residual.transpose() << std::endl << std::endl;
    for (size_t b = 0; b < blocks.size(); b++)
    {
        Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> J(
            row_major[b].data(), kResidual, blocks[b].ambient);
        std::cout << J << std::endl << std::endl;
    }

    puts("num (from Evaluate after +/- eps)");
    for (size_t b = 0; b < blocks.size(); b++)
    {
        Eigen::MatrixXd num(kResidual, blocks[b].local);
        for (int c = 0; c < blocks[b].local; c++)
        {
            std::vector<double> delta(static_cast<size_t>(blocks[b].local), 0.0);
            std::vector<double> plus_vals(static_cast<size_t>(blocks[b].ambient), 0.0);
            std::vector<double> minus_vals(static_cast<size_t>(blocks[b].ambient), 0.0);

            delta[static_cast<size_t>(c)] = eps;
            applyDelta(blocks[b], params[b], delta.data(), plus_vals.data());
            std::vector<double *> plus = params;
            plus[b] = plus_vals.data();
            Eigen::Vector2d r_plus = Eigen::Vector2d::Zero();
            factor.Evaluate(plus.data(), r_plus.data(), nullptr);

            delta[static_cast<size_t>(c)] = -eps;
            applyDelta(blocks[b], params[b], delta.data(), minus_vals.data());
            std::vector<double *> minus = params;
            minus[b] = minus_vals.data();
            Eigen::Vector2d r_minus = Eigen::Vector2d::Zero();
            factor.Evaluate(minus.data(), r_minus.data(), nullptr);

            num.col(c) = (r_plus - r_minus) / (2.0 * eps);
        }
        std::cout << num << std::endl << std::endl;
    }
}

}  // namespace projection_factor_check
