from launch import LaunchDescription
from launch_ros.actions import Node
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from launch_common import sim_time_launch_args, sim_time_params


def generate_launch_description():
    return LaunchDescription([
        *sim_time_launch_args(),
        Node(
            package='pht_global_fusion_ros',
            executable='pht_global_fusion_node',
            name='pht_global_fusion',
            parameters=sim_time_params(),
            output='screen'),
    ])
