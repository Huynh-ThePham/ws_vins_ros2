from setuptools import find_packages, setup

package_name = 'yolo_dynamic_mask'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name + '/launch', ['launch/yolo_dynamic_mask.launch.py']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='Huynh The Pham',
    maintainer_email='thepham@example.com',
    description='YOLOv11 segmentation dynamic-object mask for SAD-VINS',
    license='GPL-3.0',
    entry_points={
        'console_scripts': [
            'mask_node = yolo_dynamic_mask.mask_node:main',
        ],
    },
)
