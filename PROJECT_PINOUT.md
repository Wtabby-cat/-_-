# CFRP 电磁感应加热控制工程说明

本工程用于 CFRP 电磁感应加热功率控制，MCU 为 STM32G474RET6。核心输出为 HRTIM1 产生的全桥 PWM，当前配置约为 20.1 kHz、50% 占空比，并开启上下桥臂死区。

## 功率桥 PWM

| 引脚 | MCU 功能    | 项目功能                 |
| ---- | ----------- | ------------------------ |
| PA8  | HRTIM1_CHA1 | 左侧上桥 PWM             |
| PA9  | HRTIM1_CHA2 | 左侧下桥 PWM             |
| PA10 | HRTIM1_CHB1 | 右侧上桥 PWM             |
| PA11 | HRTIM1_CHB2 | 右侧下桥 PWM             |
| PB1  | GPIO 输出   | PWM/驱动使能，低电平有效 |

当前 `main.c` 中 PB1 上电后保持高电平，因此外部驱动默认禁用。HRTIM 波形会启动，但功率级是否真正输出取决于 PB1 是否被拉低使能。

## 软启动继电器

硬件有两路供电通路：一路经热敏电阻（NTC）限流，另一路经继电器触点直通。上电时 PB0 默认低电平，继电器断开，电流经热敏电阻通路，限制浪涌电流；延时 `RELAY_DELAY_MS`（当前 500 ms）后 PB0 拉高，继电器吸合，触点闭合旁路热敏电阻，防止热敏电阻长期通电过热烧毁。

| 引脚 | 默认状态 | 延时后 | 说明 |
| --- | --- | --- | --- |
| PB0 | 低电平（继电器断开） | 高电平（继电器吸合） | 热敏电阻旁路控制 |

时序：上电 → DAC/COMP 初始化 → 故障保护武装 → PWM 启动 → 等待 `RELAY_DELAY_MS` → PB0 拉高。

## 信号链参数

所有可调参数集中在 `main.c` 最前面，修改硬件后只需改对应 `#define`，换算值自动派生。

### 谐振电流采样

```
1/1000 CT → 10Ω 采样电阻 → 5.6x 运放 → +1.65V 偏置 → ADC
```

| 参数 | 宏 | 值 |
| --- | --- | --- |
| CT 变比 | `I_CURR_CT_RATIO` | 1000 (1:1000) |
| 采样电阻 | `I_CURR_SENSE_R` | 10 Ω |
| 运放增益 | `I_CURR_OPAMP_GAIN` | 5.6 V/V |
| 偏置电压 | `I_CURR_BIAS_V` | 1.65 V |
| 合成系数 | `I_CURR_SCALE` | 0.056 V/A |

**换算公式：**

```
Vadc = I × 0.056 + 1.65
I = (Vadc - 1.65) / 0.056
```

### 谐振电压采样

```
1/31 分压 → 0.4x 隔离运放 → 3.3x 运放 → +1.65V 偏置 → ADC
```

| 参数 | 宏 | 值 |
| --- | --- | --- |
| 分压比 | `V_RES_DIV_RATIO` | 31 (1:31) |
| 隔离运放增益 | `V_RES_ISO_GAIN` | 0.4 V/V |
| 运放增益 | `V_RES_OPAMP_GAIN` | 3.3 V/V |
| 偏置电压 | `V_RES_BIAS_V` | 1.65 V |
| 合成系数 | `V_RES_SCALE` | 0.04258 V/V |

**换算公式：**

```
Vadc = Vres × 0.04258 + 1.65
Vres = (Vadc - 1.65) / 0.04258
```

## 保护比较器

| 保护项 | 比较器 | 正输入引脚 | 负输入基准 | DAC code | 阈值电压 | 保护动作 |
| --- | --- | --- | --- | --- | --- | --- |
| 母线过压保护 | COMP2 | PA7 / COMP2_INP | DAC1_CH2 | 3971 | 3.20 V | FAULT1（内部比较器源，filter=15） |
| 谐振过流保护 | COMP6 | PB11 / COMP6_INP | DAC4_CH2 | 3847 | 3.10 V | FAULT3（内部比较器源，filter=8） |

