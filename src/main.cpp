/***************************************************************
 * WiFi Relay Module (WRM) Firmware.
 * Hardware: NodeMCU 32S, Chip ESP32
 * Copyright (c) 2023 VNEMEX Ltd.
 *
 * How to pair to ControlBox.
 *    1. Press WPS button on Router to join into Router network
 *    2. Mobile App setup ControlBox's IP address
 *
 * Status LED (pin 15 - IO2)
 *    1. Init:                    OFF
 *    2. Join AP's network:       ON 1s, OFF 3s
 *    3. Pairing:                 ON 1s, OFF 1s
 *    4. Connect to ControlBox:   ON 3s, OFF 1s
 *    5. Normal:                  ON
 *
 * Reset button (pin 14 - IO0)
 *    Press and hold this button continuously for 5 seconds to clear SSID/password, ControlBox's IP address, ...
 * Relay control pin (pin 3 - IO22) or (pin 2 - IO 23)
 *
 * 1, 6, 7, 8, 9, 11: crash system
 * 24, 28, 29, 30, 31: invalid pin selected
 * 34, 35, 36, 37, 38, 39: GPIO can only be used as input mode
 *
 * WRM' status:
 *    1. Init
 *    2. Join AP'network (WPS)
 *    3. Connect to ControlBox
 *    4. Normal
 *       + Keep alive message.
 *       + Control relay.
 *
 * WRM's elements:
 *    1. Status LED, Reset button, Relay control pin.
 *    2. Station WiFi mode.
 *    3. WPS mode.
 *    4. mDNS daemon.
 *    5. REST server.
 *    6. MQTT client.
 *    7. JSON.
 *    8. Timers.
 *    9. EEPROM.
 ****************************************************************/
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ESPmDNS.h>

#include <EEPROM.h>
#include <pthread.h>

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp32-hal-log.h"
#include "esp_wps.h"
#include "esp_event.h"

#include "VMXExt.h"

#define WRMFWVER 2

#define RESET_BTN_PIN 0
#define STATUS_LED_PIN 2  // for nodemcu 32S - GCS not work
#define RELAY_CTRL_PIN 22 // or 23

/*
 * Header: VMXWRM - 6 bytes
 * SSID: 32 bytes
 * Password: 64 bytes
 * CTRLBOX IP address: 16 bytes
 */
#define EEPROM_HEADER_SIZE 6
#define EEPROM_SSID_SIZE 32
#define EEPROM_PASSWORD_SIZE 64
#define EEPROM_CTRLBOX_IP_SIZE 16
#define EEPROM_INFO_SIZE EEPROM_HEADER_SIZE + EEPROM_SSID_SIZE + EEPROM_PASSWORD_SIZE + EEPROM_CTRLBOX_IP_SIZE + 4
#define EEPROM_START_ADDR 0
#define EEPROM_OFFSET_HEADER EEPROM_START_ADDR
#define EEPROM_OFFSET_SSID EEPROM_OFFSET_HEADER + EEPROM_HEADER_SIZE + 1
#define EEPROM_OFFSET_PASSWORD EEPROM_OFFSET_SSID + EEPROM_SSID_SIZE + 1
#define EEPROM_OFFSET_CTRLBOX_IP EEPROM_OFFSET_PASSWORD + EEPROM_PASSWORD_SIZE + 1

#define WPS_MODE WPS_TYPE_PBC
#define MAX_RETRY_ATTEMPTS 2
#ifndef PIN2STR
#define PIN2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5], (a)[6], (a)[7]
#define PINSTR "%c%c%c%c%c%c%c%c"
#endif

#define MQTT_MAX_RECONNECT_TRIES 5000
#define WiFi_retries 250

#define REST_SERVER_PORT 80
#define MQTT_BROKER_PORT 1883

static esp_wps_config_t config = WPS_CONFIG_INIT_DEFAULT(WPS_MODE);
static wifi_config_t wps_ap_creds[MAX_WPS_AP_CRED];
static int s_ap_creds_num = 0;
static int s_retry_num = 0;

enum WRMSTATUS
{
  WRMSTATUS_INIT = 0,
  WRMSTATUS_JOIN_AP,
  WRMSTATUS_PAIRING,
  WRMSTATUS_CONNECT_CTRLBOX,
  WRMSTATUS_NORMAL,
  WRMSTATUS_MAX
};

enum RELAYSTATUS
{
  RELAYSTATUS_OFF = 0,
  RELAYSTATUS_ON,
  RELAYSTATUS_MAX
};

long lastDebounceTime_statusLED = 0;
long debounceDelay_statusLED = 1000; // for 1 second

long lastDebounceTime_resetBtn = 0;
long debounceDelay_resetBtn = 5000; // for 5 seconds
bool resetBtnReleased;

long lastDebounceTime_keepAlive = 0;
long debounceDelay_keepAlive = 10000; // for 10 seconds
long keepAliveTime = 0;

long lastDebounceTime_Relay = 0;
long debounceDelay_Relay = 10000; // for 10 seconds
bool checkAutoRelay = false;

char chip_id[40] = {};
uint64_t chipid;
int offset = 0;

char eeprom_info[EEPROM_INFO_SIZE] = {};
char eeprom_header[EEPROM_HEADER_SIZE] = {};
char eeprom_ssid[EEPROM_SSID_SIZE] = {};
char eeprom_password[EEPROM_PASSWORD_SIZE] = {};
char eeprom_ctrlbox_ipaddr[EEPROM_CTRLBOX_IP_SIZE] = {};

