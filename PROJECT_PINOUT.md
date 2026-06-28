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
| 母线过压保护 | COMP2 | PA7 / COMP2_INP | DAC1_CH2 | 计划值 DAC code 3475，约 2.80 V @ VDDA=3.3 V | 当前暂时禁用 |
| 过流保护 | COMP6 | PB11 / COMP6_INP | DAC4_CH2 | 计划值 DAC code 3723，约 3.00 V @ VDDA=3.3 V | 当前暂时禁用 |

当前为方便调试 PWM 波形，保护比较器暂时没有启动，HRTIM 也没有启用 fault/event 关断。重新启用保护前，需要恢复 DAC/COMP 启动和 HRTIM 保护链路。

阈值计算：

```text
Vthreshold = DAC_code / 4095 * VDDA
DAC_code = Vthreshold / VDDA * 4095
```

如果 PA7 或 PB11 前端有分压、电流采样放大器或运放增益，实际母线电压和电流阈值需要按前端比例换算。

## HRTIM 保护链路

| HRTIM 事件/故障 | 来源 | 用途 |
| --- | --- | --- |
| EEV1 | COMP2_OUT | 母线过压时复位 PWM 输出到 inactive |
| EEV3 | COMP6_OUT | 过流时复位 PWM 输出到 inactive |
| FAULT1 | HRTIM fault input | 参与 Timer A/B 故障关断 |
| FAULT2 | HRTIM fault input | 参与 Timer A/B 故障关断 |

以上是计划保护链路，当前代码中暂时未启用。Timer A 和 Timer B 当前配置为 `HRTIM_TIMFAULTENABLE_NONE`，四路 HRTIM 输出也没有额外加入 `EEV1 | EEV3` 复位源。

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
