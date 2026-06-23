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
    pkg_share = get_package_share_directory('pht_vio_ros')
    default_config = os.path.join(
        pkg_share, 'config', 'euroc', 'euroc_stereo_config.yaml')
    rviz_config = os.path.join(pkg_share, 'config', 'pht_vio_rviz_config.rviz')

    return LaunchDescription([
        DeclareLaunchArgument(
            'config',
            default_value=default_config,
            description='Path to VINS config YAML file'),
        DeclareLaunchArgument(
            'enable_loop',
            default_value='false',
            description='Enable visual loop closure node'),
        DeclareLaunchArgument(
            'rviz',
            default_value='true',
            description='Launch RViz2 with VINS config'),
        *sim_time_launch_args(),
        Node(
            package='pht_vio_ros',
            executable='pht_vio_node',
            name='pht_vio_estimator',
            arguments=[LaunchConfiguration('config')],
            parameters=sim_time_params(),
            output='screen'),
        Node(
            package='pht_loop_closure_ros',
            executable='pht_loop_closure_node',
            name='pht_loop_closure_ros',
            arguments=[LaunchConfiguration('config')],
            parameters=sim_time_params(),
            output='screen',
            condition=IfCondition(LaunchConfiguration('enable_loop'))),
        Node(
            package='rviz2',
            executable='rviz2',
            name='rvizvisualisation',
            arguments=['-d', rviz_config],
            parameters=sim_time_params(),
            output='log',
            condition=IfCondition(LaunchConfiguration('rviz'))),
    ])
