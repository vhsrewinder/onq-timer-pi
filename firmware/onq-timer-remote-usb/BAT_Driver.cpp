#include "BAT_Driver.h"
#include "Config.h"

float BAT_analogVolts = 0;

// Voltage smoothing buffer
static float voltageBuffer[BAT_SMOOTH_SAMPLES] = {0};
static uint8_t voltageIndex = 0;
static bool bufferFilled = false;

void BAT_Init(void)
{
  //set the resolution to 12 bits (0-4095)
  analogReadResolution(12);

  // Initialize voltage buffer
  for (int i = 0; i < BAT_SMOOTH_SAMPLES; i++) {
    voltageBuffer[i] = 0;
  }
  voltageIndex = 0;
  bufferFilled = false;
}

float BAT_Get_Volts(void)
{
  // Read raw voltage
  int Volts = analogReadMilliVolts(BAT_ADC_PIN); // millivolts
  float rawVoltage = (float)(Volts * 3.0 / 1000.0) / Measurement_offset;

  // Add to smoothing buffer
  voltageBuffer[voltageIndex] = rawVoltage;
  voltageIndex = (voltageIndex + 1) % BAT_SMOOTH_SAMPLES;
  if (voltageIndex == 0) {
    bufferFilled = true;
  }

  // Calculate average voltage
  float sum = 0;
  int count = bufferFilled ? BAT_SMOOTH_SAMPLES : voltageIndex;
  if (count == 0) count = 1;  // Prevent division by zero

  for (int i = 0; i < count; i++) {
    sum += voltageBuffer[i];
  }

  BAT_analogVolts = sum / count;
  // printf("BAT voltage : %.2f V (raw: %.2f V)\r\n", BAT_analogVolts, rawVoltage);
  return BAT_analogVolts;
}

uint8_t BAT_Get_Percentage(void)
{
  // Get current voltage
  float voltage = BAT_analogVolts;

  // More accurate LiPo discharge curve based on typical characteristics
  // LiPo batteries have a non-linear discharge curve - they stay high for a while,
  // then drop more rapidly as they discharge
  //
  // Voltage breakpoints (typical LiPo under moderate load):
  // 4.20V = 100% (fully charged)
  // 4.10V = 95%
  // 4.00V = 85%
  // 3.90V = 75%
  // 3.80V = 60%
  // 3.70V = 40%
  // 3.60V = 25%
  // 3.50V = 15%
  // 3.40V = 8%
  // 3.30V = 4%
  // 3.20V = 1%
  // 3.00V = 0% (cutoff)

  // Clamp to valid range
  if (voltage >= 4.20) return 100;
  if (voltage <= 3.00) return 0;

  // Multi-segment piecewise linear interpolation for accuracy
  if (voltage >= 4.10) {
    // 4.10V - 4.20V = 95% - 100%
    return 95 + (uint8_t)((voltage - 4.10) / 0.10 * 5.0);
  }
  else if (voltage >= 4.00) {
    // 4.00V - 4.10V = 85% - 95%
    return 85 + (uint8_t)((voltage - 4.00) / 0.10 * 10.0);
  }
  else if (voltage >= 3.90) {
    // 3.90V - 4.00V = 75% - 85%
    return 75 + (uint8_t)((voltage - 3.90) / 0.10 * 10.0);
  }
  else if (voltage >= 3.80) {
    // 3.80V - 3.90V = 60% - 75%
    return 60 + (uint8_t)((voltage - 3.80) / 0.10 * 15.0);
  }
  else if (voltage >= 3.70) {
    // 3.70V - 3.80V = 40% - 60%
    return 40 + (uint8_t)((voltage - 3.70) / 0.10 * 20.0);
  }
  else if (voltage >= 3.60) {
    // 3.60V - 3.70V = 25% - 40%
    return 25 + (uint8_t)((voltage - 3.60) / 0.10 * 15.0);
  }
  else if (voltage >= 3.50) {
    // 3.50V - 3.60V = 15% - 25%
    return 15 + (uint8_t)((voltage - 3.50) / 0.10 * 10.0);
  }
  else if (voltage >= 3.40) {
    // 3.40V - 3.50V = 8% - 15%
    return 8 + (uint8_t)((voltage - 3.40) / 0.10 * 7.0);
  }
  else if (voltage >= 3.30) {
    // 3.30V - 3.40V = 4% - 8%
    return 4 + (uint8_t)((voltage - 3.30) / 0.10 * 4.0);
  }
  else if (voltage >= 3.20) {
    // 3.20V - 3.30V = 1% - 4%
    return 1 + (uint8_t)((voltage - 3.20) / 0.10 * 3.0);
  }
  else {
    // 3.00V - 3.20V = 0% - 1%
    return (uint8_t)((voltage - 3.00) / 0.20 * 1.0);
  }
}

bool BAT_Is_Low(void)
{
  return (BAT_analogVolts < LOW_BATTERY_VOLTAGE) ||
         (BAT_Get_Percentage() < LOW_BATTERY_PERCENT);
}