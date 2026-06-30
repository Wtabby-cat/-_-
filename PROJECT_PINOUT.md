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

## 保护比较器

| 保护项       | 比较器 | 正输入引脚       | 负输入基准 | DAC code | 阈值（VDDA=3.3V） | 保护动作                                   |
| ------------ | ------ | ---------------- | ---------- | -------- | ----------------- | ------------------------------------------ |
| 母线过压保护 | COMP2  | PA7 / COMP2_INP  | DAC1_CH2   | 2598     | 2598/4095×3.3 ≈ 2.09 V | FAULT1（内部比较器源）                     |
| 过流保护     | COMP6  | PB11 / COMP6_INP | DAC4_CH2   | 2853     | 2853/4095×3.3 ≈ 2.30 V | FAULT3（内部比较器源，filter=0，最快响应） |

当前启用 COMP2 母线过压保护和 COMP6 谐振过流保护。COMP2 保护使用 DAC1_CH2 作为比较阈值，PA7 大于约 2.09 V 后经 HRTIM 内部比较器源直接触发 `FAULT1`；COMP6 保护同样使用内部比较器源，PB11 大于约 2.30 V 后直接触发 `FAULT3`（filter=0，不经过 EEV 链路，专为 20-40kHz 正弦波过流检测优化响应速度）。

> 阈值按理想公式 `code / 4095 × VDDA` 计算，VDDA = 3.3 V。实测发现尖峰或噪声仍会导致 FAULT1 和 FAULT3 误触发，后续可进一步提高阈值留冗余。

**DAC 阈值校准说明**：DAC1 和 DAC4 是不同 DAC 外设，各自增益和偏移特性不同。由于单点校准（如 DAC1 1299→1.00V）外推到 2V 以上线性度不可靠，当前阈值直接使用理想公式推算，实际电压以万用表实测为准。DAC1_CH2 和 DAC4_CH2 的出厂校准点仅供参考：

```text
# 理想公式（当前使用）
Vthreshold = DAC_code / 4095 × VDDA      (VDDA = 3.3 V)

# 出厂校准参考（单点外推不可靠，不用于阈值计算）
DAC1_CH2: 1299 code 实测 → 1.00 V
DAC4_CH2: 2482 code 实测 → 1.74 V
```

如果 PA7 或 PB11 前端有分压、电流采样放大器或运放增益，实际母线电压和电流阈值需要按前端比例换算。

PA7 / COMP2_INP 当前在模拟模式下启用了 MCU 内部弱下拉，用于减轻外部测试稳压源撤掉后输入浮空导致的误触发。该下拉较弱，不能完全替代硬件下拉电阻；如果前端信号源阻抗较高，实际阈值仍建议复测。

## 过零检测比较器

用于检测谐振电流和电压的过零点，计算相位差，为后续数字锁相环（PLL）提供输入。

| 检测项 | 比较器 | 正输入引脚 | 负输入基准 | DAC code | 目标阈值 | 用途 |
| --- | --- | --- | --- | --- | --- | --- |
| 电流过零检测 | COMP3 | PA0 / COMP3_INP | DAC1_CH1 | 1924 | 1.55 V（实测已确认） | 谐振电流相位检测 |
| 电压过零检测 | COMP5 | PB13 / COMP5_INP | DAC4_CH1 | 1924 | 1.55 V（实测已确认） | 谐振电压相位检测 |

谐振电流和电压信号为叠加在 1.55 V 直流偏置上的交流正弦波（20-40kHz），比较器阈值设为 1.55 V 即可检测过零点。COMP3 和 COMP5 均配置为同相输出（`COMP_OUTPUTPOL_NONINVERTED`）、无迟滞（`COMP_HYSTERESIS_NONE`），信号高于 1.55 V 时输出高电平，低于 1.55 V 时输出低电平，过零点翻转。

当前已正常启动，DAC code 1924 经实测验证输出约 1.55 V。主循环中每轮读取 COMP3/COMP5 输出电平存入 `dbg_comp3_zc_level`、`dbg_comp5_zc_level` 两个调试变量，可在 VSCode Watch 窗口实时观察过零点翻转情况。锁相环逻辑待后续实现。

## HRTIM 保护链路

| HRTIM 故障 | 来源                          | 用途             | filter 值 | 滤波延迟               |
| ---------- | ----------------------------- | ---------------- | --------- | ---------------------- |
| FAULT1     | Internal comparator / FLT_Int | 母线过压硬件关断 | 15        | 15 / 170 MHz ≈ 88 ns  |
| FAULT3     | Internal comparator / FLT_Int | 过流锁存关断     | 0         | 0（即时触发，约 6 ns） |

