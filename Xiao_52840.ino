#include <bluefruit.h>

// Set UUIDs directly using string representation
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEService        customService = BLEService(BLEUuid(SERVICE_UUID));
BLECharacteristic customCharacteristic = BLECharacteristic(BLEUuid(CHARACTERISTIC_UUID));

int cnt = 0;
unsigned long lastNotifyTime = 0;

void setLedColor(bool red, bool green, bool blue) {
  // Built-in RGB LEDs on XIAO nRF52840 are active-LOW
  digitalWrite(LED_RED,   red   ? LOW : HIGH);
  digitalWrite(LED_GREEN, green ? LOW : HIGH);
  digitalWrite(LED_BLUE,  blue  ? LOW : HIGH);
}

void connect_callback(uint16_t conn_handle) {
  Serial.println("Device Connected!");
  setLedColor(false, true, false); // Solid GREEN when connected
}

void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
  Serial.println("Device Disconnected!");
  setLedColor(true, false, false); // Solid RED on disconnect
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  setLedColor(true, false, false);

  Serial.println("XIAO - DeviceID ..."+getXiaoDeviceID());
  // Initialize Bluefruit Stack
  Bluefruit.begin();
  Bluefruit.setName("XIAO_nRF52840_BLE");

  // Set Connection Callbacks
  Bluefruit.Periph.setConnectCallback(connect_callback);
  Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

  // Configure Service & Characteristic
  customService.begin();
  
  customCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  customCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  customCharacteristic.setFixedLen(20);
  customCharacteristic.begin();
  customCharacteristic.write("Hello World");

  // Start Advertising
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(customService);
  Bluefruit.Advertising.addName();
  
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244); // 0.625ms units
  Bluefruit.Advertising.setFastTimeout(30);   
  Bluefruit.Advertising.start(0);             
  
  Serial.println("Advertising started...");
}
String getXiaoDeviceID() {
  uint32_t id1 = NRF_FICR->DEVICEID[0];
  uint32_t id2 = NRF_FICR->DEVICEID[1];
  char buf[17];
  snprintf(buf, sizeof(buf), "%08X%08X", (unsigned int)id1, (unsigned int)id2);
  return String(buf);
}
void loop() {
    Serial.println("XIAO - DeviceID ..."+getXiaoDeviceID());

  if (Bluefruit.connected()) {
    unsigned long currentMillis = millis();

    if (currentMillis - lastNotifyTime >= 2000) {
      lastNotifyTime = currentMillis;

      String msg = "XIAO -- " + String(cnt);
      customCharacteristic.notify(msg.c_str(), msg.length());
      Serial.println(msg);
      cnt++;
    }
  } else {
    // Blink BLUE while advertising
    setLedColor(false, false, true);
    delay(500);
    setLedColor(false, false, false);
    delay(500);
  }
}