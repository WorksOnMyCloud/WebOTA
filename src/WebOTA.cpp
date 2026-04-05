#include "WebOTA.h"
#include "WebOTA-html.h"

#ifdef __DEBUG__OTA__
#define DBG_OTA(...) Serial.printf(__VA_ARGS__)
#else
  #ifndef DBG_OTA
  #define DBG_OTA(...) while(0)
  #endif
#endif

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_WEB_OTA)
WebOTA_t WebOTA = WebOTA_t();
#endif

WebOTA_t::WebOTA_t() :
  onStart_cb(nullptr),
  onEnd_cb(nullptr),
  onError_cb(nullptr),
  onProgress_cb(nullptr),
  onReboot_cb(nullptr)
{}

void WebOTA_t::enable(AsyncWebServer *server) {

  //server->onNotFound([](AsyncWebServerRequest *rq){rq->send(404);});
  server->on("/reboot", HTTP_GET,
    [&](AsyncWebServerRequest *rq) {
      rq->onDisconnect([&](){
        if (onReboot_cb) {
          DBG_OTA("[OTA] onReboot\n");
          onReboot_cb(); 
        }
        restart();
      });
      AsyncWebServerResponse *rsp = rq->beginResponse(200, "text/plain", "\n");
      rsp->addHeader("Connection", "close");
      rq->send(rsp);
    }
  );

  server->on(WEB_OTA_WEB_PATH, HTTP_GET,
    /* onRequest */
    [&](AsyncWebServerRequest *rq) {
      DBG_OTA("[OTA] GET onRequest\n");
      rq->send(200, "text/html", indexHTML);
    }
  );

  server->on(WEB_OTA_WEB_PATH, HTTP_POST,
    /* onRequest */
    [&](AsyncWebServerRequest *rq) {
      ota_reboot = !Update.hasError();
      DBG_OTA("[OTA] POST onRequest\n");

      rq->onDisconnect([&](){
        if (ota_reboot) {
          if (onReboot_cb) {
            DBG_OTA("[OTA] onReboot\n");
            onReboot_cb();
          }
          restart();
        }
      });

      AsyncWebServerResponse *resp = rq->beginResponse(ota_reboot?200:500, "text/plain", (!ota_reboot) ? "FAIL\n" : "OK\n");
      resp->addHeader("Connection", "close");
      resp->addHeader("esp-ota", ota_reboot ? "ok" : "fail");
      if (ota_reboot) {
        resp->addHeader("esp-firmware", String(_firmware));
        resp->addHeader("esp-partition", String(_partition));
        resp->addHeader("esp-md5", Update.md5String());
      }
      rq->send(resp);
    },
    /* onUpload */
    [&](
      AsyncWebServerRequest *rq,
      const String& filename,
      size_t index,
      uint8_t *data,
      size_t len,
      bool final) {
        // DBG_OTA("[OTA] POST onUpload %d %d\n", index, len);
        if (!index) {
          DBG_OTA("[OTA] Start: %s\n", filename.c_str());
          Update.abort();
          Update.end();
          Update.clearError();

          if (rq->hasParam("md5", true)) {
            DBG_OTA("[OTA] MD5 %s\n", rq->getParam("md5", true)->value().c_str());
            if (!Update.setMD5(rq->getParam("md5", true)->value().c_str())) {
              if (onError_cb) { onError_cb(Update.getError(), Update.errorString()); }
              return rq->send(400, "text/plain", "OTA: FAIL MD5\n");
            }
          }

          #if defined(ESP32)
          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            DBG_OTA("[OTA] fail begin: %s\n", Update.errorString());
            if (onError_cb) { onError_cb(Update.getError(), Update.errorString()); }
            Update.abort();
            Update.end();
            return rq->send(400, "text/plain", "OTA: FAIL begin\n");
          }
          #endif

          if (onStart_cb) { onStart_cb(); }
        }

        if (len) {
          if (Update.write(data, len) != len) {
            DBG_OTA("[OTA] fail write: %s\n", Update.errorString());
            if (onError_cb) { onError_cb(Update.getError(), Update.errorString()); }
            Update.abort();
            Update.end();
            return rq->send(400, "text/plain", "OTA: FAIL write\n");
          } else {
            if (onProgress_cb) {
              onProgress_cb(index+len, rq->contentLength());
            }
          }
        }

        if (final) {
          _partition = Update.size();
          _firmware  = Update.progress();  
          if (Update.end(true)) {
            DBG_OTA("[OTA] End: %u bytes\n", index+len);
            if (onEnd_cb) { onEnd_cb(); }
            DBG_OTA("[OTA] End: %u bytes\n", index+len);
            return;
          } else {
            DBG_OTA("[OTA] fail end: %s\n", Update.errorString());
            if (onError_cb) { onError_cb(Update.getError(), Update.errorString()); }
            Update.abort();
            Update.end();
            return rq->send(400, "text/plain", "OTA: FAIL end");
          }
        }
     }
  );
}

void WebOTA_t::restart() {

  delay(500);
  yield();
  delay(500);
  yield();
  delay(100);
  ESP.restart();
}

#if defined(ESP32)
uint32_t WebOTA_t::ESP_getChipId(void) {
  uint32_t id = 0;
  for (uint32_t i = 0; i < 17; i = i +8) {
    id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  return id;
}
#endif
