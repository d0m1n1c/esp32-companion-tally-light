#include <Arduino.h>
#include "Arduino_GFX_Library.h"
#include "Arduino_DriveBus_Library.h"
#include "pin_config.h"
#include "base64.hpp"
#include <Wire.h>
#include "HWCDC.h"
#include <WiFi.h>
#include <Preferences.h>

HWCDC USBSerial;

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

// Tally state
bool tally_on = true;  // Start with green (ON)
unsigned long last_toggle_time = 0;  // Last time tally was toggled
#define DEBOUNCE_MS 300  // Debounce time in milliseconds

// WiFi and networking
Preferences preferences;
String wifi_ssid = "";
String wifi_password = "";
String companion_host = "";
uint16_t companion_port = 16622;  // Satellite API TCP port
WiFiClient companion_client;
bool wifi_connected = false;
bool companion_connected = false;
unsigned long last_connection_attempt = 0;
#define CONNECTION_RETRY_INTERVAL 5000  // Retry every 5 seconds

// Satellite protocol
String device_id = "esp32-tally";
uint32_t tally_color = 0x000000;  // Current color from Companion
uint32_t text_color = 0xffffff;   // Current text color from Companion
String button_text = "";  // Current text from Companion button
unsigned long last_ping = 0;
#define PING_INTERVAL 2000  // Send PING every 2 seconds

// Circle properties
#define CIRCLE_RADIUS 80
#define CENTER_X (LCD_WIDTH / 2)
#define CENTER_Y (LCD_HEIGHT / 2)

// Colors
#define COLOR_GREEN RGB565(0, 200, 0)    // Green for ON/Preview
#define COLOR_RED RGB565(200, 0, 0)      // Red for Program
#define COLOR_BLACK RGB565_BLACK

// Parse Satellite protocol messages from Companion
void handleSatelliteMessage(String message) {
  message.trim();
  
  // Look for KEY-STATE message with COLOR field
  // Example: KEY-STATE DEVICEID=esp32:cam1 KEY=0 TYPE=BUTTON COLOR="#ff0000" TEXT="Camera 1"
  if (message.startsWith("KEY-STATE")) {
    USBSerial.print("[SAT] Message: ");
    USBSerial.println(message);
    
    int color_pos = message.indexOf("COLOR=");
    if (color_pos != -1) {
      // Skip "COLOR=" (6 chars) and the leading quote (1 char) to get to #RRGGBB
      String color_str = message.substring(color_pos + 7, color_pos + 14);  // Skip quote, get "#RRGGBB"
      
      // Ensure we have a valid color string
      if (color_str.length() >= 7 && color_str[0] == '#') {
        // Parse hex color (skip the # at position 0)
        tally_color = (uint32_t)strtol(color_str.c_str() + 1, NULL, 16);
        
        USBSerial.print("[SAT] Color received: ");
        USBSerial.println(color_str);
        
        // Parse TEXTCOLOR field if present
        int textcolor_pos = message.indexOf("TEXTCOLOR=");
        if (textcolor_pos != -1) {
          String textcolor_str = message.substring(textcolor_pos + 11, textcolor_pos + 18);  // Skip quote, get "#RRGGBB"
          if (textcolor_str.length() >= 7 && textcolor_str[0] == '#') {
            text_color = (uint32_t)strtol(textcolor_str.c_str() + 1, NULL, 16);
            USBSerial.print("[SAT] Text color received: ");
            USBSerial.println(textcolor_str);
          }
        }
        
        // Parse TEXT field if present
        int text_pos = message.indexOf("TEXT=");
        if (text_pos != -1) {
          // TEXT="base64encodedtext" - extract text between quotes
          int start = text_pos + 6;  // Skip TEXT="
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
          USBSerial.println("[SAT] No TEXT field in message");
          button_text = "";
        }
        
        updateDisplay();
      }
    }
  }
}

void setup(void) {
  USBSerial.begin(115200);
  USBSerial.println("\n=== ESP32-S3 Tally Light (Companion Satellite) ===");

  // Init preferences (non-volatile storage)
  preferences.begin("tally", false);
  wifi_ssid = preferences.getString("ssid", "");
  wifi_password = preferences.getString("password", "");
  companion_host = preferences.getString("comp_host", "");
  device_id = preferences.getString("device_id", "esp32-tally");
  
  // Init Wire for I2C
  Wire.begin(IIC_SDA, IIC_SCL);

  // Init Touch
  while (FT3168->begin() == false) {
    USBSerial.println("FT3168 initialization fail");
    delay(2000);
  }
  USBSerial.println("FT3168 initialized");

  FT3168->IIC_Write_Device_State(FT3168->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
                                 FT3168->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);

  // Init Display
  if (!gfx->begin()) {
    USBSerial.println("Display initialization failed!");
  }

  gfx->setBrightness(255);

  // Start WiFi
  WiFi.mode(WIFI_STA);
  if (wifi_ssid.length() > 0) {
    USBSerial.print("Connecting to WiFi: ");
    USBSerial.println(wifi_ssid);
    WiFi.setAutoReconnect(true);
    WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
    
    // Wait for WiFi connection with timeout
    int wifi_attempts = 0;
    int max_attempts = 40;  // ~10 seconds at 250ms per attempt
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
    } else {
      USBSerial.print("⚠️ WiFi not connected. Status: ");
      USBSerial.println(WiFi.status());
    }
  } else {
    USBSerial.println("No saved WiFi credentials");
  }

  // Draw initial tally state
  updateDisplay();
  
  printConfigMenu();
  USBSerial.println("Setup complete - waiting for configuration via serial");
}