int WRMStatus;
int RelayStatus;
bool mDNSDaemonExist = false;
char jsonMessage[400] = {};

AsyncWebServer server(REST_SERVER_PORT);
WiFiClient client;
PubSubClient mqtt_client(client);

char relay2CtrlBoxTopic[] = "VMXSys/Device2CtrlBox/relay";
char CtrlBox2relayTopic[] = "VMXSys/CtrlBox2Device/relay";
char mqtt_info[200] = {};
char reqSender[32] = {};

unsigned long previousMillis = 0;
const long interval = 30000; // 3 seconds

static void processFormatWRMEEPROM()
{
  memset(eeprom_info, 0, sizeof(eeprom_info));
  memset(eeprom_ssid, 0, sizeof(eeprom_ssid));
  memset(eeprom_password, 0, sizeof(eeprom_password));
  memset(eeprom_ctrlbox_ipaddr, 0, sizeof(eeprom_ctrlbox_ipaddr));

  sprintf(eeprom_info, "VMXWRM");
  EEPROM.writeString(EEPROM_START_ADDR, eeprom_info);
  EEPROM.writeString(EEPROM_OFFSET_SSID, eeprom_ssid);
  EEPROM.writeString(EEPROM_OFFSET_PASSWORD, eeprom_password);
  EEPROM.writeString(EEPROM_OFFSET_CTRLBOX_IP, eeprom_ctrlbox_ipaddr);
  EEPROM.commit();
  delay(100);
  Serial.println("Format VMXWRM format done!");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
  static int ap_idx = 1;

  switch (event_id)
  {
  case WIFI_EVENT_STA_START:
    ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
    break;
  case WIFI_EVENT_STA_DISCONNECTED:
    ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED");
    if (s_retry_num < MAX_RETRY_ATTEMPTS)
    {
      esp_wifi_connect();
      s_retry_num++;
    }
    else if (ap_idx < s_ap_creds_num)
    {
      /* Try the next AP credential if first one fails */

      if (ap_idx < s_ap_creds_num)
      {
        ESP_LOGI(TAG, "Connecting to SSID: %s, Passphrase: %s",
                 wps_ap_creds[ap_idx].sta.ssid, wps_ap_creds[ap_idx].sta.password);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wps_ap_creds[ap_idx++]));
        esp_wifi_connect();
      }
      s_retry_num = 0;
    }
    else
    {
      ESP_LOGI(TAG, "Failed to connect!");
    }

    break;
  case WIFI_EVENT_STA_WPS_ER_SUCCESS:
    ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_SUCCESS");
    {
      wifi_event_sta_wps_er_success_t *evt =
          (wifi_event_sta_wps_er_success_t *)event_data;
      int i;

      if (evt)
      {
        s_ap_creds_num = evt->ap_cred_cnt;
        for (i = 0; i < s_ap_creds_num; i++)
        {
          memcpy(wps_ap_creds[i].sta.ssid, evt->ap_cred[i].ssid,
                 sizeof(evt->ap_cred[i].ssid));
          memcpy(wps_ap_creds[i].sta.password, evt->ap_cred[i].passphrase,
                 sizeof(evt->ap_cred[i].passphrase));
        }
        /* If multiple AP credentials are received from WPS, connect with first one */
        ESP_LOGI(TAG, "Connecting to SSID: %s, Passphrase: %s",
                 wps_ap_creds[0].sta.ssid, wps_ap_creds[0].sta.password);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wps_ap_creds[0]));
        memcpy(eeprom_ssid, wps_ap_creds[0].sta.ssid, sizeof(wps_ap_creds[0].sta.ssid));
        memcpy(eeprom_password, wps_ap_creds[0].sta.password, sizeof(wps_ap_creds[0].sta.password));
        EEPROM.writeString(EEPROM_OFFSET_SSID, eeprom_ssid);
        EEPROM.writeString(EEPROM_OFFSET_PASSWORD, eeprom_password);
        EEPROM.commit();
        delay(100);
      }
      /*
       * If only one AP credential is received from WPS, there will be no event data and
       * esp_wifi_set_config() is already called by WPS modules for backward compatibility
       * with legacy apps. So directly attempt connection here.
       */
      ESP_ERROR_CHECK(esp_wifi_wps_disable());
      esp_wifi_connect();
    }
    break;
  case WIFI_EVENT_STA_WPS_ER_FAILED:
    ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_FAILED");
    ESP_ERROR_CHECK(esp_wifi_wps_disable());
    ESP_ERROR_CHECK(esp_wifi_wps_enable(&config));
    ESP_ERROR_CHECK(esp_wifi_wps_start(0));
    break;
  case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
    ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_TIMEOUT");
    ESP_ERROR_CHECK(esp_wifi_wps_disable());
    ESP_ERROR_CHECK(esp_wifi_wps_enable(&config));
    ESP_ERROR_CHECK(esp_wifi_wps_start(0));
    break;
  case WIFI_EVENT_STA_WPS_ER_PIN:
    ESP_LOGI(TAG, "WIFI_EVENT_STA_WPS_ER_PIN");
    /* display the PIN code */
    wifi_event_sta_wps_er_pin_t *event = (wifi_event_sta_wps_er_pin_t *)event_data;
    ESP_LOGI(TAG, "WPS_PIN = " PINSTR, PIN2STR(event->pin_code));
    break;
  }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
  ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
  ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
  // ESP32 need to restart to init STA mode again.
  ESP.restart();
}

