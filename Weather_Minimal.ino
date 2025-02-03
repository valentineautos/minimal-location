#include <Arduino.h>
#include <string.h>
#include <ESP_Panel_Library.h>
#include <lvgl.h>
#include "lvgl_port_v8.h"
#include "fonts/ubuntu_24.h"
#include "fonts/ubuntu_32.h"
#include "fonts/ubuntu_100.h"
#include "fonts/font_awesome_icons_small.h"
#include "fonts/weather_icons_60.h"
#include "splash_3.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "GaugeMinimal.h"

Preferences prefs;

// ESPNow structures
typedef struct struct_weather {
  uint8_t flag;
  int8_t temp_now;
  float rain_now;
  int8_t weather_code_now;
  char location_name[30];
}struct_weather;

typedef struct struct_levels {
  uint8_t flag;
  int8_t fuel;
} struct_levels;

struct_weather LocationData;
struct_buttons ButtonData;
struct_levels LevelsData;

// Global components

// Screens
lv_obj_t *startup_scr;                // Black startup screen
lv_obj_t *splash_scr;                 // Splash screen
lv_obj_t *daily_scr;                  // Daily screen
lv_obj_t *track_scr;                  // Track screen
lv_obj_t *dimmer;                     // Dimmer overlay

// Daily screen
lv_obj_t *temp_icon;                  // Temperature icon   
lv_obj_t *temp_label;                 // Temperature label
lv_obj_t *temp_meter;                 // Temperature meter
lv_obj_t *rain_icon;                  // Rain level icon
lv_obj_t *rain_label;                 // Rain level label
lv_obj_t *rain_meter;                 // Rain level meter

lv_meter_indicator_t *temp_indic;     // Temperature meter moving part
lv_meter_indicator_t *rain_indic;     // Rain level meter moving part

lv_obj_t *weather_icon;               // Weather icon
lv_obj_t *location_label;             // Location name label

// Track screen
lv_obj_t *track_fuel_arc;             // Track fuel arc
lv_obj_t *track_fuel_label;           // Track fuel label

// Global styles
static lv_style_t style_unit_text;
static lv_style_t style_track_value_text;
static lv_style_t style_location_text;
static lv_style_t style_weather_icon;
static lv_style_t style_icon;

// Font Awesome Symbols
#define TEMP_SYMBOL "\xEF\x9D\xAB"
#define RAIN_SYMBOL "\xEF\x9D\x9C"
#define FUEL_SYMBOL "\xEF\x94\xAF"

#define WEATHER_CLEAR "\xEF\x86\x85"
#define WEATHER_MAINLY_CLEAR "\xEF\x9D\xA3"
#define WEATHER_PARTLY_CLOUDY "\xEF\x9B\x84"
#define WEATHER_OVERCAST "\xEF\x9D\x84"
#define WEATHER_FOG "\xEF\x9D\x8E"
#define WEATHER_DRIZZLE "\xEF\x9C\xB8"
#define WEATHER_FREEZING_RAIN "\xEF\x9D\x81"
#define WEATHER_LIGHT_RAIN "\xEF\x9C\xBF"
#define WEATHER_HEAVY_RAIN "\xEF\x9D\x80"
#define WEATHER_LIGHT_SNOW "\xEF\x9D\x82"
#define WEATHER_HEAVY_SNOW "\xEF\x8B\x9C"
#define WEATHER_THUNDER "\xEF\x9D\xAC"

// ESPNow checks
volatile bool weather_ready = false;
volatile bool data_ready = false;
volatile bool button_pressed = false;
bool startup_ready = false;
bool startup_complete = false;

// LVGL Timer
hw_timer_t* timer = nullptr;

// Icons using the above structure.
// Positions are for the single icon only - labels will be calculated automatically
// Structure: horz_pos, vert_pos, vert_offset, alert, warning, flag_when
struct_icon_parts TempData = {-165, 0, 5, -10, 30, -1, -1, BELOW, "°C"};
struct_icon_parts RainData = {165, 0, 7, 0, 40, 25, 35, ABOVE, "mm"};
struct_icon_parts FuelData = {0, 0, 0, 0, 100, 25, 10, BELOW, "%"};

