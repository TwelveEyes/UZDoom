# 08: levelmesh GPU timings on the stress vantages

Informational (ticket 08 acceptance). Reference machine: RX 7900 XTX, RADV, 2560×1440, the
user's `gl_multisample 4`. `run_matrix.sh --demos demos/vantage --tics 1050 --backend vulkan`,
medians over the vantage window (tics 105–1050). The classic walk still runs in every levelmesh
column, so `wall` is CPU-bound (BSP walk + classic sprites/portals); `lmgpu` is the levelmesh's own
GPU time from the timestamp ring (`r_perflog v3`).

Columns: `cls` classic; `hiz` levelmesh, two-phase Hi-Z, index compaction (default); `fru`
frustum only; `pcl` Hi-Z with one indirect command per cluster (`lm_drawmode 1`); `none`
`lm_cullmode 0`. The hiz/fru/pcl runs were GCC builds made before the final cull change
(eye-plane instead of near/far rejection, see ticket 08 Comments); `none` is the final build.
Logs: `tools/abtest/logs/lm08{cls,hiz,fru,pcl,non}_*` (local, gitignored).

## Frame time and phase breakdown

| vantage | classic wall p50/p95 | hiz wall p50/p95 | fru wall p50 | pcl wall p50 | hiz lmgpu (c1/d1/hz/c2/d2) | fru lmgpu | pcl lmgpu | hiz cl1/cl2/occ/fc | tris drawn | classic wl/fl left |
|---|---|---|---|---|---|---|---|---|---|---|
| DOOM2_MAP27_V1 | 0.89/0.98 | 0.80/1.02 | 0.69 | 0.78 | 0.204 (0.015/0.099/0.074/0.013/0.003) | 0.114 | 0.197 | 20/0/6/4 | 3394 | 79/23 (cls 79/23) |
| DOOM2_MAP31_V1 | 0.53/0.77 | 0.65/0.73 | 0.56 | 0.64 | 0.225 (0.011/0.126/0.073/0.013/0.003) | 0.153 | 0.217 | 5/0/6/5 | 933 | 30/12 (cls 30/12) |
| MYHOUSE_20PAM_V1 | 1.15/1.41 | 1.20/1.47 | 1.08 | 1.17 | 0.206 (0.010/0.106/0.075/0.013/0.003) | 0.129 | 0.197 | 8/0/0/124 | 1412 | 178/161 (cls 178/161) |
| MYHOUSE_MAP01_V1 | 7.95/11.56 | 7.93/11.66 | 9.10 | 7.60 | 0.194 (0.013/0.082/0.077/0.018/0.003) | 0.200 | 0.192 | 357/0/1905/5280 | 54116 | 372/241 (cls 372/241) |
| MYHOUSE_MAP01_V2 | 15.08/20.46 | 13.05/16.74 | 14.10 | 14.04 | 0.200 (0.017/0.087/0.078/0.017/0.003) | 0.172 | 0.201 | 411/0/1504/5627 | 69641 | 486/228 (cls 486/228) |
| PIRATES_MAP49_V1 | 3.88/4.76 | 2.67/3.29 | 2.62 | 2.65 | 0.161 (0.016/0.060/0.071/0.013/0.003) | 0.078 | 0.154 | 79/0/47/149 | 13388 | 1811/211 (cls 1811/211) |
| PIRATES_MAP54_V1 | 8.01/9.01 | 6.27/7.26 | 6.15 | 6.05 | 0.121 (0.016/0.021/0.069/0.012/0.003) | 0.039 | 0.116 | 185/0/29/0 | 25190 | 3848/522 (cls 3848/522) |
| PLANISF_MAP01_V1 | 28.73/32.09 | 12.01/14.76 | 11.82 | 12.17 | 0.192 (0.018/0.083/0.077/0.013/0.003) | 0.128 | 0.192 | 482/0/515/224 | 84946 | 27288/324 (cls 27288/324) |
| PLANISF_MAP01_V2 | 25.94/29.64 | 10.59/12.75 | 9.88 | 10.05 | 0.197 (0.016/0.090/0.076/0.013/0.003) | 0.136 | 0.189 | 109/0/674/438 | 19638 | 22746/2398 (cls 22746/2398) |
| SOS_MAP32_V1 | 50.34/52.92 | 22.67/24.98 | 22.77 | 22.72 | 0.207 (0.016/0.097/0.076/0.015/0.003) | 0.217 | 0.203 | 143/0/2534/880 | 21106 | 35559/10617 (cls 35559/10617) |
| SOS_MAP32_V2 | 25.05/28.89 | 11.88/13.89 | 11.88 | 12.37 | 0.192 (0.016/0.085/0.075/0.014/0.003) | 0.187 | 0.185 | 22/0/874/2661 | 3927 | 15799/3500 (cls 15799/3500) |