static void connectToWiFi()
{
  if ((!strlen(eeprom_ssid)) || (!strlen(eeprom_password)))
  {
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  WiFi.begin(eeprom_ssid, eeprom_password);
}

bool tryToConnectWifi()
{
  if ((!strlen(eeprom_ssid)) || (!strlen(eeprom_password)))
  {
    return false;
  }

  mWifiMode = STAT_MODE;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(eeprom_ssid, eeprom_password);
  if (WiFi.waitForConnectResult() == WL_CONNECTED)
  {
    Serial.println("Connected to SSID " + String(eeprom_ssid) + +" successfully!");
    Serial.print("Local IP address: ");
    Serial.println(WiFi.localIP());
    mWifiConnected = true;
  }
  else
  {
    Serial.println("Failed to connect to SSID: " + String(eeprom_ssid) + ", Passphrase: " + String(eeprom_password));
    mWifiConnected = false;
  }
  return mWifiConnected;
}

static void rebootEspWithReason(String reason)
{
  Serial.println(reason);
  delay(1000);
  ESP.restart();
}

void performUpdate(Stream &updateSource, size_t updateSize)
{
  String result = "";
  if (Update.begin(updateSize))
  {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize)
    {
      Serial.println("Written : " + String(written) + " successfully");
    }
    else
    {
      Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");
    }
    result += "Written : " + String(written) + "/" + String(updateSize) + " [" + String((written / updateSize) * 100) + "%] \n";
    if (Update.end())
    {
      Serial.println("OTA done!");
      result += "OTA Done: ";
      if (Update.isFinished())
      {
        Serial.println("Update successfully completed. Rebooting...");
        result += "Success!\n";
      }
      else
      {
        Serial.println("Update not finished? Something went wrong!");
        result += "Failed!\n";
      }
    }
    else
    {
      Serial.println("Error Occurred. Error #: " + String(Update.getError()));
      result += "Error #: " + String(Update.getError());
    }
  }
  else
  {
    Serial.println("Not enough space to begin OTA");
    result += "Not enough space for OTA";
  }
  // http send 'result'
}

bool checkFirmware()
{
  HTTPClient http;
  bool status = false;
  http.begin(client, "http://s3.ap-southeast-2.amazonaws.com/my.aws.aipod/update.json");
  int httpCode = http.GET();
  Serial.println("httpCode: " + String(httpCode));
  String payload = http.getString();
  if (httpCode == 200)
  {
    StaticJsonDocument<200> jsonBuffer;
    DeserializationError error = deserializeJson(jsonBuffer, payload);
    if (error)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
    }
    else
    {
      int latestVersion = jsonBuffer["version"].as<int>();
      Serial.println("latestVersion: " + String(latestVersion));
      if (latestVersion > FIRMWARE_VERSION)
      {
        Serial.println("New firmware version available. Current version: " + String(FIRMWARE_VERSION) + ", Latest version: " + String(latestVersion));
        status = true;
      }
      else
      {
        Serial.println("Current firmware version is the latest version.");
      }
    }
  }
  else
  {
    Serial.println("Cannot connect to update server!");
  }
  http.end();
  return status;
}

void updateFromFS(fs::FS &fs)
{
  File updateBin = fs.open("/firmware.bin");
  if (updateBin)
  {
    if (updateBin.isDirectory())
    {
      Serial.println("Error, firmware.bin is not a file");
      updateBin.close();
      return;
    }

    size_t updateSize = updateBin.size();

    if (updateSize > 0)
    {
      Serial.println("Trying to start update");
      performUpdate(updateBin, updateSize);
    }
    else
    {
      Serial.println("Error, file is empty");
    }

    updateBin.close();

    // when finished remove the binary from spiffs to indicate end of the process
    Serial.println("Removing update file");
    fs.remove("/firmware.bin");

    rebootEspWithReason("Rebooting to complete OTA update");
  }
  else
  {
    Serial.println("Could not load update.bin from spiffs root");
  }
}

