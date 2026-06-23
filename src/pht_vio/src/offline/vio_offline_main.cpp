/*******************************************************
 * Offline VIO on EuRoC MAV dataset (no ROS).
 *
 * Usage:
 *   pht_vio_offline <config.yaml> <euroc_mav0_path> [start_sec] [duration_sec]
 *
 * Example:
 *   pht_vio_offline $(ros2 pkg prefix pht_vio_ros)/share/pht_vio_ros/config/euroc/euroc_stereo_imu_config.yaml \
 *     /data/MH_04_difficult/MH_04_difficult/mav0 15
 *******************************************************/

#include <iostream>
#include <fstream>
#include <cmath>
#include <opencv2/opencv.hpp>
#include <Eigen/Dense>
#include "estimator/estimator.h"
#include "estimator/parameters.h"
#include "offline/euroc_dataset_reader.h"

static void writeTrajectorySample(const Estimator &estimator, double timestamp)
{
    std::ofstream fout(vinsConfig().vins_result_path, std::ios::app);
    fout.setf(std::ios::fixed, std::ios::floatfield);
    fout.precision(0);
    fout << static_cast<int64_t>(timestamp * 1e9) << ",";
    fout.precision(5);
    Eigen::Quaterniond q(estimator.Rs[WINDOW_SIZE]);
    fout << estimator.Ps[WINDOW_SIZE].x() << ","
         << estimator.Ps[WINDOW_SIZE].y() << ","
         << estimator.Ps[WINDOW_SIZE].z() << ","
         << q.w() << ","
         << q.x() << ","
         << q.y() << ","
         << q.z() << ","
         << estimator.Vs[WINDOW_SIZE].x() << ","
         << estimator.Vs[WINDOW_SIZE].y() << ","
         << estimator.Vs[WINDOW_SIZE].z() << "," << std::endl;
}

static size_t findCam1Index(const std::vector<EurocImageStamp> &cam1, size_t hint, double t_sec)
{
    if (cam1.empty())
        return 0;
    while (hint + 1 < cam1.size() && cam1[hint + 1].time_sec() <= t_sec)
        ++hint;
    while (hint > 0 && cam1[hint].time_sec() > t_sec)
        --hint;
    return hint;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <config.yaml> <euroc_mav0_path> [start_sec] [duration_sec]\n";
        return 1;
    }

    const std::string config_file = argv[1];
    const std::string mav0_path = argv[2];
    const double start_sec = (argc >= 4) ? std::stod(argv[3]) : 0.0;
    const double duration_sec = (argc >= 5) ? std::stod(argv[4]) : -1.0;

    if (!vinsConfig().loadFromYaml(config_file)) {
        std::cerr << "Failed to load config: " << config_file << std::endl;
        return 1;
    }
    vinsConfig().multiple_thread = 0;

    EurocDatasetReader dataset;
    if (!dataset.open(mav0_path)) {
        std::cerr << "Failed to open EuRoC dataset: " << mav0_path << std::endl;
        return 1;
    }

    if (vinsConfig().stereo && !dataset.hasStereo()) {
        std::cerr << "Config requests stereo but cam1 is missing in dataset." << std::endl;
        return 1;
    }
    if (vinsConfig().use_imu && dataset.imuSamples().empty()) {
        std::cerr << "Config requests IMU but imu0 is missing." << std::endl;
        return 1;
    }

    Estimator estimator;
    estimator.setFrameOutputCallback([&](const Estimator &e, double t) {
        writeTrajectorySample(e, t);
    });
    estimator.setParameter();

    const auto &cam0 = dataset.cam0Stamps();
    const auto &cam1 = dataset.cam1Stamps();
    const auto &imu = dataset.imuSamples();

    size_t imu_idx = 0;
    size_t cam1_idx = 0;

    const double t0 = dataset.t0Sec();
    const double start_time = t0 + start_sec;
    const double end_time = (duration_sec > 0) ? (start_time + duration_sec) : 1e18;

    size_t frames_ok = 0;
    size_t frames_missing = 0;
    size_t frames_stereo_fail = 0;

    std::cout << "Dataset t0=" << t0 << " processing [" << start_time << ", " << end_time << "]"
              << " cam0=" << cam0.size() << " imu=" << imu.size() << std::endl;

    for (const auto &c0 : cam0) {
        const double t = c0.time_sec();
        if (t < start_time)
            continue;
        if (t > end_time)
            break;

        if (vinsConfig().use_imu) {
            while (imu_idx < imu.size() && imu[imu_idx].time_sec() <= t) {
                const auto &s = imu[imu_idx];
                Eigen::Vector3d acc(s.ax, s.ay, s.az);
                Eigen::Vector3d gyr(s.wx, s.wy, s.wz);
                estimator.inputIMU(s.time_sec(), acc, gyr);
                ++imu_idx;
            }
        }

        cv::Mat img0 = cv::imread(dataset.cam0ImagePath(c0), cv::IMREAD_GRAYSCALE);
        if (img0.empty()) {
            ++frames_missing;
            if (frames_missing <= 3)
                std::cerr << "Missing image: " << dataset.cam0ImagePath(c0) << std::endl;
            continue;
        }

        cv::Mat img1;
        if (vinsConfig().stereo) {
            cam1_idx = findCam1Index(cam1, cam1_idx, t);
            if (cam1_idx < cam1.size()) {
                const double dt = std::abs(cam1[cam1_idx].time_sec() - t);
                if (dt <= 0.003)
                    img1 = cv::imread(dataset.cam1ImagePath(cam1[cam1_idx]), cv::IMREAD_GRAYSCALE);
            }
            if (img1.empty()) {
                ++frames_stereo_fail;
                continue;
            }
        }

        estimator.inputImage(t, img0, img1);
        ++frames_ok;
    }

    std::ifstream traj(vinsConfig().vins_result_path);
    size_t pose_lines = 0;
    std::string line;
    while (std::getline(traj, line))
        if (!line.empty())
            ++pose_lines;

    std::cout << "Offline run finished. frames_ok=" << frames_ok
              << " missing=" << frames_missing
              << " stereo_fail=" << frames_stereo_fail
              << " trajectory_poses=" << pose_lines
              << " -> " << vinsConfig().vins_result_path << std::endl;

    if (pose_lines == 0) {
        std::cerr << "ERROR: empty trajectory." << std::endl;
        return 1;
    }
    return 0;
}
