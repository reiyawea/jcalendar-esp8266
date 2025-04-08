#include <Arduino.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoUZlib.h> // 解压gzip
#include <ArduinoJson.h>
#include "holiday.h"
#include "screen_ink.h"
#include <Preferences.h>

#define PREF_NAMESPACE "J_CALENDAR"
#define PREF_QWEATHER_KEY "QWEATHER_KEY" // QWEATHER KEY/TOKEN
#define PREF_QWEATHER_LOC "QWEATHER_LOC" // 地理位置
#define PREF_CD_DAY_DATE "CD_DAY_DATE"   // 倒计日
#define PREF_CD_DAY_LABLE "CD_DAY_LABLE" // 倒计日名称
#define PREF_TAG_DAYS "TAG_DAYS"         // tag day
#define PREF_HOLIDAY "HOLIDAY"           // 假期信息，tm年，假期日(int8)，假期日(int8)...
const char *portal_title = "E-Paper Calendar";

Preferences pref;
HTTPClient httpsclient;
BearSSL::WiFiClientSecure wiFiClientSecure;
WiFiManager wifiManager;
JsonDocument doc;

WiFiManagerParameter para_qweather_key("qweather_key", "和风天气Token", "", 32);                     //     和风天气key
WiFiManagerParameter para_qweather_location("qweather_loc", "位置ID", "", 9, "pattern='\\d{9}'");    //     城市code
WiFiManagerParameter para_cd_day_label("cd_day_label", "倒数日（4字以内）", "", 10);                 //     倒数日
WiFiManagerParameter para_cd_day_date("cd_day_date", "日期（yyyyMMdd）", "", 8, "pattern='\\d{8}'"); //     城市code
WiFiManagerParameter para_tag_days("tag_days", "日期Tag（yyyyMMddx，详见README）", "", 30);          //     日期Tag

struct tm tmInfo;
struct Weather weather_now;
struct Holiday holidays;
String _qweather_key;
String _qweather_loc;
String _cd_day_label;
String _cd_day_date;
String _tag_days_str;

// put function declarations here:
void enterDeepSleep();
int httpGetJson(String url, JsonDocument &doc);
void configModeCallback(WiFiManager *myWiFiManager);
void saveParamsCallback()
{
    Preferences pref;

    pref.begin(PREF_NAMESPACE);
    pref.putString(PREF_QWEATHER_KEY, para_qweather_key.getValue());
    pref.putString(PREF_QWEATHER_LOC, para_qweather_location.getValue());
    pref.putString(PREF_CD_DAY_LABLE, para_cd_day_label.getValue());
    pref.putString(PREF_CD_DAY_DATE, para_cd_day_date.getValue());
    pref.putString(PREF_TAG_DAYS, para_tag_days.getValue());
    Serial.println("Params saved.");
}

