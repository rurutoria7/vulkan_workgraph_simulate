# WorkGraph performance inconsistency investigation

日期：2026-05-28

目標：釐清為什麼先前實驗的 baseline/best 約為 `30 ms / 0.99 ms`，但目前重跑看起來變成 `40 ms / 1.9 ms`。

## 結論

這次不支持「main shader 或 request batching patch 造成整體 regression」。

分開看有兩個原因：

1. `best 0.99 ms -> 1.9 ms` 是目前 AMD/Vulkan 執行狀態不穩定造成的量測狀態差異，不是程式碼差異。用同一個舊版 optimized commit `5e57e873` 和目前 HEAD 都能在同一慢狀態下重現約 `1.9 ms`；之後明確用 `--gpu 0` 跑 RX 7900 XTX，best 立刻回到約 `1.00 ms`，後續 default run 也維持約 `1.00 ms`。
2. `baseline 30 ms -> 40 ms` 是比較了不同 baseline。`30 ms` 來自舊 `fix-wave-batched-atomics` worktree 的 Q1 dequeue batching (`--wg-q1-deq-batch --wg-no-q2-deq-batch`)；`40 ms` 是 scalar Q1 baseline (`--wg-no-q1-deq-batch`) 或 current main 的 unsharded scalar path。這兩者不是同一設定。

目前已知的內部 driver 機制仍未知；證據只支持把它標成 AMD driver / switchable graphics / GPU performance state 類問題，而不是 shader bottleneck 結論。

## 環境觀察

- Windows：Windows 10 Pro，build `26200`
- Vulkan loader：`1.4.341`
- GPU0：AMD Radeon RX 7900 XTX
- GPU1：AMD Radeon(TM) Graphics
- RX 7900 XTX Vulkan driver：`26.5.2 (LLPC)`，driverVersion `2.0.388`
- Windows display driver：`32.0.31007.5012`，Radeon Software `26.5.2`，driver date `2026-05-12`
- Vulkan device limit `timestampPeriod = 10`
- Vulkan instance layer 有 `VK_LAYER_AMD_switchable_graphics`

這和 2026-05-10 backbone 的 fast data 時間點不同；但本次沒有回退 driver，因此不能把「driver 更新」單獨證成唯一根因。

## 驗證摘要

所有 timing run 都使用 `--wg-timestamps-only`。

| Case | 設定 | Median compute |
| --- | --- | ---: |
| current HEAD 初始慢狀態 | default `Q1/Q2=256` | `1.9205 ms` |
| current HEAD 初始慢狀態 | unsharded scalar | `42.9703 ms` |
| current HEAD warmup 0/1/3/runtime10 | default `Q1/Q2=256` | `1.8968-1.9050 ms` |
| old optimized `5e57e873` 慢狀態 | default `Q1/Q2=256` | `1.8957 ms` |
| current HEAD 明確 `--gpu 0` | default `Q1/Q2=256` | `1.0033 ms` |
| current HEAD 後續 default 重跑 | default `Q1/Q2=256` | `1.0040-1.0046 ms` |
| old optimized `5e57e873` 後續 default 重跑 | default `Q1/Q2=256` | `1.0006 ms` |
| current HEAD after recovery | `Q1s16/Q2s16`, lane-pop on | `3.9630 ms` |
| current HEAD after recovery | unsharded scalar | `43.2250 ms` |
| `fix-wave-batched-atomics` | scalar Q1 off / Q2 off | `39.2028 ms` |
| `fix-wave-batched-atomics` | Q1 batch on / Q2 off | `30.9417 ms` |

歷史 CSV 也支持同一判斷：

| Historical file | Median compute | Reset median | Reset barrier median | Render median |
| --- | ---: | ---: | ---: | ---: |
| `main_integrated_repeat_1.csv` | `0.9905 ms` | `0.0042 ms` | `0.0033 ms` | `0.0155 ms` |
| `candidate_cleaned_max256_defaults_timestamps.csv` | `1.8629 ms` | `0.0088 ms` | `0.0070 ms` | `0.0291 ms` |

慢狀態不是只有 compute 變慢，reset/barrier/render timestamp 也接近倍增，這更像 GPU/driver performance state 或 timing state，而不是 queue shader 的單一新瓶頸。

## 建議量測規則

1. 之後所有可比較的 clean timing command 都明確加 `--gpu 0 --wg-timestamps-only`。
2. run manifest 記錄 GPU index、Vulkan driverInfo、Windows display driver、timestampPeriod。
3. 每輪 sweep 前先跑一個 default 256-shard preflight；若 median 大於 `1.2 ms`，標記為 invalid state，不要把後續結果寫進結論。
4. baseline 報告必須標明是哪個 baseline：current scalar baseline 約 `40 ms`，舊 Q1-batched baseline 約 `31 ms`，兩者不可混用。

