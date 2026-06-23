from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from launch_common import sim_time_launch_args, sim_time_params


def generate_launch_description():
    vins_share = get_package_share_directory('pht_vio_ros')
    default_config = os.path.join(
        vins_share, 'config', 'kitti_raw', 'kitti_10_03_config.yaml')
    rviz_config = os.path.join(vins_share, 'config', 'pht_vio_rviz_config.rviz')

    return LaunchDescription([
        DeclareLaunchArgument(
            'config',
            default_value=default_config,
            description='Path to VINS KITTI raw config YAML file'),
        DeclareLaunchArgument(
            'data_path',
            default_value='',
            description='Path to KITTI raw synced dataset folder'),
        DeclareLaunchArgument(
            'rviz',
            default_value='true',
            description='Launch RViz2 with VINS config'),
        *sim_time_launch_args(),
        Node(
            package='pht_vio_ros',
            executable='kitti_gps_test',
            name='pht_vio_estimator',
            arguments=[
                LaunchConfiguration('config'),
                LaunchConfiguration('data_path'),
            ],
            parameters=sim_time_params(),
            output='screen'),
        Node(
            package='pht_global_fusion_ros',
            executable='pht_global_fusion_node',
            name='pht_global_fusion',
            parameters=sim_time_params(),
            output='screen'),
        Node(
            package='rviz2',
            executable='rviz2',
            name='rvizvisualisation',
            arguments=['-d', rviz_config],
            parameters=sim_time_params(),
            output='log',
            condition=IfCondition(LaunchConfiguration('rviz'))),
    ])
