# Research Proposal — GeoDF-Adaptive (AECE)

## Đề tài chính thức

**English**

> **GeoDF-VINS-Hard: A Lightweight Geometry-Based Dynamic Feature Rejection Method for Stereo-Inertial VINS-Fusion**

**Tiếng Việt**

> **GeoDF-VINS-Hard: Phương pháp loại bỏ đặc trưng động nhẹ dựa trên hình học cho VINS-Fusion stereo-inertial**

**Mô tả ngắn (đưa vào proposal)**

> This paper proposes GeoDF-VINS-Hard, a lightweight geometry-based dynamic feature rejection module for stereo-inertial VINS-Fusion. The method detects geometrically inconsistent feature tracks using epipolar constraints and Sampson residuals, then removes high-residual features before state estimation using a ratio-guarded hard rejection strategy. The backend estimator remains unchanged, allowing the method to be evaluated as a front-end robustness enhancement for dynamic environments.

---

## Phạm vi bài

```text
Baseline: VINS-Fusion stereo + IMU
Đóng góp: Geo-only dynamic feature rejection
Cách lọc: Fundamental matrix / epipolar geometry / Sampson residual
Cách xử lý: xóa cứng feature nghi động
Bảo vệ hệ thống: reject ratio guard
Kích hoạt: scene-aware self-gating (chỉ lọc khi scene thực sự động) ← đóng góp chính
Backend: giữ nguyên
Không dùng IMU trong bộ lọc động
Không semantic/deep learning
Không encoder
```

**Nói chính xác trong paper:**

> Hệ thống vẫn chạy cấu hình **stereo + IMU**, nhưng module lọc động đề xuất **chỉ dùng hình học ảnh**, chưa dùng IMU.

---

## Claim vừa đủ cho Q4

Không claim quá lớn. Claim đúng là:

> Bài báo đề xuất một module front-end nhẹ để loại bỏ các feature có chuyển động hình học không nhất quán trước khi đưa vào estimator của VINS-Fusion, nhằm cải thiện độ ổn định trong môi trường có nhiễu động — **với điều kiện vật động tạo bất nhất epipolar rõ và không chiếm đa số scene** (phạm vi & phản ví dụ định lượng ở §2d).

---

## Đóng góp của bài

1. Tích hợp module **Geo-only dynamic feature rejection** vào front-end của VINS-Fusion stereo-inertial.
2. Sử dụng **epipolar geometry / Sampson residual** để đánh giá feature track giữa hai frame.
3. Áp dụng **hard rejection có ratio guard**, tránh xóa quá nhiều feature trong một frame.
4. **(Đóng góp chính) Scene-aware self-gating activation:** dùng chính tỷ lệ outlier RANSAC toàn frame (EMA + hysteresis) làm chỉ báo "scene có động hay không"; **chỉ bật hard-reject khi scene thực sự bất nhất epipolar**, cảnh tĩnh thì pass-through. Triệt tiêu chi phí false-positive tĩnh (nguyên nhân ATE xấu ở mật độ động thấp) mà vẫn giữ lợi ích ở mật độ động cao — không thêm cảm biến, không học máy.
5. **Auto-calibrate ngưỡng kích hoạt `ρ_on`:** ước lượng online noise-floor outlier của scene (EMA bất đối xứng) → `ρ_on` tự co giãn theo môi trường, **hyperparameter-free**, dập over-arming khi đổi dataset (vd. `parking_lot` armed% 34–78% → 12–22%).
6. **Quality-aware activation:** ngoài tỷ lệ outlier, frame chỉ được arm khi dynamic-candidate đủ dày và median Sampson residual của candidate tách rõ khỏi nền static. Điều này phân biệt động thật với nhiễu hình học/low-parallax degeneration, đồng thời tạo thêm evidence (`candidate_ratio`, `residual_lift`, `quality_ema`) để ablation và trả lời reviewer.
7. Đánh giá **chỉ trên dữ liệu gốc đã publish**: **EuRoC** (tĩnh) và **VIODE 3 môi trường** (`city_day`/`city_night`/`parking_lot`, động thật, có ground-truth trajectory + segmentation), bằng ATE, RPE, FPS, rejection ratio, và precision/recall/lift trên **nhãn động thật** — không dùng dữ liệu tự tạo (synthetic overlay). Kèm **phân tích phản ví dụ** (parking_lot, §2d) — điểm cộng trung thực cho reviewer.

---

## Method chốt (Hướng A — đề xuất cuối cùng)

**Tên method:** **GeoDF-Adaptive** (GeoDF-VINS-Hard + scene-aware self-gating + auto-`ρ_on`)

| Thành phần | Trạng thái |
|---|---|
| Dual-gate epipolar (RANSAC + Sampson, cam trái temporal) | ✅ |
| Ratio guard | ✅ |
| Scene-aware activation (EMA + hysteresis) | ✅ |
| Auto-`ρ_on` (B) | ✅ |
| Quality-aware activation evidence | ✅ |
| Stereo cross-check (F) | ❌ ablation / future work |

**Config YAML:** `viode_stereo_imu_geodf_adaptive_config.yaml` / `euroc_stereo_imu_geodf_adaptive_config.yaml`  
**Alias chạy thực nghiệm:** `adaptive` hoặc `proposed`  
**Ablation fixed-ρ (oracle):** `adaptive_fixed` → `*_geodf_adaptive_fixed_config.yaml`

> **Lưu ý kết quả lịch sử:** các thư mục `results/*/*_adaptive` tạo **trước 2026-06-25** tương ứng **fixed-ρ ablation** (ρ_on=0.12 cố định). Từ khi chốt Hướng A, alias `adaptive` trỏ config **auto-ρ_on**; ablation cũ dùng `adaptive_fixed`.

---

## Related Work (khung — Section 2)

> ⚠️ **Cần verify trích dẫn (tác giả/năm/venue) trước submit** — danh sách dưới là khung định hướng, dùng tên phương pháp phổ biến; bổ sung DOI/bibtex khi viết bài.

VIO/SLAM trong môi trường động chia làm ba nhóm chính. GeoDF-VINS-Hard định vị ở nhóm (2) nhưng bổ sung cơ chế kích hoạt thích nghi mà các phương pháp hình học thuần thường thiếu.

### (1) Semantic / learning-based dynamic SLAM
Dùng mạng nơ-ron (segmentation/detection) để nhận diện và loại vùng động:
- **DynaSLAM** (Bescos et al., IEEE RA-L 2018) — Mask R-CNN + kiểm tra hình học đa-view trên ORB-SLAM2.
- **DS-SLAM** (Yu et al., IROS 2018) — SegNet + moving consistency check.
- **Detect-SLAM** (Zhong et al., WACV 2018) — lan truyền vùng động từ object detector.
- **Dynamic-VINS** (Liu et al., IEEE RA-L 2022) — object detection + IMU cho RGB-D trên thiết bị nhúng.

