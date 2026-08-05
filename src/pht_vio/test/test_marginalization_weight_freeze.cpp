// Plan P1.13: an observation's residual weight must be frozen when its residual
// block is created. Otherwise a marginalized prior would disagree with the weight
// it was linearized at, and per-id recovery would retroactively rewrite history.
//
// Two invariants are checked:
//   1. sqrt_weight is compile-time immutable (const member, no setter).
//   2. Creating a new factor for the same track at a *different* weight leaves the
//      already-constructed factor's residual and Jacobians bit-identical.

#include "factor/projectionTwoFrameOneCamFactor.h"
#include "factor/projectionTwoFrameTwoCamFactor.h"
#include "factor/projectionOneFrameTwoCamFactor.h"
#include "test_support.h"

#include <type_traits>
#include <vector>

namespace
{

// Immutability is a property of the type, not of a runtime check.
static_assert(std::is_const<decltype(ProjectionTwoFrameOneCamFactor::sqrt_weight)>::value,
              "ProjectionTwoFrameOneCamFactor::sqrt_weight must be const");
static_assert(std::is_const<decltype(ProjectionTwoFrameTwoCamFactor::sqrt_weight)>::value,
              "ProjectionTwoFrameTwoCamFactor::sqrt_weight must be const");
static_assert(std::is_const<decltype(ProjectionOneFrameTwoCamFactor::sqrt_weight)>::value,
              "ProjectionOneFrameTwoCamFactor::sqrt_weight must be const");

struct Params
{
    // pose_i, pose_j, extrinsic cam0, inverse depth, td
    double pose_i[7] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0};
    double pose_j[7] = {0.10, 0.02, 0.0, 0.0, 0.0, 0.0, 1.0};
    double ex_pose0[7] = {0.05, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0};
    double ex_pose1[7] = {0.05, -0.11, 0.0, 0.0, 0.0, 0.0, 1.0};
    double inv_dep[1] = {0.25};
    double td[1] = {0.0};

    std::vector<double *> twoFrameOneCam()
    {
        return {pose_i, pose_j, ex_pose0, inv_dep, td};
    }
    std::vector<double *> twoFrameTwoCam()
    {
        return {pose_i, pose_j, ex_pose0, ex_pose1, inv_dep, td};
    }
    std::vector<double *> oneFrameTwoCam()
    {
        return {ex_pose0, ex_pose1, inv_dep, td};
    }
};

const Eigen::Vector3d kPtsI(0.05, -0.02, 1.0);
const Eigen::Vector3d kPtsJ(0.02, 0.03, 1.0);
const Eigen::Vector2d kVelI(0.4, -0.2);
const Eigen::Vector2d kVelJ(-0.3, 0.5);

// Evaluate a factor and return residual + every Jacobian entry, flattened.
template <typename Factor>
std::vector<double> evaluateAll(const Factor &factor,
                                std::vector<double *> params,
                                const std::vector<int> &block_sizes)
{
    std::vector<double> residual(2, 0.0);
    std::vector<std::vector<double>> jacobian_storage;
    std::vector<double *> jacobians;
    jacobian_storage.reserve(block_sizes.size());
    for (int size : block_sizes)
    {
        jacobian_storage.emplace_back(2 * size, 0.0);
        jacobians.push_back(jacobian_storage.back().data());
    }
    factor.Evaluate(params.data(), residual.data(), jacobians.data());

    std::vector<double> flat = residual;
    for (const auto &block : jacobian_storage)
        flat.insert(flat.end(), block.begin(), block.end());
    return flat;
}

bool identical(const std::vector<double> &a, const std::vector<double> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        // Bit-identical is the claim: nothing recomputes, nothing drifts.
        if (a[i] != b[i])
            return false;
    }
    return true;
}

}  // namespace