bool downloadFirmware(String fwUrl)
{
  HTTPClient http;
  bool stat = false;
  File f = SPIFFS.open("/firmware.bin", "w");
  if (f)
  {
    http.begin(fwUrl);
    int httpCode = http.GET();
    if (httpCode > 0)
    {
      if (httpCode == HTTP_CODE_OK)
      {
        Serial.println("Downloading...");
        http.writeToStream(&f);
        stat = true;
      }
    }
    else
    {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    f.close();
  }
  else
  {
    Serial.println("failed to open /update.bin");
  }
  http.end();

  return stat;
}

// check for new firmware version and download if available
void do_firmware_upgrade()
{
  if (checkFirmware())
  {
    if (SPIFFS.exists("/firmware.bin"))
    {
      SPIFFS.remove("/firmware.bin");
      Serial.println("Removed existing update file");
    }
    Serial.println("Start firmware upgrade process");
    if (downloadFirmware(OTA_URL))
    {
      Serial.println("Firmware downloaded successfully");
      updateFromFS(SPIFFS);
    }
    else
    {
      Serial.println("Firmware download failed");
    }
  }
}

int myVprintf(const char *format, va_list args)
{
  static File logFile;
  if (!logFile || logFile.size() == 0)
  {
    logFile = SPIFFS.open("/log.txt", FILE_APPEND);
    if (!logFile)
    {
      logFile = SPIFFS.open("/log.txt", FILE_WRITE);
      if (!logFile)
      {
        Serial.println("Failed to open log file for writing");
        return 0;
      }
    }
  }
  // write to log file
  char buffer[256];
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  if (len > 0)
  {
    logFile.write((const uint8_t *)buffer, len);
    logFile.flush();
  }
  logFile.close();
  return len;
}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(100); // delay for Serial's initialization.

  chipid = ESP.getEfuseMac(); // The chip ID is essentially its MAC address(length: 6 bytes).
  offset += sprintf(chip_id + offset, "%04X", (uint16_t)(chipid >> 32));
  offset += sprintf(chip_id + offset, "%08X", (uint32_t)chipid);

  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);
  pinMode(RELAY_CTRL_PIN, OUTPUT);

  digitalWrite(STATUS_LED_PIN, LOW);
  digitalWrite(RELAY_CTRL_PIN, LOW);
  lastDebounceTime_statusLED = lastDebounceTime_resetBtn = millis();
  resetBtnReleased = true;

  WRMStatus = WRMSTATUS_INIT;
  RelayStatus = RELAYSTATUS_OFF;

  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
  }
  Serial.println("SPIFFS mounted successfully");
  if (!SPIFFS.exists("/log.txt"))
  {
    File logFile = SPIFFS.open("/log.txt", FILE_WRITE);
    if (!logFile)
    {
      Serial.println("Failed to create log file");
    }
    else
    {
      Serial.println("Log file created");
      logFile.close();
    }
  }
  // set up logging to file
  Serial.println("Setting up logging to file");
  esp_log_level_set("*", ESP_LOG_WARN);
  esp_log_level_set(TAG, ESP_LOG_INFO);
  // esp_log_set_vprintf(myVprintf);

  ESP_LOGI(TAG, "ESP32 Chip ID: %s", chip_id);
  ESP_LOGI(TAG, "WiFi Relay Module Firmware Version: %d", WRMFWVER);

  if (!EEPROM.begin(EEPROM_INFO_SIZE))
  {
    Serial.println("Failed to initialise EEPROM");
    Serial.println("\nRestarting in 5 seconds");
    delay(5000);
    ESP.restart();
  }

  // Read EEPROM for header.
  EEPROM.readString(EEPROM_OFFSET_HEADER, eeprom_header, EEPROM_HEADER_SIZE);
  if (!strcmp(eeprom_header, "VMXWRM"))
  {
    Serial.println("WRM firmware EEPROM format is correct");
  }
  else
  {
    Serial.println("WRM firmware EEPROM format is invalid. Need to format firstly.");
    processFormatWRMEEPROM();
  }

  // Read EEPROM for SSID and password.
  EEPROM.readString(EEPROM_OFFSET_SSID, eeprom_ssid, EEPROM_SSID_SIZE);
  EEPROM.readString(EEPROM_OFFSET_PASSWORD, eeprom_password, EEPROM_PASSWORD_SIZE);

  // Check SSID and password
  WRMStatus = WRMSTATUS_JOIN_AP;
  if (!strlen(eeprom_ssid) || !strlen(eeprom_password))
  {
    Serial.println("There is no wifi configuration in EEPROM memory - ESP32 wifi network created!");

    mWifiMode = AP_MODE;
    WiFi.mode(WIFI_AP);
    uint8_t macAddr[6];
    WiFi.softAPmacAddress(macAddr);
    String ssid_ap = "ESP32-" + String(macAddr[4], HEX) + String(macAddr[5], HEX);
    ssid_ap.toUpperCase();
    WiFi.softAP(ssid_ap.c_str());

    Serial.println("Access point name:" + ssid_ap);
    Serial.println("Web server access address:" + WiFi.softAPIP().toString());
  }
  else
  {
    Serial.println("Connecting to wifi...!");
    if (tryToConnectWifi() == true)
    {

      do_firmware_upgrade();

      mLastConnTime = millis();
      if (!mDNSDaemonExist)
      {
        char mdns_name[48] = {};
        sprintf(mdns_name, "VMXWRM_%s", chip_id);
        Serial.println("mdns_name: " + String(mdns_name));
        if (!MDNS.begin(mdns_name))
        {
          Serial.println("Error setting up MDNS responder!");
          while (1)
          {
            delay(1000);
          }
        }
        // Add service to MDNS-SD
        MDNS.addService("_vnx_relay", "tcp", REST_SERVER_PORT);
        mDNSDaemonExist = true;
        Serial.println("mDNS responder started");
      }
    }
    else
    {
      Serial.println("Failed to connect to SSID: " + String(eeprom_ssid) + ", Passphrase: " + String(eeprom_password));
      Serial.println("ESP32 wifi network created!");

      mWifiMode = AP_MODE;
      WiFi.mode(WIFI_AP);
      uint8_t macAddr[6];
      WiFi.softAPmacAddress(macAddr);
      String ssid_ap = "ESP32-" + String(macAddr[4], HEX) + String(macAddr[5], HEX);
      ssid_ap.toUpperCase();
      WiFi.softAP(ssid_ap.c_str());

      Serial.println("Access point name:" + ssid_ap);
      Serial.println("Web server access address:" + WiFi.softAPIP().toString());
    }
  }
  runHttpServer();
}

void processStatusLEDwithTimer(int high, int low)
{ // units in seconds
  static int flipflop = 0;

  if (flipflop)
  {
    // handle LED high level
    if ((millis() - lastDebounceTime_statusLED) > debounceDelay_statusLED * high)
    {
      lastDebounceTime_statusLED = millis();
      flipflop = 0;
      digitalWrite(STATUS_LED_PIN, LOW);
    }
  }
  else
  {
    // handle LED low level
    if ((millis() - lastDebounceTime_statusLED) > debounceDelay_statusLED * low)
    {
      lastDebounceTime_statusLED = millis();
      flipflop = 1;
      digitalWrite(STATUS_LED_PIN, HIGH);
    }
  }
}