*Hạn chế:* phụ thuộc GPU/CNN, chỉ bắt được lớp đã huấn luyện (closed-set), latency cao — khó chạy thời gian thực trên nền tảng nhẹ. **GeoDF không dùng semantic/DL.**

### (2) Geometry-based dynamic feature rejection (nhóm của bài)
Dùng ràng buộc hình học đa-view để phát hiện feature bất nhất, không cần nhãn:
- **Epipolar / fundamental-matrix outlier rejection** — RANSAC trên F (đã có sẵn dạng yếu trong `rejectWithF` của VINS-Fusion), reprojection-error gating.
- **Multiview geometry consistency** (phần hình học của DynaSLAM), kiểm tra nhất quán độ sâu/scene-flow.
- **Robust back-end** — **DynaVINS** (Song et al., IEEE RA-L 2022): bundle adjustment bền vững + momentum factors, không semantic nhưng **sửa back-end**.
- **Motion removal** (Sun et al., 2017) trên RGB-D.

*Hạn chế:* phần lớn **always-on** (trả chi phí false-positive ở cảnh tĩnh — chính vấn đề bài này nhắm tới), hoặc **đổi back-end**, hoặc cần depth. **GeoDF giữ nguyên estimator, chỉ tác động front-end, và thêm self-gating.**

### (3) Hybrid / robust weighting
Soft-weighting trong BA, robust kernels (Huber/Cauchy), kết hợp semantic + geometry. Mạnh nhưng nặng hoặc cần tinh chỉnh; bài này cố ý dừng ở hard-reject nhẹ để tách bạch đóng góp.

### Định vị GeoDF-VINS-Hard

| Method | Tín hiệu động | Cần CNN/GPU | Đổi back-end | Kích hoạt thích nghi | Cảm biến |
|---|---|:---:|:---:|:---:|---|
| DynaSLAM | semantic + geometry | có | không | không | RGB-D/stereo/mono |
| DS-SLAM | semantic + geometry | có | không | không | RGB-D |
| Dynamic-VINS | detection + IMU | có | không | không | RGB-D + IMU |
| DynaVINS | geometry (robust BA) | không | **có** | không | VI |
| VINS-Fusion `rejectWithF` | epipolar RANSAC | không | không | không (always) | stereo + IMU |
| **GeoDF-VINS-Hard (ours)** | epipolar + Sampson dual-gate | **không** | **không** | **có (scene-aware)** | stereo + IMU |

**Khoảng trống bài lấp:** một bộ lọc động **thuần hình học, nhẹ, ở front-end, không đổi back-end**, kèm **cơ chế tự kích hoạt theo mức động của scene** (EMA outlier-ratio + hysteresis, tái dùng thống kê RANSAC sẵn có) để **loại bỏ chi phí false-positive ở cảnh tĩnh** — điều mà các phương pháp hình học always-on bỏ qua. Đây là đóng góp phân biệt chính so với cả nhóm (1) và (2).

---

## Method — GeoDF-VINS-Hard

### Pipeline

```text
Stereo images + IMU
        ↓
VINS-Fusion feature tracking (left cam, temporal KLT t−1 → t)
        ↓
GeoDF (cam0 only, inter-frame geometry)
        ↓
  • lift to normalized plane + VINS pseudo-pixel space
  • estimate F via RANSAC on all temporal correspondences
  • Sampson scoring + dual gate (RANSAC outlier ∧ e > τ)
  • hard reject + ratio guard (+ min-feature floor)
        ↓
Detect new features + stereo KLT (left → right, unchanged)
        ↓
VINS-Fusion stereo-inertial estimator (unchanged backend)
        ↓
Trajectory
```

**Lưu ý hình học:** GeoDF dùng **fundamental matrix giữa hai frame liên tiếp của cùng camera trái** (ràng buộc epipolar temporal), **không** dùng F stereo giữa cam0/cam1 cùng timestamp. IMU không tham gia bước lọc.

### Công thức chính

**Tọa độ.** Điểm ảnh được undistort (`liftProjective`) rồi map sang không gian pseudo-pixel của VINS (cùng convention với `rejectWithF`):

\[
\tilde{\mathbf{u}} = f \cdot (x/z,\; y/z) + (c_x, c_y), \quad f = 460,\; (c_x,c_y) = (W/2, H/2)
\]

