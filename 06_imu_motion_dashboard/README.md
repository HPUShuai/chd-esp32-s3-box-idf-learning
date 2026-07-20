# 06 ICM42607 姿态与运动仪表盘

本工程基于 `05_lvgl_interactive_dashboard`，在天气仪表盘中加入板载
ICM42607 六轴 IMU、姿态解算和实时曲线。页面启动时默认为
手动切换，原有 BOOT 按键短按、双击、长按功能继续保留。

## 本课功能

- GPIO8 作为 SDA、GPIO18 作为 SCL，使用 ESP-IDF 5.x 新版 I²C Master API。
- 启动时扫描 `0x08` 到 `0x77`，在串口中打印所有应答地址。
- 自动尝试 ICM42607 的 `0x68` 和 `0x69` 地址。
- 读取 `WHO_AM_I` 寄存器 `0x75`，识别 P 版 `0x60` 和 C 版 `0x61`。
- 以 100 Hz ODR、±4 g、±500 dps、低噪声模式运行传感器。
- 独立任务以 50 Hz 读取三轴加速度、三轴角速度和芯片温度。
- 启动时采集 200 点完成平放静止零偏校准。
- 使用加速度计和陀螺仪互补滤波计算横滚角、俯仰角。
- 计算 0% 到 100% 的运动强度。
- 长度为 1 的 FreeRTOS 队列只保留最新样本，UI 以 25 Hz 更新。
- 第四个 LVGL 页面显示姿态数值、三轴原始量和两条实时曲线。
- 使用 BOOT 按键手动切换页面。

## 硬件与寄存器

| 项目 | 配置 |
| --- | --- |
| I²C SDA | GPIO8 |
| I²C SCL | GPIO18 |
| I²C 频率 | 400 kHz |
| ICM42607 地址 | 0x68 或 0x69 |
| WHO_AM_I | 寄存器 0x75；P 版为 0x60，C 版为 0x61 |
| 加速度计 | ±4 g，8192 LSB/g，100 Hz |
| 陀螺仪 | ±500 dps，65.5 LSB/(°/s)，100 Hz |

## 启动校准

上电或复位前，把开发板屏幕朝上放在水平桌面，并保持约 2 秒不动。程序会：

1. 丢弃传感器刚启动时的 10 个样本。
2. 采集 200 个静止样本。
3. 检查板子是否平放、采样波动是否足够小。
4. 计算三轴陀螺仪零偏，以及 X/Y/Z 加速度静态偏差。

如果校准检查未通过，程序最多重试三次；之后仍会显示实时数据，但第四页会标记
“未校准”。重新启动并保持板子平放即可再次校准。

## 页面与交互

页脚的 `M 1/4` 表示手动模式，`A 1/4` 表示自动轮播模式。

| 操作 | 功能 |
| --- | --- |
| BOOT 短按 | 切换到下一页 |
| BOOT 双击 | 切换自动轮播/手动翻页 |
| BOOT 长按 | 在 25%、50%、75%、100% 亮度间切换 |

第四页左侧显示横滚角、俯仰角和运动强度；右上曲线中青色为 ROLL，紫色为
PITCH；右下显示 ACC 和 GYR 的 X/Y/Z 实时值。

## 工程结构

```text
main/
├─ bus/
│  └─ i2c_bus.c, i2c_bus.h       # GPIO8/18 I²C 初始化和地址扫描
├─ imu/
│  ├─ icm42607.c, icm42607.h     # 寄存器驱动、采样和静止校准
│  └─ motion.c, motion.h         # 互补滤波和运动强度
├─ input/button.c                # BOOT 按键手势
├─ settings/settings.c           # NVS 保存模式、亮度和当前页
├─ display/lcd_lvgl.c            # ILI9341 与背光 PWM
├─ network/network.c             # Wi-Fi 与 SNTP
├─ weather/weather.c             # 天气和空气质量
├─ ui/dashboard.c                # 四个 LVGL 页面和实时姿态曲线
└─ main.c                        # 采样任务、UI任务、队列和业务协调
```

## 配置、编译与烧录

先从模板生成本地私密配置并填写 Wi-Fi、天气 API 信息：

```powershell
Copy-Item main/private_config.example.h main/private_config.h
```

`main/private_config.h` 已被 `.gitignore` 排除，不能提交到 GitHub。

使用 ESP-IDF 5.5.4 编译：

```powershell
idf.py set-target esp32s3
idf.py build
idf.py -p COM12 flash monitor
```

## 串口预期信息

正常情况下可以看到类似日志：

```text
I (...) i2c_bus: I2C ready: SDA=GPIO8 SCL=GPIO18 speed=400000 Hz
I (...) i2c_bus: I2C device found at 0x68
I (...) icm42607: ICM42607-P found at 0x68, WHO_AM_I=0x60
I (...) icm42607: static calibration complete
```

本板实测安装的是返回 `0x60` 的 ICM42607-P；随板资料中的 ICM42607-C
数据手册对应 `0x61`。驱动会严格接受这两个已知型号，其他 ID 不会被误识别。
若总线扫描超时，应检查 GPIO8/GPIO18、板载上拉和是否有其他模块占用总线。

## 验收清单

1. 默认页脚显示 `M 1/4`，页面不会自动轮播。
2. 串口打印 I²C 扫描结果和本板的 `WHO_AM_I=0x60`。
3. 平放静止时横滚角、俯仰角接近 0°，陀螺仪三轴接近 0 dps。
4. 倾斜板子时姿态数字和两条曲线连续变化。
5. 网络请求期间，IMU 曲线和 BOOT 按键仍保持响应。
