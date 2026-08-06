# Paper configuration framework

One common backbone per dataset, one thin overlay per method. Nothing is
copy-pasted, and every difference between two methods is auditable.

This exists because the earlier per-method YAML copies had drifted: the Sem-GeoDF
config enabled `visual_adaptive_quality` and `imu_adaptive_covariance` while
Baseline / GeoDF-Adaptive / SAD-Sem did not, so no improvement could be
attributed to Sem-GeoDF itself.

## Layout

```
viode_common.yaml          common stereo-inertial backbone, every knob explicit
euroc_common.yaml          same, differing only in calibration / max_cnt / IMU noise
allowed_differences.yaml   which keys each method is allowed to own
overlays/
  baseline.yaml            B0    nothing on
  geodf.yaml               G     + geometric expert
  semantic.yaml            S     + semantic masking
  union_noweight.yaml      U     + adaptive gated union, w_i off
  union_weight.yaml        U+W   U with w_i on  (differs from U in ONE key)
  full_adaptive.yaml       Full  U+W + common visual/IMU factor adaptation
```

## Method matrix

| ID | Semantic | GeoDF | Gated union | Backend `w_i` | Visual adaptive | IMU adaptive |
|-----|---|---|---|---|---|---|
| B0 | 0 | 0 | 0 | 0 | 0 | 0 |
| G | 0 | 1 | 0 | 0 | 0 | 0 |
| S | 1 | 0 | 0 | 0 | 0 | 0 |
| U | 1 | 1 | 1 | 0 | 0 | 0 |
| U+W | 1 | 1 | 1 | 1 | 0 | 0 |
| Full | 1 | 1 | 1 | 1 | 1 | 1 |

`Full` is reported as its own row. Factor adaptation is a separate contribution:
run it for **all** methods (second matrix) or for none. Enabling it only for
"ours" is the confound this framework blocks.

## Generating a run config

```bash
python3 scripts/generate_paper_config.py \
  --base src/config/paper/viode_common.yaml \
  --overlay src/config/paper/overlays/union_weight.yaml \
  --out <run_dir>/resolved_config.yaml \
  --set output_path=<run_dir>
```

Writes the resolved config plus `resolved_config.yaml.provenance.json`
(base/overlay/resolved sha256). The run manifest records the resolved hash, so a
published table can be traced back to the exact configuration.

`--set` accepts per-run paths and topics only. Any algorithmic or shared protocol
key is rejected.

## Auditing

```bash
# the checked-in overlays (CI gate)
python3 scripts/audit_method_config_diff.py --base src/config/paper/viode_common.yaml
python3 scripts/audit_method_config_diff.py --base src/config/paper/euroc_common.yaml

# the configs a finished run tree actually used
python3 scripts/audit_method_config_diff.py --resolved results/<tag>
```

The audit fails when:

- two methods differ in a key neither of them owns;
- they differ in a shared protocol key (`__forbidden_differences__`), even if an
  overlay lists it;
- a method omits a key others declare (an omitted key falls back to a C++ default
  and hides the difference from this very audit);
- `sem_policy_dynamic_level != -1` anywhere — that feeds the ground-truth dynamic
  level of the scene into the online estimator;
- `failure_detection_enable != 1` anywhere;
- factor adaptation is on for some methods but not all;
- `U` and `U+W` differ in anything other than `sem_geodf_backend_weight`.

Backend weight semantics (publication / precision interpretation):
`sem_geodf_backend_weight` enables Σ_w = Σ / w via √w residual/Jacobian scaling.
Derived `w` is a precision/trust multiplier from online risk heuristics — not a
calibrated posterior inlier probability. Huber is applied on the already √w-scaled
residual. `min(w_i, w_j)` is a conservative heuristic, not covariance propagation.

## Adding a knob

Declare it in **both** backbones at its off/neutral value, then let the owning
overlay turn it on. Never introduce a key in an overlay alone: the audit cannot
compare a key that only one method mentions.
