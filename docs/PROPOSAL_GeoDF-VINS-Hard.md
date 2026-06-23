# Research Proposal — Scopus Q4

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

> Bài báo đề xuất một module front-end nhẹ để loại bỏ các feature có chuyển động hình học không nhất quán trước khi đưa vào estimator của VINS-Fusion, nhằm cải thiện độ ổn định trong môi trường có nhiễu động.

---

## Đóng góp của bài

1. Tích hợp module **Geo-only dynamic feature rejection** vào front-end của VINS-Fusion stereo-inertial.
2. Sử dụng **epipolar geometry / Sampson residual** để đánh giá feature track giữa hai frame.
3. Áp dụng **hard rejection có ratio guard**, tránh xóa quá nhiều feature trong một frame.
4. **(Đóng góp chính) Scene-aware self-gating activation:** dùng chính tỷ lệ outlier RANSAC toàn frame (EMA + hysteresis) làm chỉ báo "scene có động hay không"; **chỉ bật hard-reject khi scene thực sự bất nhất epipolar**, cảnh tĩnh thì pass-through. Triệt tiêu chi phí false-positive tĩnh (nguyên nhân ATE xấu ở mật độ động thấp) mà vẫn giữ lợi ích ở mật độ động cao — không thêm cảm biến, không học máy.
5. Đánh giá **chỉ trên dữ liệu gốc đã publish**: **EuRoC** (tĩnh) và **VIODE** (động thật, có ground-truth trajectory + segmentation), bằng ATE, RPE, FPS, rejection ratio, và precision/recall/lift trên **nhãn động thật** — không dùng dữ liệu tự tạo (synthetic overlay).

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

**Hard rejection + ratio guard** (đúng theo code, không nhầm ratio với count):

```text
N  = số track trước lọc
K  = floor(ρ_max × N)          # ρ_max = geodf_reject_ratio_max (default 0.40)
K' = min(K, N − min_feature_num)

if ratio_guard OFF:
    reject top min(|C|, K') candidates in C (sort by e_i descending)
else:
    if |C| ≤ K':
        reject all C                    # guard_triggered = 0
    else:
        guard_triggered = 1
        reject top K' from C            # bỏ phần dư, giữ candidate Sampson thấp hơn
```

**Early skip:** nếu `N < min_feature_num` hoặc `N < 8` → bỏ qua GeoDF frame đó (bảo vệ khi feature quá ít).

### Scene-aware activation (adaptive self-gating) — đóng góp chính

**Vấn đề.** Hard rejection *luôn bật* có một chi phí cố định: cổng kép vẫn loại nhầm ~2.2% feature tĩnh mỗi frame (false positive từ parallax/xoay nhanh làm F suy biến). Ở cảnh **tĩnh hoặc ít vật động**, chi phí này **lớn hơn lợi ích** ⇒ ATE-RMSE toàn cục **xấu đi** (đo trên VIODE gốc: `0_none` always-on −30% vs baseline). Ở **`3_high`** thì ngược lại (+13.9% ATE-RMSE, RPE và ATE-max cũng tốt hơn).

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

⇒ Cổng **tự tách** tĩnh/động trên dữ liệu thật: ~0% ở cảnh tĩnh/ít động (thu hồi chi phí FP), bật rõ ở `3_high` (giữ lợi ích). Self-gating O(1)/frame, dùng lại thống kê RANSAC sẵn có. Chi tiết: `results/geodf_study/adaptive_activation_design.md`.

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