void processStatusLED()
{
  switch (WRMStatus)
  {
  case WRMSTATUS_INIT:
    // Always low
    digitalWrite(STATUS_LED_PIN, LOW);
    break;
  case WRMSTATUS_JOIN_AP:
    processStatusLEDwithTimer(1, 3); // high 1 second, low 3 seconds
    break;
  case WRMSTATUS_PAIRING:
    processStatusLEDwithTimer(1, 1); // high 1 second, low 1 second
    break;
  case WRMSTATUS_CONNECT_CTRLBOX:
    processStatusLEDwithTimer(3, 1); // high 3 seconds, low 1 second
    break;
  case WRMSTATUS_NORMAL:
    // Always high
    digitalWrite(STATUS_LED_PIN, HIGH);
    break;
  default:
    break;
  }
}

void processResetBtn()
{
  if (!digitalRead(RESET_BTN_PIN))
  {
    if (((millis() - lastDebounceTime_resetBtn) > debounceDelay_resetBtn) && resetBtnReleased)
    {
      Serial.println("Processing reset ......");
      lastDebounceTime_resetBtn = millis();
      resetBtnReleased = false;

      processFormatWRMEEPROM();
      Serial.println("\nRestarting in 1 seconds");
      delay(1000);
      ESP.restart();
    }
  }
  else
  {
    lastDebounceTime_resetBtn = millis();
    resetBtnReleased = true;
  }
}

bool connectToMQTTBroker();

void handleSetupPost(AsyncWebServerRequest *req)
{
  if (!req->hasArg("plain"))
  {
    // handle error here
    Serial.println("plain is not existing");
    return;
  }

  String body = req->arg("plain");
  Serial.println(body);
  StaticJsonDocument<200> jsonBuffer, jsonBufferRes;
  DeserializationError error = deserializeJson(jsonBuffer, body.c_str());
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  const char *ctrlBoxIP = jsonBuffer["ctrlBoxIP"];
  const char *sender = jsonBuffer["sender"];
  Serial.print("CtrlBox IP: ");
  Serial.println(ctrlBoxIP);

  memcpy(eeprom_ctrlbox_ipaddr, ctrlBoxIP, sizeof(eeprom_ctrlbox_ipaddr));
  memcpy(reqSender, sender, sizeof(reqSender));

  Serial.println(eeprom_ctrlbox_ipaddr);
  EEPROM.writeString(EEPROM_OFFSET_CTRLBOX_IP, eeprom_ctrlbox_ipaddr);
  EEPROM.commit();
  delay(100);

  // Respond to the client
  jsonBufferRes["setup"] = "Commpleted";
  jsonBufferRes["sender"] = jsonBuffer["sender"];
  serializeJson(jsonBufferRes, jsonMessage);
  serializeJson(jsonBufferRes, Serial);
  req->send(200, "application/json", jsonMessage);

  if (!mqtt_client.connected())
  {
    connectToMQTTBroker();
  }
  else
  {
    mqtt_client.disconnect();
    delay(100);
    connectToMQTTBroker();
  }
}

void mqttBrokerCallback(char *topic, byte *payload, unsigned int length)
{
  int result = -1;

  Serial.print("Received. topic = ");
  Serial.println(topic);

  memset(mqtt_info, 0, sizeof(mqtt_info));
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
    mqtt_info[i] = (char)payload[i];
  }

  StaticJsonDocument<200> jsonBuffer, jsonBufferRes;
  DeserializationError error = deserializeJson(jsonBuffer, mqtt_info);
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  const char *action = jsonBuffer["action"];
  const char *deviceId = jsonBuffer["deviceId"];
  const char *command = jsonBuffer["command"];

  const char *sender = jsonBuffer["sender"];
  memcpy(reqSender, sender, sizeof(reqSender));

  if (strcmp(topic, CtrlBox2relayTopic) == 0)
  {
    Serial.print("action: ");
    Serial.println(action);
    Serial.print("deviceId: ");
    Serial.println(deviceId);

    if (!deviceId || (strcmp(deviceId, chip_id) != 0))
    {
      Serial.println("Device ID invalided");
      result = -2;
      goto mqttBrokerCallback_handle_error;
    }

    if (action && (strcmp(action, "control") == 0))
    {
      if (command && ((strcmp(command, "update") == 0) || (strcmp(command, "updateByAccessControl") == 0)))
      {
        int state = jsonBuffer["state"];

        if (state == 1)
        {
          digitalWrite(RELAY_CTRL_PIN, HIGH);
          RelayStatus = RELAYSTATUS_ON;
          result = 0;
        }
        else
        {
          digitalWrite(RELAY_CTRL_PIN, LOW);
          RelayStatus = RELAYSTATUS_OFF;
          result = 0;
        }
        jsonBufferRes["state"] = RelayStatus;
        checkAutoRelay = false;

        if (strcmp(command, "updateByAccessControl") == 0)
        {
          lastDebounceTime_Relay = millis();
          checkAutoRelay = true;
        }
      }

      if (command && (strcmp(command, "remove") == 0))
      {
        // Respond to the client
        memset(jsonMessage, 0, sizeof(jsonMessage));
        jsonBufferRes.clear();
        jsonBufferRes["action"] = "control";
        jsonBufferRes["command"] = "remove";
        jsonBufferRes["deviceId"] = chip_id;
        jsonBufferRes["result"] = "completed";
        jsonBufferRes["sender"] = sender;
        serializeJson(jsonBufferRes, jsonMessage);
        serializeJson(jsonBufferRes, Serial);

        mqtt_client.publish(relay2CtrlBoxTopic, jsonMessage);
        delay(100);
        processFormatWRMEEPROM();
        result = 0;

        Serial.println("\nRestarting in 1 seconds");
        delay(1000);
        ESP.restart();
      }
    }
  }
  else
  {
    result = -3;
    // do nothing
    return;
  }