两路故障均为锁存保护：初始化时只清一次旧故障标志，触发后软件不再清除，需 MCU 复位恢复。

> DAC code 由阈值电压和 VREF 自动计算：`code = V_threshold / 3.3 × 4095`。DAC1 和 DAC4 是不同外设，实际输出电压以万用表实测为准。

## 过零检测比较器

用于检测谐振电流和电压的过零点，计算相位差，为后续数字锁相环（PLL）提供输入。

| 检测项 | 比较器 | 正输入引脚 | 负输入基准 | DAC code | 阈值 | 用途 |
| --- | --- | --- | --- | --- | --- | --- |
| 电流过零检测 | COMP3 | PC1 / COMP3_INP | DAC1_CH1 | 2048 | 1.65 V | 谐振电流相位检测 |
| 电压过零检测 | COMP5 | PB13 / COMP5_INP | DAC4_CH1 | 2048 | 1.65 V | 谐振电压相位检测 |

谐振电流和电压信号为叠加在 1.65 V 直流偏置上的交流正弦波（20-40kHz），比较器阈值设为 1.65 V 即可检测过零点。COMP3 和 COMP5 均配置为同相输出、无迟滞，信号高于 1.65 V 时输出高电平，低于 1.65 V 时输出低电平。

## ADC 谐振波形采样

ADC1 和 ADC2 以双路同步模式（`ADC_DUALMODE_REGSIMULT`）同时采样谐振电流和电压波形，用于计算输出功率。

| 引脚 | ADC 通道 | 采样信号 | 说明 |
| --- | --- | --- | --- |
| PA0 | ADC1_IN1 | 谐振电流 | 12-bit，DMA 循环缓冲 |
| PA1 | ADC2_IN2 | 谐振电压 | 12-bit，与 ADC1 同步采样 |

两个 ADC 以双路同步模式工作，每次触发后 ADC1 和 ADC2 在同一时刻分别采样电流和电压，数据以 32-bit 打包存入 DMA 循环缓冲区：

```
adc_dma_buf[N] = ADC2[31:16] | ADC1[15:0]
```

当前配置：

| 参数 | 值 |
| --- | --- |
| 分辨率 | 12-bit |
| 采样时间 | 12.5 ADC 时钟周期 |
| 时钟预分频 | PCLK/4 |
| 双路同步延迟 | 1 周期 |
| DMA 缓冲区大小 | 256 字（循环模式） |
| 触发源 | 软件触发（`ADC_SOFTWARE_START`） |

`HAL_ADC_ConvHalfCpltCallback` 和 `HAL_ADC_ConvCpltCallback` 分别在 DMA 半缓冲、全缓冲完成时处理 128 个采样点，取该块最后一个采样点计算谐振电流/电压的 ADC 电压和物理量，更新 Watch 变量。

## HRTIM 保护链路

| HRTIM 故障 | 来源 | 用途 | filter 值 | 滤波延迟 |
| --- | --- | --- | --- | --- |
| FAULT1 | Internal comparator / FLT_Int | 母线过压硬件关断 | 15 | 15 / 170 MHz ≈ 88 ns |
| FAULT3 | Internal comparator / FLT_Int | 过流锁存关断 | 8 | 8 / 170 MHz ≈ 47 ns |

HRTIM 时钟 = SYSCLK = 170 MHz（HSI 16 MHz × 85 / 4 / 2），fault prescaler = DIV1。

启动顺序：PB1 置高禁用外部驱动 → DAC 阈值设置 → DAC 四通道启动 → COMP2/COMP3/COMP5/COMP6 全部启动 → 延迟稳定 → 一次性清除 FLT1/FLT3 旧标志 → 同时启用 FAULT1/FAULT3 → 启动 HRTIM Timer A/B 输出 → 启动 ADC1/ADC2 双路同步 DMA → 延时 500 ms 后 PB0 拉高吸合继电器。

## Keil Watch 调试变量

