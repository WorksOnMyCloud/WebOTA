#pragma once

#ifndef esp_ota_h
  #define esp_ota_h
  #include "Arduino.h"
  #include "stdlib_noniso.h"
  #include <ESPAsyncWebServer.h>
  #include "Update.h"

  class WebOTA_t {
    public:
      WebOTA_t();
      void enable(AsyncWebServer *server);
      void onStart(void (*fn)(void)) {
        onStart_cb = fn;
      }
      void onEnd(void (*fn)(void)) {
        onEnd_cb = fn;
      }
      void onError(void (*fn)(int code, const char* msg)) {
        onError_cb = fn;
      }
      void beforeReboot(void (*fn)(void)) {
        onReboot_cb = fn;
      }
      void onReboot(void (*fn)(void)) {
        onReboot_cb = fn;
      }
      void onProgress(void (*fn)(size_t current, size_t final)) {
        onProgress_cb = fn;
      }
      void restart();

      uint32_t ESP_getChipId(void);

    private:
      size_t _firmware;
      size_t _partition;
      void (*onStart_cb)(void);
      void (*onEnd_cb)(void);
      void (*onError_cb)(int code, const char* msg);
      void (*onProgress_cb)(size_t current, size_t final);
      void (*onReboot_cb)(void);
      bool ota_reboot;
  };
  #if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_WEB_OTA)
  extern WebOTA_t WebOTA;
  #endif

  #ifndef WEB_OTA_WEB_PATH
  #define WEB_OTA_WEB_PATH "/ota"
  #endif

  #ifndef WEB_OTA_TITLE
  #define WEB_OTA_TITLE "Web OTA Update"
  #endif

#endif
