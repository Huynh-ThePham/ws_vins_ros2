/*******************************************************
 * Copyright (C) 2019, Aerial Robotics Group, Hong Kong University of Science and Technology
 *
 * VINS configuration loader.
 *******************************************************/

#include "parameters.h"

#include <algorithm>

static VinsConfig g_vins_config;

VinsConfig &vinsConfig()
{
    return g_vins_config;
}

void VinsConfig::reset()
{
    *this = VinsConfig{};
}

bool VinsConfig::loadFromYaml(const std::string &config_file)
{
    reset();

    FILE *fh = fopen(config_file.c_str(), "r");
    if (fh == NULL) {
        ROS_WARN("config_file dosen't exist; wrong config_file path");
        return false;
    }
    fclose(fh);

    cv::FileStorage fsSettings(config_file, cv::FileStorage::READ);
    if (!fsSettings.isOpened()) {
        std::cerr << "ERROR: Wrong path to settings" << std::endl;
        return false;
    }

    fsSettings["image0_topic"] >> image0_topic;
    fsSettings["image1_topic"] >> image1_topic;
    max_cnt = fsSettings["max_cnt"];
    min_dist = fsSettings["min_dist"];
    f_threshold = fsSettings["F_threshold"];
    show_track = fsSettings["show_track"];
    flow_back = fsSettings["flow_back"];

    cv::FileNode sem_enable_node = fsSettings["sem_enable"];
    if (!sem_enable_node.empty())
        sem_enable = static_cast<int>(sem_enable_node);
    cv::FileNode sem_mask_topic_node = fsSettings["sem_mask_topic"];
    if (!sem_mask_topic_node.empty())
        sem_mask_topic = static_cast<std::string>(sem_mask_topic_node);
    cv::FileNode sem_static_value_node = fsSettings["sem_static_value"];
    if (!sem_static_value_node.empty())
        sem_static_value = static_cast<int>(sem_static_value_node);
    if (!fsSettings["sem_geodf_fusion"].empty())
        sem_geodf_fusion = static_cast<int>(fsSettings["sem_geodf_fusion"]);
    if (!fsSettings["sem_activate_ratio"].empty())
        sem_activate_ratio = static_cast<double>(fsSettings["sem_activate_ratio"]);
    if (!fsSettings["sem_activate_ema"].empty())
        sem_activate_ema = static_cast<double>(fsSettings["sem_activate_ema"]);
    if (!fsSettings["sem_deactivate_frac"].empty())
        sem_deactivate_frac = static_cast<double>(fsSettings["sem_deactivate_frac"]);
    if (!fsSettings["sem_mask_gated"].empty())
        sem_mask_gated = static_cast<int>(fsSettings["sem_mask_gated"]);
    if (!fsSettings["sem_vote_frames"].empty())
        sem_vote_frames = static_cast<int>(fsSettings["sem_vote_frames"]);
    if (!fsSettings["sem_adaptive_policy"].empty())
        sem_adaptive_policy = static_cast<int>(fsSettings["sem_adaptive_policy"]);
    if (!fsSettings["sem_policy_dynamic_level"].empty())
        sem_policy_dynamic_level = static_cast<int>(fsSettings["sem_policy_dynamic_level"]);
    if (!fsSettings["sem_policy_burst_ratio"].empty())
        sem_policy_burst_ratio = static_cast<double>(fsSettings["sem_policy_burst_ratio"]);
    if (!fsSettings["sem_policy_strong_ratio"].empty())
        sem_policy_strong_ratio = static_cast<double>(fsSettings["sem_policy_strong_ratio"]);
    if (!fsSettings["sem_policy_hold_frames"].empty())
        sem_policy_hold_frames = static_cast<int>(fsSettings["sem_policy_hold_frames"]);
    if (!fsSettings["sem_policy_overlap_ratio"].empty())
        sem_policy_overlap_ratio = static_cast<double>(fsSettings["sem_policy_overlap_ratio"]);
    if (!fsSettings["sem_policy_overlap_ema"].empty())
        sem_policy_overlap_ema = static_cast<double>(fsSettings["sem_policy_overlap_ema"]);
    if (!fsSettings["sem_policy_min_geo_candidates"].empty())
        sem_policy_min_geo_candidates = static_cast<int>(fsSettings["sem_policy_min_geo_candidates"]);
    if (!fsSettings["sem_geodf_backend_weight"].empty())
        sem_geodf_backend_weight = static_cast<int>(fsSettings["sem_geodf_backend_weight"]);
    if (!fsSettings["sem_geodf_backend_min_weight"].empty())
        sem_geodf_backend_min_weight = static_cast<double>(fsSettings["sem_geodf_backend_min_weight"]);
    if (!fsSettings["sem_geodf_backend_semantic_weight"].empty())
        sem_geodf_backend_semantic_weight = static_cast<double>(fsSettings["sem_geodf_backend_semantic_weight"]);
    if (!fsSettings["sem_geodf_backend_geo_weight"].empty())
        sem_geodf_backend_geo_weight = static_cast<double>(fsSettings["sem_geodf_backend_geo_weight"]);
    if (!fsSettings["sem_geodf_backend_agree_weight"].empty())
        sem_geodf_backend_agree_weight = static_cast<double>(fsSettings["sem_geodf_backend_agree_weight"]);
    if (!fsSettings["sem_geodf_backend_recovery"].empty())
        sem_geodf_backend_recovery = static_cast<double>(fsSettings["sem_geodf_backend_recovery"]);
    if (!fsSettings["sem_geodf_rank_by_risk"].empty())
        sem_geodf_rank_by_risk = static_cast<int>(fsSettings["sem_geodf_rank_by_risk"]);
    sem_geodf_backend_min_weight = std::min(1.0, std::max(0.01, sem_geodf_backend_min_weight));
    sem_geodf_backend_semantic_weight = std::min(1.0, std::max(sem_geodf_backend_min_weight, sem_geodf_backend_semantic_weight));
    sem_geodf_backend_geo_weight = std::min(1.0, std::max(sem_geodf_backend_min_weight, sem_geodf_backend_geo_weight));
    sem_geodf_backend_agree_weight = std::min(1.0, std::max(sem_geodf_backend_min_weight, sem_geodf_backend_agree_weight));
    sem_geodf_backend_recovery = std::min(1.0, std::max(0.0, sem_geodf_backend_recovery));
    sem_geodf_rank_by_risk = sem_geodf_rank_by_risk ? 1 : 0;
    if (!fsSettings["sem_mask_max_age_ms"].empty())
        sem_mask_max_age_ms = static_cast<double>(fsSettings["sem_mask_max_age_ms"]);
    if (!fsSettings["sem_use_latest_mask"].empty())
        sem_use_latest_mask = static_cast<int>(fsSettings["sem_use_latest_mask"]);
    if (!fsSettings["sem_block_on_mask"].empty())
        sem_block_on_mask = static_cast<int>(fsSettings["sem_block_on_mask"]);
    multiple_thread = fsSettings["multiple_thread"];

    use_imu = fsSettings["imu"];
    printf("USE_IMU: %d\n", use_imu);
    if (use_imu) {
        fsSettings["imu_topic"] >> imu_topic;
        printf("IMU_TOPIC: %s\n", imu_topic.c_str());
        acc_n = fsSettings["acc_n"];
        acc_w = fsSettings["acc_w"];
        gyr_n = fsSettings["gyr_n"];
        gyr_w = fsSettings["gyr_w"];
        g.z() = fsSettings["g_norm"];
    }

    solver_time = fsSettings["max_solver_time"];
    num_iterations = fsSettings["max_num_iterations"];
    if (!fsSettings["visual_sigma_px"].empty())
        visual_sigma_px = static_cast<double>(fsSettings["visual_sigma_px"]);
    if (!fsSettings["visual_huber_delta"].empty())
        visual_huber_delta = static_cast<double>(fsSettings["visual_huber_delta"]);
    visual_sigma_px = std::min(20.0, std::max(0.1, visual_sigma_px));
    visual_huber_delta = std::min(20.0, std::max(0.1, visual_huber_delta));
    if (!fsSettings["visual_adaptive_quality"].empty())
        visual_adaptive_quality = static_cast<int>(fsSettings["visual_adaptive_quality"]);
    if (!fsSettings["visual_quality_min_weight"].empty())
        visual_quality_min_weight = static_cast<double>(fsSettings["visual_quality_min_weight"]);
    if (!fsSettings["visual_lk_error_scale"].empty())
        visual_lk_error_scale = static_cast<double>(fsSettings["visual_lk_error_scale"]);
    if (!fsSettings["visual_fb_error_scale"].empty())
        visual_fb_error_scale = static_cast<double>(fsSettings["visual_fb_error_scale"]);
    if (!fsSettings["visual_quality_full_age"].empty())
        visual_quality_full_age = static_cast<int>(fsSettings["visual_quality_full_age"]);
    if (!fsSettings["visual_adaptive_huber"].empty())
        visual_adaptive_huber = static_cast<int>(fsSettings["visual_adaptive_huber"]);
    if (!fsSettings["visual_huber_delta_min"].empty())
        visual_huber_delta_min = static_cast<double>(fsSettings["visual_huber_delta_min"]);
    if (!fsSettings["visual_huber_delta_max"].empty())
        visual_huber_delta_max = static_cast<double>(fsSettings["visual_huber_delta_max"]);
    if (!fsSettings["visual_huber_k"].empty())
        visual_huber_k = static_cast<double>(fsSettings["visual_huber_k"]);
    if (!fsSettings["visual_huber_ema"].empty())
        visual_huber_ema = static_cast<double>(fsSettings["visual_huber_ema"]);
    if (!fsSettings["visual_huber_min_samples"].empty())
        visual_huber_min_samples = static_cast<int>(fsSettings["visual_huber_min_samples"]);
    if (!fsSettings["imu_adaptive_covariance"].empty())
        imu_adaptive_covariance = static_cast<int>(fsSettings["imu_adaptive_covariance"]);
    if (!fsSettings["imu_gap_threshold_s"].empty())
        imu_gap_threshold_s = static_cast<double>(fsSettings["imu_gap_threshold_s"]);
    if (!fsSettings["imu_acc_saturation"].empty())
        imu_acc_saturation = static_cast<double>(fsSettings["imu_acc_saturation"]);
    if (!fsSettings["imu_gyr_saturation"].empty())
        imu_gyr_saturation = static_cast<double>(fsSettings["imu_gyr_saturation"]);
    if (!fsSettings["imu_gap_inflation_gain"].empty())
        imu_gap_inflation_gain = static_cast<double>(fsSettings["imu_gap_inflation_gain"]);
    if (!fsSettings["imu_saturation_inflation_gain"].empty())
        imu_saturation_inflation_gain = static_cast<double>(fsSettings["imu_saturation_inflation_gain"]);
    if (!fsSettings["imu_max_cov_inflation"].empty())
        imu_max_cov_inflation = static_cast<double>(fsSettings["imu_max_cov_inflation"]);
    if (!fsSettings["calibration_observability_gate"].empty())
        calibration_observability_gate =
            static_cast<int>(fsSettings["calibration_observability_gate"]);
    if (!fsSettings["calibration_min_speed"].empty())
        calibration_min_speed = static_cast<double>(fsSettings["calibration_min_speed"]);
    if (!fsSettings["calibration_min_parallax_px"].empty())
        calibration_min_parallax_px =
            static_cast<double>(fsSettings["calibration_min_parallax_px"]);
    if (!fsSettings["calibration_min_tracked_features"].empty())
        calibration_min_tracked_features =
            static_cast<int>(fsSettings["calibration_min_tracked_features"]);

    if (!fsSettings["sem_policy_overlap_metric"].empty())
        fsSettings["sem_policy_overlap_metric"] >> sem_policy_overlap_metric;
    if (!fsSettings["sem_policy_min_sem_candidates"].empty())
        sem_policy_min_sem_candidates =
            static_cast<int>(fsSettings["sem_policy_min_sem_candidates"]);
    if (!fsSettings["sem_policy_min_intersection"].empty())
        sem_policy_min_intersection =
            static_cast<int>(fsSettings["sem_policy_min_intersection"]);
    if (!fsSettings["sem_policy_overlap_support_saturation"].empty())
        sem_policy_overlap_support_saturation =
            static_cast<int>(fsSettings["sem_policy_overlap_support_saturation"]);
    if (!fsSettings["sem_policy_assist_hold_s"].empty())
        sem_policy_assist_hold_s = static_cast<double>(fsSettings["sem_policy_assist_hold_s"]);
    if (!fsSettings["sem_policy_strong_hold_s"].empty())
        sem_policy_strong_hold_s = static_cast<double>(fsSettings["sem_policy_strong_hold_s"]);
    if (!fsSettings["sem_policy_min_state_dwell_s"].empty())
        sem_policy_min_state_dwell_s =
            static_cast<double>(fsSettings["sem_policy_min_state_dwell_s"]);
    if (!fsSettings["sem_health_mask_saturation_ratio"].empty())
        sem_health_mask_saturation_ratio =
            static_cast<double>(fsSettings["sem_health_mask_saturation_ratio"]);
    if (!fsSettings["sem_health_min_semantic"].empty())
        sem_health_min_semantic = static_cast<double>(fsSettings["sem_health_min_semantic"]);
    if (!fsSettings["sem_health_min_geometric"].empty())
        sem_health_min_geometric = static_cast<double>(fsSettings["sem_health_min_geometric"]);
    if (!fsSettings["sem_health_min_observability"].empty())
        sem_health_min_observability =
            static_cast<double>(fsSettings["sem_health_min_observability"]);
    if (!fsSettings["sem_health_redundancy_target"].empty())
        sem_health_redundancy_target =
            static_cast<int>(fsSettings["sem_health_redundancy_target"]);
    if (!fsSettings["sem_health_parallax_target_px"].empty())
        sem_health_parallax_target_px =
            static_cast<double>(fsSettings["sem_health_parallax_target_px"]);
    if (!fsSettings["sem_health_min_tracks_for_hard_reject"].empty())
        sem_health_min_tracks_for_hard_reject =
            static_cast<int>(fsSettings["sem_health_min_tracks_for_hard_reject"]);
    if (!fsSettings["sem_lifecycle_enable"].empty())
        sem_lifecycle_enable = static_cast<int>(fsSettings["sem_lifecycle_enable"]);
    if (!fsSettings["sem_lifecycle_suspect_frames"].empty())
        sem_lifecycle_suspect_frames =
            static_cast<int>(fsSettings["sem_lifecycle_suspect_frames"]);
    if (!fsSettings["sem_lifecycle_downweight_frames"].empty())
        sem_lifecycle_downweight_frames =
            static_cast<int>(fsSettings["sem_lifecycle_downweight_frames"]);
    if (!fsSettings["sem_lifecycle_hard_reject_risk"].empty())
        sem_lifecycle_hard_reject_risk =
            static_cast<double>(fsSettings["sem_lifecycle_hard_reject_risk"]);
    if (!fsSettings["sem_lifecycle_require_agreement"].empty())
        sem_lifecycle_require_agreement =
            static_cast<int>(fsSettings["sem_lifecycle_require_agreement"]);
    if (!fsSettings["sem_lifecycle_recover_dwell_s"].empty())
        sem_lifecycle_recover_dwell_s =
            static_cast<double>(fsSettings["sem_lifecycle_recover_dwell_s"]);
    if (!fsSettings["sem_lifecycle_downweight_scale"].empty())
        sem_lifecycle_downweight_scale =
            static_cast<double>(fsSettings["sem_lifecycle_downweight_scale"]);
    if (!fsSettings["sem_adaptive_arbitration"].empty())
        sem_adaptive_arbitration = static_cast<int>(fsSettings["sem_adaptive_arbitration"]);
    if (!fsSettings["sem_arb_lambda0"].empty())
        sem_arb_lambda0 = static_cast<double>(fsSettings["sem_arb_lambda0"]);
    if (!fsSettings["sem_arb_lambda1"].empty())
        sem_arb_lambda1 = static_cast<double>(fsSettings["sem_arb_lambda1"]);
    if (!fsSettings["sem_arb_rd_downweight"].empty())
        sem_arb_rd_downweight = static_cast<double>(fsSettings["sem_arb_rd_downweight"]);
    if (!fsSettings["sem_arb_rd_hard"].empty())
        sem_arb_rd_hard = static_cast<double>(fsSettings["sem_arb_rd_hard"]);
    if (!fsSettings["sem_arb_min_expert_for_hard"].empty())
        sem_arb_min_expert_for_hard =
            static_cast<double>(fsSettings["sem_arb_min_expert_for_hard"]);
    if (!fsSettings["sem_arb_min_obs_for_hard"].empty())
        sem_arb_min_obs_for_hard = static_cast<double>(fsSettings["sem_arb_min_obs_for_hard"]);
    if (!fsSettings["sem_arb_geo_degenerate_floor"].empty())
        sem_arb_geo_degenerate_floor =
            static_cast<double>(fsSettings["sem_arb_geo_degenerate_floor"]);
    if (!fsSettings["sem_arb_geo_weak_scale"].empty())
        sem_arb_geo_weak_scale = static_cast<double>(fsSettings["sem_arb_geo_weak_scale"]);
    if (!fsSettings["sem_adaptive_arbitration_v2"].empty())
        sem_adaptive_arbitration_v2 = static_cast<int>(fsSettings["sem_adaptive_arbitration_v2"]);
    if (!fsSettings["sem_arb2_fusion_mode"].empty())
        sem_arb2_fusion_mode = static_cast<int>(fsSettings["sem_arb2_fusion_mode"]);
    if (!fsSettings["sem_arb2_geo_combination"].empty())
        sem_arb2_geo_combination = static_cast<int>(fsSettings["sem_arb2_geo_combination"]);
    if (!fsSettings["sem_arb2_min_weight"].empty())
        sem_arb2_min_weight = static_cast<double>(fsSettings["sem_arb2_min_weight"]);
    if (!fsSettings["sem_arb2_sat_start"].empty())
        sem_arb2_sat_start = static_cast<double>(fsSettings["sem_arb2_sat_start"]);
    if (!fsSettings["sem_arb2_sat_end"].empty())
        sem_arb2_sat_end = static_cast<double>(fsSettings["sem_arb2_sat_end"]);
    if (!fsSettings["sem_arb2_sat_floor"].empty())
        sem_arb2_sat_floor = static_cast<double>(fsSettings["sem_arb2_sat_floor"]);
    if (!fsSettings["sem_arb2_sem_authority_min"].empty())
        sem_arb2_sem_authority_min = static_cast<double>(fsSettings["sem_arb2_sem_authority_min"]);
    if (!fsSettings["sem_arb2_geo_authority_min"].empty())
        sem_arb2_geo_authority_min = static_cast<double>(fsSettings["sem_arb2_geo_authority_min"]);
    if (!fsSettings["sem_arb2_agreement_min"].empty())
        sem_arb2_agreement_min = static_cast<double>(fsSettings["sem_arb2_agreement_min"]);
    if (!fsSettings["sem_arb2_dynamic_threshold"].empty())
        sem_arb2_dynamic_threshold = static_cast<double>(fsSettings["sem_arb2_dynamic_threshold"]);
    if (!fsSettings["sem_arb2_static_threshold"].empty())
        sem_arb2_static_threshold = static_cast<double>(fsSettings["sem_arb2_static_threshold"]);
    if (!fsSettings["sem_arb2_downweight_threshold"].empty())
        sem_arb2_downweight_threshold = static_cast<double>(fsSettings["sem_arb2_downweight_threshold"]);
    if (!fsSettings["sem_arb2_hard_risk"].empty())
        sem_arb2_hard_risk = static_cast<double>(fsSettings["sem_arb2_hard_risk"]);
    if (!fsSettings["sem_arb2_hard_reliability"].empty())
        sem_arb2_hard_reliability = static_cast<double>(fsSettings["sem_arb2_hard_reliability"]);
    if (!fsSettings["sem_arb2_hard_persistence"].empty())
        sem_arb2_hard_persistence = static_cast<double>(fsSettings["sem_arb2_hard_persistence"]);
    if (!fsSettings["sem_arb2_hard_track_age"].empty())
        sem_arb2_hard_track_age = static_cast<double>(fsSettings["sem_arb2_hard_track_age"]);
    if (!fsSettings["sem_arb2_min_obs_for_hard"].empty())
        sem_arb2_min_obs_for_hard = static_cast<double>(fsSettings["sem_arb2_min_obs_for_hard"]);
    if (!fsSettings["sem_arb2_min_redundancy_for_hard"].empty())
        sem_arb2_min_redundancy_for_hard = static_cast<double>(fsSettings["sem_arb2_min_redundancy_for_hard"]);
    if (!fsSettings["sem_arb2_dynamic_smooth_lo"].empty())
        sem_arb2_dynamic_smooth_lo = static_cast<double>(fsSettings["sem_arb2_dynamic_smooth_lo"]);
    if (!fsSettings["sem_arb2_dynamic_smooth_hi"].empty())
        sem_arb2_dynamic_smooth_hi = static_cast<double>(fsSettings["sem_arb2_dynamic_smooth_hi"]);
    if (!fsSettings["sem_arb2_alpha_base"].empty())
        sem_arb2_alpha_base = static_cast<double>(fsSettings["sem_arb2_alpha_base"]);
    if (!fsSettings["sem_arb2_alpha_authority_gain"].empty())
        sem_arb2_alpha_authority_gain = static_cast<double>(fsSettings["sem_arb2_alpha_authority_gain"]);
    if (!fsSettings["sem_arb2_hard_dwell_frames"].empty())
        sem_arb2_hard_dwell_frames = static_cast<int>(fsSettings["sem_arb2_hard_dwell_frames"]);
    if (!fsSettings["sem_arb2_logodds_bias"].empty())
        sem_arb2_logodds_bias = static_cast<double>(fsSettings["sem_arb2_logodds_bias"]);
    if (!fsSettings["sem_arb2_logodds_beta_semantic"].empty())
        sem_arb2_logodds_beta_semantic = static_cast<double>(fsSettings["sem_arb2_logodds_beta_semantic"]);
    if (!fsSettings["sem_arb2_logodds_beta_geo"].empty())
        sem_arb2_logodds_beta_geo = static_cast<double>(fsSettings["sem_arb2_logodds_beta_geo"]);
    if (!fsSettings["sem_arb2_logodds_beta_agreement"].empty())
        sem_arb2_logodds_beta_agreement = static_cast<double>(fsSettings["sem_arb2_logodds_beta_agreement"]);

    if (!fsSettings["geodf_min_grid_occupancy"].empty())
        geodf_min_grid_occupancy = static_cast<double>(fsSettings["geodf_min_grid_occupancy"]);
    if (!fsSettings["geodf_min_median_parallax_px"].empty())
        geodf_min_median_parallax_px =
            static_cast<double>(fsSettings["geodf_min_median_parallax_px"]);
    if (!fsSettings["geodf_max_design_condition_number"].empty())
        geodf_max_design_condition_number =
            static_cast<double>(fsSettings["geodf_max_design_condition_number"]);
    if (!fsSettings["geodf_min_nullspace_gap"].empty())
        geodf_min_nullspace_gap =
            static_cast<double>(fsSettings["geodf_min_nullspace_gap"]);
    if (!fsSettings["geodf_min_ransac_inliers"].empty())
        geodf_min_ransac_inliers = static_cast<int>(fsSettings["geodf_min_ransac_inliers"]);
    if (!fsSettings["geodf_min_ransac_inlier_ratio"].empty())
        geodf_min_ransac_inlier_ratio =
            static_cast<double>(fsSettings["geodf_min_ransac_inlier_ratio"]);
    if (!fsSettings["geodf_max_mover_share"].empty())
        geodf_max_mover_share = static_cast<double>(fsSettings["geodf_max_mover_share"]);

    if (!fsSettings["stereo_validity_enable"].empty())
        stereo_validity_enable = static_cast<int>(fsSettings["stereo_validity_enable"]);
    if (!fsSettings["stereo_lr_cycle_max_px"].empty())
        stereo_lr_cycle_max_px = static_cast<double>(fsSettings["stereo_lr_cycle_max_px"]);
    if (!fsSettings["stereo_epipolar_max_px"].empty())
        stereo_epipolar_max_px = static_cast<double>(fsSettings["stereo_epipolar_max_px"]);
    if (!fsSettings["stereo_min_disparity_px"].empty())
        stereo_min_disparity_px = static_cast<double>(fsSettings["stereo_min_disparity_px"]);
    if (!fsSettings["stereo_max_disparity_px"].empty())
        stereo_max_disparity_px = static_cast<double>(fsSettings["stereo_max_disparity_px"]);
    if (!fsSettings["stereo_reprojection_max_px"].empty())
        stereo_reprojection_max_px =
            static_cast<double>(fsSettings["stereo_reprojection_max_px"]);
    if (!fsSettings["stereo_require_positive_depth"].empty())
        stereo_require_positive_depth =
            static_cast<int>(fsSettings["stereo_require_positive_depth"]);
    if (!fsSettings["stereo_contract_enable"].empty())
        stereo_contract_enable = static_cast<int>(fsSettings["stereo_contract_enable"]);
    if (!fsSettings["stereo_contract_require_calibration"].empty())
        stereo_contract_require_calibration =
            static_cast<int>(fsSettings["stereo_contract_require_calibration"]);
    if (!fsSettings["stereo_contract_allow_lk_fallback"].empty())
        stereo_contract_allow_lk_fallback =
            static_cast<int>(fsSettings["stereo_contract_allow_lk_fallback"]);

    if (!fsSettings["failure_detection_enable"].empty())
        failure_detection_enable = static_cast<int>(fsSettings["failure_detection_enable"]);
    if (!fsSettings["failure_max_acc_bias"].empty())
        failure_max_acc_bias = static_cast<double>(fsSettings["failure_max_acc_bias"]);
    if (!fsSettings["failure_max_gyro_bias"].empty())
        failure_max_gyro_bias = static_cast<double>(fsSettings["failure_max_gyro_bias"]);
    if (!fsSettings["failure_max_translation_step_m"].empty())
        failure_max_translation_step_m =
            static_cast<double>(fsSettings["failure_max_translation_step_m"]);
    if (!fsSettings["failure_max_rotation_step_deg"].empty())
        failure_max_rotation_step_deg =
            static_cast<double>(fsSettings["failure_max_rotation_step_deg"]);
    if (!fsSettings["failure_min_tracked_features"].empty())
        failure_min_tracked_features =
            static_cast<int>(fsSettings["failure_min_tracked_features"]);
    if (!fsSettings["failure_max_consecutive_low_feature_frames"].empty())
        failure_max_consecutive_low_feature_frames =
            static_cast<int>(fsSettings["failure_max_consecutive_low_feature_frames"]);

    visual_adaptive_quality = visual_adaptive_quality ? 1 : 0;
    visual_quality_min_weight = std::min(1.0, std::max(0.01, visual_quality_min_weight));
    visual_lk_error_scale = std::min(1000.0, std::max(0.1, visual_lk_error_scale));
    visual_fb_error_scale = std::min(20.0, std::max(0.05, visual_fb_error_scale));
    visual_quality_full_age = std::min(100, std::max(1, visual_quality_full_age));
    visual_adaptive_huber = visual_adaptive_huber ? 1 : 0;
    visual_huber_delta_min = std::min(20.0, std::max(0.1, visual_huber_delta_min));
    visual_huber_delta_max = std::min(20.0, std::max(visual_huber_delta_min, visual_huber_delta_max));
    visual_huber_k = std::min(10.0, std::max(0.1, visual_huber_k));
    visual_huber_ema = std::min(1.0, std::max(0.0, visual_huber_ema));
    visual_huber_min_samples = std::min(10000, std::max(4, visual_huber_min_samples));
    imu_adaptive_covariance = imu_adaptive_covariance ? 1 : 0;
    imu_gap_threshold_s = std::min(1.0, std::max(1e-4, imu_gap_threshold_s));
    imu_acc_saturation = std::min(10000.0, std::max(1.0, imu_acc_saturation));
    imu_gyr_saturation = std::min(10000.0, std::max(0.1, imu_gyr_saturation));
    imu_gap_inflation_gain = std::min(1000.0, std::max(0.0, imu_gap_inflation_gain));
    imu_saturation_inflation_gain =
        std::min(1000.0, std::max(0.0, imu_saturation_inflation_gain));
    imu_max_cov_inflation = std::min(1000.0, std::max(1.0, imu_max_cov_inflation));
    calibration_observability_gate = calibration_observability_gate ? 1 : 0;
    calibration_min_speed = std::min(100.0, std::max(0.0, calibration_min_speed));
    calibration_min_parallax_px =
        std::min(1000.0, std::max(0.0, calibration_min_parallax_px));
    calibration_min_tracked_features =
        std::min(NUM_OF_F, std::max(0, calibration_min_tracked_features));
    sem_policy_min_sem_candidates = std::max(0, sem_policy_min_sem_candidates);
    sem_policy_min_intersection = std::max(0, sem_policy_min_intersection);
    sem_policy_overlap_support_saturation =
        std::max(1, sem_policy_overlap_support_saturation);
    sem_policy_assist_hold_s = std::min(600.0, std::max(0.0, sem_policy_assist_hold_s));
    sem_policy_strong_hold_s = std::min(600.0, std::max(0.0, sem_policy_strong_hold_s));
    sem_policy_min_state_dwell_s =
        std::min(60.0, std::max(0.0, sem_policy_min_state_dwell_s));
    sem_health_mask_saturation_ratio =
        std::min(1.0, std::max(0.0, sem_health_mask_saturation_ratio));
    sem_health_min_semantic = std::min(1.0, std::max(0.0, sem_health_min_semantic));
    sem_health_min_geometric = std::min(1.0, std::max(0.0, sem_health_min_geometric));
    sem_health_min_observability = std::min(1.0, std::max(0.0, sem_health_min_observability));
    sem_health_redundancy_target = std::max(1, sem_health_redundancy_target);
    sem_health_parallax_target_px = std::max(1e-6, sem_health_parallax_target_px);
    sem_health_min_tracks_for_hard_reject =
        std::min(NUM_OF_F, std::max(0, sem_health_min_tracks_for_hard_reject));
    sem_lifecycle_enable = sem_lifecycle_enable ? 1 : 0;
    sem_lifecycle_suspect_frames = std::max(1, sem_lifecycle_suspect_frames);
    sem_lifecycle_downweight_frames =
        std::max(sem_lifecycle_suspect_frames, sem_lifecycle_downweight_frames);
    sem_lifecycle_hard_reject_risk =
        std::min(1.0, std::max(0.0, sem_lifecycle_hard_reject_risk));
    sem_lifecycle_require_agreement = sem_lifecycle_require_agreement ? 1 : 0;
    sem_lifecycle_recover_dwell_s =
        std::min(60.0, std::max(0.0, sem_lifecycle_recover_dwell_s));
    sem_lifecycle_downweight_scale =
        std::min(1.0, std::max(0.0, sem_lifecycle_downweight_scale));
    sem_adaptive_arbitration = sem_adaptive_arbitration ? 1 : 0;
    sem_arb_lambda0 = std::min(1.0, std::max(0.0, sem_arb_lambda0));
    sem_arb_lambda1 = std::min(1.0, std::max(0.0, sem_arb_lambda1));
    sem_arb_rd_downweight = std::min(1.0, std::max(0.0, sem_arb_rd_downweight));
    sem_arb_rd_hard = std::min(1.0, std::max(sem_arb_rd_downweight, sem_arb_rd_hard));
    sem_arb_min_expert_for_hard =
        std::min(1.0, std::max(0.0, sem_arb_min_expert_for_hard));
    sem_arb_min_obs_for_hard = std::min(1.0, std::max(0.0, sem_arb_min_obs_for_hard));
    sem_arb_geo_degenerate_floor =
        std::min(1.0, std::max(0.0, sem_arb_geo_degenerate_floor));
    sem_arb_geo_weak_scale = std::min(1.0, std::max(sem_arb_geo_degenerate_floor,
                                                    sem_arb_geo_weak_scale));
    sem_adaptive_arbitration_v2 = sem_adaptive_arbitration_v2 ? 1 : 0;
    // A resolved config may never enable both implementations at once.
    if (sem_adaptive_arbitration_v2)
        sem_adaptive_arbitration = 0;
    sem_arb2_fusion_mode = std::min(2, std::max(0, sem_arb2_fusion_mode));
    sem_arb2_geo_combination = std::min(2, std::max(0, sem_arb2_geo_combination));
    sem_arb2_min_weight = std::min(1.0, std::max(0.0, sem_arb2_min_weight));
    sem_arb2_sat_start = std::min(1.0, std::max(0.0, sem_arb2_sat_start));
    sem_arb2_sat_end = std::min(1.0, std::max(sem_arb2_sat_start + 1e-6, sem_arb2_sat_end));
    sem_arb2_sat_floor = std::min(1.0, std::max(0.0, sem_arb2_sat_floor));
    sem_arb2_sem_authority_min = std::min(1.0, std::max(0.0, sem_arb2_sem_authority_min));
    sem_arb2_geo_authority_min = std::min(1.0, std::max(0.0, sem_arb2_geo_authority_min));
    sem_arb2_agreement_min = std::min(1.0, std::max(0.0, sem_arb2_agreement_min));
    sem_arb2_dynamic_threshold = std::min(1.0, std::max(0.0, sem_arb2_dynamic_threshold));
    sem_arb2_static_threshold = std::min(sem_arb2_dynamic_threshold,
                                        std::max(0.0, sem_arb2_static_threshold));
    sem_arb2_downweight_threshold = std::min(1.0, std::max(0.0, sem_arb2_downweight_threshold));
    sem_arb2_hard_risk = std::min(1.0, std::max(sem_arb2_downweight_threshold, sem_arb2_hard_risk));
    sem_arb2_hard_reliability = std::min(1.0, std::max(0.0, sem_arb2_hard_reliability));
    sem_arb2_hard_persistence = std::min(1.0, std::max(0.0, sem_arb2_hard_persistence));
    sem_arb2_hard_track_age = std::min(1.0, std::max(0.0, sem_arb2_hard_track_age));
    sem_arb2_min_obs_for_hard = std::min(1.0, std::max(0.0, sem_arb2_min_obs_for_hard));
    sem_arb2_min_redundancy_for_hard = std::min(1.0, std::max(0.0, sem_arb2_min_redundancy_for_hard));
    sem_arb2_dynamic_smooth_lo = std::min(1.0, std::max(0.0, sem_arb2_dynamic_smooth_lo));
    sem_arb2_dynamic_smooth_hi = std::min(1.0, std::max(sem_arb2_dynamic_smooth_lo + 1e-6,
                                                       sem_arb2_dynamic_smooth_hi));
    sem_arb2_alpha_base = std::min(1.0, std::max(0.0, sem_arb2_alpha_base));
    sem_arb2_alpha_authority_gain = std::min(1.0, std::max(0.0, sem_arb2_alpha_authority_gain));
    sem_arb2_hard_dwell_frames = std::max(1, sem_arb2_hard_dwell_frames);
    geodf_min_grid_occupancy = std::min(1.0, std::max(0.0, geodf_min_grid_occupancy));
    geodf_min_median_parallax_px = std::max(0.0, geodf_min_median_parallax_px);
    geodf_max_design_condition_number = std::max(0.0, geodf_max_design_condition_number);
    geodf_min_nullspace_gap = std::max(0.0, geodf_min_nullspace_gap);
    geodf_min_ransac_inliers = std::max(0, geodf_min_ransac_inliers);
    geodf_min_ransac_inlier_ratio =
        std::min(1.0, std::max(0.0, geodf_min_ransac_inlier_ratio));
    geodf_max_mover_share = std::min(1.0, std::max(0.0, geodf_max_mover_share));
    stereo_validity_enable = stereo_validity_enable ? 1 : 0;
    stereo_lr_cycle_max_px = std::min(100.0, std::max(0.05, stereo_lr_cycle_max_px));
    stereo_epipolar_max_px = std::min(100.0, std::max(0.05, stereo_epipolar_max_px));
    stereo_min_disparity_px = std::max(0.0, stereo_min_disparity_px);
    stereo_max_disparity_px =
        std::max(stereo_min_disparity_px + 1e-6, stereo_max_disparity_px);
    stereo_reprojection_max_px = std::min(100.0, std::max(0.05, stereo_reprojection_max_px));
    stereo_require_positive_depth = stereo_require_positive_depth ? 1 : 0;
    stereo_contract_enable = stereo_contract_enable ? 1 : 0;
    stereo_contract_require_calibration = stereo_contract_require_calibration ? 1 : 0;
    stereo_contract_allow_lk_fallback = stereo_contract_allow_lk_fallback ? 1 : 0;
    failure_detection_enable = failure_detection_enable ? 1 : 0;
    failure_max_acc_bias = std::min(1000.0, std::max(0.01, failure_max_acc_bias));
    failure_max_gyro_bias = std::min(1000.0, std::max(0.001, failure_max_gyro_bias));
    failure_max_translation_step_m =
        std::min(1000.0, std::max(0.01, failure_max_translation_step_m));
    failure_max_rotation_step_deg =
        std::min(180.0, std::max(1.0, failure_max_rotation_step_deg));
    failure_min_tracked_features = std::min(NUM_OF_F, std::max(0, failure_min_tracked_features));
    failure_max_consecutive_low_feature_frames =
        std::min(10000, std::max(1, failure_max_consecutive_low_feature_frames));
    min_parallax = fsSettings["keyframe_parallax"];
    min_parallax = min_parallax / FOCAL_LENGTH;

    fsSettings["output_path"] >> output_folder;
    if (!output_folder.empty() && output_folder[0] == '~') {
        const char *home = getenv("HOME");
        if (home)
            output_folder = std::string(home) + output_folder.substr(1);
    }
    vins_result_path = output_folder + "/vio.csv";
    if (!output_folder.empty())
        failure_status_path = output_folder + "/failure_status.json";
    std::cout << "result path " << vins_result_path << std::endl;
    std::ofstream fout(vins_result_path, std::ios::out);
    fout.close();

    estimate_extrinsic = fsSettings["estimate_extrinsic"];
    if (estimate_extrinsic == 2) {
        ROS_WARN("have no prior about extrinsic param, calibrate extrinsic param");
        ric.push_back(Eigen::Matrix3d::Identity());
        tic.push_back(Eigen::Vector3d::Zero());
        ex_calib_result_path = output_folder + "/extrinsic_parameter.csv";
    } else {
        if (estimate_extrinsic == 1) {
            ROS_WARN(" Optimize extrinsic param around initial guess!");
            ex_calib_result_path = output_folder + "/extrinsic_parameter.csv";
        }
        if (estimate_extrinsic == 0)
            ROS_WARN(" fix extrinsic param ");

        cv::Mat cv_T;
        fsSettings["body_T_cam0"] >> cv_T;
        Eigen::Matrix4d T;
        cv::cv2eigen(cv_T, T);
        ric.push_back(T.block<3, 3>(0, 0));
        tic.push_back(T.block<3, 1>(0, 3));
    }

    num_of_cam = fsSettings["num_of_cam"];
    printf("camera number %d\n", num_of_cam);

    if (num_of_cam != 1 && num_of_cam != 2) {
        printf("num_of_cam should be 1 or 2\n");
        assert(0);
    }

    int pn = config_file.find_last_of('/');
    std::string configPath = config_file.substr(0, pn);

    std::string cam0Calib;
    fsSettings["cam0_calib"] >> cam0Calib;
    std::string cam0Path = configPath + "/" + cam0Calib;
    cam_names.push_back(cam0Path);

    if (num_of_cam == 2) {
        stereo = 1;
        std::string cam1Calib;
        fsSettings["cam1_calib"] >> cam1Calib;
        std::string cam1Path = configPath + "/" + cam1Calib;
        cam_names.push_back(cam1Path);

        cv::Mat cv_T;
        fsSettings["body_T_cam1"] >> cv_T;
        Eigen::Matrix4d T;
        cv::cv2eigen(cv_T, T);
        ric.push_back(T.block<3, 3>(0, 0));
        tic.push_back(T.block<3, 1>(0, 3));
    }

    init_depth = 5.0;
    bias_acc_threshold = 0.1;
    bias_gyr_threshold = 0.1;

    td = fsSettings["td"];
    estimate_td = fsSettings["estimate_td"];
    if (estimate_td)
        ROS_INFO("Unsynchronized sensors, online estimate time offset, initial td: %f", td);
    else
        ROS_INFO("Synchronized sensors, fix time offset: %f", td);

    row = fsSettings["image_height"];
    col = fsSettings["image_width"];
    ROS_INFO("ROW: %d COL: %d ", row, col);

    if (!use_imu) {
        estimate_extrinsic = 0;
        estimate_td = 0;
        printf("no imu, fix extrinsic param; no time offset calibration\n");
    }

    geodf_enable = 0;
    geodf_hard_reject = 1;
    geodf_ransac_th_px = 1.0;
    geodf_sampson_th = 3.0;
    geodf_min_track_cnt = 2;
    geodf_min_feature_num = 40;
    geodf_max_reject_ratio = 0.4;
    geodf_ratio_guard = 1;
    geodf_debug = 0;
    geodf_dump_features = 0;
    geodf_adaptive = 0;
    geodf_activate_ratio = 0.12;
    geodf_activate_ema = 0.15;
    geodf_deactivate_frac = 0.6;
    geodf_auto_rho = 0;
    geodf_auto_mult = 1.8;
    geodf_auto_margin = 0.05;
    geodf_activate_ratio_min = 0.08;
    geodf_activate_ratio_max = 0.40;
    geodf_auto_floor_down = 0.02;
    geodf_auto_floor_up = 0.004;
    geodf_vote_frames = 1;
    geodf_warmup_frames = 0;
    geodf_stereo_check = 0;
    geodf_stereo_sampson_th = 3.0;
    geodf_stereo_floor_max = 0.0;
    if (!fsSettings["geodf_enable"].empty())
        geodf_enable = (int)fsSettings["geodf_enable"];
    if (!fsSettings["geodf_hard_reject"].empty())
        geodf_hard_reject = (int)fsSettings["geodf_hard_reject"];
    if (!fsSettings["geodf_ransac_th_px"].empty())
        geodf_ransac_th_px = (double)fsSettings["geodf_ransac_th_px"];
    if (!fsSettings["geodf_sampson_th"].empty())
        geodf_sampson_th = (double)fsSettings["geodf_sampson_th"];
    else if (!fsSettings["geodf_tau"].empty())
        geodf_sampson_th = (double)fsSettings["geodf_tau"];
    if (!fsSettings["geodf_min_track_cnt"].empty())
        geodf_min_track_cnt = (int)fsSettings["geodf_min_track_cnt"];
    if (!fsSettings["geodf_min_feature_num"].empty())
        geodf_min_feature_num = (int)fsSettings["geodf_min_feature_num"];
    if (!fsSettings["geodf_reject_ratio_max"].empty())
        geodf_max_reject_ratio = (double)fsSettings["geodf_reject_ratio_max"];
    else if (!fsSettings["geodf_max_reject_ratio"].empty())
        geodf_max_reject_ratio = (double)fsSettings["geodf_max_reject_ratio"];
    if (!fsSettings["geodf_ratio_guard"].empty())
        geodf_ratio_guard = (int)fsSettings["geodf_ratio_guard"];
    if (!fsSettings["geodf_debug"].empty())
        geodf_debug = (int)fsSettings["geodf_debug"];
    if (!fsSettings["geodf_dump_features"].empty())
        geodf_dump_features = (int)fsSettings["geodf_dump_features"];
    if (!fsSettings["geodf_adaptive"].empty())
        geodf_adaptive = (int)fsSettings["geodf_adaptive"];
    if (!fsSettings["geodf_activate_ratio"].empty())
        geodf_activate_ratio = (double)fsSettings["geodf_activate_ratio"];
    if (!fsSettings["geodf_activate_ema"].empty())
        geodf_activate_ema = (double)fsSettings["geodf_activate_ema"];
    if (!fsSettings["geodf_deactivate_frac"].empty())
        geodf_deactivate_frac = (double)fsSettings["geodf_deactivate_frac"];
    if (!fsSettings["geodf_auto_rho"].empty())
        geodf_auto_rho = (int)fsSettings["geodf_auto_rho"];
    if (!fsSettings["geodf_auto_mult"].empty())
        geodf_auto_mult = (double)fsSettings["geodf_auto_mult"];
    if (!fsSettings["geodf_auto_margin"].empty())
        geodf_auto_margin = (double)fsSettings["geodf_auto_margin"];
    if (!fsSettings["geodf_activate_ratio_min"].empty())
        geodf_activate_ratio_min = (double)fsSettings["geodf_activate_ratio_min"];
    if (!fsSettings["geodf_activate_ratio_max"].empty())
        geodf_activate_ratio_max = (double)fsSettings["geodf_activate_ratio_max"];
    if (!fsSettings["geodf_auto_floor_down"].empty())
        geodf_auto_floor_down = (double)fsSettings["geodf_auto_floor_down"];
    if (!fsSettings["geodf_auto_floor_up"].empty())
        geodf_auto_floor_up = (double)fsSettings["geodf_auto_floor_up"];
    if (!fsSettings["geodf_vote_frames"].empty())
        geodf_vote_frames = (int)fsSettings["geodf_vote_frames"];
    if (!fsSettings["geodf_warmup_frames"].empty())
        geodf_warmup_frames = (int)fsSettings["geodf_warmup_frames"];
    if (!fsSettings["geodf_stereo_check"].empty())
        geodf_stereo_check = (int)fsSettings["geodf_stereo_check"];
    if (!fsSettings["geodf_stereo_sampson_th"].empty())
        geodf_stereo_sampson_th = (double)fsSettings["geodf_stereo_sampson_th"];
    if (!fsSettings["geodf_stereo_floor_max"].empty())
        geodf_stereo_floor_max = (double)fsSettings["geodf_stereo_floor_max"];

    if (geodf_enable) {
        geodf_stats_path = output_folder + "/geo_df_stats.csv";
        std::ofstream geo_stats(geodf_stats_path, std::ios::out);
        geo_stats << "timestamp_ns,tracks_before,scored,ransac_outliers,sampson_above_th,"
                     "candidates,rejected,reject_ratio,tracks_after,"
                     "mean_sampson,median_sampson,max_sampson,guard_triggered,guard_capped,"
                     "activation_signal,frame_active,geo_ms,rho_on,outlier_floor,stereo_added,confirmed\n";
        geo_stats.close();
        if (geodf_dump_features) {
            geodf_feat_path = output_folder + "/geo_df_features.csv";
            std::ofstream feat(geodf_feat_path, std::ios::out);
            feat << "timestamp_ns,feature_id,u,v,sampson,ransac_outlier,rejected\n";
            feat.close();
        }
        ROS_INFO_STREAM("GeoDF-VINS-Hard enabled: sampson_th=" << geodf_sampson_th
                        << " ransac_th_px=" << geodf_ransac_th_px
                        << " max_reject_ratio=" << geodf_max_reject_ratio
                        << " ratio_guard=" << geodf_ratio_guard
                        << " hard_reject=" << geodf_hard_reject
                        << " dump_features=" << geodf_dump_features
                        << " adaptive=" << geodf_adaptive
                        << " activate_ratio=" << geodf_activate_ratio
                        << " activate_ema=" << geodf_activate_ema
                        << " deactivate_frac=" << geodf_deactivate_frac
                        << " auto_rho=" << geodf_auto_rho
                        << " auto_mult=" << geodf_auto_mult
                        << " auto_margin=" << geodf_auto_margin
                        << " vote_frames=" << geodf_vote_frames
                        << " warmup_frames=" << geodf_warmup_frames
                        << " stereo_check=" << geodf_stereo_check
                        << " stereo_sampson_th=" << geodf_stereo_sampson_th
                        << " stereo_floor_max=" << geodf_stereo_floor_max);
    }

    if (sem_enable) {
        sem_stats_path = output_folder + "/sem_stats.csv";
        std::ofstream sem_stats(sem_stats_path, std::ios::out);
        sem_stats << "timestamp_ns,tracks_before,rejected,reject_ratio,tracks_after,"
                     "mask_available,dynamic_pixel_ratio,sem_candidates,sem_confirmed\n";
        sem_stats.close();
        ROS_INFO("SAD-VINS semantic mask enabled, topic: %s", sem_mask_topic.c_str());
    }

    if (sem_enable && geodf_enable && sem_geodf_fusion) {
        sem_geodf_stats_path = output_folder + "/sem_geodf_stats.csv";
        std::ofstream fusion_stats(sem_geodf_stats_path, std::ios::out);
        fusion_stats << "timestamp_ns,tracks_before,sem_scene_active,geo_frame_active,"
                        "sem_mask_applied,sem_candidates,sem_confirmed,geo_candidates,"
                        "fused_candidates,rejected,reject_ratio,tracks_after,mask_available,"
                        "sem_mask_trusted,dynamic_pixel_ratio,sem_mask_lag_ms,"
                        "sem_activation_ema,geo_activation_ema,"
                        "sem_policy_state,sem_policy_hold,sem_geo_overlap,"
                        "sem_geo_overlap_ema,sem_policy_hard_reject,"
                        "sem_policy_trigger_burst,sem_policy_trigger_strong,"
                        "sem_policy_trigger_overlap,weighted_tracks,mean_backend_weight,"
                        "geo_valid,geo_raw_candidates,geo_overlap_pool,"
                        "min_backend_weight,mean_backend_target,"
                        // P1.12: weighting acts on survivors of the shared reject
                        // budget, so pre-guard and post-guard are logged apart.
                        "weighted_candidates_pre_guard,weighted_survivors_post_guard,"
                        "rejected_weighted_tracks,mean_target_weight_pre_guard,"
                        "mean_applied_weight_post_guard,min_survivor_weight,"
                        // P1.1 overlap support, P1.3 health, P1.4 lifecycle,
                        // P1.7 geometry degeneracy.
                        "overlap_support,overlap_has_support,"
                        "health_semantic,health_geometric,health_observability,"
                        "lifecycle_trusted,lifecycle_suspect,lifecycle_downweighted,"
                        "lifecycle_rejected,lifecycle_recovering,"
                        "lifecycle_downgraded_rejections,lifecycle_blocked_health,"
                        "lifecycle_blocked_observability,lifecycle_blocked_redundancy,"
                        "geometry_health,geometry_cause,geometry_conditioning,"
                        "median_parallax_px,grid_occupancy,"
                        // Phase 3.5 authority/action and separated-weight telemetry.
                        "arb2_q_s,arb2_q_g,arb2_semantic_authoritative,"
                        "arb2_geodf_authoritative,arb2_joint_authoritative,"
                        "arb2_no_authoritative,arb2_disagreement,arb2_keep,"
                        "arb2_downweight,arb2_quarantine,arb2_hard_reject,"
                        "arb2_mean_q_m,arb2_mean_dynamic_weight,arb2_mean_final_weight,"
                        "arb2_mean_expert_agreement,arb2_lifecycle_trusted,"
                        "arb2_lifecycle_suspect,arb2_lifecycle_disagreement,"
                        "arb2_lifecycle_downweighted,arb2_lifecycle_quarantined,"
                        "arb2_lifecycle_rejected,arb2_lifecycle_recovering,"
                        "arb2_hard_blocked_dwell,arb2_hard_blocked_budget\n";
        fusion_stats.close();
    }

    if (num_of_cam == 2 && stereo_validity_enable && !output_folder.empty()) {
        stereo_stats_path = output_folder + "/stereo_stats.csv";
        std::ofstream stereo_stats(stereo_stats_path, std::ios::out);
        stereo_stats << "timestamp_ns,stereo_match_total,stereo_lk_failed,"
                        "stereo_border_failed,stereo_lr_cycle_failed,"
                        "stereo_epipolar_failed,stereo_wrong_disparity_sign,"
                        "stereo_disparity_range_failed,stereo_negative_depth,"
                        "stereo_reprojection_failed,stereo_valid_total,"
                        "mean_disparity_px,mean_depth_m,mean_epipolar_px\n";
        stereo_stats.close();
        ROS_INFO("Stereo validity contract enabled (lr_cycle<=%.2fpx, epipolar<=%.2fpx, "
                 "reproj<=%.2fpx, disparity in [%.2f, %.1f]px, positive_depth=%d)",
                 stereo_lr_cycle_max_px, stereo_epipolar_max_px, stereo_reprojection_max_px,
                 stereo_min_disparity_px, stereo_max_disparity_px,
                 stereo_require_positive_depth);
    }

    if (sem_enable && geodf_enable && sem_geodf_fusion) {
        ROS_INFO_STREAM("Semantic–GeoDF fusion enabled (scene-gated OR reject, adaptive_policy="
                        << sem_adaptive_policy
                        << ", backend_weight=" << sem_geodf_backend_weight
                        << ", risk_rank=" << sem_geodf_rank_by_risk
                        << ", adaptive_arbitration=" << sem_adaptive_arbitration
                        << ", adaptive_arbitration_v2=" << sem_adaptive_arbitration_v2
                        << ", arb2_fusion=" << sem_arb2_fusion_mode
                        << ", arb2_geo=" << sem_arb2_geo_combination << ")");
    }

    if (visual_adaptive_quality || visual_adaptive_huber || imu_adaptive_covariance) {
        adaptive_factor_stats_path = output_folder + "/adaptive_factor_stats.csv";
        std::ofstream adaptive_stats(adaptive_factor_stats_path, std::ios::out);
        adaptive_stats << "timestamp_ns,visual_huber_delta,visual_samples,"
                          "median_whitened_visual_norm,mean_visual_factor_weight,"
                          "min_visual_factor_weight,imu_mean_noise_inflation,"
                          "imu_max_noise_inflation\n";
        adaptive_stats.close();
        ROS_INFO_STREAM("Adaptive factor confidence enabled (visual_quality="
                        << visual_adaptive_quality
                        << ", visual_huber=" << visual_adaptive_huber
                        << ", imu_covariance=" << imu_adaptive_covariance << ")");
    }

    fsSettings.release();
    return true;
}
