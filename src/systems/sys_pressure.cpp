#include <systems/sys_pressure.h>

#include <assert.h>

#include <operation/fw_config.h>

bool sysPressureInit(void)
{
    assert(FW_PIN_PRESSURE_ANALOG >= 0);
    assert(FW_ADC_MAX_COUNTS > 0L);
    pinMode(FW_PIN_PRESSURE_ANALOG, INPUT);
    analogReadResolution(12);
    return true;
}

bool sysPressureReadHpa(const FwConfig *config, int32_t *pressureHpa, int32_t *adcMillivolts)
{
    int raw = 0;
    int32_t adcMv = 0L;
    int32_t sensorMv = 0L;
    assert(config != nullptr);
    assert(pressureHpa != nullptr);
    if ((config == nullptr) || (pressureHpa == nullptr)) {
        return false;
    }
    raw = analogRead(FW_PIN_PRESSURE_ANALOG);
    if ((raw < 0) || (raw > FW_ADC_MAX_COUNTS)) {
        return false;
    }
    adcMv = ((int32_t)raw * FW_ADC_REFERENCE_MV) / FW_ADC_MAX_COUNTS;
    sensorMv = (adcMv * config->dividerMultiplierX1000) / 1000L;
    *pressureHpa = (sensorMv - FW_SENSOR_ZERO_MV) / FW_SENSOR_MV_PER_HPA;
    if (adcMillivolts != nullptr) {
        *adcMillivolts = adcMv;
    }
    return true;
}

bool sysPressureCalibrate(FwConfig *config)
{
    int raw = 0;
    int32_t adcMv = 0L;
    int32_t divider = 0L;
    assert(config != nullptr);
    assert(FW_SENSOR_ZERO_MV > 0L);
    if (config == nullptr) {
        return false;
    }
    raw = analogRead(FW_PIN_PRESSURE_ANALOG);
    if ((raw <= 0) || (raw > FW_ADC_MAX_COUNTS)) {
        Serial.println("PRESSURE: calibration failed, ADC out of range");
        return false;
    }
    adcMv = ((int32_t)raw * FW_ADC_REFERENCE_MV) / FW_ADC_MAX_COUNTS;
    if (adcMv <= 0L) {
        Serial.println("PRESSURE: calibration failed, ADC millivolts invalid");
        return false;
    }
    divider = (FW_SENSOR_ZERO_MV * 1000L) / adcMv;
    if ((divider < 1000L) || (divider > 3000L)) {
        Serial.printf("PRESSURE: calibration suspicious divider=%ld adc_mV=%ld\n", divider, adcMv);
    }
    Serial.printf("PRESSURE: calibration divider_x1000=%ld adc_mV=%ld\n", divider, adcMv);
    return fwPressureCalibrationSave(config, divider);
}
