#include <Arduino.h>
#include <ArduinoHttpClient.h>
#include <SPI.h>
#include <WiFi.h>

#ifndef WIFI_SSID
#error "WIFI_SSID must be provided by build flags. Copy secrets.example.ini to secrets.ini."
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD must be provided by build flags. Copy secrets.example.ini to secrets.ini."
#endif

#ifndef PROXY_SERVER
#error "PROXY_SERVER must be provided by build flags. Copy secrets.example.ini to secrets.ini."
#endif

#ifndef PROXY_PORT
#error "PROXY_PORT must be provided by build flags. Copy secrets.example.ini to secrets.ini."
#endif

#ifdef DEBUG_SERIAL
#define DEBUG_PRINT(...) Serial.print(__VA_ARGS__)
#define DEBUG_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#define DEBUG_PRINTLN(...)
#endif

const IPAddress kProxyServer{PROXY_SERVER};
const uint16_t kProxyPort = PROXY_PORT;

struct Event {
  enum Id : uint8_t {
    kBucketFull,
    kResetFilter,
    kCount,
  };

  const Id id;
  bool pending;
  uint8_t attempt;
  const char* const json;
  unsigned long prev_attempt_ms;
};

Event kEvents[Event::kCount] = {
    {
        .id = Event::kBucketFull,
        .pending = false,
        .attempt = 0,
        .json = R"({"who":"dehumidifier", "what":"bucket full", "where":"basement"})",
        .prev_attempt_ms = 0UL,
    },
    {
        .id = Event::kResetFilter,
        .pending = false,
        .attempt = 0,
        .json = R"({"who":"dehumidifier", "what":"reset filter", "where":"basement"})",
        .prev_attempt_ms = 0UL,
    }};

class Notifier {
 public:
  explicit Notifier(IPAddress address, uint16_t port) : http_client_(wifi_client_, address, port) {}

  void begin() {
    // The WiFi Shield API requires a mutable SSID buffer.
    char wifi_ssid[] = WIFI_SSID;

    if (WiFi.status() == WL_NO_SHIELD) {
      DEBUG_PRINTLN(F("WiFi shield not present"));
      return;
    }

    int wifi_status = WL_IDLE_STATUS;
    while (wifi_status != WL_CONNECTED) {
      DEBUG_PRINT(F("Attempting to connect to WPA SSID: "));
      DEBUG_PRINTLN(wifi_ssid);
      wifi_status = WiFi.begin(wifi_ssid, WIFI_PASSWORD);
      delay(10000);
    }

    DEBUG_PRINTLN(F("Connected to the network"));
  }

  void send() {
    for (uint8_t i = 0; i < Event::kCount; i++) {
      Event& event = kEvents[i];
      if (!event.pending) {
        continue;
      }

      unsigned long now = millis();
      if (event.attempt < kMaxAttempts) {
        bool notified = false;

        if (event.attempt == 0 || (now - event.prev_attempt_ms >= kRetryAttemptMs)) {
          DEBUG_PRINT(F("Attempt: "));
          DEBUG_PRINTLN(event.attempt);
          notified = post_event(event);
        }

        if (notified) {
          event.pending = false;
          event.attempt = 0;
        } else {
          event.attempt += 1;
          event.prev_attempt_ms = now;
        }
      } else if (now - event.prev_attempt_ms >= kRetryAttemptMs) {
        DEBUG_PRINTLN(F("Resetting notifier retry count"));
        event.attempt = 0;
      }
    }
  }

 private:
  static constexpr unsigned long kRetryAttemptMs = 1UL * 60UL * 1000UL;
  static constexpr uint8_t kMaxAttempts = 3;

  WiFiClient wifi_client_;
  HttpClient http_client_;

  bool post_event(Event& event) {
    http_client_.stop();

    DEBUG_PRINTLN(F("Sending POST /events to proxy"));

    int result = http_client_.post("/events", "application/json", event.json);
    if (result != 0) {
      DEBUG_PRINT(F("ArduinoHttpClient POST failed: "));
      DEBUG_PRINTLN(result);
      http_client_.stop();
      return false;
    }

    int status_code = http_client_.responseStatusCode();
    http_client_.stop();

    DEBUG_PRINT(F("Proxy response status: "));
    DEBUG_PRINTLN(status_code);

    return status_code >= 200 && status_code < 300;
  }
};

class Photoresistor {
 public:
  explicit Photoresistor(uint8_t pin, Event& event) : pin_(pin), event_(event) {}

  void begin() { pinMode(pin_, INPUT); }

  void detect() {
    int value = analogRead(pin_);
    if (value < kThreshold) {
      return;
    }

    unsigned long now = millis();
    if (!initialized_ || (now - last_detection_ms_) >= kResetDetectionMs) {
      DEBUG_PRINTLN(F("Detected!"));
      initialized_ = true;
      event_.pending = true;
    }

    last_detection_ms_ = now;
  }

 private:
  static constexpr unsigned long kResetDetectionMs = 5UL * 60UL * 1000UL;
  static constexpr int kThreshold = 250;

  uint8_t pin_;
  Event& event_;
  unsigned long last_detection_ms_ = 0UL;
  bool initialized_ = false;
};

Notifier notifier{kProxyServer, kProxyPort};
Photoresistor bucketFullPhotoresistor{A0, kEvents[Event::kBucketFull]};

void setup() {
#ifdef DEBUG_SERIAL
  Serial.begin(9600);
#endif

  notifier.begin();
  bucketFullPhotoresistor.begin();
}

void loop() {
  bucketFullPhotoresistor.detect();
  notifier.send();
}
