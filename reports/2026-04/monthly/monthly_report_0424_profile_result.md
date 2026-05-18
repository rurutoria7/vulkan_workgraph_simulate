* 實驗設定

* work graphs
    * overview
        ![alt text](assets/profile/image-3.png)
    * wavefront
        ![alt text](assets/profile/image-2.png)
    * only 1 wavefront at beginning
        ![alt text](assets/profile/image-8.png)
    * wavefront - select compute
        ![alt text](assets/profile/image-10.png)
    * overview - depth 8
        ![alt text](assets/profile/image-15.png)
    * wavefront - depth 8
        ![alt text](assets/profile/image-16.png)
    * wavefront - depth 8 select compute
        ![alt text](assets/profile/image-22.png)



* vulkan persistent thread
    * overview
        ![alt text](assets/profile/image-4.png)
    * wavefront
        ![alt text](assets/profile/image-5.png)
    * wavefront - select compute
        ![alt text](assets/profile/image-9.png)
    * overview - depth 8
        ![alt text](assets/profile/image-11.png)
    * wavefront - select compute - depth 8
        ![alt text](assets/profile/image-12.png)
    * depth 8, no active wavefront but lots of r/w
        ![alt text](assets/profile/image-13.png)
    * depth 8， 3072 group, overview
        ![alt text](assets/profile/image-17.png)
    * depth 8, 3072 group, wavefront
        ![alt text](assets/profile/image-18.png)
    * depth 8, 3072 group, reverse node b c ratio, overview
        ![alt text](assets/profile/image-20.png)
    * depth 8, 3072 group, reverse node b c ratio, wavefront
        ![alt text](assets/profile/image-21.png)

## Profile summary table

| 組別 | FPS | duration | compute duration |
|---|---:|---:|---:|
| wg | 6,920.1 Hz | 144.507 us | 34.791 us |
| pt-96 | 298.6 Hz | 3,349.001 us | 3,174.813 us |
| wg8 | 6,157.8 Hz | 162.397 us | 50.183 us |
| pt8-96 | 19.5 Hz | 51,407.951 us | 51,078.034 us |
| pt8-96x32 | 6.5 Hz | 153,038.548 us | N/A |
| pt8-96x32-reverse-worker | 6.3 Hz | 157,964.844 us | N/A |

Note: `compute duration` is read from the right-side Selection `Duration` field in the select-compute screenshots. `pt8-96x32` and `pt8-96x32-reverse-worker` only have wavefront screenshots in this Markdown, so their compute duration is not recorded here.

## Profile 數據整理

| 方法 | 圖片來源 | Frame duration | FPS / frame rate | 備註 |
|---|---:|---:|---:|---|
| Work Graphs | `assets/profile/image-3.png` | 144.507 us | 6,920.1 Hz | baseline |
| Vulkan persistent thread | `assets/profile/image-4.png` | 3,349.001 us | 298.6 Hz | GPU 持續執行時間明顯較長 |

* 爲什麽 work graphs 一開始只有 1 個 wavefront? trace code 來回答我

* persistent thread 版本裏面， wavefront 一直沒有變，是不是代表只要 persistent thread alive 就會被算作 active wave? 如果是如此，那麽我們 dispatch 的 workgroup 數量
* --> 實驗，
