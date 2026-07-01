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
        self.declare_parameter('publish_debug', False)
        self.declare_parameter('half_precision', True)
        self.declare_parameter('skip_frames', 0)
        self.declare_parameter('keep_latest_only', True)

        self.model_path = self.get_parameter('model_path').get_parameter_value().string_value
        self.image_topic = self.get_parameter('image_topic').get_parameter_value().string_value
        self.mask_topic = self.get_parameter('mask_topic').get_parameter_value().string_value
        self.debug_topic = self.get_parameter('debug_topic').get_parameter_value().string_value
        self.conf_thres = self.get_parameter('conf_thres').get_parameter_value().double_value
        self.imgsz = int(self.get_parameter('imgsz').get_parameter_value().integer_value)
        self.dynamic_classes = list(
            self.get_parameter('dynamic_classes').get_parameter_value().integer_array_value
        )
        self.publish_debug = self.get_parameter('publish_debug').get_parameter_value().bool_value
        self.half_precision = self.get_parameter('half_precision').get_parameter_value().bool_value
        self.skip_frames = int(self.get_parameter('skip_frames').get_parameter_value().integer_value)
        self.keep_latest_only = self.get_parameter('keep_latest_only').get_parameter_value().bool_value

        device_param = self.get_parameter('device').get_parameter_value().string_value
        if device_param == 'auto':
            self.device = 'cuda' if torch.cuda.is_available() else 'cpu'
        else:
            self.device = device_param
        self.use_half = self.half_precision and str(self.device).startswith('cuda')

        self.get_logger().info(
            f'Loading {self.model_path} on {self.device} half={self.use_half} '
            f'skip_frames={self.skip_frames} classes={self.dynamic_classes}'
        )
        self.model = YOLO(self.model_path)
        self.model.predict(
            source=np.zeros((480, 640, 3), dtype=np.uint8),
            device=self.device,
            half=self.use_half,
            verbose=False,
        )

        self.bridge = CvBridge()
        self.total_time = 0.0
        self.frame_count = 0
        self.callback_count = 0
        self.last_mask = None
        self.busy = False

        qos_profile = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            history=HistoryPolicy.KEEP_LAST,
            depth=2 if self.keep_latest_only else 3,
        )
        self.sub = self.create_subscription(Image, self.image_topic, self.image_callback, qos_profile)
        self.mask_pub = self.create_publisher(Image, self.mask_topic, 10)
        if self.publish_debug:
            self.debug_pub = self.create_publisher(Image, self.debug_topic, 10)
        self.get_logger().info(
            f'YOLO dynamic mask ready: image={self.image_topic}, mask={self.mask_topic}'
        )

    def _to_bgr(self, msg):
        if msg.encoding in ('mono8', '8UC1'):
            gray = self.bridge.imgmsg_to_cv2(msg, desired_encoding='mono8')
            return cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
        return self.bridge.imgmsg_to_cv2(msg, desired_encoding='bgr8')

    def _publish_mask(self, msg, final_mask, debug_img=None):
        out_mask = self.bridge.cv2_to_imgmsg(final_mask, encoding='mono8')
        out_mask.header = msg.header
        self.mask_pub.publish(out_mask)
        self.last_mask = final_mask
        if self.publish_debug and debug_img is not None:
            out_dbg = self.bridge.cv2_to_imgmsg(debug_img, encoding='bgr8')
            out_dbg.header = msg.header
            self.debug_pub.publish(out_dbg)

    def image_callback(self, msg):
        if self.busy and self.keep_latest_only:
            return
        self.callback_count += 1
        if self.skip_frames > 0 and (self.callback_count - 1) % (self.skip_frames + 1) != 0:
            if self.last_mask is not None:
                self._publish_mask(msg, self.last_mask)
            return

        try:
            self.busy = True
            cv_image = self._to_bgr(msg)
            h, w = cv_image.shape[:2]
            start_time = time.perf_counter()

            result = self.model.predict(
                source=cv_image,
                conf=self.conf_thres,
                device=self.device,
                half=self.use_half,
                imgsz=self.imgsz,
                retina_masks=True,
                verbose=False,
                classes=self.dynamic_classes,
            )[0]

            final_mask = np.full((h, w), 255, dtype=np.uint8)
            debug_img = None
            if result.masks is not None and result.boxes.cls.numel() > 0:
                classes = result.boxes.cls.int()
                target = torch.tensor(self.dynamic_classes, device=classes.device)
                idx = torch.isin(classes, target)
                if idx.any():
                    masks = (result.masks.data > 0.5).float().unsqueeze(1)
                    masks = F.interpolate(masks, size=(h, w), mode='nearest').squeeze(1).bool()
                    obj_mask = torch.any(masks[idx], dim=0)
                    final_mask = (~obj_mask).byte().cpu().numpy() * 255
                if self.publish_debug:
                    debug_img = result.plot()

            self._publish_mask(msg, final_mask, debug_img)

            latency_ms = (time.perf_counter() - start_time) * 1000.0
            self.total_time += latency_ms
            self.frame_count += 1
            if self.frame_count % 30 == 0:
                self.get_logger().info(
                    f'YOLO avg latency: {self.total_time / self.frame_count:.2f} ms'
                )
        except Exception as exc:
            self.get_logger().error(f'YOLO mask error: {exc}')
        finally:
            self.busy = False


def main(args=None):
    rclpy.init(args=args)
    node = YoloDynamicMask()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()
