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
const CRGB LED_CONNECTED_COLOR = CRGB::Green;
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
        const uint8_t LED_PIN = 21;
        const uint8_t NUM_LEDS = 1;
        const uint8_t LED_BRIGHTNESS = 30;
        FastLED.addLeds<WS2812, LED_PIN, RGB>(&led, NUM_LEDS);
        FastLED.setBrightness(LED_BRIGHTNESS);
        led = CRGB::Black;
        FastLED.show();
    }

    void setTemp(CRGB color) {
        blinkInterval = 0;
        led = color;
        FastLED.show();
    }

    void setSolid(CRGB color) {
        blinkInterval = 0;
        this->onColor = onColor;
        led = color;
        FastLED.show();
    }

    void setBlink(CRGB onColor, CRGB offColor, unsigned long interval) {
        this->onColor = onColor;
        this->offColor = offColor;
        blinkInterval = interval;
        lastBlinkTime = millis();
        ledState = true;
        led = onColor;
        FastLED.show();
    }

    void loop() {
        if (blinkInterval > 0) {
            if (lastBlinkTime + blinkInterval < millis()) {
                ledState = !ledState;
                led = ledState ? onColor : offColor;
                lastBlinkTime = millis();
                FastLED.show();
            }
        }
        else {
            if (led != onColor) {
                led = onColor;
                FastLED.show();
            }
        }
    }

private:
    CRGB led;
    CRGB onColor;
    CRGB offColor;
    unsigned long blinkInterval = 0;
    bool ledState = false;
    unsigned long lastBlinkTime = 0;
};

LED led;

// Callback for device connection
class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) {
        Serial.println("Device connected");
        led.setSolid(CRGB::Blue);
 
        // Update connection parameters for better performance
        Server->updateConnParams(desc->conn_handle, 6, 7, 0, 500);
    }

    void onDisconnect(NimBLEServer* server) {
        Serial.println("Device disconnected");
        led.setBlink(LED_ADVERTISING_COLOR, CRGB::Black, LED_ADVERTISING_BLINK_INTERVAL);
        // Restart advertising
        Server->getAdvertising()->start();
    }
};

// Callback for keyboard characteristic
class KeyboardCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) {
        std::vector<uint8_t> value = pCharacteristic->getValue();
        const auto MaxKeys = 6;

        if (value.size() >= 2 && value.size() <= 1 + MaxKeys) {
            led.setTemp(LED_KEYBOARD_EVENT_COLOR);
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
    void onWrite(NimBLECharacteristic *pCharacteristic) {
        std::vector<uint8_t> value = pCharacteristic->getValue();

        if (value.size() >= 3 && value.size() <= 5) {
            led.setTemp(LED_MOUSE_EVENT_COLOR);
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
    
    led.setup();
    
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
        NIMBLE_PROPERTY::WRITE_NR  // Add WRITE_NR for better performance
    );
    keyboardCharacteristic->setCallbacks(new KeyboardCallbacks());
    
    // Create Mouse Characteristic
    auto mouseCharacteristic = service->createCharacteristic(
        MOUSE_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE_NR  // Add WRITE_NR for better performance
    );
    mouseCharacteristic->setCallbacks(new MouseCallbacks());
    
    // Start the service
    service->start();
    
    // Start advertising
    auto advertising = Server->getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setName("Remote Input Dongle");
    advertising->setScanResponse(true);
    advertising->setMinPreferred(6);
    advertising->setMaxPreferred(8);
    advertising->start();
    
    Serial.println("Remote Input Dongle is Ready!");
    led.setBlink(LED_ADVERTISING_COLOR, CRGB::Black, LED_ADVERTISING_BLINK_INTERVAL);
}

void loop() {
    led.loop();
    delay(100);    // Small delay to prevent too frequent updates
}