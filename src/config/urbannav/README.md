# UrbanNav (HK) — stereo + IMU for PHT VIO

Dataset root (symlink): `data/UrbanNav` → `/media/theph/Data1/Research/dataset/UrbanNav`

| Alias | Sequence folder | Bag |
|-------|-----------------|-----|
| `medium` | `UrbanNav-HK-Medium-Urban-1` | `ros/UrbanNav-HK_TST-20210517_sensors.bag` |
| `deep` | `UrbanNav-HK-Deep-Urban-1` | `ros/UrbanNav-HK_Whampoa-20210521_sensors.bag` |
| `harsh` | `UrbanNav-HK-Harsh-Urban-1` | `ros/UrbanNav-HK_Mongkok-20210518_sensors.bag` |

## Topics (from sensors bag)

- `/zed2/camera/left/image_raw`
- `/zed2/camera/right/image_raw`
- `/imu/data` (Xsens, ~400 Hz)

## Extrinsics

`body_T_cam*` = `inv(LEFT/RIGHT_CAMERA_T_IMU) · R_x(π)` so the camera frame is the
optical convention VINS expects (x right, y down, z forward). Same correction
validated on this rig in the degrade_graph UrbanNav Stage A configs.

## Prepare

```bash
# Convert cam+imu to ROS 2 (optional --max-sec for a short smoke bag)
bash scripts/urbannav_prepare.sh medium
bash scripts/urbannav_prepare.sh medium --max-sec 60

# Run stereo-IMU smoke
bash scripts/run_urbannav_smoke.sh medium
```