mqttBrokerCallback_handle_error:
  jsonBufferRes["action"] = action;
  jsonBufferRes["command"] = command;
  jsonBufferRes["deviceId"] = deviceId;
  jsonBufferRes["sender"] = sender;
  // Respond to the client
  memset(jsonMessage, 0, sizeof(jsonMessage));
  switch (result)
  {
  case 0:
    jsonBufferRes["result"] = "success";
    break;
  case -1:
    jsonBufferRes["result"] = "failure";
    break;
  case -2:
    jsonBufferRes["result"] = "device id invalid";
    break;
  case -3:
    jsonBufferRes["result"] = "parameter invalid";
    break;
  default:
    jsonBufferRes["result"] = "failure";
    break;
  }

  serializeJson(jsonBufferRes, jsonMessage);
  serializeJson(jsonBufferRes, Serial);
  mqtt_client.publish(relay2CtrlBoxTopic, jsonMessage);
}

bool connectToMQTTBroker()
{
  int qos = 0; // Make sure the qos is zero in the MQTT broker of AWS
  static char mqtt_id[48] = {};
  static bool mqttFirstConnTime = true;
  int retries = 0;

  if (!strlen(eeprom_ctrlbox_ipaddr))
    return false;

  if (mqttFirstConnTime)
  {
    mqtt_client.setServer(eeprom_ctrlbox_ipaddr, MQTT_BROKER_PORT);
    mqtt_client.setCallback(mqttBrokerCallback);
    mqtt_client.setKeepAlive(90); // seconds
    sprintf(mqtt_id, "VMXWRM%s", chip_id);
    mqttFirstConnTime = false;
  }

  // Try to connect to the MQTT broker
  Serial.print("\nConnecting to MQTT broker: " + String(mqtt_id));
  while (!mqtt_client.connect(mqtt_id) && retries < MQTT_MAX_RECONNECT_TRIES)
  {
    // If we fail to connect to the MQTT broker, we will try again later.
    Serial.print(" ");
    Serial.print(retries);
    delay(500);
    retries++;
    processStatusLED();
    processResetBtn();
  }

  // Make sure that we did indeed successfully connect to the MQTT broker
  // If not we just end the function and wait for the next loop.
  if (!mqtt_client.connected())
  {
    Serial.println("\nTimeout! Unable to connect to MQTT broker");
    return false;
  }
  else
  {
    // If we land here, we have successfully connected to AWS!
    // And we can subscribe to topics and send messages.
    mqtt_client.subscribe(CtrlBox2relayTopic, qos);
    // mqtt_client.subscribe(relay2CtrlBoxTopic, qos);

    Serial.println("\nConnected to Broker and subscribed to Topic");
    StaticJsonDocument<200> jsonBufferRes;

    // Respond to the client
    memset(jsonMessage, 0, sizeof(jsonMessage));
    jsonBufferRes.clear();
    jsonBufferRes["action"] = "control";
    jsonBufferRes["command"] = "connect";
    jsonBufferRes["deviceId"] = chip_id;
    jsonBufferRes["state"] = RelayStatus;
    jsonBufferRes["sender"] = reqSender;
    serializeJson(jsonBufferRes, jsonMessage);
    serializeJson(jsonBufferRes, Serial);

    mqtt_client.publish(relay2CtrlBoxTopic, jsonMessage);
    WRMStatus = WRMSTATUS_NORMAL;
  }
  return true;
}

String bcrypt(String payload)
{
  uint8_t sha1Result[20];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char *)payload.c_str(), payload.length());
  mbedtls_md_finish(&ctx, sha1Result);
  mbedtls_md_free(&ctx);

  char hashString[41];
  for (int i = 0; i < 20; i++)
  {
    sprintf(&hashString[i * 2], "%02x", sha1Result[i]);
  }
  hashString[40] = '\0'; // Null-terminate the string

  return String(hashString);
}

bool requireAuthentication(AsyncWebServerRequest *req)
{
  if (req->hasHeader("Cookie"))
  {
    String cookie = req->header("Cookie");
    String token = bcrypt(String(username) + String(password) + chip_id);
    if (cookie.indexOf("ClientID=" + token) != -1)
    {
      return true; // Authenticated
    }
  }
  return false; // Not authenticated
}

// Function to get the error message from the Update process
void getUpdateErrorMsg()
{
  StreamString error;
  Update.printError(error);
  mUpdateErrorMsg = error.c_str();
  mUpdateResult = UPDATE_ERROR;
}

