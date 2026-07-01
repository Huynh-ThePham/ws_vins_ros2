from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument('model_path', default_value='yolo11n-seg.pt'),
        DeclareLaunchArgument('image_topic', default_value='/cam0/image_raw'),
        DeclareLaunchArgument('mask_topic', default_value='/dynamic_mask'),
        DeclareLaunchArgument('conf_thres', default_value='0.4'),
        DeclareLaunchArgument('device', default_value='auto'),
        DeclareLaunchArgument('imgsz', default_value='480'),
        DeclareLaunchArgument('publish_debug', default_value='false'),
        DeclareLaunchArgument('half_precision', default_value='true'),
        DeclareLaunchArgument('skip_frames', default_value='0'),
        DeclareLaunchArgument('keep_latest_only', default_value='true'),
        Node(
            package='yolo_dynamic_mask',
            executable='mask_node',
            name='yolo_dynamic_mask',
            output='screen',
            parameters=[{
                'model_path': LaunchConfiguration('model_path'),
                'image_topic': LaunchConfiguration('image_topic'),
                'mask_topic': LaunchConfiguration('mask_topic'),
                'conf_thres': LaunchConfiguration('conf_thres'),
                'device': LaunchConfiguration('device'),
                'imgsz': LaunchConfiguration('imgsz'),
                'publish_debug': LaunchConfiguration('publish_debug'),
                'half_precision': LaunchConfiguration('half_precision'),
                'skip_frames': LaunchConfiguration('skip_frames'),
                'keep_latest_only': LaunchConfiguration('keep_latest_only'),
                'dynamic_classes': [0, 2, 3, 5, 7],
            }],
        ),
    ])
