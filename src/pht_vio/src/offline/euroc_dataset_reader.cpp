#include "offline/euroc_dataset_reader.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace {

void trimInPlace(std::string &s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || std::isspace(static_cast<unsigned char>(s.back()))))
        s.pop_back();
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    if (start > 0)
        s.erase(0, start);
}

int64_t parseTimestampNs(const std::string &ts_str)
{
    return std::stoll(ts_str);
}

}  // namespace

bool EurocDatasetReader::open(const std::string &mav0_root)
{
    root_ = mav0_root;
    imu_.clear();
    cam0_stamps_.clear();
    cam1_stamps_.clear();

    const std::string cam0_csv = root_ + "/cam0/data.csv";
    const std::string cam1_csv = root_ + "/cam1/data.csv";
    const std::string imu_csv = root_ + "/imu0/data.csv";

    if (!loadCsv(cam0_csv, &cam0_stamps_)) {
        std::cerr << "Failed to load " << cam0_csv << std::endl;
        return false;
    }
    if (fs::exists(cam1_csv))
        loadCsv(cam1_csv, &cam1_stamps_);
    if (!loadImuCsv(imu_csv)) {
        std::cerr << "Failed to load " << imu_csv << std::endl;
        return false;
    }
    return !cam0_stamps_.empty() && !imu_.empty();
}

double EurocDatasetReader::t0Sec() const
{
    return cam0_stamps_.empty() ? 0.0 : cam0_stamps_.front().time_sec();
}

bool EurocDatasetReader::loadCsv(const std::string &csv_path, std::vector<EurocImageStamp> *out_images)
{
    std::ifstream ifs(csv_path);
    if (!ifs.is_open())
        return false;

    std::string line;
    while (std::getline(ifs, line)) {
        trimInPlace(line);
        if (line.empty() || line[0] == '#')
            continue;
        std::stringstream ss(line);
        std::string ts_str, filename;
        if (!std::getline(ss, ts_str, ','))
            continue;
        if (!std::getline(ss, filename, ','))
            continue;
        trimInPlace(ts_str);
        trimInPlace(filename);
        if (ts_str.empty() || filename.empty())
            continue;

        EurocImageStamp stamp;
        stamp.time_ns = parseTimestampNs(ts_str);
        stamp.filename = filename;
        out_images->push_back(stamp);
    }
    return true;
}

bool EurocDatasetReader::loadImuCsv(const std::string &csv_path)
{
    std::ifstream ifs(csv_path);
    if (!ifs.is_open())
        return false;

    std::string line;
    while (std::getline(ifs, line)) {
        trimInPlace(line);
        if (line.empty() || line[0] == '#')
            continue;
        std::stringstream ss(line);
        std::string field;
        std::vector<double> vals;
        while (std::getline(ss, field, ',')) {
            trimInPlace(field);
            if (!field.empty())
                vals.push_back(std::stod(field));
        }
        if (vals.size() < 7)
            continue;
        EurocImuSample sample;
        sample.time_ns = static_cast<int64_t>(vals[0]);
        sample.wx = vals[1];
        sample.wy = vals[2];
        sample.wz = vals[3];
        sample.ax = vals[4];
        sample.ay = vals[5];
        sample.az = vals[6];
        imu_.push_back(sample);
    }
    return true;
}

std::string EurocDatasetReader::cam0ImagePath(const EurocImageStamp &stamp) const
{
    return root_ + "/cam0/data/" + stamp.filename;
}

std::string EurocDatasetReader::cam1ImagePath(const EurocImageStamp &stamp) const
{
    return root_ + "/cam1/data/" + stamp.filename;
}
