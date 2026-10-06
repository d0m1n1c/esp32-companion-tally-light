#include <Arduino.h>
#include <lvgl.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"
#include "lv_conf.h"
#include "base64.hpp"
#include <Wire.h>
#include "HWCDC.h"
#include <WiFi.h>
#include <Preferences.h>
#include <cstdint>
#include "XPowersLib.h"

HWCDC USBSerial;

// LVGL tick period
#define EXAMPLE_LVGL_TICK_PERIOD_MS 2

// Display setup
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_SH8601 *gfx = new Arduino_SH8601(
  bus, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);

// Touch setup
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
  std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void Arduino_IIC_Touch_Interrupt(void);

std::unique_ptr<Arduino_IIC> FT3168(new Arduino_FT3x68(IIC_Bus, FT3168_DEVICE_ADDRESS,
                                                       DRIVEBUS_DEFAULT_VALUE, TP_INT, Arduino_IIC_Touch_Interrupt));

void Arduino_IIC_Touch_Interrupt(void) {
  FT3168->IIC_Interrupt_Flag = true;
}

// LVGL display objects
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[LCD_WIDTH * LCD_HEIGHT / 10];

lv_obj_t *screen;
lv_obj_t *tally_circle;  // Circle showing active color
lv_obj_t *button_label;  // Button text label (in center of circle)
lv_obj_t *status_label;  // Status label at bottom
lv_obj_t *companion_status_label;  // Companion status text
lv_obj_t *battery_label;  // Battery percentage (top right)
lv_obj_t *battery_icon_label;  // Battery icon (top right, next to percentage)
lv_obj_t *battery_charging_label;  // Charging symbol when plugged in

// Menu objects
bool menu_visible = false;
lv_obj_t *menu_bg;  // Menu background/modal
lv_obj_t *menu_btn_reconnect_wifi;
lv_obj_t *menu_btn_reconnect_companion;
lv_obj_t *menu_camera_buttons[8];
lv_obj_t *menu_brightness_slider;
lv_obj_t *menu_btn_close;
lv_obj_t *menu_button_menu;  // Button to open menu

// Power management
XPowersPMU power;

// Tally state
Preferences preferences;
String wifi_ssid = "";
String wifi_password = "";
String companion_host = "";
uint16_t companion_port = 16622;
WiFiClient companion_client;
bool wifi_connected = false;
bool companion_connected = false;
unsigned long last_connection_attempt = 0;
#define CONNECTION_RETRY_INTERVAL 5000

// Satellite protocol
String device_id = "esp32-tally";
uint32_t tally_color = 0x000000;
uint32_t text_color = 0xffffff;
String button_text = "";
unsigned long last_ping = 0;
#define PING_INTERVAL 2000

// Circle properties
#define CIRCLE_RADIUS 80
#define CENTER_X (LCD_WIDTH / 2)
#define CENTER_Y (LCD_HEIGHT / 2)

// LVGL flush callback
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif

  lv_disp_flush_ready(disp);
}

