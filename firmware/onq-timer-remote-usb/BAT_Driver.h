#pragma once
#include <Arduino.h>

#define BAT_ADC_PIN   8
#define Measurement_offset 0.990476

// Voltage smoothing - average over multiple readings
#define BAT_SMOOTH_SAMPLES 10

extern float BAT_analogVolts;

void BAT_Init(void);
float BAT_Get_Volts(void);
uint8_t BAT_Get_Percentage(void);  // Get battery percentage (0-100%)
bool BAT_Is_Low(void);              // Check if battery is low