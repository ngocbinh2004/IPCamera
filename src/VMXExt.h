#ifndef __VMXEXT_H__
#define __VMXEXT_H__


#include "setup_html.h"
#include "mbedtls/md.h"
#include <ESPAsyncWebServer.h>
#include <Ticker.h>
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "LittleFS.h"
#include <time.h>
#include "StreamString.h"

const int FIRMWARE_VERSION = 1;

#define OTA_URL "http://s3.ap-southeast-2.amazonaws.com/my.aws.aipod/firmware.bin" // once you compile this code, upload the binary to your bucket and change this URL for OTA

#define NO_CONN_RESTART_DELAY 3600000 // 1 hour in milliseconds
#define RE_CONN_WIFI_DELAY 10000 // 10 seconds in milliseconds
#define HTTP_SERVER_ACTIVE_TIME 300000 // 5 minutes in milliseconds
#define WS_CLEANUP_INTERVAL 60000 // 1 minute in milliseconds
#define MAX_CONN_WIFI_RETRIES 10 // Maximum number of WiFi connection retries
#define MAX_LOG_FILE_SIZE 1024 * 100 // 100KB

#define REST_SERVER_PORT 80

#define LOGFILE_PATH "/log.txt"
#define LOGFILE_OLD_PATH "/log_old.txt"
#define FILESYSTEM LittleFS

/* Style */
String style =
"<style>#file-input,input{width:100%;height:44px;border-radius:4px;margin:10px auto;font-size:15px}"
"input{background:#f1f1f1;border:0;padding:0 15px}body{background:#3498db;font-family:sans-serif;font-size:14px;color:#777}"
"#file-input{padding:0;border:1px solid #ddd;line-height:44px;text-align:left;display:block;cursor:pointer}"
"#bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-color:#3498db;width:0%;height:10px}"
"form{background:#fff;max-width:258px;margin:75px auto;padding:30px;border-radius:5px;text-align:center}"
".btn{background:#3498db;color:#fff;cursor:pointer}</style>";

/* Server Index Page */
String html_updater = 
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
"<input type='file' name='update' id='file' onchange='sub(this)' style=display:none>"
"<label id='file-input' for='file'>   Choose file...</label>"
"<input type='submit' class=btn value='Update'>"
"<br><br>"
"<div id='prg'></div>"
"<br><div id='prgbar'><div id='bar'></div></div><br></form>"
"<script>"
"function sub(obj){"
"var fileName = obj.value.split('\\\\');"
"document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
"};"
"$('form').submit(function(e){"
"e.preventDefault();"
"var form = $('#upload_form')[0];"
"var data = new FormData(form);"
"$.ajax({"
"url: '/update',"
"type: 'POST',"
"data: data,"
"contentType: false,"
"processData:false,"
"xhr: function() {"
"var xhr = new window.XMLHttpRequest();"
"xhr.upload.addEventListener('progress', function(evt) {"
"if (evt.lengthComputable) {"
"var per = evt.loaded / evt.total;"
"$('#prg').html('progress: ' + Math.round(per*100) + '%');"
"$('#bar').css('width',Math.round(per*100) + '%');"
"}"
"}, false);"
"return xhr;"
"},"
"success:function(d, s) {"
"console.log('success!') "
"},"
"error: function (a, b, c) {"
"}"
"});"
"});"
"</script>" + style;

const char* username = "admin";
const char* password = "admin123";

// Global variables
enum ESP_MODES {
  AP_MODE,
  STAT_MODE
};
enum UpdateResult
{
    UPDATE_OK,
    UPDATE_ABORT,
    UPDATE_ERROR,
};

AsyncWebServer server(REST_SERVER_PORT); // Create AsyncWebServer object on port 80
AsyncWebSocket ws("/ws"); // Create a WebSocket object

struct tm ntpTime;

// Ticker for restart
Ticker restartTimer;

bool mHTTPRunning = false;
bool mMQTTRunning = false;
bool mWifiConnected = false;

// Logging options
bool log2Serial = true;
bool log2File = true;
bool log2WS = true;

int mWifiMode = AP_MODE;
int mWifiRetriesCount = 0;
int mUpdateResult = UPDATE_OK;

unsigned long mLastNoConnTime = 0;
unsigned long mLastReConnTime = 0;
unsigned long mLastConnTime = 0;
unsigned long mLastWSCleanupTime = 0;

String mUpdateErrorMsg = "";

// Function prototypes
bool tryToConnectWifi();
void runHttpServer();
void setClock();

// Firmware update functions
void updateFirmware();
void downloadFirmware();
void getUpdateErrorMsg();

// WebSocket functions
void notifyToClient(String message);
void notifyToClient(const char* message, size_t len);
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

// Function to get date time string
// Format: "YYYY-MM-DD HH:MM:SS"
inline void getDateTimeString(char* buffer, size_t len) {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    snprintf(buffer, len, "0000-00-00 00:00:00");
    return;
  }
  snprintf(buffer, len, "%04d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}

inline String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + " B";
  } else if (bytes < 1048576) {
    return String(bytes / 1024.0, 2) + " KB";
  } else if (bytes < 1073741824) {
    return String(bytes / 1048576.0, 2) + " MB";
  } else {
    return String(bytes / 1073741824.0, 2) + " GB";
  }
}

#endif // __VMXEXT_H__