// Function to run the HTTP server
void runHttpServer()
{
  // HTML content to be served
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req)
            {
    AsyncWebServerResponse *res = req->beginResponse(200, "text/html", _acsetup_min_html, sizeof(_acsetup_min_html));
    res->addHeader(F("Content-Encoding"), "gzip");
    req->send(res); });

  server.on("/api/v1/login", HTTP_POST, [](AsyncWebServerRequest *req)
            {
    if (!req->hasArg("username") || !req->hasArg("password")) {
      req->send(400,"text/plain","Bad Request");
      return;
    }

    if(req->arg("username") == String(username) && req->arg("password") == String(password)) {
      String token = bcrypt(String(username)+String(password)+chip_id);
      AsyncWebServerResponse *res = req->beginResponse(200, "application/json", "{\"access_token\":\""+token+"\"}");
      res->addHeader("Set-Cookie","ClientID=" + token + "; Path=/; Max-Age=3600");
      req->send(res);
    } else{
      req->send(401,"text/plain","Login failed!");
    } });

  server.on("/api/v1/logout", HTTP_GET, [](AsyncWebServerRequest *req)
            {
   AsyncWebServerResponse *res = req->beginResponse(200, "application/json", "{\"message\":\"Logout successful\"}");
   res->addHeader("Set-Cookie","ClientID=0; Max-Age=0");
   req->send(res); });
  server.on("/api/v1/scan", HTTP_GET, [](AsyncWebServerRequest *req)
            {
    if (!requireAuthentication(req)) {
      req->send(401,"text/plain","Access denied");
      return;
    }
    /** https://github.com/ESP32Async/ESPAsyncWebServer/wiki#scanning-for-available-wifi-networks */
    ESP_LOGI(TAG, "Scanning for WiFi networks...");
    int res = WiFi.scanComplete();
    if (res == -2){
        WiFi.scanNetworks(true);
        // The very first request will be empty, reload /scan endpoint
        req->send(200, "application/json", "{\"reload\" : 1}");
    } else if (res) {
      ESP_LOGI(TAG, "Number of networks: %d", res);
      DynamicJsonDocument doc(512);
      for(int i = 0; i < res; ++i) {
        Serial.println(WiFi.SSID(i));
        doc.add(WiFi.SSID(i));
      }
      String json;
      serializeJson(doc, json);
      req->send(200, "application/json", json);
      WiFi.scanDelete(); // clean up RAM
      if(WiFi.scanComplete() == -2){
        WiFi.scanNetworks(true);
      }
    } });
  server.on("/api/v1/connect", HTTP_POST, [](AsyncWebServerRequest *req)
            {
    if (!requireAuthentication(req)) {
      req->send(401,"text/plain","Access denied");
      return;
    }
    if (!req->hasArg("ssid") || !req->hasArg("password")) {
      req->send(400,"text/plain","Bad Request");
      return;
    }
    char resp[256];
    String ssid_temp = req->arg("ssid");
    String password_temp = req->arg("password");
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(resp, sizeof(resp),
            "ESP is currently connected to a WiFi network.<br><br>"
            "Actual connection will be closed and a new attempt will be done with <b>"
            "%s</b> WiFi network.", ssid_temp.c_str());
        req->send(200,"text/html",resp);
        delay(1000);
        WiFi.disconnect();
    }
    if (ssid_temp.length() != 0) {
      WiFi.begin(ssid_temp.c_str(), password_temp.c_str());
      if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        req->send(500,"text/plain","Failed to connect to WiFi");
        return;
      }
      mWifiConnected = true;
      IPAddress ip = WiFi.localIP();
      snprintf(resp, sizeof(resp),
          "Restart ESP and then reload this page from "
          "<a href='%d:%d.%d.%d:%d'>the new LAN address</a>",
          ip[0], ip[1], ip[2], ip[3], REST_SERVER_PORT);
      // Save the new credentials to EEPROM
      EEPROM.writeString(EEPROM_OFFSET_SSID,ssid_temp);
      EEPROM.writeString(EEPROM_OFFSET_PASSWORD,password_temp);
      EEPROM.commit();
      req->send(200, "text/plain", resp);
      // automatically restart ESP after 10 seconds
      restartTimer.once_ms(10000, []() {
        ESP.restart();
      });
    } else {
      req->send(400,"text/plain","SSID cannot be empty");
    } });
  server.on("/api/v1/status", HTTP_GET, [](AsyncWebServerRequest *req)
            {
    if (!requireAuthentication(req)) {
      req->send(401,"text/plain","Access denied");
      return;
    }
    DynamicJsonDocument doc(512);
    doc["firmware_version"] = WRMFWVER;
    doc["chip_id"] = chip_id;
    doc["wifi_mode"] = mWifiMode == AP_MODE ? "Access Point" : ("Station-[" + WiFi.SSID() +"]");
    doc["wifi_connected"] = mWifiConnected;
    doc["ip_address"] = mWifiConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    doc["ctrlbox_ip"] = strlen(eeprom_ctrlbox_ipaddr) ? String(eeprom_ctrlbox_ipaddr) : "Not set";
    doc["mqtt_status"] = mqtt_client.connected() ? "Connected" : "Disconnected";
    doc["wrm_status"] = WRMStatus == WRMSTATUS_INIT ? "Init" : WRMStatus == WRMSTATUS_JOIN_AP ? "Joining AP" : WRMStatus == WRMSTATUS_PAIRING ? "Paring" : WRMStatus == WRMSTATUS_CONNECT_CTRLBOX ? "Connecting MQTT" : "Normal";
    doc["relay_status"] = RelayStatus == RELAYSTATUS_ON ? "On" : "Off";
    String json;
    serializeJson(doc, json);
    req->send(200, "application/json", json); });
  server.on("/api/v1/update", HTTP_POST, [](AsyncWebServerRequest *req)
            {
    if (mUpdateResult != UPDATE_OK) {
      req->send(500,"text/plain","update error: " + mUpdateErrorMsg);
      return;
    } else {
      req->send(200,"text/plain","update ok, rebooting...");
      restartTimer.once_ms(3000, []() {
        ESP.restart();
      });
    } }, [](AsyncWebServerRequest *req, String filename, size_t index, uint8_t *data, size_t len, bool final)
            {

    StreamString stream;
    if (!index) {
      mUpdateErrorMsg.clear();
      Serial.printf("Update Start: %s\n", filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)){
        getUpdateErrorMsg();
      }
    }
    if (Update.write(data, len) != len) {
      getUpdateErrorMsg();
    }
    if (final){
      if (Update.end(true)){
        Serial.printf("Update Success: %u\nRebooting...\n", index+len);
        mUpdateResult = UPDATE_OK;
      } else {
        getUpdateErrorMsg();
      }
    } });

  server.on("/api/v1/reboot", HTTP_GET, [](AsyncWebServerRequest *req)
            {
    req->send(200,"text/plain","Rebooting...");
    ESP.restart(); });

  server.on("/api/v1/add", HTTP_POST, handleSetupPost);

  server.onNotFound([](AsyncWebServerRequest *req)
                    { req->send(404, "text/plain", "404: Not found"); });
  /*
    server.on("/update", HTTP_POST, [](AsyncWebServerRequest *req) {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      ESP.restart();
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Update: %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      }
    });

    server.on("/log",HTTP_GET,[]{
      String logData = "";

      File logFile = SPIFFS.open("/log.txt", "r");
      if (!logFile) {
        server.send(500, "text/plain", "Failed to open log file for reading");
        return;
      }
      while (logFile.available()) {
        logData += logFile.readString();
      }
      logFile.close();
      server.send(200, "text/plain", logData);
    });


  */
  // Actually start the server
  server.begin();
  mHTTPRunning = true;
  Serial.println("HTTP server started");
}

