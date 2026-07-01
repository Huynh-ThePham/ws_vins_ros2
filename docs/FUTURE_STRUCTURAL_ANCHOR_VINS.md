# Future Paper Note: Structural-Anchor VINS

This direction is intentionally **out of scope for the current SGTA-VINS paper**.

## Core Idea

Use static structural anchors such as **planes, lines, and ground/wall regions** to keep VIO stable when dynamic objects occupy large image areas.

Unlike semantic dynamic masking, this direction does not only remove moving-object features. It actively prefers and constrains features that belong to persistent static scene structure.

## Motivation

Semantic filtering does not scale perfectly:

- Dynamic-capable object classes are not always moving.
- Unknown object classes can still move.
- YOLO false positives can remove useful static features.
- In crowded scenes, masking may leave too few reliable points.

Structural anchors provide a complementary signal: walls, ground planes, lane-like lines, building edges, and other long-lived structures are usually more reliable than object surfaces.

## Possible Method

1. Detect static structural primitives:
   - ground / wall semantic regions,
   - line segments,
   - dominant planes,
   - Manhattan-world directions when available.

2. Maintain feature quotas:
   - reserve part of `max_cnt` for structural regions,
   - avoid all features being selected on vehicles/people,
   - prefer long-lived static anchors during dynamic bursts.

3. Add structural constraints:
   - line reprojection residuals,
   - point-to-plane or plane-normal consistency,
   - ground-plane height/orientation priors,
   - optional Manhattan direction factors.

4. Evaluate separately:
   - dynamic VIODE scenes,
   - urban driving sequences,
   - indoor/outdoor scenes with strong planes,
   - ablation: points only vs structural quota vs structural factors.

## Why Not In Current Paper

Adding this now would require:

- line/plane extraction,
- temporal association of structural primitives,
- new backend factors,
- additional diagnostics,
- new datasets and ablations.

That would blur the current SGTA contribution, which is focused on semantic-geometric-inertial dynamic feature uncertainty.

## Paper Potential

Potential title:

**Structural-Anchor VINS: Dynamic-Robust Visual-Inertial Odometry with Persistent Planar and Linear Scene Support**

Potential claim:

> Dynamic robustness should not rely only on removing dynamic-object features. VIO can be stabilized by explicitly preserving and constraining persistent static scene structure.

