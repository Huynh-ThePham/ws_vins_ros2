#pragma once

#include <cstdio>
#include <cassert>
#include <sstream>

// Portable logging for algorithm modules (no ROS dependency).
// ROS wrapper packages may redefine these macros before including core headers.

#ifndef PHT_LOG_DEBUG
#define PHT_LOG_DEBUG(...) ((void)0)
#endif

#ifndef PHT_LOG_INFO
#define PHT_LOG_INFO(...) do { \
    std::fprintf(stdout, __VA_ARGS__); \
    std::fprintf(stdout, "\n"); \
} while (0)
#endif

#ifndef PHT_LOG_WARN
#define PHT_LOG_WARN(...) do { \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fprintf(stderr, "\n"); \
} while (0)
#endif

#ifndef PHT_LOG_ERROR
#define PHT_LOG_ERROR(...) do { \
    std::fprintf(stderr, __VA_ARGS__); \
    std::fprintf(stderr, "\n"); \
} while (0)
#endif

#ifndef ROS_DEBUG
#define ROS_DEBUG(...) PHT_LOG_DEBUG(__VA_ARGS__)
#endif

#ifndef ROS_INFO
#define ROS_INFO(...) PHT_LOG_INFO(__VA_ARGS__)
#endif

#ifndef ROS_WARN
#define ROS_WARN(...) PHT_LOG_WARN(__VA_ARGS__)
#endif

#ifndef ROS_ERROR
#define ROS_ERROR(...) PHT_LOG_ERROR(__VA_ARGS__)
#endif

#define ROS_DEBUG_STREAM(args) do { \
    std::stringstream _vins_log_ss; \
    _vins_log_ss << args; \
    PHT_LOG_DEBUG("%s", _vins_log_ss.str().c_str()); \
} while (0)

#define ROS_INFO_STREAM(args) do { \
    std::stringstream _vins_log_ss; \
    _vins_log_ss << args; \
    PHT_LOG_INFO("%s", _vins_log_ss.str().c_str()); \
} while (0)

#define ROS_WARN_STREAM(args) do { \
    std::stringstream _vins_log_ss; \
    _vins_log_ss << args; \
    PHT_LOG_WARN("%s", _vins_log_ss.str().c_str()); \
} while (0)

#define ROS_ERROR_STREAM(args) do { \
    std::stringstream _vins_log_ss; \
    _vins_log_ss << args; \
    PHT_LOG_ERROR("%s", _vins_log_ss.str().c_str()); \
} while (0)

#define ROS_BREAK() assert(false)