`main.c` 中保留了 12 个 `volatile` 全局变量，在 Keil5 调试时加入 Watch 窗口即可观察：

| 分组 | 变量 | 类型 | 含义 |
| --- | --- | --- | --- |
| **谐振电流** | `dbg_i_adc_v` | `float` | ADC 采样电压 (V) |
| | `dbg_i_a` | `float` | 谐振电流 (A)，`(Vadc - 1.65) / 0.056` |
| **谐振电压** | `dbg_v_adc_v` | `float` | ADC 采样电压 (V) |
| | `dbg_v_res_v` | `float` | 谐振电压 (V)，`(Vadc - 1.65) / 0.04258` |
| **继电器/PWM** | `dbg_relay` | `uint32_t` | 继电器 0=关 1=开 |
| | `dbg_pwm_en` | `uint32_t` | PWM 使能 0=禁止 1=使能（已取反） |
| **保护状态** | `dbg_ovp` | `uint32_t` | COMP2 过压实时输出电平 |
| | `dbg_ocp` | `uint32_t` | COMP6 过流实时输出电平 |
| | `dbg_ovp_fault` | `uint32_t` | FAULT1 锁存标志，1=已触发 |
| | `dbg_ocp_fault` | `uint32_t` | FAULT3 锁存标志，1=已触发 |
| **过零** | `dbg_zc_i` | `uint32_t` | COMP3 电流过零实时输出电平 |
| | `dbg_zc_v` | `uint32_t` | COMP5 电压过零实时输出电平 |

## 其他已配置引脚

| 引脚 | MCU 功能 | 项目功能/备注 |
| --- | --- | --- |
| PA0 | ADC1_IN1 | 谐振电流采样 |
| PA1 | ADC2_IN2 | 谐振电压采样 |
| PA2 | ADC1_IN3 (注入) | 温度检测（预留） |
| PA3 | ADC1_IN4 (注入) | 温度检测（预留） |
| PA7 | COMP2_INP | 母线过压保护采样输入 |
| PA8 | HRTIM1_CHA1 | 左侧上桥 PWM |
| PA9 | HRTIM1_CHA2 | 左侧下桥 PWM |
| PA10 | HRTIM1_CHB1 | 右侧上桥 PWM |
| PA11 | HRTIM1_CHB2 | 右侧下桥 PWM |
| PA15 | USART2_RX | 串口 2 接收 |
| PB0 | GPIO 输出 | RELAY_CTRL，继电器控制（软启动旁路） |
| PB1 | GPIO 输出 | PWM_EN_N，低电平有效的 PWM/驱动使能 |
| PB3 | USART2_TX | 串口 2 发送 |
| PB11 | COMP6_INP | 过流保护采样输入 |
| PB13 | COMP5_INP | 谐振电压过零检测输入 |
| PC1 | COMP3_INP | 谐振电流过零检测输入 |
| PC4 | USART1_TX | 串口 1 发送 |
| PC5 | USART1_RX | 串口 1 接收 |
| PC13 | GPIO 输出 | 运行指示灯，500 ms 翻转一次 |

## 内部外设连接

| 内部资源 | 用途 |
| --- | --- |
| DAC1_CH1 | COMP3 电流过零比较阈值 |
| DAC1_CH2 | COMP2 母线过压比较阈值 |
| DAC4_CH1 | COMP5 电压过零比较阈值 |
| DAC4_CH2 | COMP6 过流比较阈值 |
| COMP2 | 母线过压保护 |
| COMP3 | 谐振电流过零检测 |
| COMP5 | 谐振电压过零检测 |
| COMP6 | 谐振过流保护 |
| HRTIM FAULT1 | COMP2 过压锁存关断 |
| HRTIM FAULT3 | COMP6 过流锁存关断 |
| HRTIM1 Timer A | 左侧半桥 PWM |
| HRTIM1 Timer B | 右侧半桥 PWM |
| ADC1 + ADC2 | 电流、电压双路同步采样 |
| DMA1_Channel1 | ADC 双路同步采样循环搬运 |
| USART1 / USART2 | 调试或通讯接口 |