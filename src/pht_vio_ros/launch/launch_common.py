"""Shared launch arguments for rosbag / sim-time workflows."""
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration


def sim_time_launch_args():
    return [
        DeclareLaunchArgument(
            'use_sim_time',
            default_value='true',
            description='Use /clock from rosbag (ros2 bag play --clock). Set false for live sensors.',
        ),
    ]


def sim_time_params():
    return [{'use_sim_time': LaunchConfiguration('use_sim_time')}]
