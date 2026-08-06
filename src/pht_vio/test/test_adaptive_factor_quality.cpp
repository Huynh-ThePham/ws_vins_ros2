#include "factor/adaptive_factor_quality.h"

#include <cassert>
#include <cmath>
#include <vector>

namespace
{
bool near(double a, double b, double eps = 1e-9)
{
    return std::abs(a - b) <= eps;
}
}  // namespace

int main()
{
    adaptive_factor::VisualQualityConfig visual;
    assert(near(adaptive_factor::visualObservationWeight(100.0, 10.0, 1, visual), 1.0));
    visual.enabled = true;
    const double clean = adaptive_factor::visualObservationWeight(0.0, 0.0, 4, visual);
    const double young = adaptive_factor::visualObservationWeight(0.0, 0.0, 1, visual);
    const double noisy = adaptive_factor::visualObservationWeight(20.0, 0.5, 4, visual);
    assert(near(clean, 1.0));
    assert(young < clean);
    assert(noisy < young);
    assert(noisy >= visual.min_weight);

    adaptive_factor::AdaptiveHuberConfig huber;
    huber.enabled = true;
    huber.ema = 1.0;
    huber.min_samples = 4;
    const std::vector<double> nominal{1.0, 1.1, 1.2, 1.3, 15.0};
    const double delta =
        adaptive_factor::adaptiveHuberDelta(nominal, 1.0, 1.0, huber);
    assert(delta >= huber.min_delta);
    assert(delta <= huber.max_delta);
    assert(std::isfinite(delta));

    adaptive_factor::ImuQualityConfig imu;
    imu.enabled = true;
    const Eigen::Vector3d acc(0.0, 0.0, 9.81);
    const Eigen::Vector3d gyr(0.0, 0.0, 0.1);
    assert(near(adaptive_factor::imuNoiseInflation(0.005, acc, gyr, acc, gyr, imu), 1.0));
    assert(adaptive_factor::imuNoiseInflation(0.04, acc, gyr, acc, gyr, imu) > 1.0);
    assert(adaptive_factor::imuNoiseInflation(
               0.005, Eigen::Vector3d(100.0, 0.0, 0.0), gyr, acc, gyr, imu) > 1.0);
    assert(adaptive_factor::imuNoiseInflation(
               1.0, Eigen::Vector3d(1000.0, 0.0, 0.0), gyr, acc, gyr, imu) <=
           imu.max_inflation);

    // Accel saturation must inflate accel measurement noise, not gyro.
    {
        const auto split = adaptive_factor::imuNoiseInflationSplit(
            0.005, Eigen::Vector3d(200.0, 0.0, 0.0), gyr, acc, gyr, imu);
        assert(split.acc_measurement > 1.0);
        assert(near(split.gyr_measurement, 1.0));
        assert(near(split.gyr_bias_walk, 1.0));
    }
    // Gyro saturation must not force accel measurement inflation.
    {
        const auto split = adaptive_factor::imuNoiseInflationSplit(
            0.005, acc, Eigen::Vector3d(50.0, 0.0, 0.0), acc, gyr, imu);
        assert(split.gyr_measurement > 1.0);
        assert(near(split.acc_measurement, 1.0));
    }
    // Packet gap inflates measurement more than bias walk.
    {
        const auto split = adaptive_factor::imuNoiseInflationSplit(
            0.05, acc, gyr, acc, gyr, imu);
        assert(split.acc_measurement > split.acc_bias_walk);
        assert(split.gyr_measurement > split.gyr_bias_walk);
    }
    return 0;
}