int main()
{
    ProjectionTwoFrameOneCamFactor::sqrt_info = FOCAL_LENGTH / 1.5 * Eigen::Matrix2d::Identity();
    ProjectionTwoFrameTwoCamFactor::sqrt_info = FOCAL_LENGTH / 1.5 * Eigen::Matrix2d::Identity();
    ProjectionOneFrameTwoCamFactor::sqrt_info = FOCAL_LENGTH / 1.5 * Eigen::Matrix2d::Identity();

    TEST_CASE("MarginalizationWeightFreeze.WeightScalesResidualBySqrt");
    {
        Params p;
        const ProjectionTwoFrameOneCamFactor unit(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.0, 1.0);
        const ProjectionTwoFrameOneCamFactor quarter(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.0, 0.25);
        CHECK_NEAR(unit.sqrt_weight, 1.0, 1e-15);
        CHECK_NEAR(quarter.sqrt_weight, 0.5, 1e-15);

        const std::vector<int> sizes{7, 7, 7, 1, 1};
        const auto ru = evaluateAll(unit, p.twoFrameOneCam(), sizes);
        const auto rq = evaluateAll(quarter, p.twoFrameOneCam(), sizes);
        CHECK(ru.size() == rq.size());
        // Every residual and Jacobian entry scales by exactly sqrt(w).
        bool nonzero_seen = false;
        for (size_t i = 0; i < ru.size(); i++)
        {
            CHECK_NEAR(rq[i], 0.5 * ru[i], 1e-9);
            if (std::abs(ru[i]) > 1e-9)
                nonzero_seen = true;
        }
        CHECK(nonzero_seen);
    }

    TEST_CASE("MarginalizationWeightFreeze.NewWeightDoesNotAlterExistingFactor");
    {
        Params p;
        const std::vector<int> sizes{7, 7, 7, 1, 1};
        // The residual block that would already be inside a marginalized prior.
        const ProjectionTwoFrameOneCamFactor marginalized(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.0, 0.40);
        const auto before = evaluateAll(marginalized, p.twoFrameOneCam(), sizes);

        // Per-id recovery raises the track's weight and new observations are added.
        for (double recovered : {0.52, 0.64, 0.76, 1.0})
        {
            const ProjectionTwoFrameOneCamFactor fresh(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.0, recovered);
            (void)evaluateAll(fresh, p.twoFrameOneCam(), sizes);
            CHECK_NEAR(fresh.sqrt_weight, std::sqrt(recovered), 1e-15);
            // The older block is untouched by the newer, higher weight.
            CHECK_NEAR(marginalized.sqrt_weight, std::sqrt(0.40), 1e-15);
        }

        const auto after = evaluateAll(marginalized, p.twoFrameOneCam(), sizes);
        CHECK(identical(before, after));
    }

    TEST_CASE("MarginalizationWeightFreeze.AllThreeProjectionFactorsAgree");
    {
        Params p;
        const std::vector<int> two_cam_sizes{7, 7, 7, 7, 1, 1};
        const std::vector<int> one_frame_sizes{7, 7, 1, 1};

        const ProjectionTwoFrameTwoCamFactor tf2c_unit(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.0, 1.0);
        const ProjectionTwoFrameTwoCamFactor tf2c_quarter(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.0, 0.25);
        const auto a = evaluateAll(tf2c_unit, p.twoFrameTwoCam(), two_cam_sizes);
        const auto b = evaluateAll(tf2c_quarter, p.twoFrameTwoCam(), two_cam_sizes);
        for (size_t i = 0; i < a.size(); i++)
            CHECK_NEAR(b[i], 0.5 * a[i], 1e-9);

        const ProjectionOneFrameTwoCamFactor of2c_unit(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.0, 1.0);
        const ProjectionOneFrameTwoCamFactor of2c_quarter(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.0, 0.25);
        const auto c = evaluateAll(of2c_unit, p.oneFrameTwoCam(), one_frame_sizes);
        const auto d = evaluateAll(of2c_quarter, p.oneFrameTwoCam(), one_frame_sizes);
        for (size_t i = 0; i < c.size(); i++)
            CHECK_NEAR(d[i], 0.5 * c[i], 1e-9);
    }

    TEST_CASE("MarginalizationWeightFreeze.WeightIsClampedToUnitInterval");
    {
        const ProjectionTwoFrameOneCamFactor over(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.0, 4.0);
        const ProjectionTwoFrameOneCamFactor under(kPtsI, kPtsJ, kVelI, kVelJ, 0.0, 0.0, -1.0);
        CHECK_NEAR(over.sqrt_weight, 1.0, 1e-15);
        CHECK_NEAR(under.sqrt_weight, 0.0, 1e-15);
    }

    TEST_MAIN_RETURN();
}
