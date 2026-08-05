"""YOLOv11 segmentation node: publish a static/dynamic pixel mask for the VINS front-end.

Three defects from the upgrade plan are fixed here.

P1.8 A reused mask must keep its ORIGINAL timestamp. The node used to republish
     `last_mask` under the *new* image's header, so a stale mask looked fresh and the
     estimator's freshness check (sem_mask_max_age_ms) could not do its job. The mask
     message is now stored whole and republished untouched, and diagnostics say
     explicitly that it was reused.

P1.9 `keep_latest_only` was a `busy` flag in the subscription callback, which drops
     frames but is not a latest-frame queue: whichever frame happened to arrive while
     the model was busy was lost, even if a newer one never came. There is now a real
     worker: the callback only overwrites `latest_msg`, and the worker always picks up
     the most recent frame, so no backlog accumulates and no frame is dropped
     unnecessarily.

P1.10 The model is identified by a manifest with a sha256. A publication run refuses
     to start on an unverified or auto-downloaded model.
"""

import hashlib
import json
import threading
import time
from pathlib import Path

import cv2
import numpy as np
import rclpy
import torch
import torch.nn.functional as F
from cv_bridge import CvBridge
from rclpy.node import Node
from rclpy.qos import HistoryPolicy, QoSProfile, ReliabilityPolicy
from sensor_msgs.msg import Image
from std_msgs.msg import Bool, Float32, Header
from ultralytics import YOLO


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


