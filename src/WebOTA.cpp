#include "WebOTA.h"
#include "WebOTA-up-html.h"
#include "WebOTA-ota-html.h"

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

void WebOTA_t::GetFileUpload(AsyncWebServerRequest *rq) {
  DBG_OTA("[UPLOAD] GET onRequest\n");
  rq->send(200, "text/html", uploadHTML);
}

void WebOTA_t::PostFileUpload(AsyncWebServerRequest *rq) {
  DBG_OTA("[UPLOAD] POST onRequest\n");

  if (rq->_tempObject) {
    WebOTA_t *afu = static_cast<WebOTA_t *>(rq->_tempObject);
    rq->onDisconnect([&](){
      if (afu->onReboot_cb) {
        DBG_OTA("[UPLOAD] onReboot\n");
        afu->onReboot_cb();
      }
      afu->restart();
    });
  }

  AsyncWebServerResponse *resp = rq->beginResponse(200, "text/plain", "OK\n");
  resp->addHeader("Connection", "close");
  rq->send(resp);
}

void WebOTA_t::handleFileUpload(AsyncWebServerRequest *rq,
                                const String& filename,
                                size_t index,
                                uint8_t *data,
                                size_t len,
                                bool final) {

  if (!rq->_tempObject)
    return;

  WebOTA_t *afu = static_cast<WebOTA_t *>(rq->_tempObject);

  if (!afu->_filesystem)
    return;

  if (!index) {
    String fn = filename;
    if (!fn.startsWith("/")) {
      fn = "/" + fn;
    }
    rq->_tempFile = afu->_filesystem->open(fn, "w");
  }

  if (rq->_tempFile) {
    rq->_tempFile.write(data, len);
  }

  if (final) {
    if (rq->_tempFile) {
      rq->_tempFile.close();
    }
  }
}

void WebOTA_t::enable(AsyncWebServer *server, fs::FS* fsptr) {

  _filesystem = fsptr;

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

  if (_filesystem) {
    server->on(WEB_UPLOAD_WEB_PATH, HTTP_GET, GetFileUpload);
    server->on(WEB_UPLOAD_WEB_PATH, HTTP_POST,
               PostFileUpload,
               [&](AsyncWebServerRequest *rq, String filename,
                   size_t index, uint8_t *data,
                   size_t len, bool last) {
                 //typename decltype(this)::force_compiler_error error;
                 if (!index)
                   rq->_tempObject = this;
                 handleFileUpload(rq, filename, index, data, len, last);
               }
              );
  }

  server->on(WEB_OTA_WEB_PATH, HTTP_GET,
    /* onRequest */
    [&](AsyncWebServerRequest *rq) {
      DBG_OTA("[OTA] GET onRequest\n");
      rq->send(200, "text/html", otaHTML);
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

          if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            DBG_OTA("[OTA] fail begin: %s\n", Update.errorString());
            if (onError_cb) { onError_cb(Update.getError(), Update.errorString()); }
            Update.abort();
            Update.end();
            return rq->send(400, "text/plain", "OTA: FAIL begin\n");
          }

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
