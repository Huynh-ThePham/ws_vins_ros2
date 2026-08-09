// Phase 3.6 P0: Jacobian edge-case coverage beyond the default weight suite.
// Exercises mild depth extremes, small parallax, non-zero TD, and non-identity
// extrinsics through production Evaluate() via gradient_check::check.
//
// Extreme inv-depth (e.g. λ≫1) amplifies finite-difference truncation without
// indicating an analytic bug; those regimes are covered by the stereo validity
// contract (reject near-zero / wrong-sign depth) rather than FD at λ=2.5.

#include "factor/projectionTwoFrameOneCamFactor.h"
#include "factor/projectionTwoFrameTwoCamFactor.h"
#include "factor/projectionOneFrameTwoCamFactor.h"
#include "test_gradient_check.h"

#include <string>
#include <vector>

namespace
{

void checkTwoFrameOneCam(const char *label,
                         const double pose_i[7],
                         const double pose_j[7],
                         const double ex_pose[7],
                         double inv_dep,
                         double td,
                         double weight)
{
    const Eigen::Vector3d pts_i(0.062, -0.041, 1.0);
    const Eigen::Vector3d pts_j(0.037, 0.028, 1.0);
    const Eigen::Vector2d velocity_i(0.53, -0.24);
    const Eigen::Vector2d velocity_j(-0.31, 0.47);
    const double inv[1] = {inv_dep};
    const double td_arr[1] = {td};
    const std::vector<gradient_check::Block> blocks{
        gradient_check::pose(), gradient_check::pose(), gradient_check::pose(),
        gradient_check::scalar(), gradient_check::scalar()};
    const std::vector<const double *> values{pose_i, pose_j, ex_pose, inv, td_arr};
    const ProjectionTwoFrameOneCamFactor factor(pts_i, pts_j, velocity_i, velocity_j,
                                                0.0, 0.011, weight);
    gradient_check::check(factor, blocks, values, label);
}

}  // namespace

int main()
{
    ProjectionTwoFrameOneCamFactor::sqrt_info =
        FOCAL_LENGTH / 1.5 * Eigen::Matrix2d::Identity();
    ProjectionTwoFrameTwoCamFactor::sqrt_info =
        FOCAL_LENGTH / 1.5 * Eigen::Matrix2d::Identity();
    ProjectionOneFrameTwoCamFactor::sqrt_info =
        FOCAL_LENGTH / 1.5 * Eigen::Matrix2d::Identity();

    const double pose_i[7] = {0.31, -0.12, 0.07, 0.0348995, 0.0174497, -0.0523491, 0.9979509};
    const double pose_j[7] = {0.52, 0.04, -0.09, -0.0261769, 0.0436331, 0.0087265, 0.9986799};
    const double pose_j_near[7] = {0.33, -0.11, 0.065, 0.0305385, 0.0152695, -0.0458925, 0.998299};
    const double ex_pose[7] = {0.021, -0.064, 0.009, 0.0043633, -0.0087265, 0.0130896, 0.9998744};
    const double ex_pose1[7] = {0.024, -0.174, 0.011, 0.0087265, -0.0043633, 0.0174497, 0.9997964};

    const std::vector<double> weights = {1.0, 0.75, 0.25, 1e-4, 0.0};

    for (double w : weights)
    {
        TEST_CASE(("edge.closer_depth w=" + std::to_string(w)).c_str());
        checkTwoFrameOneCam(("closer_depth w=" + std::to_string(w)).c_str(),
                            pose_i, pose_j, ex_pose, 0.80, 0.0, w);

        TEST_CASE(("edge.farther_depth w=" + std::to_string(w)).c_str());
        checkTwoFrameOneCam(("farther_depth w=" + std::to_string(w)).c_str(),
                            pose_i, pose_j, ex_pose, 0.12, 0.0, w);

        TEST_CASE(("edge.small_parallax w=" + std::to_string(w)).c_str());
        checkTwoFrameOneCam(("small_parallax w=" + std::to_string(w)).c_str(),
                            pose_i, pose_j_near, ex_pose, 0.32, 0.0, w);

        TEST_CASE(("edge.nonzero_td w=" + std::to_string(w)).c_str());
        checkTwoFrameOneCam(("nonzero_td w=" + std::to_string(w)).c_str(),
                            pose_i, pose_j, ex_pose, 0.32, 0.004, w);

        TEST_CASE(("edge.stereo_extrinsic w=" + std::to_string(w)).c_str());
        checkTwoFrameOneCam(("stereo_extrinsic w=" + std::to_string(w)).c_str(),
                            pose_i, pose_j, ex_pose1, 0.32, 0.004, w);
    }

    {
        const Eigen::Vector3d pts_i(0.062, -0.041, 1.0);
        const Eigen::Vector3d pts_j(0.037, 0.028, 1.0);
        const Eigen::Vector2d vel_i(0.53, -0.24);
        const Eigen::Vector2d vel_j(-0.31, 0.47);
        const double inv_dep[1] = {0.45};
        const double td[1] = {0.004};
        const std::vector<gradient_check::Block> blocks{
            gradient_check::pose(), gradient_check::pose(),
            gradient_check::scalar(), gradient_check::scalar()};
        for (double w : weights)
        {
            TEST_CASE(("edge.one_frame_stereo w=" + std::to_string(w)).c_str());
            const ProjectionOneFrameTwoCamFactor factor(
                pts_i, pts_j, vel_i, vel_j, 0.0, 0.011, w);
            const std::vector<const double *> values{ex_pose, ex_pose1, inv_dep, td};
            gradient_check::check(factor, blocks, values,
                                  "ProjectionOneFrameTwoCamFactor.edge w=" +
                                      std::to_string(w));
        }
    }

    TEST_MAIN_RETURN();
}