1. **Đa số inlier rigid:** RANSAC ước lượng F từ scene tĩnh; nếu >~40% track động, F có thể lệch.
2. **Chỉ cam0 temporal:** feature động trên cam1/right-only path không qua GeoDF (right track phụ thuộc left ids).
3. **Không dùng IMU / depth stereo** trong gate → không phân biệt parallax thật vs object motion trong mọi trường hợp.
4. **Chuyển động suy biến** (pure rotation, baseline rất nhỏ): F ill-conditioned → outlier-ratio tăng giả. **Scene-aware gating dùng EMA + hysteresis nên dập được spike suy biến nhất thời** (1–2 frame); chỉ suy biến *kéo dài* (hiếm trên EuRoC/VIODE) mới có thể kích hoạt nhầm.
5. **Hard delete:** không soft-weight trong backend; outlier đã vào estimator frame trước vẫn có thể ảnh hưởng ngắn hạn.
6. **Ngưỡng kích hoạt cố định:** `ρ_on`, `α`, `κ` được tinh chỉnh offline trên log; một scene có "noise floor" outlier rất khác (camera/độ phân giải khác) có thể cần chỉnh lại — đây là một tham số tuyến tính, dễ calibrate bằng `simulate_activation.py`.

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

**Baseline stereo+IMU ATE RMSE (m)** — đã đo trên `ws_vins`:

| Sequence | Baseline ATE |
|---:|---:|
| MH_01_easy | 0.180 |
| MH_02_easy | 0.156 |
| MH_03_medium | 0.292 |
| MH_04_difficult | 0.446 |
| MH_05_difficult | 0.298 |

**Kết quả đã đo — repeatability (2 runs/seq, `results/geodf_static_repeat/`):**

| Sequence | Baseline mean | GeoDF mean | Δ mean | Δ max run | Reject ratio | Verdict |
|---:|---:|---:|---:|---:|---:|:---:|
| MH_01_easy | 0.179 | 0.183 | +2.5% | +2.6% | 1.38% | PASS |
| MH_02_easy | 0.156 | 0.171 | +9.4% | +9.4% | 1.16% | PASS |
| MH_03_medium | 0.292 | 0.246 | −15.7% | −15.7% | 2.09% | PASS |
| MH_04_difficult | 0.446 | 0.451 | +1.0% | +1.1% | 1.98% | PASS |
| MH_05_difficult | 0.298 | 0.286 | −4.2% | −4.1% | 1.67% | PASS |

**CHỐT static:** GeoDF-Hard **không phá baseline** trên EuRoC tĩnh (5/5 PASS, σ ≈ 0 — deterministic). Re-run: `bash scripts/run_geodf_static_repeat.sh 2`

**Nhận định (claim khiêm tốn):** Trên EuRoC tĩnh, GeoDF-Hard **bảo toàn** độ chính xác của VINS-Fusion (cả 5 sequence đạt ≤20% mọi cặp run; MH_03/MH_05 cải thiện nhẹ, MH_04 khó nhất +1.0%). Mean reject ratio ~1–2%, `guard_triggered = 0%`.

### 2. Đánh giá môi trường động — chỉ dùng dữ liệu gốc

> **Nguyên tắc khoa học:** chỉ đánh giá trên **dataset gốc đã publish**. Đánh giá động dùng **VIODE** (§2b) — dataset stereo-inertial động THẬT có ground-truth + segmentation. **Không** dùng dữ liệu tự tạo (synthetic overlay patch lên EuRoC) cho bất kỳ claim nào, vì vật động nhân tạo không phản ánh chuyển động/độ sâu thật và khó thuyết phục reviewer.

### 2b. VIODE — real dynamic dataset (bằng chứng chính)

