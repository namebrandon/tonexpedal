// Compatibility adapter for ESP-Hosted 1.4.x on ESP32-P4 boards with the
// factory ESP32-C6 co-processor firmware.
#include "sdkconfig.h"

#if defined(CONFIG_ESP_HOSTED_ENABLED)

#include "esp32-hal-hosted.h"
#include "esp32-hal-log.h"
#include "esp_hosted.h"

static bool hosted_initialized = false;
static bool hosted_wifi_active = false;
static sdio_pin_config_t sdio_pins = {
  .pin_clk = CONFIG_ESP_HOSTED_SDIO_PIN_CLK,
  .pin_cmd = CONFIG_ESP_HOSTED_SDIO_PIN_CMD,
  .pin_d0 = CONFIG_ESP_HOSTED_SDIO_PIN_D0,
  .pin_d1 = CONFIG_ESP_HOSTED_SDIO_PIN_D1,
  .pin_d2 = CONFIG_ESP_HOSTED_SDIO_PIN_D2,
  .pin_d3 = CONFIG_ESP_HOSTED_SDIO_PIN_D3,
  .pin_reset = CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE,
};

static bool hostedInit(void) {
  if (hosted_initialized) return true;
  // ESP-Hosted 1.4 configures SDIO in its startup constructor from sdkconfig.
  // Reconfiguring it here is rejected and prevents Arduino WiFi from starting.
  if (esp_hosted_init() != ESP_OK) {
    log_e("esp_hosted_init failed");
    return false;
  }
  hosted_initialized = true;
  return true;
}

bool hostedInitWiFi() {
  hosted_wifi_active = true;
  return hostedInit();
}

bool hostedDeinitWiFi() {
  hosted_wifi_active = false;
  if (hosted_initialized && esp_hosted_deinit() != ESP_OK) return false;
  hosted_initialized = false;
  return true;
}

bool hostedInitBLE() { return false; }
bool hostedDeinitBLE() { return true; }
bool hostedIsInitialized() { return hosted_initialized; }
bool hostedIsBLEActive() { return false; }
bool hostedIsWiFiActive() { return hosted_wifi_active; }

bool hostedSetPins(int8_t clk, int8_t cmd, int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t rst) {
  if (hosted_initialized || clk < 0 || cmd < 0 || d0 < 0 || d1 < 0 || d2 < 0 || d3 < 0 || rst < 0) return false;
  sdio_pins = (sdio_pin_config_t){clk, cmd, d0, d1, d2, d3, rst};
  return true;
}

void hostedGetPins(int8_t *clk, int8_t *cmd, int8_t *d0, int8_t *d1, int8_t *d2, int8_t *d3, int8_t *rst) {
  *clk = sdio_pins.pin_clk; *cmd = sdio_pins.pin_cmd; *d0 = sdio_pins.pin_d0;
  *d1 = sdio_pins.pin_d1; *d2 = sdio_pins.pin_d2; *d3 = sdio_pins.pin_d3; *rst = sdio_pins.pin_reset;
}

void hostedGetHostVersion(uint32_t *major, uint32_t *minor, uint32_t *patch) { *major = 1; *minor = 4; *patch = 7; }
void hostedGetSlaveVersion(uint32_t *major, uint32_t *minor, uint32_t *patch) { *major = 0; *minor = 0; *patch = 0; }
const char *hostedGetSlaveTargetName() { return CONFIG_ESP_HOSTED_IDF_SLAVE_TARGET; }
bool hostedHasUpdate() { return false; }
const char *hostedGetUpdateURL() { return ""; }
bool hostedBeginUpdate() { return false; }
bool hostedWriteUpdate(uint8_t *buf, uint32_t len) { (void)buf; (void)len; return false; }
bool hostedEndUpdate() { return false; }
bool hostedActivateUpdate() { return false; }

#endif
