param([string]$HmiProject = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force tmp | Out-Null
    & python Tests/test_keil_project.py
    if ($LASTEXITCODE -ne 0) { throw 'Keil project module check failed' }
    $includeArgs = @('-ICore/Inc', '-IDrivers/STM32G4xx_HAL_Driver/Inc',
        '-IDrivers/CMSIS/Device/ST/STM32G4xx/Include', '-IDrivers/CMSIS/Include')
    $flags = @('-std=c99', '-DSTM32G474xx', '-DUSE_HAL_DRIVER', '-Wall', '-Wextra', '-Werror',
        '-Wno-pointer-to-int-cast', '-Wno-int-to-pointer-cast')
    & gcc @flags @includeArgs Tests/test_protocol_protection.c Core/Src/ih_protocol.c Core/Src/ih_protection.c -o tmp/test_protocol_protection.exe
    if ($LASTEXITCODE -ne 0) { throw 'Protocol test compilation failed' }
    & ./tmp/test_protocol_protection.exe
    if ($LASTEXITCODE -ne 0) { throw 'Protocol/protection tests failed' }
    & gcc @flags @includeArgs Tests/test_hmi.c Core/Src/ih_hmi.c Core/Src/ih_history.c Core/Src/ih_protocol.c Core/Src/ih_protection.c Core/Src/ih_program.c Core/Src/ih_uart.c Core/Src/ih_workpiece.c -o tmp/test_hmi.exe
    if ($LASTEXITCODE -ne 0) { throw 'HMI test compilation failed' }
    & ./tmp/test_hmi.exe
    if ($LASTEXITCODE -ne 0) { throw 'HMI tests failed' }
    & ./tmp/test_hmi.exe curves
    if ($LASTEXITCODE -ne 0) { throw 'HMI history replay/queue tests failed' }
    & gcc @flags @includeArgs Tests/test_history.c Core/Src/ih_history.c -o tmp/test_history.exe
    if ($LASTEXITCODE -ne 0) { throw 'History test compilation failed' }
    & ./tmp/test_history.exe
    if ($LASTEXITCODE -ne 0) { throw 'History tests failed' }
    & gcc @flags @includeArgs Tests/test_app.c Core/Src/ih_protection.c Core/Src/ih_program.c Core/Src/ih_history.c Core/Src/ih_tracking.c Core/Src/ih_power.c -o tmp/test_app.exe
    if ($LASTEXITCODE -ne 0) { throw 'Application test compilation failed' }
    & ./tmp/test_app.exe
    if ($LASTEXITCODE -ne 0) { throw 'Application tests failed' }
    & ./tmp/test_app.exe boot_fault
    if ($LASTEXITCODE -ne 0) { throw 'NTC bypass boot-fault test failed' }
    & ./tmp/test_app.exe boot_saved
    if ($LASTEXITCODE -ne 0) { throw 'Saved hardware OCP threshold boot test failed' }
    & gcc @flags @includeArgs Tests/test_pwm.c -o tmp/test_pwm.exe
    if ($LASTEXITCODE -ne 0) { throw 'PWM test compilation failed' }
    & ./tmp/test_pwm.exe
    if ($LASTEXITCODE -ne 0) { throw 'PWM soft-start tests failed' }
    & gcc @flags @includeArgs Tests/test_tracking.c Core/Src/ih_tracking.c -o tmp/test_tracking.exe
    if ($LASTEXITCODE -ne 0) { throw 'Search/PLL test compilation failed' }
    & ./tmp/test_tracking.exe
    if ($LASTEXITCODE -ne 0) { throw 'Search/PLL tests failed' }
    & gcc @flags @includeArgs Tests/test_power.c Core/Src/ih_power.c -o tmp/test_power.exe
    if ($LASTEXITCODE -ne 0) { throw 'Power loop test compilation failed' }
    & ./tmp/test_power.exe
    if ($LASTEXITCODE -ne 0) { throw 'Power loop tests failed' }
    & gcc @flags @includeArgs Tests/test_phase_pwm.c Core/Src/ih_phase.c -o tmp/test_phase_pwm.exe
    if ($LASTEXITCODE -ne 0) { throw 'PWM phase test compilation failed' }
    & ./tmp/test_phase_pwm.exe
    if ($LASTEXITCODE -ne 0) { throw 'PWM phase tests failed' }
    & gcc @flags @includeArgs Tests/test_temperature.c Core/Src/ih_temperature.c -o tmp/test_temperature.exe
    if ($LASTEXITCODE -ne 0) { throw 'Temperature test compilation failed' }
    & ./tmp/test_temperature.exe
    if ($LASTEXITCODE -ne 0) { throw 'Temperature disabled-front-end tests failed' }
    & gcc @flags @includeArgs '-DIH_TEMP1_CONNECTED=1' '-DIH_TEMP1_GAIN_C_PER_V=100.0f' '-DIH_TEMP1_OFFSET_C=0.0f' Tests/test_temperature.c Core/Src/ih_temperature.c -o tmp/test_temperature_calibrated.exe
    if ($LASTEXITCODE -ne 0) { throw 'Calibrated temperature test compilation failed' }
    & ./tmp/test_temperature_calibrated.exe
    if ($LASTEXITCODE -ne 0) { throw 'Temperature calibration tests failed' }
    & gcc @flags @includeArgs Tests/test_measurement.c Core/Src/ih_phase.c -o tmp/test_measurement.exe
    if ($LASTEXITCODE -ne 0) { throw 'Power measurement test compilation failed' }
    & ./tmp/test_measurement.exe
    if ($LASTEXITCODE -ne 0) { throw 'Power measurement tests failed' }
    & gcc @flags @includeArgs '-DIH_CURRENT_SENSOR_CONNECTED=0' Tests/test_measurement.c Core/Src/ih_phase.c -o tmp/test_measurement_disconnected.exe
    if ($LASTEXITCODE -ne 0) { throw 'Disconnected CT measurement test compilation failed' }
    & ./tmp/test_measurement_disconnected.exe
    if ($LASTEXITCODE -ne 0) { throw 'Disconnected CT measurement tests failed' }
    & gcc @flags @includeArgs Tests/test_workpiece.c Core/Src/ih_workpiece.c Core/Src/ih_uart.c -o tmp/test_workpiece.exe
    if ($LASTEXITCODE -ne 0) { throw 'Workpiece UART test compilation failed' }
    & ./tmp/test_workpiece.exe
    if ($LASTEXITCODE -ne 0) { throw 'Workpiece UART tests failed' }
    & gcc @flags @includeArgs '-DIH_WORKPIECE_UART_ENABLED=1' Tests/test_workpiece.c Core/Src/ih_workpiece.c Core/Src/ih_uart.c -o tmp/test_workpiece_enabled.exe
    if ($LASTEXITCODE -ne 0) { throw 'Enabled workpiece UART test compilation failed' }
    & ./tmp/test_workpiece_enabled.exe
    if ($LASTEXITCODE -ne 0) { throw 'Enabled workpiece UART tests failed' }
    & python Tests/prepare_adc_config_test.py
    if ($LASTEXITCODE -ne 0) { throw 'ADC test source preparation failed' }
    & gcc @flags @includeArgs -O2 Tests/test_adc_config.c '-Wl,--image-base,0x400000' -o tmp/test_adc_config.exe
    if ($LASTEXITCODE -ne 0) { throw 'ADC configuration test compilation failed' }
    & ./tmp/test_adc_config.exe
    if ($LASTEXITCODE -ne 0) { throw 'ADC configuration test failed' }
    $hmiTestArgs = @('Tests/test_hmi_v9.py')
    if ($HmiProject) { $hmiTestArgs += $HmiProject }
    & python @hmiTestArgs
    if ($LASTEXITCODE -ne 0) { throw 'V9 display/history tests failed (generate build_hmi.py first)' }
} finally {
    Pop-Location
}
