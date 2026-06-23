#pragma once

#include <opencv2/opencv.hpp>
#include <cstdint>
#include <string>
#include <vector>

struct EurocImuSample
{
    int64_t time_ns = 0;
    double time_sec() const { return static_cast<double>(time_ns) * 1e-9; }
    double wx, wy, wz;
    double ax, ay, az;
};

struct EurocImageStamp
{
    int64_t time_ns = 0;
    double time_sec() const { return static_cast<double>(time_ns) * 1e-9; }
    std::string filename;
};

class EurocDatasetReader
{
public:
    bool open(const std::string &mav0_root);
    bool hasStereo() const { return !cam1_stamps_.empty(); }

    const std::vector<EurocImuSample> &imuSamples() const { return imu_; }
    const std::vector<EurocImageStamp> &cam0Stamps() const { return cam0_stamps_; }
    const std::vector<EurocImageStamp> &cam1Stamps() const { return cam1_stamps_; }

    std::string cam0ImagePath(const EurocImageStamp &stamp) const;
    std::string cam1ImagePath(const EurocImageStamp &stamp) const;

    /** First camera timestamp (seconds). */
    double t0Sec() const;

private:
    bool loadCsv(const std::string &csv_path, std::vector<EurocImageStamp> *out_images);
    bool loadImuCsv(const std::string &csv_path);

    std::string root_;
    std::vector<EurocImuSample> imu_;
    std::vector<EurocImageStamp> cam0_stamps_;
    std::vector<EurocImageStamp> cam1_stamps_;
};