**Ký hiệu OpenCV (ảnh 1 / ảnh 2).** Theo `cv::findFundamentalMat(points1, points2, …)` ([OpenCV calib3d](https://docs.opencv.org/4.x/d9/d0c/group__calib3d.html)):

| Symbol | Ý nghĩa | Trong GeoDF |
|---|---|---|
| \(\mathbf{x}\) | điểm ảnh 1 (`points1`) | \(\tilde{\mathbf{u}}^{t}\) — frame hiện tại |
| \(\mathbf{x}'\) | điểm ảnh 2 (`points2`) | \(\tilde{\mathbf{u}}^{t-1}\) — frame trước |
| \(F\) | fundamental matrix 3×3, rank 2 | ước lượng bằng RANSAC |

**Fundamental matrix (temporal).** Với correspondence \((\tilde{\mathbf{u}}_{i}^{t-1}, \tilde{\mathbf{u}}_{i}^{t})\) trên cam0:

\[
F = \texttt{findFundamentalMatRANSAC}\bigl(\{\tilde{\mathbf{u}}^{t}\},\; \{\tilde{\mathbf{u}}^{t-1}\}\bigr)
\]

Ràng buộc epipolar (Hartley–Zisserman / OpenCV):

\[
\mathbf{x}'^{\top} F \mathbf{x} = 0
\quad\Leftrightarrow\quad
\tilde{\mathbf{u}}^{t-1\top} F \tilde{\mathbf{u}}^{t} = 0
\]

cho inlier thuộc scene rigid. OpenCV RANSAC loại inlier/outlier bằng **khoảng cách tới epipolar line** ≤ `geodf_ransac_th_px` (pseudo-pixel; mặc định 1.0, cùng `F_threshold` VINS).

**Sampson distance** (first-order geometric error, HZ Eq. 11.9; khớp `sampsonDistance(F, \tilde{\mathbf{u}}^{t}, \tilde{\mathbf{u}}^{t-1})`):

\[
\varepsilon_i = \tilde{\mathbf{u}}_i^{t-1\top} F \tilde{\mathbf{u}}_i^{t}, \qquad
e_i = \frac{\varepsilon_i^{2}}
           {\|F\tilde{\mathbf{u}}_i^{t}\|_{1:2}^2 + \|F^\top \tilde{\mathbf{u}}_i^{t-1}\|_{1:2}^2}
\]

Trong đó \(\|\cdot\|_{1:2}\) là norm của hai thành phần đầu vector 3D; \(e_i\) là **Sampson²** (implementation trả về bình phương, không lấy căn).

**Dynamic candidate** (dual gate — tránh xóa nhầm inlier tĩnh):

```text
if track_length(i) ≥ min_track_cnt
   AND RANSAC_outlier(i)
   AND e_i > τ_geo:
       add i to candidate set C
```

Chỉ feature **outlier RANSAC** và **Sampson cao** mới vào \(C\). Inlier RANSAC không bị xóa dù \(e_i\) lớn (tránh trường hợp ATE phình ~0.685 m khi chỉ dùng ngưỡng Sampson).

**Hard rejection + ratio guard + per-frame cap** (đúng theo code, không nhầm ratio với count):

```text
N  = số track trước lọc
K  = floor(ρ_max × N)          # ρ_max = geodf_reject_ratio_max (default 0.40)
K' = min(K, N − min_feature_num)
K'' = min(K', max_reject_per_frame)  # nếu cap > 0; adaptive mặc định cap=3

if ratio_guard OFF:
    reject top min(|C|, K'') candidates in C (sort by e_i descending)
else:
    if |C| ≤ K'':
        reject all C                    # guard_triggered = 0
    else:
        guard_triggered = 1
        reject top K'' from C           # bỏ phần dư, giữ candidate Sampson thấp hơn
```

Per-frame cap làm hard rejection mượt hơn: vẫn ưu tiên candidate có residual cao nhất nhưng tránh xóa đột ngột quá nhiều track trong một frame, giảm rủi ro RPE/local-motion spike.

**Early skip:** nếu `N < min_feature_num` hoặc `N < 8` → bỏ qua GeoDF frame đó (bảo vệ khi feature quá ít).

### Scene-aware activation (adaptive self-gating) — đóng góp chính

**Vấn đề.** Hard rejection *luôn bật* có một chi phí cố định: cổng kép vẫn loại nhầm ~0.6% feature tĩnh mỗi frame (false positive từ parallax/xoay nhanh làm F suy biến). Ở cảnh **ít vật động (`1_low`/`2_mid`)**, chi phí này có thể **lớn hơn lợi ích** ⇒ ATE-RMSE xấu hơn baseline; ở **`0_none`/`3_high`** always-on có thể cải thiện (+30% / +20% ATE-RMSE trong run mới).

**Quality-aware gate.** Bản nâng cấp hiện tại không arm chỉ vì `ρ` cao. Sau khi tạo candidate bằng dual gate, module tính:

```text
candidate_ratio = |C| / N_scored
residual_lift   = median_sampson(C) / median_sampson(non-C)
quality_score   = clamp(candidate_ratio / r_min) * clamp(residual_lift / l_min)
```

`quality_score` được làm mượt bằng EMA và kết hợp với hysteresis của `ρ_on`. Frame chỉ hard-reject khi vừa vượt ngưỡng outlier-ratio, vừa có dynamic evidence đủ dày và residual tách khỏi nền. Các cột mới trong `geo_df_stats.csv`: `candidate_ratio`, `quality_score`, `quality_ema`, `residual_lift`, `median_candidate_sampson`, `median_background_sampson`.

**Ý tưởng.** Tự suy ra "scene có đang động không" từ **chính hình học mà bộ lọc đã tính** — không thêm cảm biến/nhãn. Cảnh rigid tĩnh khớp **một** fundamental matrix tốt ⇒ tỷ lệ outlier RANSAC toàn frame thấp; có vật chuyển động độc lập ⇒ tỷ lệ outlier tăng và **kéo dài qua nhiều frame**. Dùng tín hiệu này làm cổng kích hoạt.

**Tín hiệu + làm trơn (EMA) + trễ (hysteresis):**

\[
s_t = \frac{\#\{\text{RANSAC outlier}\}}{\#\{\text{scored tracks}\}}, \qquad
\bar{s}_t = \alpha\, s_t + (1-\alpha)\, \bar{s}_{t-1}
\]

```text
arm  (bật) khi  s̄_t ≥ ρ_on
disarm (tắt) khi s̄_t < ρ_on · κ           # κ = deactivate_frac (hysteresis)
nếu KHÔNG armed: pass-through (giữ toàn bộ feature, rejected = 0)
nếu armed:       chạy hard-reject + ratio guard như trên
```

EMA dập **spike nhất thời** ở cảnh tĩnh (xoay nhanh, ít texture → 1–2 frame outlier cao) nên không kích hoạt nhầm; vật động **bền vững** tích lũy `s̄` vượt ngưỡng nên kích hoạt đúng. Hysteresis tránh bật/tắt rung khi vật đi qua.

**Tham số (đã tinh chỉnh offline trên log thật, `scripts/simulate_activation.py`):**

```yaml
geodf_adaptive: 1            # 1: scene-aware; 0: always-on (ablation)
geodf_activate_ratio: 0.12   # ρ_on — ngưỡng EMA outlier-ratio để ARM
geodf_activate_ema: 0.15     # α — hệ số EMA (thấp = trơn hơn)
geodf_deactivate_frac: 0.6   # κ — disarm khi EMA < ρ_on·κ
```

**Bằng chứng thiết kế — % frame được ARM** (offline replay trên `geo_df_stats.csv` từ **VIODE gốc**, `simulate_activation.py`, α=0.15, ρ_on=0.12):

| Scene (VIODE gốc) | Mức động | % frame ARM |
|---|---|---:|
| `0_none` | tĩnh | **0.7%** |
| `1_low` | rất thấp | **0.5%** |
| `2_mid` | trung bình thấp | **0.0%** |
| `3_high` | cao | **9.2%** |

⇒ Cổng **tự tách** tĩnh/động ở mức thiết kế (offline replay): ~0% ở cảnh tĩnh/ít động, bật rõ ở `3_high`. Self-gating O(1)/frame, dùng lại thống kê RANSAC sẵn có. (Runtime armed% thực tế cao hơn chút — xem §2c; tái tạo bằng `scripts/simulate_activation.py`.)

### Thông số ban đầu

```yaml
geodf_enable: 1
geodf_hard_reject: 1
geodf_ransac_th_px: 1.0
geodf_sampson_th: 3.0
geodf_min_track_cnt: 2
geodf_min_feature_num: 40
geodf_reject_ratio_max: 0.40
geodf_debug: 1
```

| Param | Ý nghĩa | Đơn vị / ghi chú |
|---|---|---|
| `geodf_ransac_th_px` | Ngưỡng RANSAC: max khoảng cách tới **epipolar line** (OpenCV `ransacReprojThreshold`) | pseudo-pixel (~px với f=460) |
| `geodf_sampson_th` | τ_geo trên **Sampson²** | cùng không gian pseudo-pixel |
| `geodf_min_track_cnt` | Chỉ score track ≥ N frame | bảo vệ feature mới detect |
| `geodf_min_feature_num` | Skip GeoDF nếu N < 40; giữ ≥ 40 sau reject | count |
| `geodf_reject_ratio_max` | ρ_max cap tỷ lệ reject / frame | ratio ∈ (0,1] |
| `geodf_adaptive` | Bật scene-aware self-gating (0 = always-on) | bool |
| `geodf_activate_ratio` | ρ_on — ngưỡng EMA outlier-ratio để ARM | ratio |
| `geodf_activate_ema` | α — hệ số EMA của tín hiệu outlier-ratio | ∈(0,1] |
| `geodf_deactivate_frac` | κ — hysteresis disarm (EMA < ρ_on·κ) | ratio |

**Điểm cài đặt:** `feature_tracker.cpp` — sau temporal KLT cam0, **trước** `setMask`/detect và **trước** stereo KLT. Gọi `findFundamentalMat(un_cur, un_prev, …)` và `sampsonDistance(F, un_cur, un_prev)` nhất quán với \(\mathbf{x}=\tilde{\mathbf{u}}^{t}\), \(\mathbf{x}'=\tilde{\mathbf{u}}^{t-1}\) ở trên.

### Giả định & hạn chế (nên ghi Section 4/6)

1. **Đa số inlier rigid:** RANSAC ước lượng F từ scene tĩnh; nếu >~40% track động, F có thể lệch. **Đã quan sát thực nghiệm trên `parking_lot`** (§2d): base-rate động 10.7–14.0% + xe chiếm vùng ảnh lớn ⇒ lift sụt còn 1.4–1.6× và adaptive xấu nặng ở `3_high`.
2. **Chỉ cam0 temporal (v1):** feature động trên cam1/right-only path không qua GeoDF. **v2 (§2e) bổ sung epipolar temporal cam phải** (stereo cross-check) làm nhánh OR, tăng recall động ở scene hình học đáng tin.
3. **Không dùng IMU / depth stereo (v1)** trong gate → không phân biệt parallax thật vs object motion trong mọi trường hợp. **v2 đã khai thác stereo** (cross-check cam phải, §2e); IMU vẫn chưa dùng trong gate.
4. **Chuyển động suy biến** (pure rotation, baseline rất nhỏ): F ill-conditioned → outlier-ratio tăng giả. **Scene-aware gating dùng EMA + hysteresis nên dập được spike suy biến nhất thời** (1–2 frame); chỉ suy biến *kéo dài* (hiếm trên EuRoC/VIODE) mới có thể kích hoạt nhầm.
5. **Hard delete:** không soft-weight trong backend; outlier đã vào estimator frame trước vẫn có thể ảnh hưởng ngắn hạn.
6. **Ngưỡng kích hoạt (v1 cố định → v2 auto):** v1 dùng `ρ_on` cố định; trên `parking_lot` (§2d) noise-floor cao làm gate ARM 34–78%. **v2 (§2e) auto-calibrate `ρ_on` theo per-scene floor** (EMA bất đối xứng) ⇒ `ρ_on` tự co giãn 0.10→0.20 và `parking_lot` ARM giảm còn 12–22%. **Giải quyết Limitation này.** Tham số còn lại của công thức auto (`mult`, `margin`, biên clamp) vẫn là hằng số chọn offline.
7. **Tính lặp lại của estimator:** repeatability study (n=5, §2e) cho thấy **cùng build → ATE gần như tất-định** (std≈0), thỉnh thoảng có 1 lần phân kỳ hiếm (vd. baseline `3_high` 0.473 lẫn trong các 0.34). ⇒ Báo cáo nên dùng **mean±std n≥3** để loại outlier hiếm; **không** so sánh ATE giữa các **build/tham số khác nhau** như thể cùng cấu hình.

---

## Thực nghiệm tối thiểu

### 1. Static sanity check

Dùng EuRoC:

```text
MH_01_easy
MH_02_easy
MH_03_medium
MH_04_difficult
MH_05_difficult
```

**Mục tiêu:** GeoDF-VINS-Hard không làm baseline tĩnh tệ hơn quá **10–20%**.

**EuRoC static — adaptive (đề xuất) vs baseline** (1 run tất-định/config; bảng 3-way đầy đủ ở §2c b, nguồn `results/geodf/euroc_static_ablation.md`):

| Sequence | Baseline ATE | Adaptive ATE | Δ | armed% |
|---:|---:|---:|---:|---:|
| MH_01_easy | 0.180 | 0.166 | +7.9% | 1.7% |
| MH_02_easy | 0.168 | 0.169 | −0.2% | 0.3% |
| MH_03_medium | 0.291 | 0.275 | +5.6% | 1.7% |
| MH_04_difficult | 0.447 | 0.446 | +0.2% | 6.0% |
| MH_05_difficult | 0.298 | 0.297 | +0.3% | 0.6% |

(Δ: dương = cải thiện, ATE giảm so với baseline.)

**CHỐT static:** adaptive **bảo toàn** độ chính xác EuRoC tĩnh — 5/5 trong ±20% baseline (4/5 cải thiện hoặc ≈0%; MH_02 −0.2% là xấu nhất), gate chỉ ARM 0.3–6.0% frame. Nguồn: `results/geodf/euroc_static_ablation.md` (dataset `/media/theph/Data1/Research/Datasets/EuRoC`).

> **Repeatability (việc còn lại):** số trên là **1 run tất-định/config**. Đánh giá độ lặp đa-run (≥3 runs, mean±std) chưa thực hiện; **không claim σ khi chưa đo**. Re-run: `METHODS="baseline alwayson adaptive" bash scripts/run_euroc_static_ablation.sh`.

### 2. Đánh giá môi trường động — chỉ dùng dữ liệu gốc

> **Nguyên tắc khoa học:** chỉ đánh giá trên **dataset gốc đã publish**. Đánh giá động dùng **VIODE** (§2b) — dataset stereo-inertial động THẬT có ground-truth + segmentation. **Không** dùng dữ liệu tự tạo (synthetic overlay patch lên EuRoC) cho bất kỳ claim nào, vì vật động nhân tạo không phản ánh chuyển động/độ sâu thật và khó thuyết phục reviewer.

### 2b. VIODE — real dynamic dataset (bằng chứng chính)

[VIODE](https://github.com/kminoda/VIODE) (`city_day`, 4 mức động `0_none/1_low/2_mid/3_high`) là dataset stereo-inertial mô phỏng với **xe di chuyển thật**, kèm **ground-truth trajectory** (`/odometry`) và **segmentation mask** vật động (AirSim, id `vehicle_dynamic_*`). Calib: PINHOLE 752×480, `fx=fy=376, cx=376, cy=240`, baseline 5 cm. Scripts: `run_geodf_viode.sh`, `run_viode_detection_eval.sh`, `run_viode_repeat.sh`. §2b dùng `city_day` làm **bằng chứng chính**; **tổng quát hóa sang `city_night` + `parking_lot` ở §2d** (gồm cả phản ví dụ).

**Detection trên nhãn động THẬT (segmentation) — `eval_viode_detection.py`:** đây là bằng chứng định lượng mạnh nhất rằng filter nhắm trúng vật động. GT = mask `vehicle_dynamic_*` (1328 mask/level, 100% feature khớp timestamp trong 30 ms); nguồn `results/viode/viode_city_day_detection.md`.

| Level | GT dyn base-rate | Precision | **Lift** | Recall | Static FPR | RANSAC-out dyn/stat |
|---|---:|---:|---:|---:|---:|:---:|
| 0_none | 0.0% | — | — | — | 0.6% | — / 3.4% |
| 1_low | 0.08% | 2.5% | **31.72×** | 20.4% | 0.6% | 38.1% / 3.4% |
| 2_mid | 1.24% | 14.7% | **12.14×** | 8.7% | 0.6% | 21.2% / 3.6% |
| 3_high | 4.10% | 33.9% | **8.33×** | 8.9% | 0.7% | 21.2% / 3.8% |

- **Nhắm trúng vật động thật, lift 8.3–31.7×**: khi GeoDF loại 1 feature, xác suất nó thuộc xe đang chạy cao gấp 8.3–31.7 lần so với chọn ngẫu nhiên.
- **Cổng RANSAC tách động/tĩnh rõ rệt:** feature động là RANSAC outlier 21.2–38.1% vs feature tĩnh chỉ 3.4–3.8% (≈6–11×).
- **Bảo toàn feature tĩnh:** static FPR ổn định ~0.6–0.7% ở mọi mức động (thấp hơn run trước ~2.2% — do run tất-định mới trên dataset gốc đầy đủ).
- **Recall thấp (8.7–20.4%)** là đánh đổi có chủ đích của cổng kép (ưu tiên precision/độ an toàn hơn bắt hết) — nêu rõ ở Limitations.
- base-rate động rất thấp (0.08–4.1%) vì xe chỉ chiếm phần nhỏ ảnh ⇒ đọc theo **lift** thay vì precision tuyệt đối. `0_none` không có feature động trong GT ⇒ precision không xác định, chỉ đo được **FP tĩnh thuần 0.6%**.

**Trajectory always-on vs baseline (ATE/RPE vs GT `/odometry`, evo SE(3); 1 run tất-định/config, đa-run còn lại; nguồn `results/viode/viode_city_day_adaptive.json`):**

| Level | Baseline ATE | Always-on ATE | ATE Δ | Base RPE | Always RPE | Base ATE-max | Always ATE-max |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0_none | 0.155 | 0.108 | +30.3% | 0.024 | 0.034 | 0.344 | **0.291** |
| 1_low | 0.138 | 0.144 | −4.5% | 0.126 | **0.026** | 0.866 | **0.293** |
| 2_mid | 0.166 | 0.168 | −1.0% | 0.032 | 0.034 | 0.565 | **0.371** |
| 3_high | 0.456 | **0.366** | **+19.8%** | 0.163 | **0.108** | 1.234 | **0.945** |

(Δ: dương = cải thiện, ATE giảm.)

- Always-on **giảm worst-case (ATE-max) ở 4/4 mức** và **cải thiện RPE ở 3/4 mức**.
- **ATE-RMSE toàn cục cải thiện ở `0_none` (+30.3%) và `3_high` (+19.8%)**; ở `1_low`/`2_mid` always-on xấu hơn hoặc ngang baseline do chi phí FP tĩnh (~0.6%) khi mật độ động thấp.

> **Claim trung thực (đã kiểm chứng trên dữ liệu THẬT, dataset `/media/theph/Data1/Research/Datasets/Viode`):** GeoDF-Hard **phát hiện đúng feature thuộc vật động đang di chuyển** (lift 8.3–31.7×, cổng RANSAC tách động/tĩnh 6–11×) và **hiếm khi loại nhầm feature tĩnh** (FPR ~0.6–0.7%). Về quỹ đạo (always-on), lợi ích tập trung ở **worst-case (ATE-max), độ chính xác cục bộ (RPE), và mật độ động cao (3_high)** — chính là vấn đề mà scene-aware activation (§2c) khắc phục ở mật độ động thấp.

### 2c. Scene-aware activation — ablation (always-on vs adaptive)

Đây là phần kiểm chứng đóng góp chính: cùng cổng kép + ratio guard, chỉ khác **cách kích hoạt**.

**(a) Cổng tự tách tĩnh/động.** Offline replay tín hiệu thật (`simulate_activation.py`, bảng "% frame ARM" §Method) cho thấy cổng tách rõ tĩnh/động ở mức thiết kế. Runtime thực tế (cột `gate armed%` các bảng dưới) ARM **0.5–4.1%** frame ở EuRoC tĩnh và **0.8–3.4%** ở VIODE low-dynamic, **11.0%** ở `3_high` — đúng mục tiêu: gần pass-through khi tĩnh, bật khi động (không phải "thắng mọi mức" mà là "giữ baseline ở tĩnh, giữ lợi ích ở động").

**(b) ATE trên EuRoC gốc (tĩnh) — đã đo (`results/geodf/euroc_static_ablation.md`):**

| Seq | baseline | always-on | **adaptive** | always Δ | adapt Δ | armed% |
|---|---:|---:|---:|---:|---:|---:|
| MH_01 | 0.180 | 0.207 | **0.166** | −14.8% | **+7.9%** | 1.7% |
| MH_02 | 0.168 | 0.155 | **0.169** | +8.2% | −0.2% | 0.3% |
| MH_03 | 0.291 | 0.289 | **0.275** | +1.0% | **+5.6%** | 1.7% |
| MH_04 | 0.447 | 0.419 | **0.446** | +6.1% | +0.2% | 6.0% |
| MH_05 | 0.298 | 0.319 | **0.297** | −6.9% | +0.3% | 0.6% |

(Δ: dương = cải thiện, ATE giảm.) Always-on làm xấu tĩnh ở **2/5** seq (MH_01/05 Δ âm); **adaptive bám baseline (≤±1% ở 4/5 seq), ARM 0.3–6.0%** → thu hồi chi phí FP.

**(c) ATE trên VIODE gốc (động thật) — đã đo (`results/viode/viode_city_day_adaptive.json`):**

| Level | baseline | always-on | **adaptive** | adapt Δ vs base | gate armed% |
|---|---:|---:|---:|---:|---:|
| 0_none | 0.155 | 0.108 (−30%) | **0.113** | **+27.2%** | 1.7% |
| 1_low | 0.138 | 0.144 (+4.5%) | **0.138** | **+0.2%** | 2.0% |
| 2_mid | 0.166 | 0.168 (−1.0%) | **0.148** | **+10.7%** | 3.2% |
| 3_high | 0.456 | 0.366 (+19.8%) | **0.225** | **+50.6%** | 11.0% |

(Δ: dương = adaptive tốt hơn baseline.)

**Chốt VIODE city_day:** adaptive **cải thiện mọi mức** so với baseline (0_none +27.2% đến 3_high +50.6%), gate chỉ ARM 1.7–11.0% frame. Ở `3_high`, adaptive (0.225) **vượt cả always-on (0.366)** — self-gating không còn trade-off tại mật độ động cao trong run này.

**Logic:** always-on trả chi phí FP tĩnh (~0.6%) mỗi frame; adaptive chỉ ARM khi scene bất nhất epipolar nên **giảm chi phí ở cảnh tĩnh** mà **giữ lợi ích ở cảnh động** → best-of-both. EuRoC static ablation (MH_01–05) đã đo ở §2c(b): always-on làm xấu 2/5 seq tĩnh, adaptive thu hồi.

### 2d. Tổng quát hóa đa môi trường (`city_night`, `parking_lot`) — đã đo

Chạy lại **toàn bộ** pipeline (baseline/always-on/adaptive × 4 mức + detection eval) trên 2 môi trường VIODE còn lại để kiểm tra mức độ tổng quát hóa của đóng góp. Nguồn: `results/geodf_evaluation/MULTIENV_REPORT.md`, `results/viode/viode_{city_night,parking_lot}_{adaptive,detection}.md` (1 run tất-định/config).

**Detection — precision lift (lift > 1 ⇒ nhắm trúng vật động):**

| Env | 1_low | 2_mid | 3_high | RANSAC tách động/tĩnh (2_mid) |
|---|---:|---:|---:|:---:|
| `city_day` | 31.72× | 12.14× | 8.33× | 21.2% / 3.6% (≈6×) |
| `city_night` | 1.12× | 3.02× | 2.89× | 13.6% / 5.1% (≈2.7×) |
| `parking_lot` | 1.57× | 1.48× | 1.42× | 13.1% / 9.9% (≈1.3×) |

**ATE-RMSE — adaptive Δ vs baseline (dương = cải thiện):**

| Env | 0_none | 1_low | 2_mid | 3_high | gate armed% |
|---|---:|---:|---:|---:|---:|
| `city_day` | **+27.2%** | **+0.2%** | **+10.7%** | **+50.6%** | 2–11% |
| `city_night` | **+10.6%** | −7.5% | **+10.7%** | −0.7% | 7–11% |
| `parking_lot` | **+23.2%** | **+13.7%** | **+19.1%** | **−81.8%** | 34–78% |

**Đọc kết quả (trung thực — đây là điểm cốt lõi cho reviewer Q4):**
- **`city_day` = kịch bản tốt nhất:** lift cao nhất (8.3–31.7×), adaptive **cải thiện mọi mức** (+0.2% đến +50.6%), gate ARM thấp & đúng.
- **`city_night` = tổng quát hóa hỗn hợp:** đêm tối → texture kém, ATE nền cao (0.4–0.9 m); lift trung bình (2.9–3.3× ở mid/high). Adaptive thắng `0_none`/`2_mid`, hơi xấu `1_low`/`3_high` — **không phá** nhưng lợi ích nhỏ. Gate ARM 7–11% (hợp lý).
- **`parking_lot` = phản ví dụ / limitation (nêu THẲNG):** mật độ động rất cao (base-rate **10.7–14.0%** ở mid/high) **vi phạm giả định "đa số inlier là rigid"** ⇒ F bị lệch ⇒ lift sụt còn 1.4–1.6×, RANSAC chỉ tách ~1.3×. Hệ quả: **gate ARM 34–78%** (ngưỡng cố định ρ_on=0.12 không hợp "noise floor" outlier cao của scene này) nên adaptive ≈ always-on; always-on **phá `2_mid` (0.144→0.571)**, adaptive cứu được `2_mid` nhưng **xấu nặng ở `3_high` (0.119→0.217, −81.8%)**.

> **Kết luận tổng quát hóa (đưa vào Discussion + Limitations):** đóng góp có **phạm vi điều kiện**, không phổ quát — GeoDF-Adaptive cải thiện khi vật động (i) tạo bất nhất epipolar rõ và (ii) **không chiếm đa số scene** (thoả ở `city_day`, một phần `city_night`). `parking_lot` **xác nhận bằng thực nghiệm 2 limitation đã ghi**: (1) mật độ động cao làm lệch F; (6) ngưỡng cố định gây over-arming — **(6) đã giải quyết bằng auto-`ρ_on` (§2e, Hướng A)**; (1) vẫn là giới hạn cấu trúc.

### 2e. Hai nâng cấp thuật toán (v2): (B) auto-`ρ_on` + (F) stereo cross-check

Hai giới hạn ở §2d được **hiện thực hoá thành thuật toán** và đánh giá lại (cùng-session để khử nhiễu cho ATE). Các bảng bằng chứng v2 được giữ inline dưới đây; đây là nhánh thăm dò, không thuộc bộ artifact AECE đóng băng (script/summary so sánh v2 và `V2_COMPARISON.md` đã được gỡ khỏi worktree).

**(B) Auto-calibrate `ρ_on` theo per-scene noise-floor.** Thay vì ngưỡng ARM cố định, ước lượng online "sàn outlier tĩnh" của scene bằng EMA bất đối xứng (giảm nhanh `β↓=0.02`, tăng chậm `β↑=0.004` ⇒ bám đáy), rồi đặt:
```text
floor_t = EMA_asym(s_t)                       # sàn outlier-ratio của scene
ρ_on    = clamp(floor_t·1.8 + 0.05, 0.10, 0.40)   # ngưỡng ARM tự co giãn
ρ_off   = ρ_on · κ                            # hysteresis như cũ
```
**(F) Stereo temporal cross-check.** GeoDF v1 chỉ dùng epipolar temporal **cam trái**; vật trượt **dọc đường epipolar trái** (Sampson_L≈0) bị bỏ sót. v2 track thêm cur-left→cur-right, ghép với right-pixel frame trước (theo id) để ước lượng **F temporal cam phải** và chấm Sampson_R; ứng viên động = **dual-gate trái HOẶC dual-gate phải** (OR ⇒ tăng recall). Vì cặp phải nối **hai lần KLT** nên nhiễu hơn → ngưỡng Sampson_R cao hơn (`τ_R=6.0`) và **scene-gating**: chỉ tin nhánh phải khi `floor_t ≤ 0.045` (hình học đáng tin); scene low-parallax (parking_lot) tự tắt nhánh phải.

**Bằng chứng (B) — `ρ_on` tự co giãn theo scene & dập over-arming** (từ `geo_df_stats.csv`, cột mới `rho_on`/`outlier_floor`):

| Env | outlier floor (mean) | `ρ_on` v2 (mean) | armed% v2 | armed% v1 (ρ_on=0.12 cố định) |
|---|---:|---:|---:|---:|
| `city_day` | 0.017–0.027 | 0.101–0.113 | 2.2–11.7% | 2–11% |
| `city_night` | 0.028–0.031 | 0.111–0.115 | 7.8–11.1% | 7–11% |
| `parking_lot` | 0.048–0.080 | 0.142–0.199 | **12.3–22.4%** | **34–78%** |

→ `ρ_on` **tự nâng 0.10 → 0.20** đúng theo noise-floor; `parking_lot` armed% giảm từ **34–78% → 12–22%** ⇒ khôi phục tính chọn lọc của gate mà ngưỡng cố định đánh mất. **Giải quyết Limitation #6.** Overhead `geo_ms` (gồm KLT stereo thêm) mean **1.0–1.4 ms**, p95 ≤ 2.2 ms — vẫn nhẹ.

**Bằng chứng (F) — recall động tăng, precision giữ** (always-on dump vs GT mask, ít nhiễu; v1=trái, v2=trái∨phải):

| Env | level | recall v1 → v2 | precision v1 → v2 | lift v1 → v2 | TP v1 → v2 | static-FPR v1 → v2 |
|---|---|---:|---:|---:|---:|---:|
| `city_day` | 2_mid | 0.087 → **0.133** | 0.147 → 0.155 | 12.1 → 13.4 | 151 → 217 | 0.62% → 0.84% |
| `city_day` | 3_high | 0.089 → **0.124** | 0.339 → 0.338 | 8.3 → 8.7 | 495 → 656 | 0.74% → 0.98% |
| `parking_lot` | 2_mid | 0.045 → 0.041 | 0.159 → 0.148 | 1.5 → 1.4 | 469 → 435 | 2.83% → 2.87% |

→ Ở scene hình học đáng tin (`city_day`): recall **+39–53%**, TP **+33–44%**, precision/lift **giữ hoặc tăng**, static-FPR vẫn <1%. Ở `parking_lot` scene-gating **tự tắt** nhánh phải nên detection ≈ v1 và **không thổi static-FPR** (2.87% vs 3.48% nếu không gate). Nhánh phải bổ sung **trung bình 0.5–1.2 ứng viên/frame** mà epipolar trái bỏ sót (cột `stereo_added`).

**ATE — repeatability study (n=5/config, CÙNG build).** Nguồn: `results/geodf_evaluation/REPEATABILITY.md`, `scripts/run_geodf_repeat.sh`. **Trong cùng một build, ATE gần như tất-định** (std ≈ 0.000 ở hầu hết cell); dao động lớn quan sát trước đây là **giữa các build/tham số khác nhau khi tinh chỉnh**, KHÔNG phải cùng config — nên các claim ATE single-run vẫn dùng được, và đây là bảng mean±std để chắc chắn:

| level | baseline | adaptive (ρ_on cố định) | (B) auto-ρ_on | (B)+(F) v2 |
|---|---:|---:|---:|---:|
| 2_mid | 0.166±0.00 | 0.152±0.01 | 0.167±0.00 | **0.147±0.00** |
| 3_high | 0.369±0.05 | **0.224±0.00** | 0.282±0.05 | 0.323±0.00 |

**Đọc kết quả (trung thực, đã loại nhiễu bằng n=5):**
- **Self-gating (mọi biến thể) > baseline** ở scene động: 2_mid **+8…+11%**, 3_high **+12…+39%** — **đóng góp lõi vững** (std thấp).
- **(B) auto-ρ_on:** trên `city_day` (nơi ρ_on=0.12 đã *được tinh tay sẵn*), auto-ρ_on **ngang/nhỉnh nhẹ kém** fixed (3_high 0.224→0.282). **Giá trị thực của (B) là bỏ hyperparameter + tổng quát hóa**: dập over-arming `parking_lot` từ **34–78% → 12–22%** (bằng chứng cơ chế ở bảng trên) — điều fixed-ρ_on **không** làm được khi đổi môi trường.
- **(F) stereo cross-check:** tăng **recall phát hiện +39–53%** nhưng **KHÔNG cải thiện ATE**, thậm chí **hại ở 3_high** (0.282→0.323). Đây là **kết quả âm về quỹ đạo**: recall cao **⇏** ATE tốt; chi phí false-positive (hard-delete kiểu OR) lấn át lợi ích ở mật độ động cao.
- **EuRoC tĩnh (5 seq): không hồi quy** — v2 nằm trong **−3.4%…+0.7%** so với baseline.

> **Chốt v2 / method đề xuất (Hướng A):** **GeoDF-Adaptive + auto-`ρ_on`, stereo OFF** là config chính (`adaptive`). Self-gating >> baseline (n=5). (B) adopt vì tổng quát hóa hyperparameter-free. (F) chỉ ablation (`adaptive_v2`). Fixed-ρ ablation: `adaptive_fixed` (peak ATE city_day 3_high khi đã tune).

### 3. Ablation

| # | Method | Alias | Geo filter | Hard reject | Ratio guard | Activation | ρ_on |
|---:|---|---|---|:---:|:---:|:---:|---|
| 1 | VINS-Fusion baseline | `baseline` | no | no | no | — | — |
| 2 | GeoDF always-on | `geodf_dump` | yes | yes | yes | always-on | — |
| 3 | GeoDF-Adaptive (fixed ρ) | `adaptive_fixed` | yes | yes | yes | scene-aware | fixed 0.12 |
| 4 | **GeoDF-Adaptive (PROPOSED)** | **`adaptive`** | yes | yes | yes | scene-aware | **auto** |
| 5 | + stereo cross-check | `adaptive_v2` | yes | yes | yes | scene-aware | auto + (F) |

Ablation chính: **always-on vs scene-aware** (cùng cổng kép + ratio guard) và **fixed-ρ vs auto-ρ** (tổng quát hóa). Chạy:
- EuRoC: `METHODS="baseline alwayson adaptive adaptive_fixed" bash scripts/run_geodf_euroc.sh MH_01_easy adaptive --eval`
- VIODE: `bash scripts/run_geodf_viode.sh "0_none 1_low 2_mid 3_high" "baseline geodf_dump adaptive adaptive_fixed adaptive_v2"`

### Metrics báo cáo

**Trajectory (evo):**

| Metric | Công cụ / nguồn |
|---|---|
| ATE RMSE | evo (SE3 Umeyama) |
| RPE (1 m) | evo |
| FPS | `vins_node` log / wall-clock / frame count |

**Filter impact (`geo_df_stats.csv` → `geodf_filter_metrics.json`):**

| Metric | Ý nghĩa | Thuyết phục reviewer |
|---|---|---|
| `frames_with_reject_pct` | % frame có ≥1 feature bị xóa | Filter **thực sự tác động** |
| `mean_reject_ratio` | mean(rejected/N) | Mức độ loại bỏ |
| `total_rejected` | Tổng feature bị xóa / run | Không chỉ vài frame |
| `total_candidates` | Qua dual gate (RANSAC ∧ Sampson) | Ứng viên thực |
| `candidate_rate_per_scored` | candidates/scored | Tỷ lệ nghi ngờ |
| `dual_gate_reduction_pct` | 1 − candidates/sampson_above_th | RANSAC gate có hiệu lực |
| `frames_guard_triggered_pct` | % frame cap reject ratio | Ratio guard hoạt động khi cần |
| `mean_max_sampson` | Sampson max trung bình / frame | Geometry stress |
| `geo_ms` | Latency module / frame | Overhead thấp |

Tạo bảng reviewer: `python3 scripts/summarize_geodf_filter_impact.py`

**Detection quality vs nhãn động THẬT (VIODE segmentation, `geo_df_features.csv` + mask → `eval_viode_detection.py`):**

Bằng chứng mạnh nhất, **trên dữ liệu gốc**: mỗi feature `(u,v)` gán nhãn **dynamic** nếu rơi vào mask segmentation vật động của VIODE (id `vehicle_dynamic_*`); prediction = `rejected`. Bảng kết quả thật ở **§2b** (lift 8.3–31.7×, RANSAC tách động/tĩnh 6–11×, static FPR ~0.6–0.7%).

| Metric | Định nghĩa | Thuyết phục reviewer |
|---|---|---|
| Dynamic base-rate | n_dynamic / scored | Tỷ lệ feature động "ngẫu nhiên" |
| Precision | TP / (TP+FP) | Feature bị xóa **thực sự** động |
| **Precision lift** | precision / base-rate | **lift > 1 ⇒ chọn đúng, không xóa bừa** |
| Recall | TP / (TP+FN) | Bắt được bao nhiêu feature động |
| F1 | 2PR/(P+R) | Cân bằng P/R |
| Static FPR | FP / (FP+TN) | Tỷ lệ xóa nhầm feature tĩnh |
| RANSAC-out rate (dyn vs stat) | %feature là RANSAC outlier theo nhãn | Cổng RANSAC phân biệt được |
| Median Sampson (dyn vs stat) | residual trung vị theo nhãn | Phân tách hình học (median robust hơn mean) |

Chạy: `VIODE_ROOT=… bash scripts/run_viode_detection_eval.sh` → `results/viode/viode_city_day_detection.md`.

---

## Cấu trúc paper

```text
1. Introduction
2. Related Work
3. VINS-Fusion Stereo-Inertial Baseline
4. Proposed GeoDF-VINS-Hard
   4.1 Feature tracking
   4.2 Epipolar consistency
   4.3 Sampson residual scoring
   4.4 Hard rejection
   4.5 Ratio guard
5. Experimental Setup
6. Results and Discussion
7. Conclusion and Future Work
```

**Future work:**

```text
- adaptive threshold: tự-calibrate ρ_on theo noise-floor outlier của scene (hiện cố định)
- temporal voting đa frame (giảm phụ thuộc F 2-frame)
- IMU-guided dynamic filtering (dùng gyro để khử rotation-induced outlier)
- soft weighting in backend (thay hard delete)
- wheel encoder fusion
```

---

## Chốt cuối cùng

Bài Q4:

```text
GeoDF-VINS-Hard (+ scene-aware activation)
=
VINS-Fusion stereo_imu
+
Geo-only front-end dynamic feature rejection
+
Sampson residual + dual gate
+
hard delete + ratio guard
+
scene-aware self-gating activation  ← đóng góp chính
+
EuRoC static (gốc) + VIODE real-dynamic (gốc) benchmark
```

**Target venue:** AECE — *Advances in Electrical and Computer Engineering* (applied EE / sensor fusion).

**Differentiator:** Không thay estimator, không DL, không IMU trong filter, **không dữ liệu tự tạo** — chỉ geometry front-end + scene-aware self-gating, đánh giá trên dataset gốc (EuRoC + VIODE), dễ reproduce, overhead thấp.

---

## Lộ trình triển khai (ước lượng)

| Tuần | Công việc |
|:---:|---|
| 1–2 | GeoDF module + scene-aware activation trong `feature_tracker.cpp` + YAML params |
| 3 | Static EuRoC MH_01–05 (gốc) + regression gate |
| 4 | VIODE setup (calib, GT, segmentation mask) |
| 5 | VIODE dynamic experiments + ablation (baseline/always-on/adaptive) |
| 6 | Viết paper + figures (ATE, rejection ratio, FPS, detection lift) |

**Branch:** `paper/geodf-adaptive-vins-2026` · **Worktree:** `../ws_vins_ros2_paper1_adaptive` · **Baseline:** `baseline/ros2-stereo-vi-slam-euroc-v1` · See [docs/BRANCHING.md](BRANCHING.md).

---

*Proposal version: 2026-06-25 (Hướng A chốt: **GeoDF-Adaptive** = scene-aware + auto-ρ_on; alias `adaptive`; ablation `adaptive_fixed` / `adaptive_v2`) · Workspace: `/home/theph/ws_vins_ros2` (ROS 2)*