void loop(void) {
  // Handle TCP connection to Companion
  handleCompanionConnection();
  
  // Handle serial configuration commands
  handleSerialInput();
  
  // Check for touch
  if (FT3168->IIC_Interrupt_Flag == true) {
    FT3168->IIC_Interrupt_Flag = false;

    int32_t touchX = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
    int32_t touchY = FT3168->IIC_Read_Device_Value(FT3168->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);

    // Check if touch is valid and within screen bounds
    if (touchX > 0 && touchX < LCD_WIDTH && touchY > 0 && touchY < LCD_HEIGHT) {
      // Only toggle if enough time has passed since last toggle (debounce)
      unsigned long current_time = millis();
      if (current_time - last_toggle_time >= DEBOUNCE_MS) {
        last_toggle_time = current_time;
        
        // If not connected to companion, allow local toggle for testing
        if (!companion_connected) {
          tally_on = !tally_on;
          USBSerial.print("Local tally state: ");
          USBSerial.println(tally_on ? "ON (Green)" : "OFF (Red)");
          updateDisplay();
        }
      }
    }
  }

  // Keep-alive: send PING periodically if connected
  if (companion_connected) {
    unsigned long current_time = millis();
    if (current_time - last_ping >= PING_INTERVAL) {
      last_ping = current_time;
      companion_client.println("PING");
    }
    
    // Read incoming messages
    while (companion_client.available()) {
      String line = companion_client.readStringUntil('\n');
      if (line.length() > 0) {
        handleSatelliteMessage(line);
      }
    }
  }

  delay(10);
}

void handleCompanionConnection(void) {
  unsigned long current_time = millis();
  
  // Check WiFi status
  int wifi_status = WiFi.status();
  wifi_connected = (wifi_status == WL_CONNECTED);
  
  // Connect to Companion if WiFi is up and we're not already connected
  if (wifi_connected && !companion_connected && companion_host.length() > 0) {
    if (current_time - last_connection_attempt >= CONNECTION_RETRY_INTERVAL) {
      last_connection_attempt = current_time;
      
      USBSerial.print("[SAT] Connecting to ");
      USBSerial.print(companion_host);
      USBSerial.print(":");
      USBSerial.println(companion_port);
      
      // Connect to Companion's Satellite API (TCP port 16622)
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
        
        updateDisplay();
      } else {
        USBSerial.println("[SAT] Failed to connect. Check host and port.");
      }
    }
  } else if (!wifi_connected && companion_connected) {
    companion_client.stop();
    companion_connected = false;
    USBSerial.println("[SAT] WiFi lost, disconnecting");
    updateDisplay();
  }
}

void updateDisplay(void) {
  // Fill entire screen with black for AMOLED power saving
  gfx->fillScreen(COLOR_BLACK);
  
  if (!wifi_connected) {
    // Show WiFi connection needed
    gfx->setTextSize(2);
    gfx->setTextColor(RGB565_YELLOW);
    gfx->setCursor(50, 100);
    gfx->println("WiFi Needed");
    
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setCursor(30, 200);
    gfx->println("SSID:");
    gfx->setCursor(30, 215);
    gfx->println(wifi_ssid.c_str());
    
    gfx->setTextColor(RGB565_YELLOW);
    gfx->setCursor(30, 240);
    gfx->println("Status: Searching...");
  } 
  else if (!companion_connected) {
    // WiFi connected but no Companion
    gfx->setTextSize(2);
    gfx->setTextColor(RGB565_YELLOW);
    gfx->setCursor(30, 100);
    gfx->println("WiFi OK");
    
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setCursor(30, 200);
    gfx->print("SSID: ");
    gfx->println(WiFi.SSID().c_str());
    
    gfx->setCursor(30, 220);
    gfx->print("IP: ");
    gfx->println(WiFi.localIP().toString().c_str());
    
    gfx->setTextColor(RGB565_YELLOW);
    gfx->setCursor(30, 250);
    gfx->println("Waiting for Companion...");
    if (companion_host.length() == 0) {
      gfx->setCursor(30, 270);
      gfx->setTextColor(RGB565_RED);
      gfx->println("Set Companion host!");
    }
  } 
  else {
    // Connected to Companion - show tally circle with actual color from Companion
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_GREEN);
    gfx->setCursor(10, 10);
    gfx->println("COMPANION");
    
    // Show button text if available - debug: always show if received from Companion
    if (button_text.length() > 0) {
      gfx->setTextSize(1);
      gfx->setTextColor(RGB565_WHITE);
      gfx->setCursor(10, 30);
      gfx->print("BTN: ");
      gfx->println(button_text.c_str());
    }
    
    // Convert tally_color (24-bit RGB) to RGB565 and display
    uint8_t r = (tally_color >> 16) & 0xFF;
    uint8_t g = (tally_color >> 8) & 0xFF;
    uint8_t b = tally_color & 0xFF;
    
    uint16_t circle_color = RGB565(r, g, b);
    gfx->fillCircle(CENTER_X, CENTER_Y, CIRCLE_RADIUS, circle_color);
    
    // Draw button text in center of circle if available
    if (button_text.length() > 0) {
      uint8_t tr = (text_color >> 16) & 0xFF;
      uint8_t tg = (text_color >> 8) & 0xFF;
      uint8_t tb = text_color & 0xFF;
      uint16_t text_rgb565 = RGB565(tr, tg, tb);
      
      gfx->setTextSize(3);
      gfx->setTextColor(text_rgb565);
      
      // Calculate text position to center it
      int text_x = CENTER_X - (button_text.length() * 9);  // Approximate center for size 3
      int text_y = CENTER_Y - 15;
      
      gfx->setCursor(text_x, text_y);
      gfx->println(button_text.c_str());
    }
    
    // Show status at bottom based on dominant color
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setCursor(30, 420);
    
    // Determine label based on color
    if (r > 150 && r > g && r > b) {
      gfx->println("PROGRAM");
    } else if (g > 150 && g > r && g > b) {
      gfx->println("READY");
    } else if (r == 0 && g == 0 && b == 0) {
      gfx->println("IDLE");
    } else {
      gfx->println("TALLY");
    }
  }
}

