#include <mutex>

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <FastLED.h>

#ifdef ARDUINO_USB_MODE
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#endif

// BLE Service and Characteristic UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define KEYBOARD_CHAR_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define MOUSE_CHAR_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26a9"

// LED Configuration
const unsigned long LED_ADVERTISING_BLINK_INTERVAL = 1000;
const CRGB LED_ADVERTISING_COLOR = CRGB::Blue;
const CRGB LED_CONNECTED_COLOR = CRGB::Blue;
const CRGB LED_KEYBOARD_EVENT_COLOR = CRGB::Red;
const CRGB LED_MOUSE_EVENT_COLOR = CRGB::Green;

NimBLEServer* Server = nullptr;

#ifdef ARDUINO_USB_MODE
USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;
#endif

class LED {
public:
    void setup() {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint8_t LED_PIN = 21;
        const uint8_t NUM_LEDS = 1;
        const uint8_t LED_BRIGHTNESS = 30;
        FastLED.addLeds<WS2812, LED_PIN, RGB>(&led_, NUM_LEDS);
        FastLED.setBrightness(LED_BRIGHTNESS);
        led_ = CRGB::Black;
        FastLED.show();
    }

    void setTemp(CRGB color) {
        std::lock_guard<std::mutex> lock(mutex_);
        blinkInterval_ = 0;
        led_ = color;
        FastLED.show();
    }

    void setSolid(CRGB color) {
        std::lock_guard<std::mutex> lock(mutex_);
        blinkInterval_ = 0;
        onColor_ = color;
        led_ = color;
        FastLED.show();
    }

    void setBlink(CRGB onColor, CRGB offColor, unsigned long interval) {
        std::lock_guard<std::mutex> lock(mutex_);
        onColor_ = onColor;
        offColor_ = offColor;
        blinkInterval_ = interval;
        lastBlinkTime_ = millis();
        ledState_ = true;
        led_ = onColor;
        FastLED.show();
    }

    void loop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (blinkInterval_ > 0) {
            if (lastBlinkTime_ + blinkInterval_ < millis()) {
                ledState_ = !ledState_;
                led_ = ledState_ ? onColor_ : offColor_;
                lastBlinkTime_ = millis();
                FastLED.show();
            }
        }
        else {
            if (led_ != onColor_) {
                led_ = onColor_;
                FastLED.show();
            }
        }
    }

private:
    CRGB led_;
    CRGB onColor_;
    CRGB offColor_;
    unsigned long blinkInterval_ = 0;
    bool ledState_ = false;
    unsigned long lastBlinkTime_ = 0;
    std::mutex mutex_;
};

LED Led;

// Callback for device connection
class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        Serial.println("Device connected");

        Led.setSolid(LED_CONNECTED_COLOR);
 
        // Update connection parameters for better performance
        Server->updateConnParams(connInfo.getConnHandle(), 6, 7, 0, 500);
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        Serial.println("Device disconnected");

        Led.setBlink(LED_ADVERTISING_COLOR, CRGB::Black, LED_ADVERTISING_BLINK_INTERVAL);

        // Restart advertising
        Server->getAdvertising()->start();

    }
};

// Callback for keyboard characteristic
class KeyboardCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& ) override {
        std::vector<uint8_t> value = pCharacteristic->getValue();
        const auto MaxKeys = 6;

        if (value.size() >= 2 && value.size() <= 1 + MaxKeys) {
            Led.setTemp(LED_KEYBOARD_EVENT_COLOR);
            // Format: modifiers, key1 [, key2, key3, key4, key5, key6]
#ifdef ARDUINO_USB_MODE
            KeyReport report;
#else
            struct KeyReport {
                uint8_t modifiers;
                uint8_t reserved;
                uint8_t keys[MaxKeys];
            };
            KeyReport report;
#endif
            memset(&report, 0, sizeof(report));
            report.modifiers = value[0];

            const auto keysSize = value.size() - 1;
            for (int i = 0; i < keysSize; i++) {
                report.keys[i] = value[i + 1];
            }

            Serial.print("Keyboard event: ");
            Serial.print(report.modifiers);
            for (int i = 0; i < keysSize; i++) {
                Serial.print(", ");
                Serial.print(report.keys[i]);
            }
            Serial.println();
#ifdef ARDUINO_USB_MODE
            Keyboard.sendReport(&report);
#endif
        }
    }
};

// Callback for mouse characteristic
class MouseCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& ) override {
        std::vector<uint8_t> value = pCharacteristic->getValue();

        if (value.size() >= 3 && value.size() <= 5) {
            Led.setTemp(LED_MOUSE_EVENT_COLOR);
            // Format: buttons, x, y, wheel, pan
            uint8_t buttons = value[0];
            int8_t x = value[1];
            int8_t y = value[2];
            int8_t wheel = (value.size() > 3) ? value[3] : 0;
            int8_t pan = (value.size() > 4) ? value[4] : 0;
            Serial.print("Mouse event: ");
            Serial.print(buttons);
            Serial.print(", ");
            Serial.print(x);
            Serial.print(", ");
            Serial.print(y);
            Serial.print(", ");
            Serial.print(wheel);
            Serial.print(", ");
            Serial.println(pan);
#ifdef ARDUINO_USB_MODE
            Mouse.move(x, y, wheel, pan);
            if (buttons != 0) {
                Mouse.press(buttons);
            } else {
                Mouse.release();
            }
#endif
        }
    }
};

void setup() {
    Serial.begin(115200);
    
    Led.setup();

#ifdef ARDUINO_USB_MODE
    USB.begin();
    Keyboard.begin();
    Mouse.begin();
#endif
    
    // Initialize BLE
    NimBLEDevice::init("Remote Input Dongle");
    
    NimBLEDevice::setPower(ESP_PWR_LVL_N0);

    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(false, true, true);

    NimBLEDevice::setMTU(32);

    Server = NimBLEDevice::createServer();
    Server->setCallbacks(new ServerCallbacks());
   
    // Create BLE Service
    auto service = Server->createService(SERVICE_UUID);
    
    // Create Keyboard Characteristic
    auto keyboardCharacteristic = service->createCharacteristic(
        KEYBOARD_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE_NR
    );
    keyboardCharacteristic->setCallbacks(new KeyboardCallbacks());
    
    // Create Mouse Characteristic
    auto mouseCharacteristic = service->createCharacteristic(
        MOUSE_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE_NR
    );
    mouseCharacteristic->setCallbacks(new MouseCallbacks());
    
    // Start the service
    service->start();

    Led.setBlink(LED_ADVERTISING_COLOR, CRGB::Black, LED_ADVERTISING_BLINK_INTERVAL);

    // Start advertising
    auto advertising = Server->getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setName("Remote Input Dongle");
    advertising->enableScanResponse(true);
    advertising->setPreferredParams(6, 8);
    advertising->start();
    
    Serial.println("Remote Input Dongle is Ready!");
}

void loop() {
    Led.loop();
    delay(100);
}