void setup()
{
    // put your setup code here, to run once:
    bool success = false;

    Serial.begin(115200);
    pref.begin(PREF_NAMESPACE);
    _qweather_key = pref.getString(PREF_QWEATHER_KEY, "");
    _qweather_loc = pref.getString(PREF_QWEATHER_LOC, "");
    _cd_day_label = pref.getString(PREF_CD_DAY_LABLE, "");
    _cd_day_date = pref.getString(PREF_CD_DAY_DATE, "");
    _tag_days_str = pref.getString(PREF_TAG_DAYS, "");
    pref.getBytes(PREF_HOLIDAY, &holidays, sizeof(struct Holiday));
    pref.end();

    wifiManager.setTitle("JCalendar");
    wifiManager.addParameter(&para_qweather_key);
    wifiManager.addParameter(&para_qweather_location);
    wifiManager.addParameter(&para_cd_day_label);
    wifiManager.addParameter(&para_cd_day_date);
    wifiManager.addParameter(&para_tag_days);
    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setSaveParamsCallback(saveParamsCallback);

    if (!_qweather_key.length() || !_qweather_loc.length()) {
        wifiManager.startConfigPortal(portal_title);
        ESP.restart();
    } else if (wifiManager.autoConnect(portal_title)) {
        Serial.println("connected...yeey :)");
    } else {
        Serial.println("Failed to connect");
        enterDeepSleep();
    }

    do {
        if (!_qweather_key.length() || !_qweather_loc.length()) {
            Serial.println("Qweather key/locationID invalid.");
            break;
        }
        wiFiClientSecure.setInsecure();
        if (httpGetJson("https://devapi.qweather.com/v7/weather/now?key=" + _qweather_key + "&location=" + _qweather_loc, doc)) break;
        JsonObject now = doc["now"].as<JsonObject>();
        weather_now.temp = atoi(now["temp"]);
        weather_now.humidity = atoi(now["humidity"]);
        weather_now.wind360 = atoi(now["wind360"]);
        weather_now.windDir = now["windDir"].as<const char *>();
        weather_now.windScale = atoi(now["windScale"]);
        weather_now.windSpeed = atoi(now["windSpeed"]);
        weather_now.icon = atoi(now["icon"]);
        weather_now.text = now["text"].as<const char *>();
        weather_now.time = now["obsTime"].as<const char *>();
        doc.clear();
        tmInfo.tm_year = weather_now.time.substring(0, 4).toInt() - 1900; // 2025-04-08T18:58+08:00
        tmInfo.tm_mon = weather_now.time.substring(5, 7).toInt() - 1;
        tmInfo.tm_mday = weather_now.time.substring(8, 10).toInt();
        tmInfo.tm_hour = weather_now.time.substring(11, 13).toInt();
        tmInfo.tm_min = weather_now.time.substring(14, 16).toInt();
        tmInfo.tm_sec = 0;
        time_t t = mktime(&tmInfo);
        localtime_r(&t, &tmInfo);
        success = true;
        //====================================
        if (holidays.year == tmInfo.tm_year + 1900 && holidays.month == tmInfo.tm_mon + 1) break;
        if (httpGetJson("https://timor.tech/api/holiday/year/" + String(tmInfo.tm_year + 1900) + "-" + String(tmInfo.tm_mon + 1), doc)) break;
        holidays.year = tmInfo.tm_year + 1900;
        holidays.month = tmInfo.tm_mon + 1;
        JsonObject oHoliday = doc["holiday"].as<JsonObject>();
        size_t i = 0;
        Serial.printf("Holiday: ");
        for (JsonPair kv : oHoliday) {
            String key = String(kv.key().c_str());
            JsonObject value = kv.value();
            bool isHoliday = value["holiday"].as<bool>();

            int day = key.substring(3, 5).toInt();
            holidays.holidays[i] = day * (isHoliday ? 1 : -1); // 假期为正，补工作日为负
            Serial.printf("%d ", holidays.holidays[i]);
            holidays.length = ++i;
            if (i >= sizeof(holidays.holidays) / sizeof(holidays.holidays[0])) break;
        }
        doc.clear();
        Serial.println();
        pref.begin(PREF_NAMESPACE);
        pref.putBytes(PREF_HOLIDAY, &holidays, sizeof(struct Holiday));
        pref.end();
    } while (0);
    WiFi.disconnect(true, false);
    if (success) si_screen();
    delay(100);
    enterDeepSleep();
}

void loop()
{
    // put your main code here, to run repeatedly:
}

int httpGetJson(String url, JsonDocument &doc)
{
    const char *headerKeys[] = {"Content-Encoding", "Content-Type"};

    bool res = httpsclient.begin(wiFiClientSecure, url);
    if (!res) {
        Serial.println("httpsclient begin failed");
        return -1;
    }
    httpsclient.collectHeaders(headerKeys, 2);
    int httpCode = httpsclient.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("httpsclient get failed %d\n", httpCode);
        return -2;
    }
    int content_len = httpsclient.getSize(); // get length of document (is -1 when Server sends no Content-Length header)
    if (content_len <= 0) {
        Serial.println("unknown content_len");
        return -3;
    }
    Serial.printf("content-len = %d\n", content_len);
    for (int i = 0; i < httpsclient.headers(); i++) {
        Serial.printf("%s: %s\n", httpsclient.headerName(i).c_str(), httpsclient.header(i).c_str());
    }
    uint8_t *inbuf = (uint8_t *)malloc(content_len);
    int offset = 0;
    while (httpsclient.connected() && offset < content_len) {
        if (wiFiClientSecure.available()) {
            int readBytesSize = wiFiClientSecure.readBytes(inbuf + offset, content_len);
            Serial.printf("read %d bytes\n", readBytesSize);
            offset += readBytesSize;
        }
    }
    String content_encoding = httpsclient.header("Content-Encoding");
    httpsclient.end();
    if (content_encoding.equals("gzip")) {
        uint8_t *outbuf = NULL;
        uint32_t outsize;
        int32_t res = ArduinoUZlib::decompress(inbuf, content_len, outbuf, outsize);
        free(inbuf);
        if (res <= 0) {
            Serial.printf("decompress failed %d\n", res);
            if (outbuf) free(outbuf);
            return -4;
        }
        Serial.write(outbuf, outsize);
        Serial.println("---");
        Serial.flush();
        inbuf = outbuf;
    }
    DeserializationError error = deserializeJson(doc, (char *)inbuf);
    free(inbuf);
    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.f_str());
        return -5;
    }
    return 0;
}

// put function definitions here:
void configModeCallback(WiFiManager *myWiFiManager)
{
    Serial.println("configModeCallback");
}

void enterDeepSleep()
{
    uint64_t t = ESP.deepSleepMax();
    Serial.printf("sleep for %.1lf s...\n", (double)t * 1e-6);
    Serial.flush();
    delay(100);
    ESP.deepSleep(t, RF_DISABLED);
    Serial.println("This line should never be executed..");
    while (1);
}
