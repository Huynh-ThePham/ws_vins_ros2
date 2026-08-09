# Phase 3.6 — Trajectory Accuracy First

## HEAD lock

- Branch: `paper/sem-geodf-vins-2026`
- Pre-Phase-3.6 HEAD (confirmed after fetch): `017f8dd0a1723f60afc55a7f8ddf1bd0a8f6b08f`
- Phase 3.5 decision: adaptive arbitration v2 **rejected as paper default**
  (`docs/PHASE3_5_ADAPTIVE_ARBITRATION_V2.md`).

## BEFORE (do not overwrite)

Canonical Phase 3.5 paired U+W numbers are the Phase 3.6 BEFORE reference.
They live under:

- `experiments/phase3_5/`
- `results/sem_geodf_ablation/p35-stageb-970fcf0/`
- `results/sem_geodf_ablation/p35-holdout-citynight-970fcf0/`

### U+W BEFORE (N=5 TRAIN, from Phase 3.5 Stage C)

| Scene | U+W ATE mean±std |
|---|---:|
| MH_03_medium | 0.256908±0.000021 |
| MH_04_difficult | 0.456913±0.012887 |
| city_day_2_mid | 0.163623±0.009071 |
| city_day_3_high | 0.264292±0.028753 |

### U+W BEFORE extras

| Scene | Source | U+W ATE |
|---|---|---:|
| MH_05_difficult | BEFORE tag `before-ate-92c48f8` median N=3 | 0.2921 |
| city_night_3_high | Phase 3.5 hold-out N=3 | 0.346529±0.000272 |

New Phase 3.6 result trees must use distinct tags (`p36-*`). Never rewrite
`before-ate-*` or `p35-*`.

## Mechanism isolation (P0)

Phase 3.5 confound: `w_d ≈ 1` ⇒ `w ≈ q_m`. Four methods share the same
backbone / seed / bag / calibration:

| ID | Method overlay | Owns |
|---|---|---|
| M0 | `union_weight` | fixed U+W |
| M1 | `qm_only` | U+W + `visual_adaptive_quality` |
| M2 | `arbitration_only` | v2 + `sem_arb2_force_qm_one` |
| M3 | `adaptive_arbitration_v2` | full v2 (`q_m` + arbitration) |

Target decomposition:

```text
ΔE_total = ΔE_qm + ΔE_arb + ΔE_interaction
```

## Covariance-aware q_m (candidate Q3)

Overlay `qm_stereo` enables `visual_quality_stereo_aware` on top of M1 so that
disparity / triangulation angle / stereo reprojection / border proximity inflate
measurement covariance (precision↓ when quality↓). Dynamic weight `w_d` stays
separate.

## Protocol

```bash
# N=3 mechanism isolation (train + hold-out)
PROTOCOL_TAG=p36-mech-n3-<sha> N=3 ADAPTATION_MODE=off \
  METHODS="union_weight qm_only arbitration_only adaptive_arbitration_v2 qm_stereo" \
  bash scripts/run_ate_matrix.sh
```

Acceptance: paired same seeds, coverage/SR conserved, no GT/sequence branches,
no cherry-pick. Proposed keeps only mechanisms with isolated ATE evidence.


## Status (after N=3 + N=5)

- N=3 isolation tag: `p36-mech-n3-59440f8` — see `docs/PHASE3_6_N3_MECHANISM_ISOLATION.md`
- N=5 final tag: `p36-final-n5-653defa` — see `docs/PHASE3_6_N5_FINAL.md`
- **Proposed:** `adaptive_arbitration_v2` (TRAIN −4.40% vs U+W, bootstrap CI excludes 0)
- Rejected as Proposed: `qm_only` alone, `arbitration_only`, `qm_stereo`
