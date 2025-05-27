#include <mutex>
#include <condition_variable>
#include <Bounce2.h>

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <FastLED.h>

#ifdef ARDUINO_USB_MODE
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#else
static const size_t MaxKeys = 6;

struct KeyReport {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[MaxKeys];
};

class USBHIDKeyboard {
public:
    void begin() {}
    void sendReport(KeyReport* report) {}
    size_t write(const uint8_t *buffer, size_t size) {
        return size;
    }
};

class USBHIDMouse {  
public:
    void begin() {}
    void move(int x, int y, int wheel, int pan) {}
    void press(int buttons) {}
    void release() {}
};
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

// Button configuration
const uint8_t BOOT_BUTTON_PIN = 0;  // ESP32-S3 boot button
Bounce2::Button bootButton = Bounce2::Button();

USBHIDKeyboard keyboard;
USBHIDMouse mouse;

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

LED statusLed;

class PairingConfirmation {
public:
    const unsigned long PAIRING_REQUEST_TIMEOUT = 30000;

    PairingConfirmation(Bounce2::Button& bootButton, LED& statusLed, USBHIDKeyboard& keyboard): 
        bootButton_(bootButton), statusLed_(statusLed), keyboard_(keyboard) {}

    bool wait_for_confirmation(uint32_t pin) {
        std::unique_lock<std::mutex> lock(mutex_);
        pairing_request_time_ = millis();
        is_pairing_requested_ = true;
        pin_ = pin;
        statusLed_.setBlink(CRGB::Yellow, CRGB::Black, 500);  // Blink yellow while waiting for confirmation
        cv_.wait(lock, [this] { return !is_pairing_requested_; });
        bool result = is_pairing_confirmed_;
        is_pairing_confirmed_ = false;
        return result;
    }

    void confirm() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_pairing_requested_) {
            is_pairing_confirmed_ = true;
            is_pairing_requested_ = false;
            cv_.notify_all();
            Serial.println("Pairing confirmed");
        }
    }

    void reject() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (is_pairing_requested_) {
            is_pairing_confirmed_ = false;
            is_pairing_requested_ = false;
            cv_.notify_all();
            Serial.println("Pairing rejected");
        }
    }

    void loop() {
        bootButton_.update();
        if (bootButton_.pressed()) {
            confirm();
        }

        std::unique_lock<std::mutex> lock(mutex_);
        if (is_pairing_requested_) {
            Serial.println("Pairing requested");
            if (pin_ != 0) {
                String pin_str = String("PIN: ") + String(pin_) + String("\n") + String("Press button on dongle to confirm\n");
                keyboard_.write((uint8_t*)pin_str.c_str(), pin_str.length());
                pin_ = 0;
            }
            if (millis() - pairing_request_time_ > PAIRING_REQUEST_TIMEOUT) {
                lock.unlock();
                reject();
            }
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool is_pairing_requested_{false};
    bool is_pairing_confirmed_{false};
    unsigned long pairing_request_time_ = 0;
    uint32_t pin_ = 0;
    Bounce2::Button& bootButton_;
    LED& statusLed_;
    USBHIDKeyboard& keyboard_;
};

PairingConfirmation pairingConfirmation(bootButton, statusLed, keyboard);

// Callback for device connection
class ServerCallbacks: public NimBLEServerCallbacks {
public:
    ServerCallbacks(LED& statusLed, PairingConfirmation& pairingConfirmation, NimBLEServer* server): 
        statusLed_(statusLed), pairingConfirmation_(pairingConfirmation), server_(server) {}
private:
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        Serial.println("Device connected");

        // Update connection parameters for better performance
        server->updateConnParams(connInfo.getConnHandle(), 6, 7, 0, 500);

        NimBLEDevice::startSecurity(connInfo.getConnHandle());
    }

    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        Serial.println("Device disconnected");

        statusLed_.setBlink(LED_ADVERTISING_COLOR, CRGB::Black, LED_ADVERTISING_BLINK_INTERVAL);

        // Restart advertising
        server->getAdvertising()->start();
    }

    void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) override {
        NimBLEDevice::injectConfirmPasskey(connInfo, pairingConfirmation_.wait_for_confirmation(pin));
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        Serial.println("Authentication complete");
        Serial.print("Bonded: ");
        Serial.println(connInfo.isBonded());
        Serial.print("Authenticated: ");
        Serial.println(connInfo.isAuthenticated());
        Serial.print("Encrypted: ");
        Serial.println(connInfo.isEncrypted());

        if (!connInfo.isBonded() || !connInfo.isAuthenticated() || !connInfo.isEncrypted()) {
            Serial.println("Authentication failed");
            server_->disconnect(connInfo);
        } else {
            statusLed_.setSolid(LED_CONNECTED_COLOR);
        }
    }

