from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from launch_common import sim_time_launch_args, sim_time_params


def generate_launch_description():
    rviz_config = os.path.join(
        get_package_share_directory('pht_vio_ros'),
        'config',
        'pht_vio_rviz_config.rviz',
    )

    return LaunchDescription([
        *sim_time_launch_args(),
        Node(
            package='rviz2',
            executable='rviz2',
            name='rvizvisualisation',
            output='log',
            arguments=['-d', rviz_config],
            parameters=sim_time_params(),
        ),
    ])
