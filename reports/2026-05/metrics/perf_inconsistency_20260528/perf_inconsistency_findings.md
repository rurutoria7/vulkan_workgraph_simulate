# WorkGraph performance inconsistency investigation

日期：2026-05-28

目標：釐清為什麼先前實驗的 baseline/best 約為 `30 ms / 0.99 ms`，但目前重跑看起來變成 `40 ms / 1.9 ms`。

## 結論

這次不支持「main shader 或 request batching patch 造成整體 regression」。

分開看有兩個原因：

1. `best 0.99 ms -> 1.9 ms` 是目前 AMD/Vulkan 執行狀態不穩定造成的量測狀態差異，不是程式碼差異。用同一個舊版 optimized commit `5e57e873` 和目前 HEAD 都能在同一慢狀態下重現約 `1.9 ms`；第一次調查中，明確用 `--gpu 0` 跑 RX 7900 XTX 後，best 立刻回到約 `1.00 ms`，後續 default run 也維持約 `1.00 ms`。後續重測顯示 `--gpu 0` 不是穩定的修復開關，只能說那一次剛好和外部 GPU/driver 狀態切換同時發生。
2. `baseline 30 ms -> 40 ms` 是比較了不同 baseline。`30 ms` 來自舊 `fix-wave-batched-atomics` worktree 的 Q1 dequeue batching (`--wg-q1-deq-batch --wg-no-q2-deq-batch`)；`40 ms` 是 scalar Q1 baseline (`--wg-no-q1-deq-batch`) 或 current main 的 unsharded scalar path。這兩者不是同一設定。

目前已知的內部 driver 決策機制仍未知；但後續 ADL PMLog 取樣已把慢狀態收斂到 RX 7900 XTX 的 GFX clock / DVFS state。optimized default path 在目前慢狀態下只跑在約 `1.45-1.50 GHz`，沒有 throttle flag；用 concurrent unsharded scalar anchor 把同一張卡維持在約 `3.16 GHz` 時，同一個 default timing 回到 `1.003 ms`。因此 `~1.9 ms` vs `~1.0 ms` 的主因是 GPU clock state，而不是 shader/code regression。AMD switchable graphics layer、Steam/RenderDoc implicit layer、雙 AMD GPU 枚舉本身，目前都沒有被證成是充分原因。

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

## 2026-05-28 follow-up：`--gpu 0` 不是穩定修復

新增受控重跑資料放在 `reports/2026-05/metrics/perf_inconsistency_20260528/controlled_runs/`。

| Case | 控制變因 | Median compute | 判讀 |
| --- | --- | ---: | --- |
| `slow_default_pre1` | default | `1.8744 ms` | 慢狀態仍可重現 |
| `slow_default_pre2` | default | `1.9014 ms` | 慢狀態穩定 |
| `explicit_gpu0` | 加 `--gpu 0` | `1.9106 ms` | 這次沒有恢復到 `~1.0 ms` |
| `default_post_gpu0` | `--gpu 0` 後再跑 default | `1.8620 ms` | 後續 default 也沒有恢復 |
| `disable_nonamd_layers` | 停用 Steam overlay/fossilize 與 RenderDoc capture env | `1.8738 ms` | 非 AMD implicit layers 不是充分原因 |
| `disable_all_known_implicit` | 停用已知 AMD/Steam/RenderDoc layer env | `1.8845 ms` | 未恢復 |
| `loader_disable_implicit` | `VK_LOADER_LAYERS_DISABLE=~implicit~` | `2.4938 ms` | 未恢復，且前段更慢 |
| `loader_filter_dgpu_744c` | `VK_LOADER_DEVICE_ID_FILTER=0x744c` | `2.4923 ms` | 只篩 dGPU 仍未恢復 |
| `vk_driver_files_dgpu_json` | `VK_DRIVER_FILES=<RX 7900 XTX amd-vulkan64.json>` | `1.8776 ms` | 指定 dGPU ICD 仍未恢復 |
| `vk_icd_filenames_dgpu_json` | `VK_ICD_FILENAMES=<RX 7900 XTX amd-vulkan64.json>` | `1.9092 ms` | 指定 dGPU ICD 仍未恢復 |
| `vk_driver_files_igpu_json` | 指定 iGPU ICD | failed | iGPU path 不能作為可比較結果 |

這輪重要修正：第一次看到 `--gpu 0` 後恢復，不能解讀成 default 和 `--gpu 0` 走了不同 shader 或不同裝置。程式碼上 default 已選 device index 0；後續受控重跑也證明 `--gpu 0` 不會穩定讓 `1.9 ms` 變回 `1.0 ms`。

## 外部程序檢查

Vulkan loader 環境：

- `VK_LAYER_AMD_switchable_graphics` 有兩份 manifest，loader 會移除 duplicate。
- Epic EOS overlay registry 指到不存在的 JSON，loader 會報 failed-to-open，但停用 implicit layer 後沒有回到 fast。
- Steam overlay/fossilize、RenderDoc capture layer 的停用測試沒有回到 fast。

Vanguard / Parsec 檢查：

| Case | 狀態 | Default median compute |
| --- | --- | ---: |
| `after_tools_closed_default_r1` | `vgk`/`vgc` stopped；Parsec service still running | `1.8984 ms` |
| `after_tools_closed_default_r2` | same | `1.9048 ms` |
| `after_tools_closed_default_r3` | same | `1.9033 ms` |

Vanguard 目前不是可觀察到的充分原因，因為 `vgk` kernel driver 與 `vgc` service 都是 stopped 後，default 還是 `~1.90 ms`。Parsec 還沒有完全排除，因為前台 app 關掉後，`Parsec` service 仍在跑，PID `6532`，image `C:\Program Files\Parsec\pservice.exe`。要完整排除 Parsec，需要在不依賴 Parsec 遠端連線的前提下停掉該 service 後再重跑同一組 default preflight。

