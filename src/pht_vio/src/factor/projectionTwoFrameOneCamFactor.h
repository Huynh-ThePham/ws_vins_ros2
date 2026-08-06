/*******************************************************
 * Copyright (C) 2019, Aerial Robotics Group, Hong Kong University of Science and Technology
 *
 * This file is part of VINS.
 *
 * Licensed under the GNU General Public License v3.0;
 * you may not use this file except in compliance with the License.
 *
 * Author: Qin Tong (qintonguav@gmail.com)
 *******************************************************/

#pragma once

#include <cassert>
#include <ceres/ceres.h>
#include <Eigen/Dense>
#include <pht_slam_common/utility.hpp>
#include <pht_slam_common/tic_toc.hpp>
#include "../estimator/parameters.h"

class ProjectionTwoFrameOneCamFactor : public ceres::SizedCostFunction<2, 7, 7, 7, 1, 1>
{
  public:
    ProjectionTwoFrameOneCamFactor(const Eigen::Vector3d &_pts_i, const Eigen::Vector3d &_pts_j,
                                   const Eigen::Vector2d &_velocity_i, const Eigen::Vector2d &_velocity_j,
                                   const double _td_i, const double _td_j, const double _weight = 1.0);
    virtual bool Evaluate(double const *const *parameters, double *residuals, double **jacobians) const;
    void check(double **parameters);

    Eigen::Vector3d pts_i, pts_j;
    Eigen::Vector3d velocity_i, velocity_j;
    double td_i, td_j;
    // Frozen at construction: an observation's weight must never be mutated
    // after its residual block exists, or a marginalized prior would silently
    // disagree with the weight it was linearized at.
    //
    // Semantics (publication / Hướng A — precision interpretation):
    //   w ∈ (0, 1] is a measurement-precision multiplier: Σ_w = Σ / w.
    //   Implemented by scaling the whitened residual and Jacobian by √w, which is
    //   algebraically equivalent to that covariance scaling under quadratic cost.
    //   w is NOT a calibrated posterior inlier probability; do not treat it as one
    //   without an explicit calibration study. Huber ρ(·) is applied by Ceres on
    //   top of the already √w-scaled residual.
    const double sqrt_weight;
    Eigen::Matrix<double, 2, 3> tangent_base;
    static Eigen::Matrix2d sqrt_info;
    static double sum_t;
};