// ------------------------------------------------------------
// Various initialisations for the ESP32-S3 LCD Driver Board &
// ST7701 LCD Screen featured in the project videos.
//
// Only change if you know what you're doing
// ------------------------------------------------------------

void scr_init() {
    ESP_Panel *panel = new ESP_Panel();
    panel->init();

    #if LVGL_PORT_AVOID_TEAR
        // When avoid tearing function is enabled, configure the RGB bus according to the LVGL configuration
        ESP_PanelBus_RGB *rgb_bus = static_cast<ESP_PanelBus_RGB *>(panel->getLcd()->getBus());
        rgb_bus->configRgbFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
        rgb_bus->configRgbBounceBufferSize(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE);
    #endif
    panel->begin();

    lvgl_port_init(panel->getLcd(), panel->getTouch());

    startup_scr = lv_scr_act(); // Make startup screen active
    lv_obj_set_style_bg_color(startup_scr, PALETTE_BLACK, 0);
    lv_obj_set_style_bg_opa(startup_scr, LV_OPA_COVER, 0);
}

void wifi_init() {
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

void IRAM_ATTR onTimer() {
    lv_tick_inc(1); // Increment LVGL's tick by 1ms
}

void timer_init() {

   // Setup the timer
  const uint32_t lv_tick_frequency = 1000; // 1 kHz = 1ms period

  timer = timerBegin(lv_tick_frequency); // Configure the timer with 1kHz frequency
  if (!timer) {
      Serial.println("Failed to configure timer!");
      while (1); // Halt execution on failure
  }

  timerAttachInterrupt(timer, &onTimer); // Attach the ISR to the timer
  Serial.println("Timer initialized for LVGL tick!");
}

// Global
void make_styles(void) {
    lv_style_init(&style_unit_text);
    lv_style_set_text_font(&style_unit_text, &ubuntu_24);
    lv_style_set_text_color(&style_unit_text, PALETTE_WHITE);

    lv_style_init(&style_location_text);
    lv_style_set_text_font(&style_location_text, &ubuntu_32);
    lv_style_set_text_color(&style_location_text, PALETTE_WHITE);

    lv_style_init(&style_track_value_text);
    lv_style_set_text_font(&style_track_value_text, &ubuntu_100);
    lv_style_set_text_color(&style_track_value_text, PALETTE_WHITE);

    lv_style_init(&style_icon);
    lv_style_set_text_font(&style_icon, &font_awesome_icons_small);
    lv_style_set_text_color(&style_icon, PALETTE_GREY);

    lv_style_init(&style_weather_icon);
    lv_style_set_text_font(&style_weather_icon, &weather_icons_60);
    lv_style_set_text_color(&style_weather_icon, PALETTE_WHITE);
}

// ESPNow received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // Write to the correct structure based on ESPNow flag
  switch (incomingData[0]) {
    case (FLAG_SET_CHANNEL): {
      int8_t new_channel = incomingData[1];
      esp_wifi_set_channel(new_channel, WIFI_SECOND_CHAN_NONE);
      break;
    }
    case (FLAG_STARTUP):
      startup_ready = true;
      break;
    case (FLAG_BUTTONS):
      memcpy(&ButtonData, incomingData, sizeof(ButtonData));
      button_pressed = true;
      break;
    case (FLAG_CANBUS):
      memcpy(&LevelsData, incomingData, sizeof(LevelsData));
      data_ready = true;
      break;
    case (FLAG_GPS):
      memcpy(&LocationData, incomingData, sizeof(LocationData));
      weather_ready = true;
      break;
  }
}