// Touch read callback
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  int32_t touchX = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
  int32_t touchY = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);

  if (FT3168->IIC_Interrupt_Flag == true) {
    FT3168->IIC_Interrupt_Flag = false;
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// Increase LVGL tick
void example_increase_lvgl_tick(void *arg) {
  lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

// Menu button event handlers
static void menu_reconnect_wifi_handler(lv_event_t *e) {
  WiFi.disconnect(false);
  delay(500);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  USBSerial.println("Reconnecting WiFi...");
}

static void menu_reconnect_companion_handler(lv_event_t *e) {
  if (companion_connected) {
    companion_client.stop();
  }
  companion_connected = false;
  last_connection_attempt = 0;
  USBSerial.println("Reconnecting to Companion...");
  updateStatusUI();
}

static void menu_camera_handler(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  uint32_t camera_num = (uint32_t)lv_event_get_user_data(e);
  
  // Clear highlights from all buttons
  for (int i = 0; i < 8; i++) {
    lv_obj_set_style_bg_color(menu_camera_buttons[i], lv_color_make(80, 80, 80), 0);
    lv_obj_set_style_border_width(menu_camera_buttons[i], 0, 0);
  }
  
  // Highlight the selected button
  lv_obj_set_style_bg_color(btn, lv_color_make(0, 200, 0), 0);
  lv_obj_set_style_border_width(btn, 3, 0);
  lv_obj_set_style_border_color(btn, lv_color_make(0, 255, 0), 0);
  
  // Update device_id
  device_id = String("camera") + String(camera_num);
  preferences.putString("device_id", device_id);
  USBSerial.print("Device ID changed to: ");
  USBSerial.println(device_id);
  last_connection_attempt = 0;
  if (companion_connected) {
    companion_client.stop();
    companion_connected = false;
  }
  updateStatusUI();
}

static void menu_brightness_handler(lv_event_t *e) {
  lv_obj_t *slider = lv_event_get_target(e);
  int32_t brightness = lv_slider_get_value(slider);
  gfx->setBrightness(brightness);
  USBSerial.print("Brightness: ");
  USBSerial.println(brightness);
}

static void menu_close_handler(lv_event_t *e) {
  lv_obj_add_flag(menu_bg, LV_OBJ_FLAG_HIDDEN);
  menu_visible = false;
}

static void menu_open_handler(lv_event_t *e) {
  lv_obj_clear_flag(menu_bg, LV_OBJ_FLAG_HIDDEN);
  menu_visible = true;
}

void toggleMenu() {
  if (menu_visible) {
    lv_obj_add_flag(menu_bg, LV_OBJ_FLAG_HIDDEN);
    menu_visible = false;
  } else {
    lv_obj_clear_flag(menu_bg, LV_OBJ_FLAG_HIDDEN);
    menu_visible = true;
  }
}

// Create UI elements
void createUI() {
  // Get default theme
  lv_theme_t *th = lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
  lv_disp_set_theme(NULL, th);

  // Create main screen
  screen = lv_disp_get_scr_act(NULL);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

  // Create tally circle - centered on screen
  tally_circle = lv_obj_create(screen);
  lv_obj_set_size(tally_circle, CIRCLE_RADIUS * 2, CIRCLE_RADIUS * 2);
  lv_obj_set_pos(tally_circle, CENTER_X - CIRCLE_RADIUS, CENTER_Y - CIRCLE_RADIUS);
  lv_obj_set_style_radius(tally_circle, CIRCLE_RADIUS, 0);
  lv_obj_set_style_bg_color(tally_circle, lv_color_black(), 0);
  lv_obj_set_style_border_width(tally_circle, 0, 0);
  lv_obj_set_style_pad_all(tally_circle, 0, 0);

  // Create button text label (inside circle)
  button_label = lv_label_create(tally_circle);
  lv_label_set_text(button_label, "");
  lv_obj_set_align(button_label, LV_ALIGN_CENTER);
  lv_obj_set_style_text_font(button_label, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(button_label, lv_color_white(), 0);

  // Create companion status label (top)
  companion_status_label = lv_label_create(screen);
  lv_label_set_text(companion_status_label, "COMPANION");
  lv_obj_set_pos(companion_status_label, 30, 10);
  lv_obj_set_style_text_color(companion_status_label, lv_color_make(0, 200, 0), 0);

  // Create battery label (top right) - right aligned
  battery_label = lv_label_create(screen);
  lv_label_set_text(battery_label, "100%");
  lv_obj_set_pos(battery_label, 258, 10);
  lv_obj_set_width(battery_label, 60);
  lv_obj_set_style_text_color(battery_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_align(battery_label, LV_TEXT_ALIGN_RIGHT, 0);

  // Create battery icon label (top right, next to percentage) - bigger font
  battery_icon_label = lv_label_create(screen);
  lv_label_set_text(battery_icon_label, LV_SYMBOL_BATTERY_FULL);
  lv_obj_set_pos(battery_icon_label, 320, 8);
  lv_obj_set_style_text_color(battery_icon_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(battery_icon_label, &lv_font_montserrat_20, 0);

  // Create battery charging symbol (top right, next to icon) - smaller
  battery_charging_label = lv_label_create(screen);
  lv_label_set_text(battery_charging_label, "");
  lv_obj_set_pos(battery_charging_label, 327, 10);
  lv_obj_set_style_text_color(battery_charging_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(battery_charging_label, &lv_font_montserrat_16, 0);

  // Create status label (bottom)
  status_label = lv_label_create(screen);
  lv_label_set_text(status_label, "IDLE");
  lv_obj_set_pos(status_label, 30, 420);
  lv_obj_set_style_text_color(status_label, lv_color_white(), 0);

  // Create menu button (bottom right) - cog icon with large hitbox
  menu_button_menu = lv_btn_create(screen);
  lv_obj_set_size(menu_button_menu, 120, 120);
  lv_obj_set_pos(menu_button_menu, 223, 318);
  lv_obj_set_style_bg_opa(menu_button_menu, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(menu_button_menu, 0, 0);
  lv_obj_set_style_pad_all(menu_button_menu, 0, 0);
  lv_obj_set_style_shadow_width(menu_button_menu, 0, 0);
  lv_obj_add_event_cb(menu_button_menu, menu_open_handler, LV_EVENT_CLICKED, NULL);
  lv_obj_t *label = lv_label_create(menu_button_menu);
  lv_label_set_text(label, LV_SYMBOL_SETTINGS);
  lv_obj_set_align(label, LV_ALIGN_BOTTOM_RIGHT);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);

  // Create menu background (modal overlay)
  menu_bg = lv_obj_create(screen);
  lv_obj_set_size(menu_bg, LCD_WIDTH, LCD_HEIGHT);
  lv_obj_set_pos(menu_bg, 0, 0);
  lv_obj_set_style_bg_color(menu_bg, lv_color_make(30, 30, 30), 0);
  lv_obj_set_style_bg_opa(menu_bg, LV_OPA_90, 0);
  lv_obj_set_style_border_width(menu_bg, 0, 0);
  lv_obj_add_flag(menu_bg, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(menu_bg, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_clear_flag(menu_bg, LV_OBJ_FLAG_SCROLLABLE);

  // Title label
  lv_obj_t *menu_title = lv_label_create(menu_bg);
  lv_label_set_text(menu_title, "Menu");
  lv_obj_set_pos(menu_title, 18, 9);
  lv_obj_set_style_text_font(menu_title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(menu_title, lv_color_white(), 0);

  // Reconnect WiFi button
  menu_btn_reconnect_wifi = lv_btn_create(menu_bg);
  lv_obj_set_size(menu_btn_reconnect_wifi, 153, 45);
  lv_obj_set_pos(menu_btn_reconnect_wifi, 9, 45);
  lv_obj_set_style_bg_color(menu_btn_reconnect_wifi, lv_color_make(0, 100, 200), 0);
  lv_obj_set_style_border_width(menu_btn_reconnect_wifi, 2, 0);
  lv_obj_set_style_border_color(menu_btn_reconnect_wifi, lv_color_make(0, 150, 255), 0);
  lv_obj_set_style_radius(menu_btn_reconnect_wifi, 8, 0);
  lv_obj_add_event_cb(menu_btn_reconnect_wifi, menu_reconnect_wifi_handler, LV_EVENT_CLICKED, NULL);
  label = lv_label_create(menu_btn_reconnect_wifi);
  lv_label_set_text(label, "WiFi\nReconnect");
  lv_obj_set_align(label, LV_ALIGN_CENTER);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);

  // Reconnect Companion button
  menu_btn_reconnect_companion = lv_btn_create(menu_bg);
  lv_obj_set_size(menu_btn_reconnect_companion, 153, 45);
  lv_obj_set_pos(menu_btn_reconnect_companion, 171, 45);
  lv_obj_set_style_bg_color(menu_btn_reconnect_companion, lv_color_make(200, 100, 0), 0);
  lv_obj_set_style_border_width(menu_btn_reconnect_companion, 2, 0);
  lv_obj_set_style_border_color(menu_btn_reconnect_companion, lv_color_make(255, 150, 0), 0);
  lv_obj_set_style_radius(menu_btn_reconnect_companion, 8, 0);
  lv_obj_add_event_cb(menu_btn_reconnect_companion, menu_reconnect_companion_handler, LV_EVENT_CLICKED, NULL);
  label = lv_label_create(menu_btn_reconnect_companion);
  lv_label_set_text(label, "Companion\nReconnect");
  lv_obj_set_align(label, LV_ALIGN_CENTER);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);

  // Camera selector title
  lv_obj_t *camera_title = lv_label_create(menu_bg);
  lv_label_set_text(camera_title, "Camera:");
  lv_obj_set_pos(camera_title, 9, 108);
  lv_obj_set_style_text_color(camera_title, lv_color_white(), 0);
  lv_obj_set_style_text_font(camera_title, &lv_font_montserrat_16, 0);

  // Camera selector buttons (4 columns x 2 rows)
  int col_width = 72;
  int row_height = 72;
  int start_x = 18;
  int start_y = 135;
  
  for (int i = 0; i < 8; i++) {
    int col = i % 4;
    int row = i / 4;
    menu_camera_buttons[i] = lv_btn_create(menu_bg);
    lv_obj_set_size(menu_camera_buttons[i], 63, 63);
    lv_obj_set_pos(menu_camera_buttons[i], start_x + (col * col_width), start_y + (row * row_height));
    lv_obj_set_style_bg_color(menu_camera_buttons[i], lv_color_make(80, 80, 80), 0);
    lv_obj_add_event_cb(menu_camera_buttons[i], menu_camera_handler, LV_EVENT_CLICKED, (void *)(uintptr_t)(i + 1));
    label = lv_label_create(menu_camera_buttons[i]);
    lv_label_set_text_fmt(label, "%d", i + 1);
    lv_obj_set_align(label, LV_ALIGN_CENTER);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
  }
  
  // Highlight the currently selected camera
  int current_camera = 1;  // Default to 1
  if (device_id.startsWith("camera")) {
    String num_str = device_id.substring(6);  // Extract number after "camera"
    current_camera = num_str.toInt();
  }
  if (current_camera >= 1 && current_camera <= 8) {
    int idx = current_camera - 1;
    lv_obj_set_style_bg_color(menu_camera_buttons[idx], lv_color_make(0, 200, 0), 0);
    lv_obj_set_style_border_width(menu_camera_buttons[idx], 3, 0);
    lv_obj_set_style_border_color(menu_camera_buttons[idx], lv_color_make(0, 255, 0), 0);
  }

  // Brightness slider label
  lv_obj_t *brightness_title = lv_label_create(menu_bg);
  lv_label_set_text(brightness_title, "Brightness:");
  lv_obj_set_pos(brightness_title, 9, 279);
  lv_obj_set_style_text_color(brightness_title, lv_color_white(), 0);
  lv_obj_set_style_text_font(brightness_title, &lv_font_montserrat_16, 0);

  // Brightness slider
  menu_brightness_slider = lv_slider_create(menu_bg);
  lv_obj_set_size(menu_brightness_slider, 306, 27);
  lv_obj_set_pos(menu_brightness_slider, 9, 302);
  lv_slider_set_range(menu_brightness_slider, 20, 255);
  lv_slider_set_value(menu_brightness_slider, 200, LV_ANIM_OFF);
  lv_obj_add_event_cb(menu_brightness_slider, menu_brightness_handler, LV_EVENT_VALUE_CHANGED, NULL);
  
  // Slider styling
  lv_obj_set_style_bg_color(menu_brightness_slider, lv_color_make(60, 60, 60), LV_PART_MAIN);
  lv_obj_set_style_border_width(menu_brightness_slider, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(menu_brightness_slider, lv_color_make(100, 100, 100), LV_PART_MAIN);
  lv_obj_set_style_radius(menu_brightness_slider, 5, LV_PART_MAIN);
  
  // Slider indicator styling
  lv_obj_set_style_bg_color(menu_brightness_slider, lv_color_make(0, 150, 200), LV_PART_INDICATOR);
  lv_obj_set_style_radius(menu_brightness_slider, 5, LV_PART_INDICATOR);
  
  // Slider knob styling
  lv_obj_set_style_bg_color(menu_brightness_slider, lv_color_make(0, 200, 255), LV_PART_KNOB);
  lv_obj_set_style_border_width(menu_brightness_slider, 2, LV_PART_KNOB);
  lv_obj_set_style_border_color(menu_brightness_slider, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_radius(menu_brightness_slider, 8, LV_PART_KNOB);

  // Close button
  menu_btn_close = lv_btn_create(menu_bg);
  lv_obj_set_size(menu_btn_close, 135, 45);
  lv_obj_set_pos(menu_btn_close, 90, 358);
  lv_obj_set_style_bg_color(menu_btn_close, lv_color_make(180, 50, 50), 0);
  lv_obj_set_style_border_width(menu_btn_close, 2, 0);
  lv_obj_set_style_border_color(menu_btn_close, lv_color_make(255, 100, 100), 0);
  lv_obj_set_style_radius(menu_btn_close, 8, 0);
  lv_obj_add_event_cb(menu_btn_close, menu_close_handler, LV_EVENT_CLICKED, NULL);
  label = lv_label_create(menu_btn_close);
  lv_label_set_text(label, "Close");
  lv_obj_set_align(label, LV_ALIGN_CENTER);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
}

// Update UI with new tally state (power-optimized - only invalidates changed areas)
void updateTallyUI() {
  // Update circle color
  uint8_t r = (tally_color >> 16) & 0xFF;
  uint8_t g = (tally_color >> 8) & 0xFF;
  uint8_t b = tally_color & 0xFF;
  
  lv_color_t circle_color = lv_color_make(r, g, b);
  lv_obj_set_style_bg_color(tally_circle, circle_color, 0);
  lv_obj_invalidate(tally_circle);

  // Update button text and its color
  if (button_text.length() > 0) {
    lv_label_set_text(button_label, button_text.c_str());
    
    uint8_t tr = (text_color >> 16) & 0xFF;
    uint8_t tg = (text_color >> 8) & 0xFF;
    uint8_t tb = text_color & 0xFF;
    
    lv_obj_set_style_text_color(button_label, lv_color_make(tr, tg, tb), 0);
  } else {
    lv_label_set_text(button_label, "");
  }
  lv_obj_invalidate(button_label);

  // Update status label text
  if (r > 150 && r > g && r > b) {
    lv_label_set_text(status_label, "ON AIR");
  } else if (g > 150 && g > r && g > b) {
    lv_label_set_text(status_label, "READY");
  } else if (r == b && b == g) {
    lv_label_set_text(status_label, "IDLE");
  } else {
    lv_label_set_text(status_label, "TALLY");
  }
  lv_obj_invalidate(status_label);
}

// Update display based on WiFi/Companion status
void updateStatusUI() {
  if (!wifi_connected) {
    lv_label_set_text(companion_status_label, "WiFi Needed");
    lv_obj_set_style_text_color(companion_status_label, lv_color_make(255, 255, 0), 0);
    lv_label_set_text(status_label, "Searching...");
  } else if (!companion_connected) {
    lv_label_set_text(companion_status_label, "WiFi OK");
    lv_obj_set_style_text_color(companion_status_label, lv_color_make(255, 255, 0), 0);
    lv_label_set_text(status_label, "Waiting...");
  } else {
    lv_label_set_text(companion_status_label, "COMPANION");
    lv_obj_set_style_text_color(companion_status_label, lv_color_make(0, 200, 0), 0);
    updateTallyUI();
  }
  lv_obj_invalidate(companion_status_label);
  lv_obj_invalidate(status_label);
}

// Parse Satellite protocol messages
void handleSatelliteMessage(String message) {
  message.trim();

  if (message.startsWith("KEY-STATE")) {
    USBSerial.print("[SAT] Message: ");
    USBSerial.println(message);

    int color_pos = message.indexOf("COLOR=");
    if (color_pos != -1) {
      String color_str = message.substring(color_pos + 7, color_pos + 14);

      if (color_str.length() >= 7 && color_str[0] == '#') {
        tally_color = (uint32_t)strtol(color_str.c_str() + 1, NULL, 16);

        USBSerial.print("[SAT] Color received: ");
        USBSerial.println(color_str);

        // Parse TEXTCOLOR field
        int textcolor_pos = message.indexOf("TEXTCOLOR=");
        if (textcolor_pos != -1) {
          String textcolor_str = message.substring(textcolor_pos + 11, textcolor_pos + 18);
          if (textcolor_str.length() >= 7 && textcolor_str[0] == '#') {
            text_color = (uint32_t)strtol(textcolor_str.c_str() + 1, NULL, 16);
            USBSerial.print("[SAT] Text color received: ");
            USBSerial.println(textcolor_str);
          }
        }

        // Parse TEXT field
        int text_pos = message.indexOf("TEXT=");
        if (text_pos != -1) {
          int start = text_pos + 6;
          int end = message.indexOf("\"", start);
          if (end > start) {
            String encoded_text = message.substring(start, end);

            // Decode base64
            unsigned char decoded[256];
            unsigned int decoded_len = decode_base64((unsigned char *)encoded_text.c_str(), decoded);
            decoded[decoded_len] = '\0';

            button_text = String((char *)decoded);
            USBSerial.print("[SAT] Text received (decoded): ");
            USBSerial.println(button_text);
          }
        } else {
          button_text = "";
        }

        // Update UI (event-driven, only on new message)
        updateTallyUI();
      }
    }
  }
}

// Handle Companion connection
void handleCompanionConnection() {
  unsigned long current_time = millis();

  // Check if Companion connection was lost but flag still says connected
  if (companion_connected && !companion_client.connected()) {
    USBSerial.println("[SAT] Companion connection lost, attempting to reconnect");
    companion_connected = false;
    last_connection_attempt = 0;  // Force immediate reconnection attempt
    updateStatusUI();
  }

  if (wifi_connected && !companion_connected && (current_time - last_connection_attempt >= CONNECTION_RETRY_INTERVAL)) {
    if (companion_host.length() == 0) {
      return;
    }

    if (companion_client.connect(companion_host.c_str(), companion_port)) {
      companion_connected = true;
      last_ping = current_time;
      USBSerial.println("[SAT] Connected to Companion!");

      // Register as a Satellite device
      String msg = "ADD-DEVICE DEVICEID=";
      msg += device_id;
      msg += " PRODUCT_NAME=\"ESP32 Tally\" KEYS_TOTAL=1 KEYS_PER_ROW=1 BITMAPS=0 COLORS=hex TEXT=true\n";
      companion_client.print(msg);
      USBSerial.print("[SAT] Sending: ");
      USBSerial.println(msg);

      updateStatusUI();
    } else {
      last_connection_attempt = current_time;
    }
  }

  if (!wifi_connected && companion_connected) {
    companion_client.stop();
    companion_connected = false;
    USBSerial.println("[SAT] WiFi lost, disconnecting");
    updateStatusUI();
  }

  // Keep-alive PING
  if (companion_connected && (current_time - last_ping >= PING_INTERVAL)) {
    companion_client.println("PING");
    last_ping = current_time;
  }

  // Read messages from Companion
  if (companion_connected) {
    if (companion_client.available()) {
      String line = companion_client.readStringUntil('\n');
      handleSatelliteMessage(line);
    }
  }
}

// Serial CLI
void handleSerialInput(void) {
  if (USBSerial.available()) {
    String input = USBSerial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("ssid ")) {
      wifi_ssid = input.substring(5);
      USBSerial.print("SSID set to: ");
      USBSerial.println(wifi_ssid);
    }
    else if (input.startsWith("pass ")) {
      wifi_password = input.substring(5);
      USBSerial.println("Password set");
    }
    else if (input.startsWith("host ")) {
      companion_host = input.substring(5);
      preferences.putString("comp_host", companion_host);
      USBSerial.print("Companion host set to: ");
      USBSerial.println(companion_host);
      last_connection_attempt = 0;
      if (companion_connected) {
        companion_client.stop();
        companion_connected = false;
      }
    }
    else if (input.startsWith("deviceid ")) {
      device_id = input.substring(9);
      preferences.putString("device_id", device_id);
      USBSerial.print("Device ID set to: ");
      USBSerial.println(device_id);
      last_connection_attempt = 0;
      if (companion_connected) {
        companion_client.stop();
        companion_connected = false;
      }
    }
    else if (input == "save") {
      preferences.putString("ssid", wifi_ssid);
      preferences.putString("password", wifi_password);
      preferences.putString("comp_host", companion_host);
      preferences.putString("device_id", device_id);
      USBSerial.println("Configuration saved!");
    }
    else if (input == "status") {
      USBSerial.println("\n=== Current Status ===");
      USBSerial.print("WiFi SSID: ");
      USBSerial.println(wifi_ssid);

      if (wifi_connected) {
        USBSerial.print("IP Address: ");
        USBSerial.println(WiFi.localIP().toString());
        USBSerial.print("Signal Strength: ");
        USBSerial.print(WiFi.RSSI());
        USBSerial.println(" dBm");
      }

      USBSerial.print("Companion Host: ");
      USBSerial.println(companion_host);
      USBSerial.print("Device ID: ");
      USBSerial.println(device_id);
      USBSerial.print("Companion Connected: ");
      USBSerial.println(companion_connected ? "✓ Yes" : "✗ No");
      USBSerial.println("====================\n");
    }
    else if (input == "scan") {
      USBSerial.println("Scanning WiFi networks...");
      int networks = WiFi.scanNetworks();
      if (networks < 0) {
        USBSerial.println("Scan failed. Try again in a moment.");
        return;
      }
      USBSerial.print("Found ");
      USBSerial.print(networks);
      USBSerial.println(" networks:");
      for (int i = 0; i < networks; i++) {
        USBSerial.print("  ");
        USBSerial.print(i + 1);
        USBSerial.print(". ");
        USBSerial.print(WiFi.SSID(i));
        USBSerial.print(" (");
        USBSerial.print(WiFi.RSSI(i));
        USBSerial.println(" dBm)");
      }
    }
    else if (input == "reconnect") {
      if (companion_connected) {
        companion_client.stop();
      }
      companion_connected = false;
      last_connection_attempt = 0;
      USBSerial.println("Reconnecting to Companion...");
      updateStatusUI();
    }
    else if (input == "help") {
      printConfigMenu();
    }
    else if (input == "connect") {
      if (wifi_ssid.length() == 0) {
        USBSerial.println("WiFi SSID not set. Use: ssid <network>");
        return;
      }

      USBSerial.println("Disconnecting from any previous connection...");
      WiFi.disconnect(true);
      delay(1000);

      USBSerial.println("Turning WiFi back on...");
      WiFi.mode(WIFI_STA);
      delay(500);

      USBSerial.print("Connecting to WiFi: ");
      USBSerial.println(wifi_ssid);
      WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

      int wifi_attempts = 0;
      int max_attempts = 40;
      while (WiFi.status() != WL_CONNECTED && wifi_attempts < max_attempts) {
        delay(250);
        USBSerial.print(".");
        wifi_attempts++;
      }
      USBSerial.println();

      if (WiFi.status() == WL_CONNECTED) {
        USBSerial.print("✓ WiFi connected! IP: ");
        USBSerial.println(WiFi.localIP());
        wifi_connected = true;
        last_connection_attempt = 0;
        updateStatusUI();
      } else {
        USBSerial.print("✗ WiFi connection failed. Status: ");
        USBSerial.println(WiFi.status());
      }
    }
    else if (input.length() > 0) {
      USBSerial.println("Unknown command. Type 'help' for options.");
    }
  }
}

void printConfigMenu(void) {
  USBSerial.println("\n=== Tally Light Configuration ===");
  USBSerial.println("WiFi Commands:");
  USBSerial.println("  ssid <network>          - Set WiFi SSID");
  USBSerial.println("  pass <password>         - Set WiFi password");
  USBSerial.println("  connect                 - Connect to WiFi");
  USBSerial.println("  scan                    - Scan WiFi networks");
  USBSerial.println("Companion Commands:");
  USBSerial.println("  host <ip:port>          - Set Companion host (auto-reconnect)");
  USBSerial.println("  deviceid <id>           - Set device ID (auto-reconnect)");
  USBSerial.println("  reconnect               - Force reconnect to Companion");
  USBSerial.println("Storage:");
  USBSerial.println("  save                    - Save all settings to EEPROM");
  USBSerial.println("Info:");
  USBSerial.println("  status                  - Shw current status");
  USBSerial.println("  help                    - Show this menu");
  USBSerial.println("================================\n");
}

void setup(void) {
  USBSerial.begin(115200);
  USBSerial.println("\n=== ESP32-S3 Tally Light (LVGL + Companion Satellite) ===");

  // Load preferences
  preferences.begin("tally", false);
  wifi_ssid = preferences.getString("ssid", "");
  wifi_password = preferences.getString("password", "");
  companion_host = preferences.getString("comp_host", "");
  device_id = preferences.getString("device_id", "esp32-tally");

  // Init Wire for I2C
  Wire.begin(IIC_SDA, IIC_SCL);

  // Init Power Management
  if (!power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    USBSerial.println("AXP2101 initialization failed");
  } else {
    USBSerial.println("AXP2101 initialized successfully");
    power.enableBattVoltageMeasure();
  }

  // Init Touch
  while (FT3168->begin() == false) {
    USBSerial.println("FT3168 initialization fail");
    delay(2000);
  }
  USBSerial.println("FT3168 initialization successfully");

  // Init GFX
  gfx->begin();
  gfx->setBrightness(200);

  // Init LVGL
  lv_init();

  // Setup LVGL display driver
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, LCD_WIDTH * LCD_HEIGHT / 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_WIDTH;
  disp_drv.ver_res = LCD_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Setup LVGL touch driver
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  // Create UI
  createUI();
  updateStatusUI();

  // Setup LVGL tick timer (using esp_timer)
  esp_timer_create_args_t timer_conf = {
    .callback = &example_increase_lvgl_tick,
    .arg = NULL,
  };
  esp_timer_handle_t timer_handle = NULL;
  esp_timer_create(&timer_conf, &timer_handle);
  esp_timer_start_periodic(timer_handle, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000);

  // Init WiFi - auto-connect if SSID is configured
  if (wifi_ssid.length() > 0) {
    USBSerial.print("Auto-connecting to WiFi: ");
    USBSerial.println(wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
    last_connection_attempt = 0;  // Force Companion connection attempt immediately
  } else {
    WiFi.mode(WIFI_OFF);
  }

  printConfigMenu();
}

void loop(void) {
  // Handle connections
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifi_connected) {
      wifi_connected = true;
      last_connection_attempt = 0;  // Force immediate Companion reconnection
      updateStatusUI();
    }
  } else {
    if (wifi_connected) {
      wifi_connected = false;
      updateStatusUI();
    }
  }

  handleCompanionConnection();
  handleSerialInput();

  // Update battery percentage and icon (every 5 seconds to save power)
  static unsigned long last_battery_update = 0;
  unsigned long current_time = millis();
  if (current_time - last_battery_update >= 5000) {
    last_battery_update = current_time;
    if (power.isBatteryConnect()) {
      uint8_t battery_percent = power.getBatteryPercent();
      bool is_charging = power.isCharging();
      
      // Update percentage (always white text)
      lv_label_set_text_fmt(battery_label, "%d%%", battery_percent);
      
      // Determine icon color based on charging state or battery level (ICON ONLY)
      lv_color_t icon_color;
      if (is_charging) {
        icon_color = lv_color_make(0, 255, 0);  // Green when charging
      } else if (battery_percent >= 26) {
        icon_color = lv_color_white();  // White - healthy
      } else if (battery_percent >= 15) {
        icon_color = lv_color_make(255, 200, 0);  // Orange - warning
      } else {
        icon_color = lv_color_make(255, 0, 0);  // Red - critical
      }
      
      // Apply color ONLY to icon, never to percentage text
      lv_obj_set_style_text_color(battery_icon_label, icon_color, 0);
      
      // Show charging symbol if charging (always white)
      if (is_charging) {
        lv_label_set_text(battery_charging_label, LV_SYMBOL_CHARGE);
      } else {
        lv_label_set_text(battery_charging_label, "");
      }
      
      // Select icon based on battery percentage
      const char *icon;
      if (battery_percent >= 85) {
        icon = LV_SYMBOL_BATTERY_FULL;
      } else if (battery_percent >= 60) {
        icon = LV_SYMBOL_BATTERY_3;
      } else if (battery_percent >= 40) {
        icon = LV_SYMBOL_BATTERY_2;
      } else if (battery_percent >= 15) {
        icon = LV_SYMBOL_BATTERY_1;
      } else {
        icon = LV_SYMBOL_BATTERY_EMPTY;
      }
      
      lv_label_set_text(battery_icon_label, icon);
      lv_obj_invalidate(battery_label);
      lv_obj_invalidate(battery_icon_label);
      lv_obj_invalidate(battery_charging_label);
    }
  }

  // LVGL handler - only process when needed
  // This is the key to power optimization: we're NOT calling lv_timer_handler() continuously
  // LVGL will refresh when objects are invalidated
  lv_timer_handler();

  // Small delay to prevent hogging CPU
  delay(5);
}
