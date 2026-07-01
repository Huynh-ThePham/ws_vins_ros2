import time

import cv2
import numpy as np
import rclpy
import torch
import torch.nn.functional as F
from cv_bridge import CvBridge
from rclpy.node import Node
from rclpy.qos import HistoryPolicy, QoSProfile, ReliabilityPolicy
from sensor_msgs.msg import Image
from ultralytics import YOLO


class YoloDynamicMask(Node):
    """YOLOv11 segmentation node: publish static/dynamic pixel mask for VINS front-end."""

    def __init__(self):
        super().__init__('yolo_dynamic_mask')
        self.declare_parameter('model_path', 'yolo11n-seg.pt')
        self.declare_parameter('image_topic', '/cam0/image_raw')
        self.declare_parameter('mask_topic', '/dynamic_mask')
        self.declare_parameter('debug_topic', '/yolo/debug_image')
        self.declare_parameter('conf_thres', 0.4)
        self.declare_parameter('device', 'cuda')
        self.declare_parameter('imgsz', 480)
        self.declare_parameter('dynamic_classes', [0, 2, 3, 5, 7])
        self.declare_parameter('process_every_n', 1)
        self.declare_parameter('propagate_skipped_masks', True)

        self.model_path = self.get_parameter('model_path').get_parameter_value().string_value
        self.image_topic = self.get_parameter('image_topic').get_parameter_value().string_value
        self.mask_topic = self.get_parameter('mask_topic').get_parameter_value().string_value
        self.debug_topic = self.get_parameter('debug_topic').get_parameter_value().string_value
        self.conf_thres = self.get_parameter('conf_thres').get_parameter_value().double_value
        self.imgsz = int(self.get_parameter('imgsz').get_parameter_value().integer_value)
        self.process_every_n = max(
            1, int(self.get_parameter('process_every_n').get_parameter_value().integer_value)
        )
        self.propagate_skipped_masks = (
            self.get_parameter('propagate_skipped_masks').get_parameter_value().bool_value
        )
        self.dynamic_classes = list(
            self.get_parameter('dynamic_classes').get_parameter_value().integer_array_value
        )

        device_param = self.get_parameter('device').get_parameter_value().string_value
        if device_param == 'auto':
            self.device = 'cuda' if torch.cuda.is_available() else 'cpu'
        else:
            self.device = device_param

        self.get_logger().info(
            f'Loading {self.model_path} on {self.device}, classes={self.dynamic_classes}'
        )
        self.model = YOLO(self.model_path)
        self.model.predict(
            source=np.zeros((480, 640, 3), dtype=np.uint8),
            device=self.device,
            verbose=False,
        )

        self.bridge = CvBridge()
        self.total_time = 0.0
        self.frame_count = 0
        self.skipped_count = 0
        self.prev_gray = None
        self.prev_mask = None

        qos_profile = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            history=HistoryPolicy.KEEP_LAST,
            depth=3,
        )
        self.sub = self.create_subscription(Image, self.image_topic, self.image_callback, qos_profile)
        self.mask_pub = self.create_publisher(Image, self.mask_topic, 10)
        self.debug_pub = self.create_publisher(Image, self.debug_topic, 10)
        self.get_logger().info(
            f'YOLO dynamic mask ready: image={self.image_topic}, mask={self.mask_topic}'
        )

    def _to_bgr(self, msg):
        if msg.encoding in ('mono8', '8UC1'):
            gray = self.bridge.imgmsg_to_cv2(msg, desired_encoding='mono8')
            return cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
        return self.bridge.imgmsg_to_cv2(msg, desired_encoding='bgr8')

    def image_callback(self, msg):
        try:
            cv_image = self._to_bgr(msg)
            h, w = cv_image.shape[:2]
            gray = cv2.cvtColor(cv_image, cv2.COLOR_BGR2GRAY)
            start_time = time.perf_counter()

            if (
                self.process_every_n > 1
                and self.frame_count > 0
                and self.frame_count % self.process_every_n != 0
                and self.prev_mask is not None
            ):
                final_mask = self.prev_mask
                if self.propagate_skipped_masks and self.prev_gray is not None:
                    flow = cv2.calcOpticalFlowFarneback(
                        self.prev_gray, gray, None, 0.5, 3, 15, 3, 5, 1.2, 0
                    )
                    h, w = gray.shape[:2]
                    grid_x, grid_y = np.meshgrid(
                        np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32)
                    )
                    map_x = grid_x - flow[:, :, 0]
                    map_y = grid_y - flow[:, :, 1]
                    final_mask = cv2.remap(
                        self.prev_mask, map_x, map_y, cv2.INTER_NEAREST,
                        borderMode=cv2.BORDER_CONSTANT, borderValue=255
                    )
                self.skipped_count += 1
            else:
                result = self.model.predict(
                    source=cv_image,
                    conf=self.conf_thres,
                    device=self.device,
                    imgsz=self.imgsz,
                    retina_masks=True,
                    verbose=False,
                    classes=self.dynamic_classes,
                )[0]

                final_mask = np.full((h, w), 255, dtype=np.uint8)
                if result.masks is not None and result.boxes.cls.numel() > 0:
                    classes = result.boxes.cls.int()
                    target = torch.tensor(self.dynamic_classes, device=classes.device)
                    idx = torch.isin(classes, target)
                    if idx.any():
                        masks = (result.masks.data > 0.5).float().unsqueeze(1)
                        masks = F.interpolate(masks, size=(h, w), mode='nearest').squeeze(1).bool()
                        obj_mask = torch.any(masks[idx], dim=0)
                        final_mask = (~obj_mask).byte().cpu().numpy() * 255

            out_mask = self.bridge.cv2_to_imgmsg(final_mask, encoding='mono8')
            out_mask.header = msg.header
            self.mask_pub.publish(out_mask)

            if 'result' in locals():
                debug_img = result.plot()
                out_dbg = self.bridge.cv2_to_imgmsg(debug_img, encoding='bgr8')
                out_dbg.header = msg.header
                self.debug_pub.publish(out_dbg)

            latency_ms = (time.perf_counter() - start_time) * 1000.0
            self.total_time += latency_ms
            self.frame_count += 1
            self.prev_gray = gray
            self.prev_mask = final_mask
            if self.frame_count % 30 == 0:
                self.get_logger().info(
                    f'YOLO avg latency: {self.total_time / self.frame_count:.2f} ms '
                    f'(skipped={self.skipped_count})'
                )
        except Exception as exc:
            self.get_logger().error(f'YOLO mask error: {exc}')


def main(args=None):
    rclpy.init(args=args)
    node = YoloDynamicMask()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()