void setClock()
{
  configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // UTC

  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2)
  {
    yield();
    delay(500);
    Serial.print(F("."));
    now = time(nullptr);
  }

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
}

void loop()
{
  // put your main code here, to run repeatedly:
  processStatusLED();
  processResetBtn();

  // try to connect to WiFi again if it is disconnected.
  if ((mWifiMode != AP_MODE) && (WiFi.status() != WL_CONNECTED))
  {
    if (millis() - mLastReConnTime > RE_CONN_WIFI_DELAY)
    { // 10 seconds
      Serial.println("WiFi not connected, try to reconnect ...");
      mLastReConnTime = millis();
      if (tryToConnectWifi() == true)
      {
        mLastConnTime = millis();
      }
    }
    // If we cannot connect to WiFi after 1h, we restart the system.
    if (millis() - mLastConnTime > NO_CONN_RESTART_DELAY)
    {
      ESP.restart();
    }
  }
  else if ((WRMStatus == WRMSTATUS_JOIN_AP) && (WiFi.status() == WL_CONNECTED))
  {
    Serial.println("Local ip: " + WiFi.localIP().toString());
    WRMStatus = WRMSTATUS_PAIRING;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    mLastConnTime = millis();
    if (WRMStatus == WRMSTATUS_PAIRING)
    {
      WRMStatus = WRMSTATUS_CONNECT_CTRLBOX;
      EEPROM.readString(EEPROM_OFFSET_CTRLBOX_IP, eeprom_ctrlbox_ipaddr, EEPROM_CTRLBOX_IP_SIZE);
      Serial.print("CtrlBoxIP: ");
      Serial.println(eeprom_ctrlbox_ipaddr);
    }

    if (WRMStatus == WRMSTATUS_CONNECT_CTRLBOX)
    {
      // check if control box IP address is exist.
      if (strlen(eeprom_ctrlbox_ipaddr) > 0)
      {
        Serial.println("MQTT Broker: " + String(eeprom_ctrlbox_ipaddr) + ", Port: " + String(MQTT_BROKER_PORT));
        connectToMQTTBroker();
      }
    }

    if (WRMStatus == WRMSTATUS_NORMAL)
    {
      if (!mqtt_client.connected())
      {
        WRMStatus = WRMSTATUS_CONNECT_CTRLBOX;
      }
      else
      {
        mqtt_client.loop(); // Listen for incoming messages
      }

      // If 10 seconds have passed, the relay is turned off
      if (checkAutoRelay)
      {
        if ((millis() - lastDebounceTime_Relay) > debounceDelay_Relay && RelayStatus == RELAYSTATUS_ON)
        {
          checkAutoRelay = false;
          digitalWrite(RELAY_CTRL_PIN, LOW);
          RelayStatus = RELAYSTATUS_OFF;

          Serial.println("ON after 10 seconds");
          // Respond to the client
          StaticJsonDocument<200> jsonBufferRes;

          memset(jsonMessage, 0, sizeof(jsonMessage));
          jsonBufferRes.clear();
          jsonBufferRes["action"] = "status";
          jsonBufferRes["command"] = "updateByAccessControl";
          jsonBufferRes["deviceId"] = chip_id;
          jsonBufferRes["state"] = RelayStatus;
          jsonBufferRes["sender"] = reqSender;
          serializeJson(jsonBufferRes, jsonMessage);
          serializeJson(jsonBufferRes, Serial);
          Serial.println("");

          mqtt_client.publish(relay2CtrlBoxTopic, jsonMessage);
        }
      }
    }
  }
}