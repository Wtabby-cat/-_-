# CFRP 电磁感应加热控制工程说明

本工程用于 CFRP 电磁感应加热功率控制，MCU 为 STM32G474RET6。核心输出为 HRTIM1 产生的全桥 PWM，当前配置约为 20.1 kHz、50% 占空比，并开启上下桥臂死区。

## 功率桥 PWM

| 引脚 | MCU 功能 | 项目功能 |
| --- | --- | --- |
| PA8 | HRTIM1_CHA1 | 左侧上桥 PWM |
| PA9 | HRTIM1_CHA2 | 左侧下桥 PWM |
| PA10 | HRTIM1_CHB1 | 右侧上桥 PWM |
| PA11 | HRTIM1_CHB2 | 右侧下桥 PWM |
| PB1 | GPIO 输出 | PWM/驱动使能，低电平有效 |

当前 `main.c` 中 PB1 上电后保持高电平，因此外部驱动默认禁用。HRTIM 波形会启动，但功率级是否真正输出取决于 PB1 是否被拉低使能。

## 保护比较器

| 保护项 | 比较器 | 正输入引脚 | 负输入基准 | 当前阈值 | 保护动作 |
| --- | --- | --- | --- | --- | --- |
| 母线过压保护 | COMP2 | PA7 / COMP2_INP | DAC1_CH2 | DAC code 2598，按 1299 code 实测 1.00 V 校准到约 2.00 V | FAULT1（内部比较器源） |
| 过流保护 | COMP6 | PB11 / COMP6_INP | DAC4_CH2 | DAC code 2853，按 2482 code 实测 1.74 V 校准到约 2.00 V | FAULT5（EEV5 链路，快速响应） |

当前启用 COMP2 母线过压保护和 COMP6 谐振过流保护。COMP2 保护使用 DAC1_CH2 作为比较阈值，PA7 大于约 2.00 V 后经 HRTIM 内部比较器源直接触发 `FAULT1`；COMP6 保护使用 DAC4_CH2 作为比较阈值，PB11 大于约 2.00 V 后经 `EEV5`（FastMode 使能）→ `FAULT5` 硬件关断 PWM，专为 20-40kHz 正弦波过流检测优化响应速度。

**DAC 阈值校准说明**：两者目标阈值都是约 2.00 V，但 DAC code 不同（2598 vs 2853），原因是 DAC1 和 DAC4 是不同 DAC 外设，各自增益和偏移特性不同，需分别实测校准。DAC1_CH2 实测 1299 code → 1.00 V，线性推算 2598 → 约 2.00 V；DAC4_CH2 实测 2482 code → 1.74 V，线性推算 2853 → 约 2.00 V。

阈值计算：

```text
# 理想公式（仅作参考，实际需校准）
Vthreshold = DAC_code / 4095 * VDDA
DAC_code = Vthreshold / VDDA * 4095

# 校准后反推（以实测为准）
DAC1_CH2: V ≈ code * 1.00 / 1299
DAC4_CH2: V ≈ code * 1.74 / 2482
```

如果 PA7 或 PB11 前端有分压、电流采样放大器或运放增益，实际母线电压和电流阈值需要按前端比例换算。

PA7 / COMP2_INP 当前在模拟模式下启用了 MCU 内部弱下拉，用于减轻外部测试稳压源撤掉后输入浮空导致的误触发。该下拉较弱，不能完全替代硬件下拉电阻；如果前端信号源阻抗较高，实际阈值仍建议复测。

## HRTIM 保护链路

| HRTIM 事件/故障 | 来源 | 用途 |
| --- | --- | --- |
| EEV1 | COMP2_OUT | 当前未启用 |
| EEV5 | COMP6_OUT | 过流检测 → FAULT5（FastMode 使能，旁路预分频，最快响应） |
| FAULT1 | Internal comparator / FLT_Int | 母线过压数字滤波硬件关断 |
| FAULT5 | EEV5 (EEVINPUT) | 过流锁存关断（filter=0，无滤波延迟） |

当前 `FAULT1` 配置为 `HRTIM_FAULTSOURCE_INTERNAL`、高电平有效，数字滤波配置为 `HRTIM_FAULTPRESCALER_DIV1`、`HRTIM_FAULTFILTER_15`，并已接入 Timer A/B。保护动作由 HRTIM fault 硬件完成。

COMP6 过流保护走 `EEV5` 链路：`COMP6_OUT → EEV5 (FastMode=ENABLE) → FAULT5 (EEVINPUT, filter=0)`。EEV5 配置为高电平有效、level sensitivity、`HRTIM_FAULTPRESCALER_DIV1`、`HRTIM_EVENTFASTMODE_ENABLE`（旁路预分频以消除延迟）。FAULT5 配置为 `HRTIM_FAULTSOURCE_EEVINPUT`、filter=0（无数字滤波），锁存关断。当 PB11 高于阈值时，FAULT5 立即锁存，四路 PWM 硬件关断；主循环中 `UpdatePowerStageProtection()` 定期清除 FAULT5 标志以允许恢复。

启动顺序：DAC1_CH2/DAC4_CH2 阈值设置 → DAC 启动 → COMP2/COMP6 启动 → 延迟稳定 → 清除 FAULT5 标志并启用 FAULT5 → 检查 COMP2 输出，仅当 COMP2 为低电平时才清除 FLT1 并启用 FAULT1。FAULT5 在 COMP6 启动后立即武装，不依赖 COMP2 状态。

## 其他已配置引脚

| 引脚 | MCU 功能 | 项目功能/备注 |
| --- | --- | --- |
| PA0 | ADC1_IN1 | TANK_CURR，谐振/负载电流采样 |
| PA1 | ADC2_IN2 | TANK_VOLT，谐振/负载电压采样 |
| PA2 | ADC1_IN3 injected | 备用/注入 ADC 通道 |
| PA3 | ADC1_IN4 injected | 备用/注入 ADC 通道 |
| PA7 | COMP2_INP | 母线过压保护采样输入 |
| PA8 | HRTIM1_CHA1 | 左侧上桥 PWM |
| PA9 | HRTIM1_CHA2 | 左侧下桥 PWM |
| PA10 | HRTIM1_CHB1 | 右侧上桥 PWM |
| PA11 | HRTIM1_CHB2 | 右侧下桥 PWM |
| PA15 | USART2_RX | 串口 2 接收 |
| PB0 | GPIO 输出 | RELAY_CTRL，继电器控制 |
| PB1 | GPIO 输出 | PWM_EN_N，低电平有效的 PWM/驱动使能 |
| PB3 | USART2_TX | 串口 2 发送 |
| PB11 | COMP6_INP | 过流保护采样输入 |
| PB13 | COMP5_INP | COMP5 输入，当前初始化但未启动 |
| PC1 | COMP3_INP | COMP3 输入，当前初始化但未启动 |
| PC4 | USART1_TX | 串口 1 发送 |
| PC5 | USART1_RX | 串口 1 接收 |
| PC13 | GPIO 输出 | 运行指示灯，500 ms 翻转一次 |

## 内部外设连接

| 内部资源 | 用途 |
| --- | --- |
| DAC1_CH2 | COMP2 母线过压比较阈值 |
| DAC4_CH2 | COMP6 过流比较阈值 |
| HRTIM1 Timer A | 左侧半桥 PWM |
| HRTIM1 Timer B | 右侧半桥 PWM |
| ADC1 + ADC2 | 电流、电压采样 |
| USART1 / USART2 | 调试或通讯接口 |
