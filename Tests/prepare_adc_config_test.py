"""Use the real source functions, with no copied HAL implementation or mocks.

The Windows COFF linker retains unused exported functions and their references;
extract only the complete functions needed for this register-level test.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
functions = [
    ("Core/Src/main.c", r"static void MX_ADC2_Init\(void\)"),
    ("Core/Src/ih_measurement.c", r"void Measurement_BoardBusInit\(void\)"),
    ("Core/Src/ih_measurement.c", r"int Measurement_BoardBusRead\(uint16_t \*raw\)"),
    ("Core/Src/stm32g4xx_hal_msp.c", r"void HAL_COMP_MspInit\([^)]*\)"),
    ("Core/Src/stm32g4xx_hal_msp.c", r"void HAL_UART_MspInit\([^)]*\)"),
    ("Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_gpio.c", r"void HAL_GPIO_Init\([^)]*\)"),
    ("Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_adc_ex.c",
     r"HAL_StatusTypeDef HAL_ADCEx_InjectedConfigChannel\([^)]*\)"),
]
chunks = ['#include "main.h"\n#include "ih_config.h"\n#include "ih_temperature.h"\n',
          'static volatile uint8_t bus_pending;\nstatic uint32_t bus_conversion_tick, bus_result_tick;\n',
          'static volatile uint32_t injection_epoch;\n',
          'static uint32_t bus_burst_count, bus_sum1, bus_sum2, temp_sum1, temp_sum2, bus_min, bus_max;\n',
          'volatile uint32_t dbg_bus_rank1_raw, dbg_bus_rank2_raw, dbg_bus_min_raw, dbg_bus_max_raw;\n']
vendor = (root / functions[-1][0]).read_text(encoding="utf-8")
private_macro = vendor[vendor.index("#define ADC_JSQR_FIELDS"):].split("\n\n", 1)[0]
chunks.append(private_macro + "\n")
for filename, declaration in functions:
    source = (root / filename).read_text(encoding="utf-8")
    match = re.search(r"^" + declaration + r"\n\{", source, re.MULTILINE)
    if match is None:
        raise RuntimeError(f"Cannot find source function in {filename}")
    end = source.index("\n}", match.end()) + 2
    function = source[match.start():end]
    function = function.replace("static void MX_ADC2_Init", "void Test_MX_ADC2_Init", 1)
    chunks.append(function + "\n")
(root / "tmp" / "adc_config_under_test.c").write_text("\n".join(chunks), encoding="utf-8")
