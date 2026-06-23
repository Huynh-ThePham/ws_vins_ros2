# help for checking input parameters.
rosrun pht_camera_models Calibrations --help

# example pinhole model.
rosrun pht_camera_models Calibrations -w 12 -h 8 -s 80 -i calibrationdata --camera-model pinhole

# example mei model.
rosrun pht_camera_models Calibrations -w 12 -h 8 -s 80 -i calibrationdata --camera-model mei
