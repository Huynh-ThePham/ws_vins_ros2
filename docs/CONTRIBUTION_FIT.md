# Contribution fit audit — Sem-GeoDF

## Verdict

**Không nên nộp IEEE RA-L với bộ đóng góp/kết quả hiện tại.**  
**Chuyển hẳn sang IEEE Access** (primary venue).

## Evidence (current `paper_postfix` ATE)

| Check | Finding |
|-------|---------|
| Mean ATE (12 VIODE cells) | baseline 0.298 · adaptive 0.302 · **sad_sem 0.210** · **sem_geodf 0.213** |
| #best cells | sad_sem **6** · sem_geodf **3** · baseline 2 · adaptive 1 |
| vs GeoDF-Adaptive (±3%) | fusion tốt hơn rõ trên hold-out động cao (**OK**) |
| vs SAD-Sem (±3%) | fusion **không** thắng (≈ hòa / thua nhẹ) |
| Novelty vs literature | gated OR + online policy — **incremental** so với DynaSLAM/DynaVINS |
| Data | chủ yếu **VIODE mô phỏng**; EuRoC tĩnh |
| Gaps RA-L hay hỏi | real-world bag, matched SOTA numbers, clear win vs strong semantic baseline |

## RA-L bar (scope)

RA-L yêu cầu *“innovative research ideas and … significant … findings”* trong letter ngắn.  
Reviewer RA-L thường reject nếu:

1. Method là engineering fusion của hai ý đã có,  
2. Proposed **không outperform** ablation semantic mạnh,  
3. Thiếu real data / external SOTA.

→ Xác suất accept RA-L với package hiện tại: **thấp**.

## Vì sao Access phù hợp hơn

- Scope rộng: reproducible systems + applied VIO OK.  
- Không ép “letter-level novelty” như RA-L.  
- Length linh hoạt → kể đủ protocol, failure modes, static-safety story.  
- Claim chân thực: *robustness/static-safety + gains vs geometry-only*, không cần “best ATE everywhere”.

## What we keep claiming (Access)

- Label-free adaptive gated **OR** fusion (not AND).  
- Strong hold-out gains vs **GeoDF-Adaptive**.  
- Better static safety than always-on **SAD-Sem**.  
- Fair bag-rate + train/hold-out protocol.

## What we do **not** claim

- State-of-the-art vs DynaVINS.  
- Best method on every VIODE cell.  
- Solved high-dynamic parking universally.
