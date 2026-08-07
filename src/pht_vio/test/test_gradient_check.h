#pragma once

// Central finite-difference gradient checker for the weighted projection factors
// (plan section 8, "Factor gradient tests").
//
// Numerical residuals always come from Factor::Evaluate() after local perturbation
// so sqrt_weight is applied identically on analytic and numeric sides. Production
// factor::check() helpers use the same Evaluate-based approach via
// projection_factor_check_util.h.

#include "test_support.h"

#include <Eigen/Dense>
#include <pht_slam_common/utility.hpp>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace gradient_check
{

// A parameter block: ambient size as Ceres sees it, and the dimension of the
// local tangent space we perturb in.
struct Block
{
    int ambient = 0;
    int local = 0;
    bool is_pose = false;  // 7 ambient / 6 local, quaternion stored as (x,y,z,w)
};

inline Block pose()
{
    return Block{7, 6, true};
}

inline Block scalar()
{
    return Block{1, 1, false};
}

// x <- x (+) delta, using the same retraction PoseLocalParameterization applies.
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

// Compare the analytic Jacobians (first `local` columns of each block) against a
// central finite difference of the factor's own residual.
//
// Reports mismatches through the test harness.
template <typename Factor>
void check(const Factor &factor,
           const std::vector<Block> &blocks,
           const std::vector<const double *> &values,
           const std::string &label,
           double eps = 1e-6,
           double tol = 2e-4)
{
    const int kResidual = 2;

    std::vector<std::vector<double>> ambient;
    std::vector<double *> params;
    ambient.reserve(blocks.size());
    for (size_t b = 0; b < blocks.size(); b++)
    {
        ambient.emplace_back(values[b], values[b] + blocks[b].ambient);
        params.push_back(ambient.back().data());
    }

    // Analytic evaluation.
    Eigen::Vector2d residual = Eigen::Vector2d::Zero();
    std::vector<Eigen::MatrixXd> analytic;
    std::vector<double *> jac_ptrs;
    analytic.reserve(blocks.size());
    for (const auto &block : blocks)
        analytic.emplace_back(Eigen::MatrixXd::Zero(kResidual, block.ambient));
    for (auto &m : analytic)
        jac_ptrs.push_back(m.data());
    // Eigen default storage is column-major; Ceres expects row-major, so evaluate
    // into row-major scratch and transpose in.
    std::vector<std::vector<double>> row_major;
    jac_ptrs.clear();
    for (const auto &block : blocks)
    {
        row_major.emplace_back(kResidual * block.ambient, 0.0);
        jac_ptrs.push_back(row_major.back().data());
    }
    factor.Evaluate(params.data(), residual.data(), jac_ptrs.data());
    for (size_t b = 0; b < blocks.size(); b++)
        for (int r = 0; r < kResidual; r++)
            for (int c = 0; c < blocks[b].ambient; c++)
                analytic[b](r, c) = row_major[b][r * blocks[b].ambient + c];

    // Scale for the relative comparison: the factor is whitened by sqrt_info, so
    // absolute Jacobian magnitudes are large and a pure absolute tolerance is
    // meaningless. When w=0 every Jacobian is zero; keep scale=1 so tol still applies.
    double scale = 1.0;
    for (size_t b = 0; b < blocks.size(); b++)
        scale = std::max(scale, analytic[b].cwiseAbs().maxCoeff());

    for (size_t b = 0; b < blocks.size(); b++)
    {
        for (int c = 0; c < blocks[b].local; c++)
        {
            std::vector<double> delta(blocks[b].local, 0.0);
            std::vector<double> perturbed(blocks[b].ambient, 0.0);

            delta[c] = eps;
            applyDelta(blocks[b], values[b], delta.data(), perturbed.data());
            std::vector<double *> plus = params;
            plus[b] = perturbed.data();
            Eigen::Vector2d r_plus = Eigen::Vector2d::Zero();
            factor.Evaluate(plus.data(), r_plus.data(), nullptr);

            std::vector<double> perturbed_minus(blocks[b].ambient, 0.0);
            delta[c] = -eps;
            applyDelta(blocks[b], values[b], delta.data(), perturbed_minus.data());
            std::vector<double *> minus = params;
            minus[b] = perturbed_minus.data();
            Eigen::Vector2d r_minus = Eigen::Vector2d::Zero();
            factor.Evaluate(minus.data(), r_minus.data(), nullptr);

            const Eigen::Vector2d numeric = (r_plus - r_minus) / (2.0 * eps);
            for (int r = 0; r < kResidual; r++)
            {
                const double diff = std::abs(numeric(r) - analytic[b](r, c));
                if (diff <= tol * scale)
                    continue;
                test_support::failureCount()++;
                std::printf("[  FAILED  ] %s\n"
                            "            block %zu col %d row %d: analytic %.9g vs numeric %.9g"
                            " (|diff| %.3g > %.3g)\n",
                            label.c_str(), b, c, r, analytic[b](r, c), numeric(r), diff,
                            tol * scale);
            }
        }
        // Columns beyond the local dimension must be exactly zero: Ceres multiplies
        // by the local parameterization's plus-Jacobian, which ignores them.
        for (int c = blocks[b].local; c < blocks[b].ambient; c++)
        {
            for (int r = 0; r < kResidual; r++)
            {
                if (analytic[b](r, c) == 0.0)
                    continue;
                test_support::failureCount()++;
                std::printf("[  FAILED  ] %s\n"
                            "            block %zu padding col %d row %d must be 0, got %.9g\n",
                            label.c_str(), b, c, r, analytic[b](r, c));
            }
        }
    }
}

// Weights every projection-factor gradient test must cover (plan P0.5).
// w=0 is allowed by the factor clamp (sqrt_weight = 0).
inline const std::vector<double> &weights()
{
    static const std::vector<double> w{1.0, 0.75, 0.25, 1e-4, 0.0};
    return w;
}

}  // namespace gradient_check