private:
    LED& statusLed_;
    PairingConfirmation& pairingConfirmation_;
    NimBLEServer* server_;
};

// Callback for keyboard characteristic
class KeyboardCallbacks: public NimBLECharacteristicCallbacks {
public:
    KeyboardCallbacks(LED& statusLed, USBHIDKeyboard& keyboard): 
        statusLed_(statusLed), keyboard_(keyboard) {}
private:
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& ) override {
        std::vector<uint8_t> value = pCharacteristic->getValue();
        const auto MaxKeys = 6;

        if (value.size() >= 2 && value.size() <= 1 + MaxKeys) {
            statusLed_.setTemp(LED_KEYBOARD_EVENT_COLOR);
            // Format: modifiers, key1 [, key2, key3, key4, key5, key6]
            KeyReport report;
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

            keyboard_.sendReport(&report);
        }
    }

private:
    LED& statusLed_;
    USBHIDKeyboard& keyboard_;
};

// Callback for mouse characteristic
class MouseCallbacks: public NimBLECharacteristicCallbacks {
public:
    MouseCallbacks(LED& statusLed, USBHIDMouse& mouse) : statusLed_(statusLed), mouse_(mouse) {}
private:
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& ) override {
        std::vector<uint8_t> value = pCharacteristic->getValue();

        if (value.size() >= 3 && value.size() <= 5) {
            statusLed_.setTemp(LED_MOUSE_EVENT_COLOR);
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

            mouse_.move(x, y, wheel, pan);
            if (buttons != 0) {
                mouse_.press(buttons);
            } else {
                mouse_.release();
            }
        }
    }

private:
    LED& statusLed_;
    USBHIDMouse& mouse_;
};

uint32_t getDeviceSerialNumber() {
    uint64_t chipid = ESP.getEfuseMac(); // Get chip ID from eFuse
    return chipid % 10000;
}

void setup() {
    Serial.begin(115200);
    
    // Setup boot button
    bootButton.attach(BOOT_BUTTON_PIN, INPUT_PULLUP);
    bootButton.interval(2);
    bootButton.setPressedState(LOW);
    
    statusLed.setup();

#ifdef ARDUINO_USB_MODE
    USB.begin();
#endif
    keyboard.begin();
    mouse.begin();
    
    String deviceName = String("Remote Input ") + String(getDeviceSerialNumber());
    
    // Initialize BLE
    NimBLEDevice::init(deviceName.c_str());
    
    NimBLEDevice::setPower(ESP_PWR_LVL_N0);

    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_DISPLAY);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setMTU(32);

    auto server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks(statusLed, pairingConfirmation, server));
   
    // Create BLE Service
    auto service = server->createService(SERVICE_UUID);
    
    // Create Keyboard Characteristic
    auto keyboardCharacteristic = service->createCharacteristic(
        KEYBOARD_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC
    );
    keyboardCharacteristic->setCallbacks(new KeyboardCallbacks(statusLed, keyboard));
    
    // Create Mouse Characteristic
    auto mouseCharacteristic = service->createCharacteristic(
        MOUSE_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC
    );
    mouseCharacteristic->setCallbacks(new MouseCallbacks(statusLed, mouse));
    
    // Start the service
    service->start();

    statusLed.setBlink(LED_ADVERTISING_COLOR, CRGB::Black, LED_ADVERTISING_BLINK_INTERVAL);

    // Start advertising
    auto advertising = server->getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setName(deviceName.c_str());
    advertising->enableScanResponse(true);
    advertising->setPreferredParams(6, 8);
    advertising->start();
    
    Serial.print(deviceName);
    Serial.println(" Dongle is Ready!");
}

void loop() {
    statusLed.loop();
    pairingConfirmation.loop();
    delay(100);
}