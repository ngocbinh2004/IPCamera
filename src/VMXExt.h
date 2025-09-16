#ifndef __VMXEXT_H__
#define __VMXEXT_H__


#include "setup_html.h"
#include "mbedtls/md.h"
#include <ESPAsyncWebServer.h>
#include <Ticker.h>
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "FS.h"
#include "SPIFFS.h"
#include <time.h>
#include "StreamString.h"

const int FIRMWARE_VERSION = 1;

#define OTA_URL "http://s3.ap-southeast-2.amazonaws.com/my.aws.aipod/firmware.bin" // once you compile this code, upload the binary to your bucket and change this URL for OTA


#define NO_CONN_RESTART_DELAY 3600000 // 1 hour in milliseconds
#define RE_CONN_WIFI_DELAY 10000 // 10 seconds in milliseconds
#define HTTP_SERVER_ACTIVE_TIME 300000 // 5 minutes in milliseconds
#define MAX_CONN_WIFI_RETRIES 10 // Maximum number of WiFi connection retries

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

Ticker restartTimer;

bool mHTTPRunning = false;
bool mMQTTRunning = false;
bool mWifiConnected = false;

int mWifiMode = AP_MODE;
int mWifiRetriesCount = 0;
int mUpdateResult = UPDATE_OK;

unsigned long mLastNoConnTime = 0;
unsigned long mLastReConnTime = 0;
unsigned long mLastConnTime = 0;

String mUpdateErrorMsg = "";

// Function prototypes
bool tryToConnectWifi();
void runHttpServer();
void setClock();
void updateFirmware();
void downloadFirmware();
void getUpdateErrorMsg();
#endif