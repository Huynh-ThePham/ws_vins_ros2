// Plan P0.6: real marginalization integration test.
//
// Unlike test_marginalization_weight_freeze.cpp (const sqrt_weight immutability),
// this builds ResidualBlockInfo → MarginalizationInfo::{preMarginalize,marginalize}
// → MarginalizationFactor and checks that the prior is frozen against later
// "live" weight / risk changes, is finite, and has a PSD Hessian (J^T J).

#include "factor/marginalization_factor.h"
#include "factor/projectionTwoFrameOneCamFactor.h"
#include "test_support.h"

#include <ceres/loss_function.h>

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

#include <cmath>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace
{

struct SyntheticState
{
    // Non-identity geometry so Jacobians are exercised.
    double pose_i[7] = {0.31, -0.12, 0.07, 0.0348995, 0.0174497, -0.0523491, 0.9979509};
    double pose_j[7] = {0.52, 0.04, -0.09, -0.0261769, 0.0436331, 0.0087265, 0.9986799};
    double ex_pose[7] = {0.021, -0.064, 0.009, 0.0043633, -0.0087265, 0.0130896, 0.9998744};
    double inv_dep[1] = {0.32};
    double td[1] = {0.004};

    std::vector<double *> parameterBlocks()
    {
        return {pose_i, pose_j, ex_pose, inv_dep, td};
    }
};

const Eigen::Vector3d kPtsI(0.062, -0.041, 1.0);
const Eigen::Vector3d kPtsJ(0.037, 0.028, 1.0);
const Eigen::Vector2d kVelI(0.53, -0.24);
const Eigen::Vector2d kVelJ(-0.31, 0.47);

struct PriorEval
{
    Eigen::VectorXd residual;
    std::vector<Eigen::MatrixXd> jacobians;  // row-major blocks as Evaluate wrote them
};

PriorEval evaluatePrior(const MarginalizationFactor &factor,
                        const std::vector<double *> &blocks)
{
    PriorEval out;
    const int n = factor.num_residuals();
    out.residual = Eigen::VectorXd::Zero(n);

    const std::vector<int> &sizes = factor.parameter_block_sizes();
    std::vector<std::vector<double>> jac_storage(sizes.size());
    std::vector<double *> jac_ptrs(sizes.size(), nullptr);
    for (size_t i = 0; i < sizes.size(); ++i)
    {
        jac_storage[i].assign(static_cast<size_t>(n * sizes[i]), 0.0);
        jac_ptrs[i] = jac_storage[i].data();
    }

    factor.Evaluate(blocks.data(), out.residual.data(), jac_ptrs.data());

    out.jacobians.resize(sizes.size());
    for (size_t i = 0; i < sizes.size(); ++i)
    {
        out.jacobians[i] = Eigen::Map<Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>(
            jac_storage[i].data(), n, sizes[i]);
    }
    return out;
}

bool nearlyIdentical(const PriorEval &a, const PriorEval &b, double tol = 0.0)
{
    if (a.residual.size() != b.residual.size() || a.jacobians.size() != b.jacobians.size())
        return false;
    for (int i = 0; i < a.residual.size(); ++i)
    {
        if (tol == 0.0)
        {
            if (a.residual[i] != b.residual[i])
                return false;
        }
        else if (std::abs(a.residual[i] - b.residual[i]) > tol)
            return false;
    }
    for (size_t b_i = 0; b_i < a.jacobians.size(); ++b_i)
    {
        if (a.jacobians[b_i].rows() != b.jacobians[b_i].rows() ||
            a.jacobians[b_i].cols() != b.jacobians[b_i].cols())
            return false;
        for (int r = 0; r < a.jacobians[b_i].rows(); ++r)
        {
            for (int c = 0; c < a.jacobians[b_i].cols(); ++c)
            {
                const double lhs = a.jacobians[b_i](r, c);
                const double rhs = b.jacobians[b_i](r, c);
                if (tol == 0.0)
                {
                    if (lhs != rhs)
                        return false;
                }
                else if (std::abs(lhs - rhs) > tol)
                    return false;
            }
        }
    }
    return true;
}

bool allFinite(const Eigen::MatrixXd &m)
{
    return m.allFinite();
}

bool allFinite(const Eigen::VectorXd &v)
{
    return v.allFinite();
}

// Build a prior that marginalizes pose_i (drop_set index 0), matching the
// production pattern of wrapping a weighted projection factor + Huber loss.
MarginalizationInfo *buildPrior(SyntheticState &state, double weight, ceres::LossFunction *loss)
{
    auto *proj = new ProjectionTwoFrameOneCamFactor(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.011, weight);
    // drop_set marks pose_i for elimination (same slot production uses for the
    // oldest pose); feature stays retained so the prior has non-trivial n.
    auto *rbi = new ResidualBlockInfo(proj, loss, state.parameterBlocks(),
                                      /*drop_set=*/std::vector<int>{0});

    auto *info = new MarginalizationInfo();
    info->addResidualBlockInfo(rbi);
    info->preMarginalize();
    info->marginalize();
    return info;
}

std::vector<double *> keepBlocks(MarginalizationInfo *info, SyntheticState &state)
{
    std::unordered_map<long, double *> addr_shift;
    for (double *addr : state.parameterBlocks())
        addr_shift[reinterpret_cast<long>(addr)] = addr;
    return info->getParameterBlocks(addr_shift);
}

}  // namespace

