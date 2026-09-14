#include <Adafruit_TinyUSB.h> // Resolves Serial / Adafruit_USBD_CDC linker error
#include <bluefruit.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEService        customService = BLEService(BLEUuid(SERVICE_UUID));
BLECharacteristic dataCharacteristic = BLECharacteristic(BLEUuid(CHARACTERISTIC_UUID));

char xiaoMacStr[18];
char xiaoIdStr[17];

void fetchDeviceInfo() {
  ble_gap_addr_t macAddr = Bluefruit.getAddr();
  snprintf(xiaoMacStr, sizeof(xiaoMacStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           macAddr.addr[5], macAddr.addr[4], macAddr.addr[3], 
           macAddr.addr[2], macAddr.addr[1], macAddr.addr[0]);

  uint32_t id1 = NRF_FICR->DEVICEID[0];
  uint32_t id2 = NRF_FICR->DEVICEID[1];
  snprintf(xiaoIdStr, sizeof(xiaoIdStr), "%08X%08X", (unsigned int)id1, (unsigned int)id2);
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  Bluefruit.begin();
  Bluefruit.setName("XIAO_BLE");

  fetchDeviceInfo();

  customService.begin();
  dataCharacteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_WRITE | CHR_PROPS_NOTIFY);
  dataCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  dataCharacteristic.setFixedLen(20);
  dataCharacteristic.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(customService);
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.start(0);

  Serial.println("System Initialized.");
}

void loop() {
  if (Bluefruit.connected()) {
    digitalWrite(LED_BLUE, HIGH);
    digitalWrite(LED_GREEN, LOW);

    static unsigned long lastNotify = 0;
    if (millis() - lastNotify >= 1000) {
      lastNotify = millis();
      String val = "Data: " + String(millis() / 1000);
      dataCharacteristic.notify(val.c_str(), val.length());
    }
  } else {
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_BLUE, LOW);
    delay(500);
    digitalWrite(LED_BLUE, HIGH);
    delay(500);
  }
}