class YoloDynamicMask(Node):
    def __init__(self):
        super().__init__('yolo_dynamic_mask')
        self.declare_parameter('model_path', 'yolo11n-seg.pt')
        self.declare_parameter('model_manifest', '')
        self.declare_parameter('require_verified_model', False)
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
        self.model_manifest_path = \
            self.get_parameter('model_manifest').get_parameter_value().string_value
        self.require_verified_model = \
            self.get_parameter('require_verified_model').get_parameter_value().bool_value
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

        self._verify_model()

        self.get_logger().info(
            f'Loading {self.model_path} on {self.device} half={self.use_half} '
            f'skip_frames={self.skip_frames} classes={self.dynamic_classes}'
        )
        self.model = YOLO(self.model_path)
        # Warm up so the first real frame is not charged the lazy-init cost, which
        # would otherwise show up as a mask-age spike in the runtime table.
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
        self.reused_count = 0
        self.dropped_count = 0

        # P1.8: the whole mask MESSAGE is kept, header included, so a reuse cannot
        # accidentally acquire a fresh stamp.
        self.last_mask_msg = None

        # P1.9: a real latest-frame handoff. The callback never blocks and never runs
        # inference; it only replaces the pending frame.
        self.latest_lock = threading.Lock()
        self.latest_msg = None
        self.worker_event = threading.Event()
        self.shutdown_event = threading.Event()

        qos_profile = QoSProfile(
            reliability=ReliabilityPolicy.RELIABLE,
            history=HistoryPolicy.KEEP_LAST,
            depth=1 if self.keep_latest_only else 5,
        )
        self.sub = self.create_subscription(Image, self.image_topic, self.image_callback, qos_profile)
        self.mask_pub = self.create_publisher(Image, self.mask_topic, 10)
        # Diagnostics required by the plan, so mask age and reuse are measurable from
        # the bag rather than inferred.
        self.source_stamp_pub = self.create_publisher(Header, f'{self.mask_topic}/source_stamp', 10)
        self.finish_stamp_pub = self.create_publisher(
            Header, f'{self.mask_topic}/inference_finish_stamp', 10)
        self.reused_pub = self.create_publisher(Bool, f'{self.mask_topic}/reused', 10)
        self.latency_pub = self.create_publisher(Float32, f'{self.mask_topic}/model_latency_ms', 10)
        if self.publish_debug:
            self.debug_pub = self.create_publisher(Image, self.debug_topic, 10)

        self.worker_thread = threading.Thread(target=self._worker, daemon=True,
                                              name='yolo_inference')
        self.worker_thread.start()

        self.get_logger().info(
            f'YOLO dynamic mask ready: image={self.image_topic}, mask={self.mask_topic}, '
            f'latest_only={self.keep_latest_only}'
        )

    # ------------------------------------------------------------------ P1.10 ---
    def _verify_model(self):
        """Refuse an unidentified model when a publication run asks for verification."""
        manifest_path = Path(self.model_manifest_path) if self.model_manifest_path else None
        if manifest_path is None or not manifest_path.is_file():
            message = (f'no model manifest ({self.model_manifest_path or "unset"}); the '
                       f'segmentation model is unidentified')
            if self.require_verified_model:
                raise RuntimeError(
                    f'{message}. A publication run must not use an unverified model. '
                    f'Write models/model_manifest.json and pass model_manifest:=<path>.')
            self.get_logger().warning(f'{message} (not required for this run)')
            return

        manifest = json.loads(manifest_path.read_text())
        model_file = Path(self.model_path)
        if not model_file.is_file():
            candidate = manifest_path.parent / manifest.get('file', '')
            if candidate.is_file():
                model_file = candidate
                self.model_path = str(candidate)
        if not model_file.is_file():
            raise RuntimeError(
                f'model file not found: {self.model_path}. Automatic download is refused '
                f'for a verified run; place the file recorded in {manifest_path} instead.')

        expected = manifest.get('sha256')
        actual = sha256_file(model_file)
        if not expected:
            message = f'{manifest_path} records no sha256 for {model_file.name}'
            if self.require_verified_model:
                raise RuntimeError(message)
            self.get_logger().warning(message)
        elif actual != expected:
            raise RuntimeError(
                f'model hash mismatch for {model_file}: got {actual[:16]}, manifest says '
                f'{str(expected)[:16]}. Refusing to run: a different model would silently '
                f'change every semantic result.')
        else:
            self.get_logger().info(f'model verified: {model_file.name} sha256={actual[:16]}')

        # Config recorded in the manifest wins, so a run cannot drift from what the
        # artifact says it used.
        for key, attr in (('confidence_threshold', 'conf_thres'),
                          ('image_size', 'imgsz'),
                          ('dynamic_classes', 'dynamic_classes')):
            if key not in manifest:
                continue
            current = getattr(self, attr)
            declared = manifest[key]
            if isinstance(current, list):
                declared = list(declared)
            if current != declared:
                self.get_logger().warning(
                    f'{key}: manifest says {declared}, launch says {current}; using the '
                    f'manifest so the run matches its recorded provenance')
                setattr(self, attr, declared)

    # ------------------------------------------------------------------- P1.9 ---
    def image_callback(self, msg):
        """Only ever replaces the pending frame. No inference, no blocking."""
        self.callback_count += 1
        with self.latest_lock:
            if self.latest_msg is not None:
                # A frame is being displaced by a newer one. Counted so the runtime
                # table can report the real drop rate instead of assuming 1:1.
                self.dropped_count += 1
            self.latest_msg = msg
        self.worker_event.set()

    def _worker(self):
        while not self.shutdown_event.is_set():
            if not self.worker_event.wait(timeout=0.1):
                continue
            self.worker_event.clear()
            while True:
                with self.latest_lock:
                    msg = self.latest_msg
                    self.latest_msg = None
                if msg is None:
                    break
                try:
                    self._process(msg)
                except Exception as exc:  # a bad frame must not kill the worker
                    self.get_logger().error(f'YOLO mask error: {exc}')
                if not self.keep_latest_only:
                    break

    def _process(self, msg):
        self.frame_count += 1
        # Frame skipping republishes the previous mask WITH ITS ORIGINAL STAMP (P1.8).
        if self.skip_frames > 0 and (self.frame_count - 1) % (self.skip_frames + 1) != 0:
            if self.last_mask_msg is not None:
                self._republish_last_mask()
            return

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

        latency_ms = (time.perf_counter() - start_time) * 1000.0
        self._publish_fresh_mask(msg, final_mask, latency_ms, debug_img)

        self.total_time += latency_ms
        if self.frame_count % 30 == 0:
            processed = max(1, self.frame_count)
            self.get_logger().info(
                f'YOLO avg latency: {self.total_time / processed:.2f} ms, '
                f'reused={self.reused_count}, displaced={self.dropped_count}, '
                f'callbacks={self.callback_count}'
            )

    def _to_bgr(self, msg):
        if msg.encoding in ('mono8', '8UC1'):
            gray = self.bridge.imgmsg_to_cv2(msg, desired_encoding='mono8')
            return cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
        return self.bridge.imgmsg_to_cv2(msg, desired_encoding='bgr8')

    def _publish_fresh_mask(self, msg, final_mask, latency_ms, debug_img=None):
        out_mask = self.bridge.cv2_to_imgmsg(final_mask, encoding='mono8')
        # The mask carries the stamp of the IMAGE IT WAS COMPUTED FROM, which is what
        # makes the consumer's freshness check meaningful.
        out_mask.header = msg.header
        self.mask_pub.publish(out_mask)
        self.last_mask_msg = out_mask

        self.source_stamp_pub.publish(msg.header)
        finish = Header()
        finish.stamp = self.get_clock().now().to_msg()
        finish.frame_id = msg.header.frame_id
        self.finish_stamp_pub.publish(finish)
        self.reused_pub.publish(Bool(data=False))
        self.latency_pub.publish(Float32(data=float(latency_ms)))

        if self.publish_debug and debug_img is not None:
            out_dbg = self.bridge.cv2_to_imgmsg(debug_img, encoding='bgr8')
            out_dbg.header = msg.header
            self.debug_pub.publish(out_dbg)

    def _republish_last_mask(self):
        """P1.8: republish the stored message UNCHANGED.

        Rewriting header.stamp here is what made a stale mask look fresh and defeated
        sem_mask_max_age_ms in the estimator.
        """
        self.reused_count += 1
        self.mask_pub.publish(self.last_mask_msg)
        self.source_stamp_pub.publish(self.last_mask_msg.header)
        self.reused_pub.publish(Bool(data=True))

    def destroy_node(self):
        self.shutdown_event.set()
        self.worker_event.set()
        if self.worker_thread.is_alive():
            self.worker_thread.join(timeout=2.0)
        return super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = YoloDynamicMask()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()
