/*****************************************************************************
  | File        :   LVGL_Driver.c
  
  | help        : 
    The provided LVGL library file must be installed first
******************************************************************************/
#include "LVGL_Driver.h"

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[ LVGL_BUF_LEN ];
static lv_color_t buf2[ LVGL_BUF_LEN ];
// static lv_color_t* buf1 = (lv_color_t*) heap_caps_malloc(LVGL_BUF_LEN, MALLOC_CAP_SPIRAM);
// static lv_color_t* buf2 = (lv_color_t*) heap_caps_malloc(LVGL_BUF_LEN, MALLOC_CAP_SPIRAM);
    


/* Serial debugging */
void Lvgl_print(const char * buf)
{
    // Serial.printf(buf);
    // Serial.flush();
}
void Lvgl_port_rounder_callback(struct _lv_disp_drv_t * disp_drv, lv_area_t * area)
{
  uint16_t x1 = area->x1;
  uint16_t x2 = area->x2;

  // round the start of coordinate down to the nearest 4M number
  area->x1 = (x1 >> 2) << 2;

  // round the end of coordinate up to the nearest 4N+3 number
  area->x2 = ((x2 >> 2) << 2) + 3;
}
/*  Display flushing
    Displays LVGL content on the LCD
    This function implements associating LVGL data to the LCD screen
*/
void Lvgl_Display_LCD( lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p )
{
  static uint32_t drawCount = 0;
  static uint32_t lastReportTime = 0;
  static uint32_t maxDrawTime = 0;

  uint32_t drawStartTime = micros();

  // DEBUG: Log what area is being drawn (v2.9.9-debug)
  // Log every 5th draw to reduce spam but still capture steady-state pattern
  static uint32_t detailLogCount = 0;
  detailLogCount++;
  if (detailLogCount % 5 == 0) {  // Log every 5th draw
    printf("[LVGL_AREA_%lu] x1=%d, y1=%d, x2=%d, y2=%d (w=%d, h=%d)\n",
           detailLogCount,
           area->x1, area->y1, area->x2, area->y2,
           area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
  }

  // Call the SPI display transfer (this may block if SPI is stuck)
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2, ( uint16_t *)&color_p->full);

  uint32_t drawTime = micros() - drawStartTime;

  // Track statistics
  drawCount++;
  if (drawTime > maxDrawTime) {
    maxDrawTime = drawTime;
  }

  // Report every 5 seconds
  uint32_t now = millis();
  if (now - lastReportTime >= 5000) {
    printf("[LVGL_DRAW] %lu draws in last 5s, max draw time: %lu us\n", drawCount, maxDrawTime);

    // Warn if any draw took longer than 100ms (severe lag)
    if (maxDrawTime > 100000) {
      printf("[LVGL_DRAW] WARNING: Extremely slow draw detected (%lu us = %lu ms)\n",
             maxDrawTime, maxDrawTime / 1000);
    }

    drawCount = 0;
    maxDrawTime = 0;
    lastReportTime = now;
  }

  lv_disp_flush_ready( disp_drv );
}
/*Read the touchpad*/
void Lvgl_Touchpad_Read( lv_indev_drv_t * indev_drv, lv_indev_data_t * data )
{
  bool tp_pressed = false;
  uint16_t tp_x = 0;
  uint16_t tp_y = 0;
  uint8_t tp_cnt = 0;
  tp_pressed = Touch_Get_xy(&tp_x, &tp_y, NULL, &tp_cnt, CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
  if (tp_pressed && (tp_cnt > 0)) {
    data->point.x = tp_x;
    data->point.y = tp_y;
    data->state = LV_INDEV_STATE_PR;
    // printf("LVGL : X=%u Y=%u points=%d\r\n",  tp_x , tp_y,tp_cnt);
  }else {
    data->state = LV_INDEV_STATE_REL;
  }

}
void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}
void Lvgl_Init(void)
{
  lv_init();
  lv_disp_draw_buf_init( &draw_buf, buf1, buf2, LVGL_BUF_LEN);

  /*Initialize the display*/
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init( &disp_drv );
  /*Change the following line to your display resolution*/
  disp_drv.hor_res = LCD_WIDTH;
  disp_drv.ver_res = LCD_HEIGHT;
  disp_drv.flush_cb = Lvgl_Display_LCD;
  disp_drv.rounder_cb = Lvgl_port_rounder_callback;
  disp_drv.full_refresh = 0;                    /**< 0: Partial refresh (only redraw changed areas) - v2.9.0 optimization */
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register( &disp_drv );

  /*Initialize the (dummy) input device driver*/
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init( &indev_drv );
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = Lvgl_Touchpad_Read;
  lv_indev_drv_register( &indev_drv );

  // Demo label removed - Timer_UI creates its own display
  // (Old code created "Hello Ardino and LVGL!" label that blocked timer)

  const esp_timer_create_args_t lvgl_tick_timer_args = {
    .callback = &example_increase_lvgl_tick,
    .name = "lvgl_tick"
  };
  esp_timer_handle_t lvgl_tick_timer = NULL;
  esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);

}
void Lvgl_Loop(void)
{
  // DEBUG: Track when LVGL actually processes invalidations (v2.9.7-debug)
  static uint32_t loopCounter = 0;
  static uint32_t lastReportTime = 0;
  loopCounter++;
  uint32_t now = millis();
  if (now - lastReportTime >= 5000) {
    printf("[DEBUG_v2.9.7] Lvgl_Loop() called %lu times in last 5s\n", loopCounter);
    loopCounter = 0;
    lastReportTime = now;
  }

  lv_timer_handler(); /* let the GUI do its work */
  // delay( 5 );
}