// Check and update colors
void update_alert_colors(void) {
  lv_obj_set_style_text_color(temp_icon, get_state_color(TempData, LocationData.temp_now, true), 0);

  lv_obj_set_style_text_color(rain_icon, get_state_color(RainData, LocationData.rain_now, true), 0);

  lv_obj_set_style_arc_color(track_fuel_arc, get_state_color(FuelData, LevelsData.fuel, false), LV_PART_INDICATOR);
}

// Move icons and set opacities
void update_show_num(void) {
  // Set temperature icon position
  lv_obj_align(temp_icon, LV_ALIGN_CENTER, TempData.horz_pos, TempData.vert_pos - (is_show_num ? (ICON_MOVEMENT - TempData.vert_offset) : 0));
  lv_obj_set_style_opa(temp_label, (is_show_num ? LV_OPA_COVER : LV_OPA_TRANSP), 0);

  // Set rain icon position
  lv_obj_align(rain_icon, LV_ALIGN_CENTER, RainData.horz_pos, RainData.vert_pos - (is_show_num ? (ICON_MOVEMENT - RainData.vert_offset ): 0));
  lv_obj_set_style_opa(rain_label, (is_show_num) ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

  update_alert_colors();
}

// Switch between screens
void update_mode(void) {
  if (is_track_mode) {
    lv_scr_load_anim(track_scr, LV_SCR_LOAD_ANIM_MOVE_TOP, 1000, 0, false);
  } else {
    lv_scr_load_anim(daily_scr, LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 1000, 0, false);
  }
}

void update_temp(int8_t new_temp = LocationData.temp_now) {
  // check for -1 initialisation
  if (new_temp == -1) {
    lv_label_set_text(temp_label, DEFAULT_LABEL);   // Icon label with unit
    lv_meter_set_indicator_value(temp_meter, temp_indic, TempData.min); // Meter position

    return;
  }

  // Convert to text for labels
  char temp_text_unit[5];  // With unit

  // Convert to strings and add unit
  sprintf(temp_text_unit, "%d%s", new_temp, TempData.unit);

  // Daily screen
  lv_label_set_text(temp_label, temp_text_unit);   // Icon label with unit
  lv_meter_set_indicator_value(temp_meter, temp_indic, new_temp); // Meter position
}

void update_rain(float new_rain = LocationData.rain_now) {
  // check for -1 initialisation
   if (new_rain == -1) {
    lv_label_set_text(rain_label, DEFAULT_LABEL);   // Icon label with unit
    lv_meter_set_indicator_value(rain_meter, rain_indic, RainData.min); // Meter position

    return;
  }

  // Convert to text for labels
  char rain_text_unit[6];  // With unit

  // Convert to strings and add unit
  sprintf(rain_text_unit, "%.1f", new_rain);

  // Daily screen
  lv_label_set_text(rain_label, rain_text_unit);   // Icon label with unit
  lv_meter_set_indicator_value(rain_meter, rain_indic, new_rain); // Meter position
}

void update_weather_icon(int8_t new_weather_icon = LocationData.weather_code_now) {
  if (new_weather_icon == -1) {
    lv_label_set_text(weather_icon, "");
    return;
  }

  switch(new_weather_icon) {
    case 0:
      lv_label_set_text(weather_icon, WEATHER_CLEAR);
      break;
    case 1:
      lv_label_set_text(weather_icon, WEATHER_MAINLY_CLEAR);
      break;
    case 2:
      lv_label_set_text(weather_icon, WEATHER_PARTLY_CLOUDY);
      break;
    case 3: 
      lv_label_set_text(weather_icon, WEATHER_OVERCAST);
      break;
    case 45:
    case 48: 
      lv_label_set_text(weather_icon, WEATHER_FOG);
      break;
    case 51:
    case 53:
    case 55:
      lv_label_set_text(weather_icon, WEATHER_DRIZZLE);
      break;
    case 56:
    case 57:
    case 66:
    case 67: 
      lv_label_set_text(weather_icon, WEATHER_FREEZING_RAIN);
      break;
    case 61:
    case 63:
    case 80:
    case 81:
      lv_label_set_text(weather_icon, WEATHER_LIGHT_RAIN);
      break;
    case 65:
    case 82:
      lv_label_set_text(weather_icon, WEATHER_HEAVY_RAIN);
      break;
    case 71:
    case 85:
      lv_label_set_text(weather_icon, WEATHER_LIGHT_SNOW);
      break;
    case 73:
    case 75:
    case 77:
    case 86:
      lv_label_set_text(weather_icon, WEATHER_HEAVY_SNOW);
      break;
    case 96:
    case 99:
      lv_label_set_text(weather_icon, WEATHER_THUNDER);
      break;
    default:
      lv_label_set_text(weather_icon, "");
      break;
  };
}

void update_location(void) {
   lv_label_set_text(location_label, LocationData.location_name);
}

void update_fuel(int8_t new_fuel = LevelsData.fuel) {
  if (new_fuel == -1) {
    lv_label_set_text(track_fuel_label, DEFAULT_LABEL);   // Icon label with unit
    lv_arc_set_value(track_fuel_arc, FuelData.min); // Arc position

    return;
  }

     // Convert to text for labels
  char fuel_text[6];  // With unit

  // Convert to strings and add unit
  sprintf(fuel_text, "%d%s", new_fuel, FuelData.unit);

  // Daily screen
  lv_label_set_text(track_fuel_label, fuel_text);   // Icon label with unit
  lv_arc_set_value(track_fuel_arc, new_fuel); // Arc position
}

void update_weather(void) {
  update_temp();
  update_rain();
  update_weather_icon();
  update_location();

  // Check and adjust colors
  update_alert_colors();
}

void update_levels(void) {
  update_fuel();

  // Check and adjust colors
  update_alert_colors();
}

void update_brightness(void) {
  // convert from 0-11 to 0-198
  uint8_t dimmed_perc = dimmer_lv * 22;
  lv_obj_set_style_bg_opa(dimmer, dimmed_perc, 0);
}


// Save states to memory
void save_mode(bool new_mode) {
  is_track_mode = new_mode;
  prefs.putBool("is_track_mode", new_mode);

  update_mode();
}

void save_show_num(bool new_show) {
  is_show_num = new_show;
  prefs.putBool("is_show_num", new_show);

  // update display after save
  update_show_num();
}

void save_brightness() {
  prefs.putInt("dimmer_lv", dimmer_lv);

  // update brightness after save
  update_brightness();
}

// Button press handling
void handle_button_press(void) {

  // 350z - Top left button
  if (ButtonData.button == BUTTON_SETTING) {
    switch (ButtonData.press_type) {
      case CLICK_EVENT_CLICK:
        // Show minimal design
        save_show_num(false);
        break;
      case CLICK_EVENT_HOLD:
        // Show daily mode
        save_mode(false);
        break;
    }
  }

  // 350z - Bottom left button
  if (ButtonData.button == BUTTON_MODE) {
    switch (ButtonData.press_type) {
      case CLICK_EVENT_CLICK:
        // Show values display mode
        save_show_num(true);
        break;
      case CLICK_EVENT_HOLD:
        // Show track mode
        save_mode(true);
        break;
    }
  }

  // 350z - Top right button
  if (ButtonData.button == BUTTON_BRIGHTNESS_UP) {
    switch (ButtonData.press_type) {
      case CLICK_EVENT_CLICK:
        // Increase brightness / lower overlay opacity
        if (dimmer_lv > 0) {
          dimmer_lv--;
          save_brightness();
        }
        break;
    }
  }

  // 350z - Bottom right button
  if (ButtonData.button == BUTTON_BRIGHTNESS_DOWN) {
    switch (ButtonData.press_type) {
      case CLICK_EVENT_CLICK:
      // Lower brightness / increase overlay opacity
        if (dimmer_lv < 9) {
          dimmer_lv++;
          save_brightness();
        }
        break;
    }
  }
}

void make_temp_meter(void) {
  // Make the meter
  temp_meter = lv_meter_create(daily_scr);
  lv_obj_set_size(temp_meter, 460, 460);
  lv_obj_set_style_bg_opa(temp_meter, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(temp_meter, 0, 0);
  lv_obj_center(temp_meter);
  lv_obj_set_style_text_font(temp_meter, &ubuntu_24, LV_PART_TICKS);
  lv_obj_set_style_text_color(temp_meter, PALETTE_WHITE, LV_PART_TICKS);

  // Add the scale
  lv_meter_scale_t *temp_scale = lv_meter_add_scale(temp_meter);
  lv_meter_set_scale_ticks(temp_meter, temp_scale, HALF_METER_TICKS, TICK_WIDTH, TICK_LENGTH, PALETTE_WHITE);
  lv_meter_set_scale_major_ticks(temp_meter, temp_scale, HALF_METER_TICKS - 1, TICK_WIDTH, TICK_LENGTH, PALETTE_WHITE, TICK_TEXT_OFFSET);
  lv_meter_set_scale_range(temp_meter, temp_scale, TempData.min, TempData.max, 90, 135);

  lv_meter_indicator_t *outline = lv_meter_add_arc(temp_meter, temp_scale, 2, PALETTE_WHITE, OUTLINE_WIDTH);
  lv_meter_set_indicator_start_value(temp_meter, outline, TempData.min);
  lv_meter_set_indicator_end_value(temp_meter, outline, TempData.max);

  lv_obj_t *temp_zero_label = lv_label_create(daily_scr);
  lv_label_set_text(temp_zero_label, "0");
  lv_obj_add_style(temp_zero_label, &style_unit_text, 0);
  lv_obj_align(temp_zero_label, LV_ALIGN_CENTER, -165, 68);

  temp_indic = lv_meter_add_needle_line(temp_meter, temp_scale, NEEDLE_WIDTH, NEEDLE_COLOR, NEEDLE_OFFSET);
}

void make_rain_meter(void) {
  // Make the meter
  rain_meter = lv_meter_create(daily_scr);
  lv_obj_set_size(rain_meter, 460, 460);
  lv_obj_set_style_bg_opa(rain_meter, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(rain_meter, 0, 0);
  lv_obj_center(rain_meter);
  lv_obj_set_style_text_font(rain_meter, &ubuntu_24, LV_PART_TICKS);
  lv_obj_set_style_text_color(rain_meter, PALETTE_WHITE, LV_PART_TICKS);

  // Add the scale
  lv_meter_scale_t *rain_scale = lv_meter_add_scale(rain_meter);
  lv_meter_set_scale_ticks(rain_meter, rain_scale, HALF_METER_TICKS, TICK_WIDTH, TICK_LENGTH, PALETTE_WHITE);
  lv_meter_set_scale_major_ticks(rain_meter, rain_scale, HALF_METER_TICKS - 1, TICK_WIDTH, TICK_LENGTH, PALETTE_WHITE, TICK_TEXT_OFFSET);
  lv_meter_set_scale_range(rain_meter, rain_scale, RainData.max, RainData.min, 90, 315);

  lv_meter_indicator_t *outline = lv_meter_add_arc(rain_meter, rain_scale, 2, PALETTE_WHITE, OUTLINE_WIDTH);
  lv_meter_set_indicator_start_value(rain_meter, outline,  RainData.max);
  lv_meter_set_indicator_end_value(rain_meter, outline, RainData.min);

  rain_indic = lv_meter_add_needle_line(rain_meter, rain_scale, NEEDLE_WIDTH, NEEDLE_COLOR, NEEDLE_OFFSET);
}

void make_inner_circle(void) {
    lv_obj_t *inner_circle = lv_obj_create(daily_scr);
    lv_obj_set_size(inner_circle, 312, 312);
    lv_obj_center(inner_circle);
    lv_obj_set_style_bg_color(inner_circle, PALETTE_BLACK, 0);
    lv_obj_set_style_bg_opa(inner_circle, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(inner_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(inner_circle, 0, 0);
}

void make_icons(void) {
    temp_icon = lv_label_create(daily_scr);
    lv_label_set_text(temp_icon, TEMP_SYMBOL);
    lv_obj_add_style(temp_icon, &style_icon, 0);
    lv_obj_align(temp_icon, LV_ALIGN_CENTER, -165, 0);

    temp_label = lv_label_create(daily_scr);
    lv_label_set_text(temp_label, "---");
    lv_obj_add_style(temp_label, &style_unit_text, 0);
    lv_obj_set_style_opa(temp_label, 0, 0);
    lv_obj_align(temp_label, LV_ALIGN_CENTER, -165, 20);

    rain_icon = lv_label_create(daily_scr);
    lv_label_set_text(rain_icon, RAIN_SYMBOL);
    lv_obj_add_style(rain_icon, &style_icon, 0);
    lv_obj_align(rain_icon, LV_ALIGN_CENTER, 165, 0);

    rain_label = lv_label_create(daily_scr);
    lv_label_set_text(rain_label, "---");
    lv_obj_add_style(rain_label, &style_unit_text, 0);
    lv_obj_set_style_opa(rain_label, 0, 0);
    lv_obj_align(rain_label, LV_ALIGN_CENTER, 165, 20);
}

void make_weather_icon(void) {
    weather_icon = lv_label_create(daily_scr);
    lv_label_set_text(weather_icon, "");
    lv_obj_add_style(weather_icon, &style_weather_icon, 0);
    lv_obj_align(weather_icon, LV_ALIGN_CENTER, 0, 170);
}

void make_location(void) {
    location_label = lv_label_create(daily_scr);
    lv_label_set_text(location_label, "");
    lv_obj_add_style(location_label, &style_location_text, 0);
    lv_obj_align(location_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_long_mode(location_label, LV_LABEL_LONG_WRAP); 
    lv_obj_set_width(location_label, 260); 
    lv_obj_set_style_text_align(location_label, LV_TEXT_ALIGN_CENTER, 0);
}

void make_track_fuel() {
    track_fuel_arc = lv_arc_create(track_scr);
    lv_obj_set_size(track_fuel_arc, 400, 400);
    lv_arc_set_rotation(track_fuel_arc, 120);
    lv_arc_set_range(track_fuel_arc, FuelData.min, FuelData.max);
    lv_arc_set_bg_angles(track_fuel_arc, 0, 300);
    lv_obj_center(track_fuel_arc);
    lv_obj_set_style_arc_color(track_fuel_arc, PALETTE_GREY, LV_PART_MAIN);
    lv_obj_set_style_arc_color(track_fuel_arc, PALETTE_WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(track_fuel_arc, 20, LV_PART_MAIN);
    lv_obj_set_style_arc_width(track_fuel_arc, 20, LV_PART_INDICATOR);
    lv_obj_remove_style(track_fuel_arc, NULL, LV_PART_KNOB); 
    
    track_fuel_label = lv_label_create(track_scr);
    lv_label_set_text(track_fuel_label, "0");
    lv_obj_add_style(track_fuel_label, &style_track_value_text, 0);
    lv_obj_align(track_fuel_label, LV_ALIGN_CENTER, 0, 4);

    lv_obj_t *track_fuel_icon = lv_label_create(track_scr);
    lv_label_set_text(track_fuel_icon, FUEL_SYMBOL);
    lv_obj_add_style(track_fuel_icon, &style_icon, 0);
    lv_obj_set_style_text_color(track_fuel_icon, PALETTE_WHITE, 0);
    lv_obj_align(track_fuel_icon, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t *track_fuel_unit = lv_label_create(track_scr);
    lv_label_set_text(track_fuel_unit, FuelData.unit);
    lv_obj_add_style(track_fuel_unit, &style_unit_text, 0);
    lv_obj_set_style_text_color(track_fuel_unit, PALETTE_WHITE, 0);
    lv_obj_align(track_fuel_unit, LV_ALIGN_CENTER, 0, 60);
}

void make_splash_screen(void) {
  splash_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(splash_scr, PALETTE_BLACK, 0);

  lv_obj_t *icon_three = lv_img_create(splash_scr);
  lv_img_set_src(icon_three, &splash_3);
  lv_obj_align(icon_three, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_img_recolor(icon_three, PALETTE_WHITE, 0);
  lv_obj_set_style_img_recolor_opa(icon_three, LV_OPA_COVER, 0);
}

void make_daily_screen() {
  daily_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(daily_scr, PALETTE_BLACK, 0);

  make_temp_meter();
  make_rain_meter();
  make_inner_circle();
  make_icons();
  make_weather_icon();
  make_location();
}

void make_track_screen() {
  track_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(track_scr, PALETTE_BLACK, 0);

  make_track_fuel();
}

void make_dimmer(void) {
  dimmer = lv_layer_top();

  lv_obj_set_size(dimmer, 480, 480);
  lv_obj_center(dimmer);
  lv_obj_set_style_bg_color(dimmer, PALETTE_BLACK, 0);

  update_brightness();
}

void init_values() {
  // Initialise value and view
  LocationData.temp_now = -1;
  LocationData.weather_code_now = -1;
  LocationData.rain_now = -1;
  LevelsData.fuel = -1;

  update_weather();
  update_levels();
  update_show_num();
}

// Two step managed 
bool complete = false; // flag for screen changes to prevent recurssion

void change_loading_scr(lv_timer_t *timer) {
  lv_obj_t *next_scr = (lv_obj_t *)timer->user_data;
  lv_scr_load_anim(next_scr, LV_SCR_LOAD_ANIM_FADE_IN, 1000, 0, true); // delete startup on exit

  if (!complete) {
    lv_timer_t *exit_timer = lv_timer_create(change_loading_scr, 2000, (is_track_mode) ? track_scr : daily_scr); // back to blank
    lv_timer_set_repeat_count(exit_timer, 1);
    complete = true;
  }
}

void start_splash() {
  lv_scr_load_anim(splash_scr, LV_SCR_LOAD_ANIM_FADE_IN, 1000, 0, false);

  lv_timer_t *exit_timer = lv_timer_create(change_loading_scr, 3500, startup_scr); // back to blank
  lv_timer_set_repeat_count(exit_timer, 1);
}

// if no startup message received, start anyway
void force_splash(lv_timer_t *timer) {
  // avoid refire if complete
  if (!startup_complete) {
    start_splash();
    startup_complete = true;
  }
}


void make_ui(void) {
  make_styles();

  make_daily_screen();
  make_track_screen();
  make_dimmer();

  make_splash_screen();

  init_values();
}

void setup()                              
{
  Serial.begin(115200);

  prefs.begin("weather_store", false);
  is_track_mode = prefs.getBool("is_track_mode", false);
  is_show_num = prefs.getBool("is_show_num", false);
  dimmer_lv = prefs.getInt("dimmer_lv", 0);

  scr_init();
  wifi_init();

  make_ui();

  timer_init();

  lv_timer_t *startup_timer = lv_timer_create(force_splash, STARTUP_OVERRIDE_TIMER, startup_scr); // back to blank
  lv_timer_set_repeat_count(startup_timer, 1);
}

void loop(){
    lv_timer_handler();

    if (startup_ready && !startup_complete) {
      start_splash();
      startup_ready = false;
      startup_complete = true;
    }

    if (weather_ready) {
      update_temp();
      update_rain();
      update_weather_icon();
      update_location();
      weather_ready = false;
    }

    if (data_ready) {
      update_fuel();
      data_ready = false;
    }

    if (button_pressed) {
      handle_button_press();
      button_pressed = false;
    }
}