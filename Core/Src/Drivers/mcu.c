#include <string.h>
#include "stm32g0xx_ll_adc.h"
#include "rtc.h"
#include "adc.h"
#include "mcu.h"

// Buffer mapping based on adc.c configuration:
extern uint16_t AdcDataBuffer[7];

#define IX_TEMP 4
#define IX_VREF 5
#define IX_VBAT 6

static McuStatus CurrentStatus = McuStatusOk;

void McuInit(void) {
    CurrentStatus = McuStatusOk;
}

Voltage McuGetVcc(void) {
    uint16_t vref_raw = AdcDataBuffer[IX_VREF];
    
    if (vref_raw == 0) return 0;

    // Use the LL macro directly; it is already defined as a pointer to the calibration addr.
    return (3000 * (*VREFINT_CAL_ADDR)) / vref_raw;
}

Temperature McuGetInternalTemp(void) {
    Voltage vcc = McuGetVcc();
    uint16_t raw = AdcDataBuffer[IX_TEMP];

    if (vcc == 0) return 0;

    // Scale raw data to a virtual 3.0V basis to match factory calibration constants.
    int32_t raw_scaled = (raw * vcc) / 3000;
    
    // Linear interpolation using TEMPSENSOR_CAL1_ADDR (30C) and TEMPSENSOR_CAL2_ADDR (110C).
    int32_t temp = (int32_t)(110000 - 30000) * (raw_scaled - *TEMPSENSOR_CAL1_ADDR);
    temp = temp / (*TEMPSENSOR_CAL2_ADDR - *TEMPSENSOR_CAL1_ADDR);
    
    return (Temperature)(temp + 30000);
}

Voltage McuGetBatteryVoltage(void) {
    Voltage vcc = McuGetVcc();
    uint16_t raw = AdcDataBuffer[IX_VBAT];

    // STM32G0 internal VBAT channel typically has a hardware divider of 3.
    return (raw * vcc * 3) / 4095;
}

Permille McuGetBatteryLevel(void) {
    Voltage vbat = McuGetBatteryVoltage();
    
    if (vbat >= 4200) return 1000;
    if (vbat <= 3000) return 0;
    
    return (Permille)(((vbat - 3000) * 1000) / (4200 - 3000));
}

McuStatus McuGetStatus(void) {
    Temperature cpuTemp = McuGetInternalTemp();
    Permille batLevel = McuGetBatteryLevel();

    if (cpuTemp > 80000) return McuStatusOverTemp;
    if (batLevel < 50)   return McuStatusCriticalBattery;
    if (batLevel < 150)  return McuStatusLowBattery;

    return McuStatusOk;
}

void McuGetTime(McuTime* Time) {
    if (!Time) return;

    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

    Time->Hours   = sTime.Hours;
    Time->Minutes = sTime.Minutes;
    Time->Seconds = sTime.Seconds;
    Time->Day     = sDate.Date;
    Time->Month   = sDate.Month;
    Time->Year    = 2000 + sDate.Year;
}

void McuSetTime(const McuTime* Time) {
    if (!Time) return;

    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    sTime.Hours   = Time->Hours;
    sTime.Minutes = Time->Minutes;
    sTime.Seconds = Time->Seconds;
    
    sDate.Date    = Time->Day;
    sDate.Month   = Time->Month;
    sDate.Year    = (uint8_t)(Time->Year - 2000);

    HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}