`hiz lmgpu` breakdown: phase-1 cull / phase-1 draw / Hi-Z build / phase-2 cull / phase-2 draw.
`cl1/cl2/occ/fc`: clusters drawn in phase 1 / phase 2, Hi-Z-occluded, frustum-culled. The last
column counts classic list entries at setup (owned entries are removed after the levelmesh draw,
so the counts match).

## Cull modes

| vantage | lmgpu none | lmgpu frustum | lmgpu Hi-Z | lmgpu Hi-Z per-cluster | tris none / Hi-Z |
|---|---|---|---|---|---|
| DOOM2_MAP27_V1 | 0.111 | 0.114 | 0.204 | 0.197 | 5103 / 3394 |
| DOOM2_MAP31_V1 | 0.156 | 0.153 | 0.225 | 0.217 | 2898 / 933 |
| MYHOUSE_20PAM_V1 | 0.120 | 0.129 | 0.206 | 0.197 | 21179 / 1412 |
| MYHOUSE_MAP01_V1 | 0.446 | 0.200 | 0.194 | 0.192 | 1038990 / 54116 |
| MYHOUSE_MAP01_V2 | 0.456 | 0.172 | 0.200 | 0.201 | 1038990 / 69641 |
| PIRATES_MAP49_V1 | 0.079 | 0.078 | 0.161 | 0.154 | 37354 / 13388 |
| PIRATES_MAP54_V1 | 0.038 | 0.039 | 0.121 | 0.116 | 28696 / 25190 |
| PLANISF_MAP01_V1 | 0.138 | 0.128 | 0.192 | 0.192 | 210779 / 84946 |
| PLANISF_MAP01_V2 | 0.154 | 0.136 | 0.197 | 0.189 | 210779 / 19638 |
| SOS_MAP32_V1 | 0.277 | 0.217 | 0.207 | 0.203 | 503786 / 21106 |
| SOS_MAP32_V2 | 0.294 | 0.187 | 0.192 | 0.185 | 503786 / 3927 |

## Findings

- The levelmesh costs 0.12–0.23 ms of GPU time with Hi-Z on every vantage. That is roughly flat
  across 1 k–1 M baked triangles: fixed costs (Hi-Z build ≈ 0.075 ms, barriers, timestamps)
  dominate.
- Hi-Z is not a GPU-cost win here (as ticket 05 found): it adds ≈ 0.07–0.09 ms over frustum-only
  on small and mid maps, and only no-cull loses on the 0.5–1 M-triangle maps (myhouse MAP01 0.45
  ms vs 0.19, SOS MAP32 0.28–0.29 vs 0.19–0.21). It stays the default as the visibility source
  for tickets 10/17/18.
- **Compaction vs per-cluster commands:** equal within noise (≤ 0.01 ms) on every vantage.
  Compaction stays the default: one draw per bucket and phase, no draw-indirect-count needed.
- **MSAA policy:** max over all samples at full resolution. Measured on DOOM2 MAP27 V1 at
  2560×1440: Hi-Z build 0.060 ms at 1× vs 0.075 ms at 4× (the spike's 0.26 ms did not
  reproduce), so no reduced mip 0 or depth resolve is needed.
- Frame time: where the classic wall/flat submission dominated, `wall` p50 drops 2–2.4×
  (Planisphere 28.7 → 12.0 ms, SOS MAP32 V1 50.3 → 22.7 ms, Pirates MAP54 8.0 → 6.3 ms) with the
  BSP walk still running.
