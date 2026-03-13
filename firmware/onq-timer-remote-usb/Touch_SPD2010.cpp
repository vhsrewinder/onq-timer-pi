#include "Touch_SPD2010.h"
#include "Timer_UI.h"
#include "Config.h"

struct SPD2010_Touch touch_data = {0};
uint8_t Touch_interrupts=0;

// ============================================================================
// GESTURE DETECTION STATE
// ============================================================================
static uint32_t s_lastTapTimes[3] = {0, 0, 0};  // Sliding window of last 3 tap times
static uint8_t s_tapCount = 0;                    // Current tap count in detection window
static bool s_wasTouched = false;                 // Previous touch state
static lv_point_t s_swipeStart = {0, 0};          // Swipe start position
static lv_point_t s_swipeEnd = {0, 0};            // Swipe end position (last valid touch)
static bool s_swipeInProgress = false;            // Is a swipe being tracked?

// Tap validation state (prevent accidental menu opening)
static uint32_t s_touchStartTime = 0;             // When touch was pressed (for duration check)
static lv_point_t s_touchStartPos = {0, 0};       // Where touch started (for movement check)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool I2C_Read_Touch(uint8_t Driver_addr, uint16_t Reg_addr, uint8_t *Reg_data, uint32_t Length)
{
  Wire.beginTransmission(Driver_addr);
  Wire.write((uint8_t)(Reg_addr >> 8)); 
  Wire.write((uint8_t)Reg_addr);        
  if (Wire.endTransmission(true)){
    printf("The I2C transmission fails. - I2C Read Touch\r\n");
    return false;
  }
  Wire.requestFrom(Driver_addr, Length);
  while (Wire.available()) {
    *Reg_data++ =Wire.read();
  }
  return true;
}
bool I2C_Write_Touch(uint8_t Driver_addr, uint16_t Reg_addr, const uint8_t *Reg_data, uint32_t Length)
{
 Wire.beginTransmission(Driver_addr);
 Wire.write((uint8_t)(Reg_addr >> 8)); 
 Wire.write((uint8_t)Reg_addr);         
  for (int i = 0; i < Length; i++) {
   Wire.write(*Reg_data++);
  }
  if (Wire.endTransmission(true))
  {
    printf("The I2C transmission fails. - I2C Write Touch\r\n");
    return false;
  }
  return true;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



void IRAM_ATTR Touch_SPD2010_ISR(void) {
  Touch_interrupts = true;
}
uint8_t Touch_Init(void) {
  SPD2010_Touch_Reset();
  SPD2010_Read_cfg();

  pinMode(EXAMPLE_PIN_NUM_TOUCH_INT, INPUT_PULLUP);
  attachInterrupt(EXAMPLE_PIN_NUM_TOUCH_INT, Touch_SPD2010_ISR, FALLING);  
  return true;
}
/* Reset controller */
uint8_t SPD2010_Touch_Reset(void)
{
  Set_EXIO(EXIO_PIN1,Low);  
  delay(50);
  Set_EXIO(EXIO_PIN1,High);
  delay(50);
  return true;
}

uint16_t SPD2010_Read_cfg(void) {
  read_fw_version();
  return 1;
}
// reads sensor and touches
// updates Touch Points
void Touch_Read_Data(void) {
  uint8_t touch_cnt = 0;
  struct SPD2010_Touch touch = {0};
  tp_read_data(&touch);
  
  noInterrupts(); 
  /* Expect Number of touched points */
  touch_cnt = (touch.touch_num > CONFIG_ESP_LCD_TOUCH_MAX_POINTS ? CONFIG_ESP_LCD_TOUCH_MAX_POINTS : touch.touch_num);
  touch_data.touch_num = touch_cnt;
  /* Fill all coordinates */
  for (int i = 0; i < touch_cnt; i++) {
    touch_data.rpt[i].x = touch.rpt[i].x;
    touch_data.rpt[i].y = touch.rpt[i].y;
    touch_data.rpt[i].weight = touch.rpt[i].weight;
  }
  interrupts();
}
bool Touch_Get_xy(uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num){
  /* Read data from touch controller */
  Touch_Read_Data();
  /* Count of points */
  *point_num = (touch_data.touch_num > max_point_num ? max_point_num : touch_data.touch_num);
  for (size_t i = 0; i < *point_num; i++) {
    x[i] = touch_data.rpt[i].x;
    y[i] = touch_data.rpt[i].y;
    if (strength) {
      strength[i] = touch_data.rpt[i].weight;
    }
  }
  /* Clear available touch points count */
  touch_data.touch_num = 0;
  return (*point_num > 0);;
}

void example_touchpad_read(void){
  bool tp_pressed = false;
  uint16_t tp_x = 0;
  uint16_t tp_y = 0;
  uint8_t tp_cnt = 0;
  /* Read data from touch controller */
  tp_pressed = Touch_Get_xy(&tp_x, &tp_y, NULL, &tp_cnt, 1);

  // Gesture Detection
  uint32_t now = millis();

  // Clamp touch coordinates to valid display range (prevents overflow in swipe calculations)
  if (tp_x > DISPLAY_WIDTH) tp_x = DISPLAY_WIDTH;
  if (tp_y > DISPLAY_HEIGHT) tp_y = DISPLAY_HEIGHT;

  // Detect touch press/release transitions
  if (tp_pressed && (tp_cnt > 0)) {
    // Touch is currently pressed
    if (!s_wasTouched) {
      // Touch just pressed (press event)
      s_swipeStart.x = tp_x;
      s_swipeStart.y = tp_y;
      s_swipeEnd.x = tp_x;
      s_swipeEnd.y = tp_y;
      s_swipeInProgress = true;
      s_wasTouched = true;

      // Record touch start for tap validation
      s_touchStartTime = now;
      s_touchStartPos.x = tp_x;
      s_touchStartPos.y = tp_y;

      printf("[TOUCH] Touch pressed at: %d,%d\r\n", tp_x, tp_y);
    } else {
      // Touch is still pressed - continuously update end position
      s_swipeEnd.x = tp_x;
      s_swipeEnd.y = tp_y;
    }
  } else {
    // Touch is currently released
    if (s_wasTouched) {
      // Touch just released (release event)
      s_wasTouched = false;
      printf("[TOUCH] Touch released at: %d,%d\n", s_swipeEnd.x, s_swipeEnd.y);

      // ===== TAP VALIDATION =====
      // Check if this was a valid tap (not a swipe or hold)
      uint32_t touchDuration = now - s_touchStartTime;
      int16_t touchMovement = abs(s_swipeEnd.x - s_touchStartPos.x) + abs(s_swipeEnd.y - s_touchStartPos.y);
      bool isValidTap = (touchMovement <= TAP_MAX_MOVEMENT) && (touchDuration <= TAP_MAX_DURATION);

      printf("[TAP_VALIDATION] Duration: %lums (max %d), Movement: %dpx (max %d), Valid: %s\n",
             touchDuration, TAP_MAX_DURATION, touchMovement, TAP_MAX_MOVEMENT, isValidTap ? "YES" : "NO");

      // ===== TRIPLE-TAP DETECTION (DISABLED - Settings now opened via Button 2 long press) =====
      // Only count as tap if validation passed
      if (isValidTap) {
        // DISABLED: Triple-tap detection has been replaced with Button 2 long press
        // Settings menu is now accessed via physical Button 2 long press (2 seconds)
        /*
        // Only detect triple-tap on main screens (not on presets/settings/etc)
        TimerUIState currentState = g_timerUI.getCurrentUIState();
        printf("[GESTURE] Current UI state: %d (tap count: %d)\n", currentState, s_tapCount);

        if (currentState == UI_STATE_STOPPED || currentState == UI_STATE_RUNNING ||
            currentState == UI_STATE_PAUSED || currentState == UI_STATE_EXPIRED) {

          // Record this tap time
          s_lastTapTimes[s_tapCount % 3] = now;
          s_tapCount++;
          printf("[GESTURE] Valid tap registered, count now: %d\n", s_tapCount);

          // Check if we have 3 taps within the window
          if (s_tapCount >= 3) {
            uint32_t oldestTap = s_lastTapTimes[0];
            uint32_t middleTap = s_lastTapTimes[1];
            uint32_t newestTap = s_lastTapTimes[2];

            // Find oldest and newest in sliding window
            uint32_t minTime = oldestTap;
            uint32_t maxTime = newestTap;
            if (middleTap < minTime) minTime = middleTap;
            if (oldestTap > maxTime) maxTime = oldestTap;
            if (middleTap > maxTime) maxTime = middleTap;

            uint32_t timeSpan = maxTime - minTime;
            printf("[GESTURE] Checking triple-tap: span=%lums (threshold=%d)\n", timeSpan, TRIPLE_TAP_WINDOW_MS);

            if (timeSpan <= TRIPLE_TAP_WINDOW_MS) {
              printf("[GESTURE] *** Triple-tap detected! Opening settings... ***\n");
              g_timerUI.handleTripleTap();
              s_tapCount = 0;  // Reset tap counter
            } else {
              printf("[GESTURE] Triple-tap too slow (span %lums > %dms)\n", timeSpan, TRIPLE_TAP_WINDOW_MS);
            }
          }
        } else {
          // Not on main screen, reset tap counter
          printf("[GESTURE] Not on main screen, resetting tap counter\n");
          s_tapCount = 0;
        }
        */
      } else {
        // Not a valid tap (swipe or hold) - don't count it
        printf("[GESTURE] Ignoring invalid tap (swipe or hold detected)\n");
      }

      // ===== SWIPE DETECTION =====
      if (s_swipeInProgress) {
        // Use last valid touch position (s_swipeEnd) instead of current tp_x/tp_y
        int16_t dx = s_swipeEnd.x - s_swipeStart.x;
        int16_t dy = s_swipeEnd.y - s_swipeStart.y;

        printf("[SWIPE] Start: (%d,%d), End: (%d,%d), Delta: dx=%d, dy=%d\n",
               s_swipeStart.x, s_swipeStart.y, s_swipeEnd.x, s_swipeEnd.y, dx, dy);

        // Check if swipe distance exceeds threshold (use config constant)
        if (abs(dx) > SWIPE_THRESHOLD || abs(dy) > SWIPE_THRESHOLD) {
          printf("[GESTURE] *** Swipe detected! dx=%d, dy=%d (threshold=%d) ***\n", dx, dy, SWIPE_THRESHOLD);
          g_timerUI.handleSwipe(dx, dy);
        } else {
          printf("[SWIPE] Too short: abs(dx)=%d, abs(dy)=%d (threshold=%d)\n", abs(dx), abs(dy), SWIPE_THRESHOLD);
        }

        s_swipeInProgress = false;
      }
    }
  }
}
void Touch_Loop(void){
  example_touchpad_read();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
esp_err_t write_tp_point_mode_cmd()
{
  uint8_t sample_data[4];
  sample_data[0] = 0x50;
  sample_data[1] = 0x00;
  sample_data[2] = 0x00;
  sample_data[3] = 0x00;
  I2C_Write_Touch(SPD2010_ADDR, (((uint16_t)sample_data[0] << 8) | (sample_data[1])), &sample_data[2], 2);
  esp_rom_delay_us(200);
  return ESP_OK;
}

esp_err_t write_tp_start_cmd()
{
  uint8_t sample_data[4];
  sample_data[0] = 0x46;
  sample_data[1] = 0x00;
  sample_data[2] = 0x00;
  sample_data[3] = 0x00;
  I2C_Write_Touch(SPD2010_ADDR, (((uint16_t)sample_data[0] << 8) | (sample_data[1])), &sample_data[2], 2);
  esp_rom_delay_us(200);
  return ESP_OK;
}

esp_err_t write_tp_cpu_start_cmd()
{
  uint8_t sample_data[4];
  sample_data[0] = 0x04;
  sample_data[1] = 0x00;
  sample_data[2] = 0x01;
  sample_data[3] = 0x00;
  I2C_Write_Touch(SPD2010_ADDR, (((uint16_t)sample_data[0] << 8) | (sample_data[1])),&sample_data[2], 2);
  esp_rom_delay_us(200);
  return ESP_OK;
}

esp_err_t write_tp_clear_int_cmd()
{
  uint8_t sample_data[4];
  sample_data[0] = 0x02;
  sample_data[1] = 0x00;
  sample_data[2] = 0x01;
  sample_data[3] = 0x00;
  I2C_Write_Touch(SPD2010_ADDR, (((uint16_t)sample_data[0] << 8) | (sample_data[1])),&sample_data[2], 2);
  esp_rom_delay_us(200);
  return ESP_OK;
}

esp_err_t read_tp_status_length(tp_status_t *tp_status)
{
  uint8_t sample_data[4];
  sample_data[0] = 0x20;
  sample_data[1] = 0x00;
  I2C_Read_Touch(SPD2010_ADDR, (((uint16_t)sample_data[0] << 8) | (sample_data[1])),sample_data, 4);
  esp_rom_delay_us(200);
  tp_status->status_low.pt_exist = (sample_data[0] & 0x01);
  tp_status->status_low.gesture = (sample_data[0] & 0x02);
  tp_status->status_low.aux = ((sample_data[0] & 0x08)); //aux, cytang
  tp_status->status_high.tic_busy = ((sample_data[1] & 0x80) >> 7);
  tp_status->status_high.tic_in_bios = ((sample_data[1] & 0x40) >> 6);
  tp_status->status_high.tic_in_cpu = ((sample_data[1] & 0x20) >> 5);
  tp_status->status_high.tint_low = ((sample_data[1] & 0x10) >> 4);
  tp_status->status_high.cpu_run = ((sample_data[1] & 0x08) >> 3);
  tp_status->read_len = (sample_data[3] << 8 | sample_data[2]);
  return ESP_OK;
}

esp_err_t read_tp_hdp(tp_status_t *tp_status, SPD2010_Touch *touch)
{
  uint8_t sample_data[4+(10*6)]; // 4 Bytes Header + 10 Finger * 6 Bytes
  uint8_t i, offset;
  uint8_t check_id;
  sample_data[0] = 0x00;
  sample_data[1] = 0x03;
  I2C_Read_Touch(SPD2010_ADDR, (((uint16_t)sample_data[0] << 8) | (sample_data[1])),sample_data, tp_status->read_len);


  check_id = sample_data[4];
  if ((check_id <= 0x0A) && tp_status->status_low.pt_exist) {
    touch->touch_num = ((tp_status->read_len - 4)/6);
    touch->gesture = 0x00;
    for (i = 0; i < touch->touch_num; i++) {
      offset = i*6;
      touch->rpt[i].id = sample_data[4 + offset];
      touch->rpt[i].x = (((sample_data[7 + offset] & 0xF0) << 4)| sample_data[5 + offset]);
      touch->rpt[i].y = (((sample_data[7 + offset] & 0x0F) << 8)| sample_data[6 + offset]);
      touch->rpt[i].weight = sample_data[8 + offset];
    }
    /* For slide gesture recognize */
    if ((touch->rpt[0].weight != 0) && (touch->down != 1)) {
      touch->down = 1;
      touch->up = 0 ;
      touch->down_x = touch->rpt[0].x;
      touch->down_y = touch->rpt[0].y;
    } else if ((touch->rpt[0].weight == 0) && (touch->down == 1)) {
      touch->up = 1;
      touch->down = 0;
      touch->up_x = touch->rpt[0].x;
      touch->up_y = touch->rpt[0].y;
    }
    /* Dump Log */
    // for (uint8_t finger_num = 0; finger_num < touch->touch_num; finger_num++) {
    //   printf("ID[%d], X[%d], Y[%d], Weight[%d]\n",
    //                 touch->rpt[finger_num].id,
    //                 touch->rpt[finger_num].x,
    //                 touch->rpt[finger_num].y,
    //                 touch->rpt[finger_num].weight);
    // }
  } else if ((check_id == 0xF6) && tp_status->status_low.gesture) {
    touch->touch_num = 0x00;
    touch->up = 0;
    touch->down = 0;
    touch->gesture = sample_data[6] & 0x07;
    printf("gesture : 0x%02x\n", touch->gesture);
  } else {
    touch->touch_num = 0x00;
    touch->gesture = 0x00;
  }
  return ESP_OK;
}

esp_err_t read_tp_hdp_status(tp_hdp_status_t *tp_hdp_status)
{
  uint8_t sample_data[8];
  sample_data[0] = 0xFC;
  sample_data[1] = 0x02;
  I2C_Read_Touch(SPD2010_ADDR, (((uint16_t)sample_data[0] << 8) | (sample_data[1])),sample_data, 8);
  tp_hdp_status->status = sample_data[5];
  tp_hdp_status->next_packet_len = (sample_data[2] | sample_data[3] << 8);
  return ESP_OK;
}

esp_err_t Read_HDP_REMAIN_DATA(tp_hdp_status_t *tp_hdp_status)
{
  uint8_t sample_data[32];
  sample_data[0] = 0x00;
  sample_data[1] = 0x03;
  I2C_Read_Touch(SPD2010_ADDR,  (((uint16_t)sample_data[0] << 8) | (sample_data[1])),sample_data, tp_hdp_status->next_packet_len);
  return ESP_OK;
}

esp_err_t read_fw_version()
{
  uint8_t sample_data[18];
	uint16_t DVer;
	uint32_t Dummy, PID, ICName_H, ICName_L;
  sample_data[0] = 0x26;
  sample_data[1] = 0x00;
  I2C_Read_Touch(SPD2010_ADDR, (((uint16_t)sample_data[0] << 8) | (sample_data[1])),sample_data, 18);
	Dummy = ((sample_data[0] << 24) | (sample_data[1] << 16) | (sample_data[3] << 8) | (sample_data[0]));
	DVer = ((sample_data[5] << 8) | (sample_data[4]));
	PID = ((sample_data[9] << 24) | (sample_data[8] << 16) | (sample_data[7] << 8) | (sample_data[6]));
	ICName_L = ((sample_data[13] << 24) | (sample_data[12] << 16) | (sample_data[11] << 8) | (sample_data[10]));    // "2010"
	ICName_H = ((sample_data[17] << 24) | (sample_data[16] << 16) | (sample_data[15] << 8) | (sample_data[14]));    // "SPD"
  printf("Dummy[%d], DVer[%d], PID[%d], Name[%d-%d]\r\n", Dummy, DVer, PID, ICName_H, ICName_L);
  return ESP_OK;
}

esp_err_t tp_read_data(SPD2010_Touch *touch)
{
  tp_status_t tp_status = {0};
  tp_hdp_status_t tp_hdp_status = {0};
  read_tp_status_length(&tp_status);
  if (tp_status.status_high.tic_in_bios) {
    /* Write Clear TINT Command */
    write_tp_clear_int_cmd();
    /* Write CPU Start Command */
    write_tp_cpu_start_cmd();
  } else if (tp_status.status_high.tic_in_cpu) {
    /* Write Touch Change Command */
    write_tp_point_mode_cmd();
    /* Write Touch Start Command */
    write_tp_start_cmd();
    /* Write Clear TINT Command */
    write_tp_clear_int_cmd();
  } else if (tp_status.status_high.cpu_run && tp_status.read_len == 0) {
    write_tp_clear_int_cmd();
  } else if (tp_status.status_low.pt_exist || tp_status.status_low.gesture) {
    /* Read HDP */
    read_tp_hdp(&tp_status, touch);
hdp_done_check:
    /* Read HDP Status */
    read_tp_hdp_status(&tp_hdp_status);
    if (tp_hdp_status.status == 0x82) {
      /* Clear INT */
      write_tp_clear_int_cmd();
    }
    else if (tp_hdp_status.status == 0x00) {
      /* Read HDP Remain Data */
      Read_HDP_REMAIN_DATA(&tp_hdp_status);
      goto hdp_done_check;
    }
  } else if (tp_status.status_high.cpu_run && tp_status.status_low.aux) {
    write_tp_clear_int_cmd();
  }

  return ESP_OK;
}

