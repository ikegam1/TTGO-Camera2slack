#include "esp_camera.h"
#include "esp_deep_sleep.h"
#include "FS.h"
#include "SPIFFS.h"
#include <ArduinoJson.h>
#include "time.h"
#include <HTTPClient.h>


#define FORMAT_SPIFFS_IF_FAILED true

//
// WARNING!!! Make sure that you have either selected ESP32 Wrover Module,
//            or another board which has PSRAM enabled
//

#define HTTPS_HOST              "slack.com"
#define SLACK_METHOD_PATH        "/api/files.upload"
#define HTTPS_PORT              443
#define SLACK_POST_STRING       "/services/XXXXXXXX/XXXXXXXX/xxxxxxxx"
#define SLACK_API_TOKEN         "xoxp-xxxxxxxx"
#define JST     3600*9

// Select camera model
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
//#define CAMERA_MODEL_AI_THINKER
#define CAMERA_MODEL_TTGO

#include "camera_pins.h"

#define I2C_SDA 21
#define I2C_SCL 22
#define AS312_PIN           33
#define BUTTON_1            0

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  600        /* Time ESP32 will go to sleep (in seconds) */

/*
 * files.upload api sample
 * https://slack.com/api/files.upload?token=xoxp-xxxxxxxx&channels=XXXXXXXX&filename=capture.jpg&filetype=image%2Fjpeg&initial_comment=%25Y%25m%25d%25H%25M%25S&pretty
 */ 
const char* slack_api_token = "xoxp-xxxxxxxx;
const char* slack_channel = "XXXXXXXX";
const char* ssid = "xxxxxxxx";
const char* password = "XXXXX";
const long gmtOffset_sec =      3600 * 9;
const int daylightOffset_sec =  0;
const int AMBIENT_CHANNEL_ID = 8000;
const char* AMBIENT_WRITE_KEY = "xxxxxxxx";
const int delay_time = 1000 * 60 * 10;

char *request_content = "--------------------------ef73a32d43e7f04d\r\n"
                        "Content-Disposition: form-data; name=\"channels\"\r\n\r\n"
                        "%s\r\n"
                        "--------------------------ef73a32d43e7f04d\r\n"
                        "Content-Disposition: form-data; name=\"filename\"\r\n\r\n"
                        "capture.jpg\r\n"
                        "--------------------------ef73a32d43e7f04d\r\n"
                        "Content-Disposition: form-data; name=\"filetype\"\r\n\r\n"
                        "image/jpeg\r\n"
                        "--------------------------ef73a32d43e7f04d\r\n"
                        "Content-Disposition: form-data; name=\"initial_comment\"\r\n\r\n"
                        "%s\r\n"
                        "--------------------------ef73a32d43e7f04d\r\n"
                        "Content-Disposition: form-data; name=\"file\"; filename=\"capture.jpg\"\r\n"
                        "Content-Type: image/jpeg\r\n"
                        "Content-Length: %d\r\n\r\n";

char *request_content_token = "--------------------------ef73a32d43e7f04d\r\n"
                        "Content-Disposition: form-data; name=\"token\"\r\n\r\n"
                        "%s\r\n";

char *request_end = "\r\n--------------------------ef73a32d43e7f04d--\r\n";

StaticJsonBuffer<512> jsonBuffer;
 
HTTPClient http;
WiFiClientSecure client;

void capture_jpg(fs::FS &fs, const char * path){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();
    char status[64] = {0};
    char buf[1024];
    int p;
    struct tm timeinfo;
        
    /*
    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("- failed to open file for writing");
        return ;
    }
    */
        
    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return ;
    }
    size_t out_len, out_width, out_height;
    uint8_t * out_buf;
    bool s;
    bool detected = false;

    /*
    file.write((const uint8_t *)fb->buf, fb->len);
    file.close();
    */

    Serial.println( "capture complete" );

    if (!client.connect(HTTPS_HOST, HTTPS_PORT))
    {
        Serial.println("Connection failed");
        return;
    }

    p = snprintf(buf, sizeof(buf), request_content_token, slack_api_token);
    if (!getLocalTime(&timeinfo))
    {
        Serial.println("Failed to obtain time");
        p += snprintf(buf + p, sizeof(buf), request_content, slack_channel, String(millis()).c_str(), fb->len);
    }
    else
    {
        strftime(status, sizeof(status), "%Y%m%d%H%M%S", &timeinfo);
        p += snprintf(buf + p, sizeof(buf), request_content, slack_channel, status, fb->len);
    }
    
    int content_len = fb->len + strlen(buf) + strlen(request_end);
    
    String request = "POST " + String(SLACK_METHOD_PATH);
    request += " HTTP/1.1\r\n";
    request += "Host: "+ String(HTTPS_HOST) + "\r\n";
    request += "User-Agent: TTGO-Camera\r\n";
    request += "Accept: */*\r\n";
    request += "Content-Length: " + String(content_len) + "\r\n";
    request += "Content-Type: multipart/form-data; boundary=------------------------ef73a32d43e7f04d\r\n";
    request += "Expect: 100-continue\r\n";
    request += "Authorization: Bearer " + String(SLACK_API_TOKEN) + "\r\n";
    request += "\r\n";
    client.print(request);
    Serial.print(request);
    client.readBytesUntil('\r', status, sizeof(status));
    Serial.println(status);

    if (strcmp(status, "HTTP/1.1 100 Continue") != 0)
    {
        Serial.print("Unexpected response: ");
        client.stop();
        return;
    }

    client.print(buf); 
    Serial.print("Continue...");
    Serial.print(buf);
    
    uint8_t *image = fb->buf;
    size_t size = fb->len;
    size_t offset = 0;
    size_t ret = 0;

    if ( fb ) {
        Serial.printf("width: %d, height: %d, buf: 0x%x, len: %d\n", fb->width, fb->height, fb->buf, fb->len);
    }
    
    while (1)
    {
        ret = client.write(image+offset, size);
        offset += ret;
        size -= ret;
        if (fb->len == offset)
        {
            break;
        }
    }

    client.flush();
    client.print(request_end);
    Serial.print(request_end);

    if (!client.find("\r\n\r\n"))
    {
        Serial.println("Invalid response");
    }

    request = client.readStringUntil('\n');
    char *str = strdup(request.c_str());
    if (!str)
    {
        client.stop();
        return;
    }

    free(str);
    client.stop();
    Serial.println( "posted slack" );
    return ;
}

void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("- failed to open file for reading");
        return;
    }

    Serial.println("- read from file:");
    while(file.available()){
        //Serial.write(file.read());
    }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  //initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);//flip it back
    s->set_brightness(s, 1);//up the blightness just a bit
    s->set_saturation(s, -2);//lower the saturation
  }
  //drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_QVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  configTime( JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");

  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
    Serial.println("SPIFFS Mount Failed");
  }

  Serial.print("Camera Ready!");
  Serial.println(WiFi.localIP());

  listDir(SPIFFS, "/", 0);
  //readFile(SPIFFS, "/capture.jpg");
}

void loop() {
  Serial.println("loop ...");
  delay(50);
  capture_jpg(SPIFFS, "/capture.jpg");
  delay(1000);
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) +  " Seconds");
  Serial.println("Going to sleep now");
  Serial.flush(); 
  esp_deep_sleep_start();
}