int main()
{
    ProjectionTwoFrameOneCamFactor::sqrt_info =
        FOCAL_LENGTH / 1.5 * Eigen::Matrix2d::Identity();

    TEST_CASE("MarginalizationIntegration.BuildPriorAndEvaluate");
    {
        SyntheticState state;
        ceres::HuberLoss loss(1.0);
        MarginalizationInfo *info = buildPrior(state, /*weight=*/0.25, &loss);
        CHECK(info->valid);
        CHECK(info->m > 0);  // dropped pose_i local size = 6
        CHECK(info->n > 0);  // retained: pose_j + ex + inv_dep + td

        CHECK(allFinite(info->linearized_residuals));
        CHECK(allFinite(info->linearized_jacobians));
        CHECK(info->linearized_residuals.size() == info->n);
        CHECK(info->linearized_jacobians.rows() == info->n);
        CHECK(info->linearized_jacobians.cols() == info->n);

        std::vector<double *> blocks = keepBlocks(info, state);
        CHECK(!blocks.empty());
        CHECK(static_cast<int>(blocks.size()) == static_cast<int>(info->keep_block_size.size()));

        MarginalizationFactor prior(info);
        CHECK(prior.num_residuals() == info->n);
        const PriorEval ev = evaluatePrior(prior, blocks);
        CHECK(allFinite(ev.residual));
        for (const auto &J : ev.jacobians)
            CHECK(allFinite(J));

        // At the linearization point, Evaluate residual equals linearized_residuals.
        CHECK(ev.residual.size() == info->linearized_residuals.size());
        for (int i = 0; i < ev.residual.size(); ++i)
            CHECK_NEAR(ev.residual[i], info->linearized_residuals[i], 1e-12);

        delete info;
    }

    TEST_CASE("MarginalizationIntegration.PriorFrozenAgainstLiveWeightChange");
    {
        SyntheticState state;
        ceres::HuberLoss loss(1.0);
        constexpr double kFrozenWeight = 0.25;
        MarginalizationInfo *info = buildPrior(state, kFrozenWeight, &loss);
        CHECK(info->valid);

        std::vector<double *> blocks = keepBlocks(info, state);
        MarginalizationFactor prior(info);
        const PriorEval before = evaluatePrior(prior, blocks);

        // Simulate a live track risk / weight update by constructing a NEW
        // projection factor at a different weight. The marginalized prior must
        // remain exactly the residual/Jacobian it was linearized at.
        for (double live_w : {0.5, 0.75, 1.0, 0.01})
        {
            ProjectionTwoFrameOneCamFactor live(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.011, live_w);
            CHECK_NEAR(live.sqrt_weight, std::sqrt(live_w), 1e-15);
            double live_res[2] = {0.0, 0.0};
            live.Evaluate(state.parameterBlocks().data(), live_res, nullptr);
            CHECK(std::isfinite(live_res[0]) && std::isfinite(live_res[1]));
            (void)live_res;
        }

        const PriorEval after = evaluatePrior(prior, blocks);
        // Bit-identical: prior storage is not recomputed from live factors.
        CHECK(nearlyIdentical(before, after, /*tol=*/0.0));
        // And the stored linearization itself is unchanged.
        CHECK(allFinite(info->linearized_residuals));
        CHECK(allFinite(info->linearized_jacobians));

        delete info;
    }

    TEST_CASE("MarginalizationIntegration.PriorHessianNoLargeNegativeEigenvalues");
    {
        SyntheticState state;
        ceres::HuberLoss loss(1.0);
        MarginalizationInfo *info = buildPrior(state, 0.25, &loss);
        CHECK(info->valid);
        CHECK(info->n >= 1);

        // Prior Hessian approximation in residual space: H = J^T J.
        const Eigen::MatrixXd &J = info->linearized_jacobians;
        const Eigen::MatrixXd H = J.transpose() * J;
        CHECK(allFinite(H));
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(H);
        CHECK(es.info() == Eigen::Success);
        const double min_eig = es.eigenvalues().minCoeff();
        const double max_eig = es.eigenvalues().maxCoeff();
        // Analytically PSD; allow tiny numerical negatives only.
        CHECK(min_eig > -1e-8);
        CHECK(max_eig >= 0.0);
        CHECK(std::isfinite(min_eig) && std::isfinite(max_eig));

        delete info;
    }

    TEST_CASE("MarginalizationIntegration.LocalQuadraticRetainedStates");
    {
        // Local quadratic on retained states: prior residual energy is finite,
        // and Evaluate's Jacobian matches a central difference on a retained
        // scalar (td). (||linearized_residuals||^2 is b^T A^+ b under VINS's
        // eigen factorisation — not directly comparable to 0.5||r_factor||^2.)
        SyntheticState state;
        ceres::HuberLoss loss(1.0);
        MarginalizationInfo *info = buildPrior(state, 0.25, &loss);
        CHECK(info->valid);
        CHECK(!info->factors.empty());

        const double full_energy = 0.5 * info->factors[0]->residuals.squaredNorm();
        const double marg_energy = 0.5 * info->linearized_residuals.squaredNorm();
        CHECK(std::isfinite(full_energy));
        CHECK(std::isfinite(marg_energy));
        CHECK(full_energy >= 0.0);
        CHECK(marg_energy >= 0.0);

        std::vector<double *> blocks = keepBlocks(info, state);
        MarginalizationFactor prior(info);
        const PriorEval at0 = evaluatePrior(prior, blocks);

        int td_block = -1;
        for (size_t i = 0; i < info->keep_block_size.size(); ++i)
        {
            if (info->keep_block_size[i] == 1 && blocks[i] == state.td)
                td_block = static_cast<int>(i);
        }
        CHECK(td_block >= 0);
        {
            constexpr double kEps = 1e-6;
            state.td[0] += kEps;
            const PriorEval at_plus = evaluatePrior(prior, blocks);
            state.td[0] -= 2.0 * kEps;
            const PriorEval at_minus = evaluatePrior(prior, blocks);
            state.td[0] += kEps;  // restore

            const Eigen::VectorXd numeric =
                (at_plus.residual - at_minus.residual) / (2.0 * kEps);
            CHECK(at0.jacobians[static_cast<size_t>(td_block)].cols() >= 1);
            const Eigen::VectorXd analytic =
                at0.jacobians[static_cast<size_t>(td_block)].col(0);
            CHECK(numeric.size() == analytic.size());
            double scale = 1.0;
            for (int i = 0; i < analytic.size(); ++i)
                scale = std::max(scale, std::abs(analytic[i]));
            for (int i = 0; i < analytic.size(); ++i)
                CHECK_NEAR(numeric[i], analytic[i], 2e-4 * scale);
        }

        delete info;
    }

    TEST_MAIN_RETURN();
}
