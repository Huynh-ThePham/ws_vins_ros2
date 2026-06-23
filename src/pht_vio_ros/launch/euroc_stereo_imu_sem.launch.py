from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.conditions import IfCondition
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from launch_common import sim_time_launch_args, sim_time_params


def generate_launch_description():
    vio_pkg = get_package_share_directory('pht_vio_ros')
    yolo_pkg = get_package_share_directory('yolo_dynamic_mask')
    default_config = os.path.join(vio_pkg, 'config', 'euroc', 'euroc_stereo_imu_sem_config.yaml')
    rviz_config = os.path.join(vio_pkg, 'config', 'pht_vio_rviz_config.rviz')

    return LaunchDescription([
        DeclareLaunchArgument(
            'config',
            default_value=default_config,
            description='Path to SAD-VINS stereo+IMU config YAML'),
        DeclareLaunchArgument(
            'enable_yolo',
            default_value='true',
            description='Launch YOLO dynamic mask node'),
        DeclareLaunchArgument(
            'yolo_device',
            default_value='auto',
            description='YOLO device: auto, cuda, or cpu'),
        DeclareLaunchArgument(
            'enable_loop',
            default_value='false',
            description='Enable visual loop closure node'),
        DeclareLaunchArgument(
            'rviz',
            default_value='true',
            description='Launch RViz2 with VINS config'),
        *sim_time_launch_args(),
        IncludeLaunchDescription(
            PythonLaunchDescriptionSource(
                os.path.join(yolo_pkg, 'launch', 'yolo_dynamic_mask.launch.py')),
            launch_arguments={
                'image_topic': '/cam0/image_raw',
                'mask_topic': '/dynamic_mask',
                'device': LaunchConfiguration('yolo_device'),
            }.items(),
            condition=IfCondition(LaunchConfiguration('enable_yolo'))),
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
