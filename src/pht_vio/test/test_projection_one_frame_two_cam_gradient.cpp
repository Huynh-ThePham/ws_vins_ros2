// Gradient test for ProjectionOneFrameTwoCamFactor at w in
// {1.0, 0.75, 0.25, 1e-4, 0}. Numeric side uses Evaluate() via test_gradient_check.h.

#include "factor/projectionOneFrameTwoCamFactor.h"
#include "test_gradient_check.h"

#include <string>

int main()
{
    ProjectionOneFrameTwoCamFactor::sqrt_info = FOCAL_LENGTH / 1.5 * Eigen::Matrix2d::Identity();

    const double ex_pose0[7] = {0.021, -0.064, 0.009, 0.0043633, -0.0087265, 0.0130896, 0.9998744};
    const double ex_pose1[7] = {0.024, -0.174, 0.011, 0.0087265, -0.0043633, 0.0174497, 0.9997964};
    const double inv_dep[1] = {0.32};
    const double td[1] = {0.004};

    const Eigen::Vector3d pts_i(0.062, -0.041, 1.0);
    const Eigen::Vector3d pts_j(0.037, 0.028, 1.0);
    const Eigen::Vector2d velocity_i(0.53, -0.24);
    const Eigen::Vector2d velocity_j(-0.31, 0.47);

    const std::vector<gradient_check::Block> blocks{
        gradient_check::pose(), gradient_check::pose(),
        gradient_check::scalar(), gradient_check::scalar()};
    const std::vector<const double *> values{ex_pose0, ex_pose1, inv_dep, td};

    for (double weight : gradient_check::weights())
    {
        const std::string label =
            "ProjectionOneFrameTwoCamFactor.Gradient w=" + std::to_string(weight);
        TEST_CASE(label.c_str());
        const ProjectionOneFrameTwoCamFactor factor(pts_i, pts_j, velocity_i, velocity_j,
                                                    0.0, 0.011, weight);
        CHECK_NEAR(factor.sqrt_weight, std::sqrt(weight), 1e-15);
        gradient_check::check(factor, blocks, values, label);
    }

    TEST_MAIN_RETURN();
}