另外，`Win32_VideoController` 仍列出 `Parsec Virtual Display Adapter` 與 `Parsec Virtual USB Adapter`，狀態為 `OK`。`WmiMonitorID` 只看到實體 BenQ monitor 為 active，所以 Parsec virtual display 目前不像 active display；但只要 service/virtual adapter 仍在，就不能把 Parsec 完整排除。

## ADL PMLog clock evidence

ADL adapter mapping 顯示 RX 7900 XTX 是 `adapterIndex=5`，`PNPString` 為 `PCI\VEN_1002&DEV_744C...`，`\\.\DISPLAY1`。

| Case | Median compute | GFXCLK median | GFX activity median | Board power median | Throttle |
| --- | ---: | ---: | ---: | ---: | ---: |
| `adl_pmlog_default_3000f_20260528` | `1.8900 ms` | `1472 MHz` | `36.5%` | `32.5 W` | `0` |
| `adl_gears_then_workgraph_20260528` / `during_gears_4k` | n/a | `1645.5 MHz` | `69%` | `59 W` | `0` |
| `adl_gears_then_workgraph_20260528` / workgraph after gears | `1.9033 ms` | `1469 MHz` | `37%` | `33 W` | `0` |
| `adl_pmlog_unsharded_scalar_120f_20260528` | `43.2102 ms` | `3166 MHz` | `100%` | `150 W` | `0` |
| `adl_pmlog_default_after_unsharded_600f_20260528` | `1.9076 ms` | `1497 MHz` | `46%` | `35 W` | `0` |
| `adl_concurrent_long_anchor_short_default_20260528` default while anchor alive | `1.0030 ms` | `3162 MHz` | `100%` | `156 W` | `0` |

判讀：

- `~1.9 ms` slow band 對應到 optimized default workload 期間的 `~1.45-1.50 GHz` GFXCLK。
- 同一張 RX 7900 XTX 可以在 unsharded scalar workload 下升到 `~3.17 GHz`、`100%` activity、`~150 W`，所以目前慢狀態不是全域時脈上限、溫度保護或供電保護。
- 高 clock 不會自動延續到 optimized default path：unsharded scalar 結束後立刻跑 default，GFXCLK 仍掉回 `~1.5 GHz`，compute 仍是 `~1.91 ms`。
- concurrent anchor 全程存活時，default 的 median compute 從 `~1.9 ms` 回到 `1.0030 ms`，同時 GFXCLK median 是 `3162 MHz`。這是目前最直接的因果驗證：同一 default path 在高 GFXCLK band 會回到歷史 fast band。
- 第一個 concurrent anchor run 因 anchor 在 default 結束前自然結束，default median 仍是 `1.8826 ms`，但 p05 已到 `0.9922 ms`。因此只把 long-anchor/short-default run 當作乾淨因果證據。

## 目前狀態模型

目前觀察到至少三個 timing band：

- fast band：`~1.00 ms`，歷史 clean run 與第一次 `--gpu 0` 後出現。
- slow steady band：`~1.86-1.91 ms`，目前 most runs。
- cold/transition band：部分 run 前段出現 `~2.45-2.55 ms`，幾十 frame 後回到 `~1.9 ms`。

這種 banding 同時影響 compute、reset、barrier、render timestamp，比較符合 GPU/driver performance state 或 timestamp/timing state。ADL PMLog 進一步顯示目前 slow band 的直接伴隨條件是 optimized default path 被 driver 放在 `~1.5 GHz` GFXCLK band；當 concurrent anchor 把 GFXCLK 維持在 `~3.16 GHz` 時，default 回到 `~1.0 ms`。已做過 unsharded scalar heavy run、4K `gears`、hidden/minimized `vkcube` background load；這些 pre-run/background graphics load 不會讓後續 default 自動穩定維持 fast band。

歷史 CSV 也支持同一判斷：

| Historical file | Median compute | Reset median | Reset barrier median | Render median |
| --- | ---: | ---: | ---: | ---: |
| `main_integrated_repeat_1.csv` | `0.9905 ms` | `0.0042 ms` | `0.0033 ms` | `0.0155 ms` |
| `candidate_cleaned_max256_defaults_timestamps.csv` | `1.8629 ms` | `0.0088 ms` | `0.0070 ms` | `0.0291 ms` |

慢狀態不是只有 compute 變慢，reset/barrier/render timestamp 也接近倍增，這更像 GPU/driver performance state 或 timing state，而不是 queue shader 的單一新瓶頸。

## 建議量測規則

1. 之後所有可比較的 clean timing command 都明確加 `--gpu 0 --wg-timestamps-only`，但不要把 `--gpu 0` 當成狀態修復手段。
2. run manifest 記錄 GPU index、Vulkan driverInfo、Windows display driver、timestampPeriod。
3. 每輪 sweep 前先跑一個 default 256-shard preflight；若 median 大於 `1.2 ms`，標記為 invalid state，不要把後續結果寫進結論。
4. baseline 報告必須標明是哪個 baseline：current scalar baseline 約 `40 ms`，舊 Q1-batched baseline 約 `31 ms`，兩者不可混用。
5. 若要完整排除 Parsec，先確認不是透過 Parsec 遠端操作，再停 `Parsec` service，重跑三次 default preflight；若 median 仍在 `~1.9 ms`，Parsec 可降級為非充分原因。
6. 若要比較 shader optimization，先用 default preflight 判定目前 clock band；`~1.9 ms` 和 `~1.0 ms` 不能混在同一張 optimization 表內。需要 clock-normalized 結果時，另開明確標記的 high-clock anchored run。