void printConfigMenu(void) {
  USBSerial.println("\n=== Tally Light Configuration ===");
  USBSerial.println("WiFi Commands:");
  USBSerial.println("  ssid <network>          - Set WiFi SSID");
  USBSerial.println("  pass <password>         - Set WiFi password");
  USBSerial.println("  connect                 - Reconnect to WiFi");
  USBSerial.println("  scan                    - Scan for WiFi networks");
  USBSerial.println("\nCompanion Commands:");
  USBSerial.println("  host <hostname/ip>      - Set Companion host (e.g., companion.local)");
  USBSerial.println("  deviceid <name>         - Set device ID (default: esp32-tally)");
  USBSerial.println("\nOther Commands:");
  USBSerial.println("  status                  - Show current configuration");
  USBSerial.println("  save                    - Save configuration to EEPROM");
  USBSerial.println("  help                    - Show this menu");
  USBSerial.println("=====================================\n");
}

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
      last_connection_attempt = 0;  // Force reconnection attempt
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
      last_connection_attempt = 0;  // Force reconnection attempt
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
      
      int wifi_status = WiFi.status();
      USBSerial.print("WiFi Status: ");
      switch(wifi_status) {
        case WL_IDLE_STATUS: USBSerial.println("Idle"); break;
        case WL_NO_SSID_AVAIL: USBSerial.println("⚠️ SSID not found"); break;
        case WL_SCAN_COMPLETED: USBSerial.println("Scan completed"); break;
        case WL_CONNECTED: USBSerial.println("✓ Connected"); break;
        case WL_CONNECT_FAILED: USBSerial.println("⚠️ Connection failed"); break;
        case WL_CONNECTION_LOST: USBSerial.println("⚠️ Connection lost"); break;
        case WL_DISCONNECTED: USBSerial.println("Disconnected"); break;
        default: USBSerial.print("Unknown ("); USBSerial.print(wifi_status); USBSerial.println(")");
      }
      
      if (wifi_connected) {
        USBSerial.print("IP Address: ");
        USBSerial.println(WiFi.localIP().toString());
        USBSerial.print("Signal Strength: ");
        USBSerial.print(WiFi.RSSI());
        USBSerial.println(" dBm");
      }
      
      USBSerial.print("Companion Host: ");
      USBSerial.println(companion_host);
      USBSerial.print("Companion Port: ");
      USBSerial.println(companion_port);
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
    else if (input == "help") {
      printConfigMenu();
    }
    else if (input == "connect") {
      if (wifi_ssid.length() == 0) {
        USBSerial.println("WiFi SSID not set. Use: ssid <network>");
        return;
      }
      
      // Properly disconnect first
      USBSerial.println("Disconnecting from any previous connection...");
      WiFi.disconnect(true);  // true = turn off WiFi radio
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
        last_connection_attempt = 0;  // Force Companion connection attempt
      } else {
        USBSerial.print("✗ WiFi connection failed. Status: ");
        USBSerial.println(WiFi.status());
        USBSerial.println("Try: scan (to verify SSID), ssid <name>, pass <password>, save, then connect");
      }
    }
    else if (input == "reconnect") {
      if (companion_connected) {
        companion_client.stop();
      }
      companion_connected = false;
      last_connection_attempt = 0;  // Force immediate reconnection attempt
      USBSerial.println("Reconnecting to Companion...");
    }
    else if (input.length() > 0) {
      USBSerial.println("Unknown command. Type 'help' for options.");
    }
  }
}