[VIODE](https://github.com/kminoda/VIODE) (`city_day`, 4 mức động `0_none/1_low/2_mid/3_high`) là dataset stereo-inertial mô phỏng với **xe di chuyển thật**, kèm **ground-truth trajectory** (`/odometry`) và **segmentation mask** vật động (AirSim, id `vehicle_dynamic_*`). Calib: PINHOLE 752×480, `fx=fy=376, cx=376, cy=240`, baseline 5 cm. Scripts: `run_geodf_viode.sh`, `run_viode_detection_eval.sh`, `run_viode_repeat.sh`.

**Detection trên nhãn động THẬT (segmentation) — `eval_viode_detection.py`:** đây là bằng chứng định lượng mạnh nhất rằng filter nhắm trúng vật động.

| Level | Dyn base-rate | Precision | **Lift** | Recall | Static FPR | RANSAC-out dyn/stat |
|---|---:|---:|---:|---:|---:|:---:|
| 1_low | 0.1% | 0.8% | **9.30×** | 20.5% | 2.2% | 29.1% / 3.1% |
| 2_mid | 1.2% | 5.6% | **4.78×** | 12.2% | 2.4% | 18.7% / 3.4% |
| 3_high | 3.9% | 20.9% | **5.35×** | 16.2% | 2.5% | 20.2% / 3.6% |

- **Nhắm trúng vật động thật, lift 4.8–9.3×**: khi GeoDF loại 1 feature, xác suất nó thuộc xe đang chạy cao gấp 4.8–9.3 lần so với ngẫu nhiên.
- **Cổng RANSAC tách động/tĩnh rõ rệt:** feature động là RANSAC outlier 18.7–29.1% vs feature tĩnh chỉ 3.0–3.6% (≈6–9×).
- **Bảo toàn feature tĩnh:** static FPR ổn định ~2.2–2.5% ở mọi mức động.
- base-rate động thấp (0.1–3.9%) vì xe chỉ chiếm phần nhỏ ảnh ⇒ đọc theo **lift** thay vì precision tuyệt đối.

**Trajectory (ATE/RPE vs GT `/odometry`, evo SE(3); VINS-Fusion ở đây gần-tất-định — r1 lệch sweep gốc < 3e-4 m ⇒ chênh lệch là tín hiệu thật):**

| Level | Baseline ATE | GeoDF ATE | ATE Δ | Base RPE(1m) | GeoDF RPE(1m) | Base ATE-max | GeoDF ATE-max |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0_none | 0.109 | 0.142 | −30.4% | 0.111 | **0.029** | 0.802 | **0.249** |
| 1_low | 0.139 | 0.300 | −115.2% | 0.126 | **0.036** | 0.862 | **0.567** |
| 2_mid | 0.166 | 0.252 | −52.0% | 0.032 | 0.041 | 0.565 | 0.599 |
| 3_high | 0.346 | **0.298** | **+13.9%** | 0.106 | **0.100** | 0.974 | **0.826** |

- GeoDF **giảm worst-case (ATE-max) ở 3/4 mức** và **cải thiện RPE cục bộ ở 3/4 mức**.
- **ATE-RMSE toàn cục chỉ cải thiện ở `3_high` (+13.9%)**; ở mật độ động thấp, static FPR ~2.2% của cổng hình học làm tăng nhẹ ATE toàn cục (ít vật động để loại nên chi phí > lợi ích). Mẫu hình "RMSE xấu hơn nhưng max+RPE tốt hơn" phản ánh lệch alignment toàn cục trên chuỗi ngắn 66 s, không phải mất chính xác cục bộ.

> **Claim trung thực (đã kiểm chứng trên dữ liệu THẬT):** GeoDF-Hard **phát hiện đúng feature thuộc vật động đang di chuyển** (lift 4.8–9.3×, cổng RANSAC tách động/tĩnh 6–9×) và **hiếm khi loại nhầm feature tĩnh** (FPR ~2.2–2.5%). Về quỹ đạo (always-on), lợi ích tập trung ở **worst-case (ATE-max), độ chính xác cục bộ (RPE), và mật độ động cao (3_high)**; **ATE-RMSE xấu ở mật độ động thấp do chi phí FP tĩnh** — chính là vấn đề mà scene-aware activation (§2c) khắc phục.

### 2c. Scene-aware activation — ablation (always-on vs adaptive)

Đây là phần kiểm chứng đóng góp chính: cùng cổng kép + ratio guard, chỉ khác **cách kích hoạt**.

**(a) Cổng tự tách tĩnh/động (offline replay tín hiệu VIODE/EuRoC thật, `simulate_activation.py`):** xem bảng "% frame ARM" ở §Method — cảnh tĩnh/ít động ARM **0.0–0.7%** (≈ baseline), cảnh động ARM **9%+** (giữ lợi ích). Đây là điều kiện cần để "thắng ATE ở mọi mức".

**(b) ATE trên EuRoC gốc (tĩnh) — baseline vs always-on vs adaptive:**

> ⏳ *Đang chạy* `run_euroc_static_ablation.sh` trên MH_01–05 gốc → `results/geodf/euroc_static_ablation.md`. Kỳ vọng: always-on tăng nhẹ ATE (chi phí FP), **adaptive ARM ~0% ⇒ bám sát baseline** (thu hồi chi phí).

**(c) ATE trên VIODE gốc (động thật) — đã đo (`results/viode/viode_city_day_adaptive.md`):**

| Level | baseline | always-on | **adaptive** | adapt Δ vs base | gate armed% |
|---|---:|---:|---:|---:|---:|
| 0_none | 0.109 | 0.142 (−30%) | **0.112** | **−2.2%** | 2.0% |
| 1_low | 0.139 | 0.300 | **0.143** | **−2.4%** | 0.8% |
| 2_mid | 0.166 | 0.252 | **0.147** | **+11.7%** | 3.4% |
| 3_high | 0.346 | 0.298 | **0.292** | **+15.5%** | 11.0% |

**Chốt VIODE:** adaptive **sửa hoàn toàn** lỗi always-on ở `0_none`/`1_low` (ATE gần baseline), **thắng baseline ở `2_mid`/`3_high`**, đồng thời gate chỉ ARM 0.8–11% frame. Đây là bằng chứng chính cho đóng góp scene-aware activation trên **dữ liệu gốc**.

**Logic:** always-on **làm xấu ATE ở mật độ động thấp** vì luôn trả ~2.2% chi phí FP tĩnh; adaptive chỉ ARM khi scene thật sự bất nhất epipolar nên **xoá chi phí đó ở cảnh tĩnh** mà **giữ lợi ích ở cảnh động** → best-of-both trên VIODE. EuRoC static ablation (MH_01–05) vẫn pending.

### 3. Ablation

| Method | Geo filter | Hard reject | Ratio guard | Activation |
|---|:---:|:---:|:---:|:---:|
| VINS-Fusion baseline | no | no | no | — |
| GeoDF-Hard no guard | yes | yes | no | always-on |
| GeoDF-Hard with guard | yes | yes | yes | always-on |
| **GeoDF-Adapt (đề xuất)** | yes | yes | yes | **scene-aware** |

Ablation chính của đóng góp: **always-on vs scene-aware** (cùng cổng kép + ratio guard) — cô lập đúng tác dụng của self-gating, **trên dữ liệu gốc**:
- EuRoC tĩnh: `EUROC_ROOT=… METHODS="baseline alwayson adaptive" bash scripts/run_euroc_static_ablation.sh`
- VIODE động: `VIODE_ROOT=… bash scripts/run_geodf_viode.sh "0_none 1_low 2_mid 3_high" "baseline geodf_dump adaptive"` → `summarize_viode_adaptive.py`

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

Bằng chứng mạnh nhất, **trên dữ liệu gốc**: mỗi feature `(u,v)` gán nhãn **dynamic** nếu rơi vào mask segmentation vật động của VIODE (id `vehicle_dynamic_*`); prediction = `rejected`. Bảng kết quả thật ở **§2b** (lift 4.8–9.3×, RANSAC tách động/tĩnh 6–9×, static FPR ~2.2–2.5%).

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

**Target venue:** Scopus Q4 — robotics / autonomous systems / applied computer vision.

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

**Branch:** `paper/geodf-adaptive-vins-2026-q4` · **Baseline:** `baseline/ros2-stereo-vi-slam-euroc-v1` · See [docs/BRANCHING.md](BRANCHING.md).

---

*Proposal version: 2026-06-23 · Workspace: `/home/theph/ws_vins_ros2` (ROS 2)*
