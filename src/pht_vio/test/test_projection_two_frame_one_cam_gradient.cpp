// Gradient test for ProjectionTwoFrameOneCamFactor at w in {1.0, 0.75, 0.25}.
// See test_gradient_check.h for why the factor's own check() is not sufficient.

#include "factor/projectionTwoFrameOneCamFactor.h"
#include "test_gradient_check.h"

#include <string>

int main()
{
    ProjectionTwoFrameOneCamFactor::sqrt_info = FOCAL_LENGTH / 1.5 * Eigen::Matrix2d::Identity();

    // Non-trivial pose pair, non-identity extrinsic, non-zero feature velocities and
    // a non-zero td, so no Jacobian block is accidentally exercised at zero.
    const double pose_i[7] = {0.31, -0.12, 0.07, 0.0348995, 0.0174497, -0.0523491, 0.9979509};
    const double pose_j[7] = {0.52, 0.04, -0.09, -0.0261769, 0.0436331, 0.0087265, 0.9986799};
    const double ex_pose[7] = {0.021, -0.064, 0.009, 0.0043633, -0.0087265, 0.0130896, 0.9998744};
    const double inv_dep[1] = {0.32};
    const double td[1] = {0.004};

    const Eigen::Vector3d pts_i(0.062, -0.041, 1.0);
    const Eigen::Vector3d pts_j(0.037, 0.028, 1.0);
    const Eigen::Vector2d velocity_i(0.53, -0.24);
    const Eigen::Vector2d velocity_j(-0.31, 0.47);

    const std::vector<gradient_check::Block> blocks{
        gradient_check::pose(), gradient_check::pose(), gradient_check::pose(),
        gradient_check::scalar(), gradient_check::scalar()};
    const std::vector<const double *> values{pose_i, pose_j, ex_pose, inv_dep, td};

    for (double weight : gradient_check::weights())
    {
        const std::string label =
            "ProjectionTwoFrameOneCamFactor.Gradient w=" + std::to_string(weight);
        TEST_CASE(label.c_str());
        const ProjectionTwoFrameOneCamFactor factor(pts_i, pts_j, velocity_i, velocity_j,
                                                    0.0, 0.011, weight);
        CHECK_NEAR(factor.sqrt_weight, std::sqrt(weight), 1e-15);
        gradient_check::check(factor, blocks, values, label);
    }

    TEST_MAIN_RETURN();
}