HRTIM 时钟 = SYSCLK = 170 MHz（HSI 16 MHz × 85 / 4 / 2），fault prescaler = DIV1。

filter 是 HRTIM 故障数字滤波器值：硬件要求故障输入信号连续维持 filter 个 fHRTIM 时钟周期后才确认有效故障。filter=0 意味着不滤波，第一个有效时钟沿立即触发；filter=15 需要连续 15 个周期确认，滤除 88 ns 以下的毛刺。

当前 `FAULT1` 配置为 `HRTIM_FAULTSOURCE_INTERNAL`、高电平有效，并已接入 Timer A/B。保护动作由 HRTIM fault 硬件完成。

COMP6 过流保护直接走内部比较器源：`COMP6_OUT → FLT_Int → FAULT3`。FAULT3 配置为 `HRTIM_FAULTSOURCE_INTERNAL`、高电平有效、filter=0（无数字滤波）、锁存关断。当 PB11 高于阈值时，FAULT3 立即锁存，四路 PWM 硬件关断；主循环中 `UpdatePowerStageProtection()` 定期清除 FAULT3 标志以允许恢复。与之前的 EEV5→FAULT5 方案相比，内部源路径省去了 EEV 链路延迟，是 20-40kHz 过流检测的最快硬件路径。

启动顺序：DAC1_CH1/DAC1_CH2/DAC4_CH1/DAC4_CH2 阈值设置 → DAC 四通道启动 → COMP2/COMP3/COMP5/COMP6 全部启动 → 延迟稳定 → 清除 FAULT3 标志并启用 FAULT3 → 检查 COMP2 输出，仅当 COMP2 为低电平时才清除 FLT1 并启用 FAULT1。FAULT3 在 COMP6 启动后立即武装，不依赖 COMP2 状态。

## 其他已配置引脚

| 引脚 | MCU 功能          | 项目功能/备注                       |
| ---- | ----------------- | ----------------------------------- |
| PA0  | ADC1_IN1          | TANK_CURR，谐振/负载电流采样        |
| PA1  | ADC2_IN2          | TANK_VOLT，谐振/负载电压采样        |
| PA2  | ADC1_IN3 injected | 备用/注入 ADC 通道                  |
| PA3  | ADC1_IN4 injected | 备用/注入 ADC 通道                  |
| PA7  | COMP2_INP         | 母线过压保护采样输入                |
| PA8  | HRTIM1_CHA1       | 左侧上桥 PWM                        |
| PA9  | HRTIM1_CHA2       | 左侧下桥 PWM                        |
| PA10 | HRTIM1_CHB1       | 右侧上桥 PWM                        |
| PA11 | HRTIM1_CHB2       | 右侧下桥 PWM                        |
| PA15 | USART2_RX         | 串口 2 接收                         |
| PB0  | GPIO 输出         | RELAY_CTRL，继电器控制              |
| PB1  | GPIO 输出         | PWM_EN_N，低电平有效的 PWM/驱动使能 |
| PB3  | USART2_TX         | 串口 2 发送                         |
| PB11 | COMP6_INP         | 过流保护采样输入                    |
| PB13 | COMP5_INP         | 谐振电压过零检测输入                |
| PC1  | COMP3_INP         | 谐振电流过零检测输入                |
| PC4  | USART1_TX         | 串口 1 发送                         |
| PC5  | USART1_RX         | 串口 1 接收                         |
| PC13 | GPIO 输出         | 运行指示灯，500 ms 翻转一次         |

## 内部外设连接

| 内部资源        | 用途                       |
| --------------- | -------------------------- |
| DAC1_CH1        | COMP3 电流过零比较阈值     |
| DAC1_CH2        | COMP2 母线过压比较阈值     |
| DAC4_CH1        | COMP5 电压过零比较阈值     |
| DAC4_CH2        | COMP6 过流比较阈值         |
| COMP3           | 谐振电流过零检测           |
| COMP5           | 谐振电压过零检测           |
| HRTIM1 Timer A  | 左侧半桥 PWM               |
| HRTIM1 Timer B  | 右侧半桥 PWM               |
| ADC1 + ADC2     | 电流、电压采样             |
| USART1 / USART2 | 调试或通讯接口             |
