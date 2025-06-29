#include <mutex>
#include <condition_variable>
#include <Bounce2.h>

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#include <FastLED.h>

static const size_t MaxKeysInReport = 6;

#ifdef ARDUINO_USB_MODE
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#else

struct KeyReport {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[MaxKeysInReport];
};

class USBHIDKeyboard {
public:
    void begin() {}
    void sendReport(KeyReport* report) {}
    size_t write(const uint8_t *buffer, size_t size) {
        return size;
    }
    void write(uint8_t key) {}
    void releaseAll() {}
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
#define RemoteInputServiceUuid  "aa8713fe-6f22-4820-9edd-e8462b0762ea"
#define KeyboardCharUuid        "9eeba577-04b8-4dc6-aeef-a9ac12eddb68"
#define MouseCharUuid           "9eeba577-04b8-4dc6-aeef-a9ac12eddb69"
#define StatusCharUuid          "9eeba577-04b8-4dc6-aeef-a9ac12eddb6a"

// Device Information Service UUIDs
#define DisServiceUuid        "180A"  // Standard Device Information Service UUID
#define ManufacturerNameUuid  "2A29"  // Manufacturer Name String
#define ModelNumberUuid       "2A24"  // Model Number String
#define SerialNumberUuid      "2A25"  // Serial Number String
#define FirmwareRevUuid       "2A26"  // Firmware Revision String
#define HardwareRevUuid       "2A27"  // Hardware Revision String

class LedMode {
public:
    LedMode(CRGB onColor, CRGB offColor, unsigned long blinkInterval): 
        onColor_(onColor), offColor_(offColor), blinkInterval_(blinkInterval) {}
    explicit LedMode(CRGB onColor) : onColor_(onColor) {}

    CRGB onColor_;
    CRGB offColor_;
    unsigned long blinkInterval_ {0};
};

// LED Configuration
const LedMode LedAdvertisingMode = LedMode(CRGB::Blue, CRGB::Black, 1000);
const LedMode LedPairingMode = LedMode(CRGB::Yellow);
const LedMode LedPairingRejectedMode = LedMode(CRGB::Red);
const LedMode LedPairingConfirmedMode = LedMode(CRGB::Green);
const LedMode LedConnectedMode = LedMode(CRGB::Blue);
const CRGB LedKeyboardEventColor = CRGB::Red;
const CRGB LedMouseEventColor = CRGB::Green;

class LED {
public:
    void setup() {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint8_t LED_PIN = 21;
        const uint8_t NUM_LEDS = 1;
        const uint8_t LED_BRIGHTNESS = 30;
        FastLED.addLeds<WS2812, LED_PIN, GRB>(&led_, NUM_LEDS);
        FastLED.setBrightness(LED_BRIGHTNESS);
        led_ = CRGB::Black;
        FastLED.show();
    }

    void setVolatileColor(CRGB color) {
        std::lock_guard<std::mutex> lock(mutex_);
        led_ = color;
        FastLED.show();
    }

    void setMode(LedMode mode) {
        std::lock_guard<std::mutex> lock(mutex_);
        ledMode_ = mode;
        led_ = mode.onColor_;
        lastBlinkTime_ = millis();
        ledState_ = true;
        FastLED.show();
    }

    void loop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ledMode_.blinkInterval_ > 0) {
            if (lastBlinkTime_ + ledMode_.blinkInterval_ < millis()) {
                ledState_ = !ledState_;
                led_ = ledState_ ? ledMode_.onColor_ : ledMode_.offColor_;
                lastBlinkTime_ = millis();
                FastLED.show();
            }
        }
        else {
            if (led_ != ledMode_.onColor_) {
                led_ = ledMode_.onColor_;
                FastLED.show();
            }
        }
    }

private:
    CRGB led_;
    LedMode ledMode_{CRGB::Black};
    bool ledState_ {false};
    unsigned long lastBlinkTime_ {0};
    std::mutex mutex_;
};

class PairingConfirmation {
public:
    const unsigned long PAIRING_REQUEST_TIMEOUT = 30000;

    PairingConfirmation(Bounce2::Button& confirmButton, LED& statusLed, USBHIDKeyboard& keyboard): 
        confirmButton_(confirmButton), statusLed_(statusLed), keyboard_(keyboard) {}

    bool waitForConfirmation(uint32_t pin) {
        std::unique_lock<std::mutex> lock(mutex_);
        pairingRequestTime_ = millis();
        isPairingRequested_ = true;
        pin_ = pin;
        statusLed_.setMode(LedPairingMode);
        cv_.wait(lock, [this] { return !isPairingRequested_; });
        bool result = isPairingConfirmed_;
        isPairingConfirmed_ = false;
        return result;
    }

    void loop() {
        if (std::unique_lock<std::mutex> lock(mutex_); isPairingRequested_) {
            if (pin_ != 0) {
                writePin();
                pin_ = 0;
            }
            if (millis() - pairingRequestTime_ > PAIRING_REQUEST_TIMEOUT) {
                lock.unlock();
                Serial.println("Pairing request timeout");
                complete(false);
            }
            else {
                lock.unlock();
                if (confirmButton_.pressed()) {
                    complete(true);
                }
            }
        }
    }

private:
    void complete(bool confirmed) {
        erasePin();
        isPairingConfirmed_ = confirmed;
        isPairingRequested_ = false;
        cv_.notify_all();
        Serial.println(confirmed ? "Pairing confirmed" : "Pairing rejected");
        statusLed_.setMode(confirmed ? LedPairingConfirmedMode : LedPairingRejectedMode);
    }

    void writePin() {
        String pin_str = String("Pairing PIN: ") + String(pin_);
        pinStringLength_ = pin_str.length();
        keyboard_.releaseAll();
        keyboard_.write((uint8_t*)pin_str.c_str(), pinStringLength_);
    }

    void erasePin() {
        const uint8_t BackspaceKey = 0x08;
        for (int i = 0; i < pinStringLength_; i++) {
            keyboard_.write(BackspaceKey);
        }
        pinStringLength_ = 0;
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    bool isPairingRequested_{false};
    bool isPairingConfirmed_{false};
    unsigned long pairingRequestTime_{0};
    uint32_t pin_{0};
    size_t pinStringLength_{0};
    Bounce2::Button& confirmButton_;
    LED& statusLed_;
    USBHIDKeyboard& keyboard_;
};

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

        statusLed_.setMode(LedAdvertisingMode);
        server->getAdvertising()->start();
    }

    void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) override {
        Serial.println("Confirm passkey");
        bool result = pairingConfirmation_.waitForConfirmation(pin);
        Serial.print("Confirm passkey result: ");
        Serial.println(result);
        NimBLEDevice::injectConfirmPasskey(connInfo, result);
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
            statusLed_.setMode(LedConnectedMode);
        }
    }

private:
    LED& statusLed_;
    PairingConfirmation& pairingConfirmation_;
    NimBLEServer* server_;
};

class KeyboardCallbacks: public NimBLECharacteristicCallbacks {
public:
    KeyboardCallbacks(LED& statusLed, USBHIDKeyboard& keyboard): 
        statusLed_(statusLed), keyboard_(keyboard) {}
private:
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& ) override {
        std::vector<uint8_t> value = pCharacteristic->getValue();

        if (value.size() >= 2 && value.size() <= 1 + MaxKeysInReport) {
            statusLed_.setVolatileColor(LedKeyboardEventColor);
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

class MouseCallbacks: public NimBLECharacteristicCallbacks {
public:
    MouseCallbacks(LED& statusLed, USBHIDMouse& mouse) : statusLed_(statusLed), mouse_(mouse) {}
private:
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& ) override {
        std::vector<uint8_t> value = pCharacteristic->getValue();

        if (value.size() >= 3 && value.size() <= 5) {
            statusLed_.setVolatileColor(LedMouseEventColor);
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

class StatusCallbacks: public NimBLECharacteristicCallbacks {
public:
    void onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {
        uint8_t status = 1;
        pCharacteristic->setValue(&status, sizeof(status));
    }
};

String getDeviceSerialNumber() {
    uint64_t chipid = ESP.getEfuseMac(); // Get chip ID from eFuse
    auto serialNumber = String(chipid % 10000);
    while (serialNumber.length() < 4) {
        serialNumber = "0" + serialNumber;
    }
    return serialNumber;
}


// Button configuration
const uint8_t BootButtonPin = 0;  // ESP32-S3 boot button
Bounce2::Button bootButton = Bounce2::Button();

USBHIDKeyboard keyboard;
USBHIDMouse mouse;

LED statusLed;

PairingConfirmation pairingConfirmation(bootButton, statusLed, keyboard);

void setup() {
    Serial.begin(115200);
    
    // Setup boot button
    bootButton.attach(BootButtonPin, INPUT_PULLUP);
    bootButton.interval(2);
    bootButton.setPressedState(LOW);
    
    statusLed.setup();

#ifdef ARDUINO_USB_MODE
    USB.begin();
#endif
    keyboard.begin();
    mouse.begin();
    
    String deviceName = String("Remote Input ") + getDeviceSerialNumber();
    
    // Initialize BLE
    NimBLEDevice::init(deviceName.c_str());
    
    NimBLEDevice::setPower(ESP_PWR_LVL_N0);

    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_DISPLAY);
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setMTU(32);

    auto server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks(statusLed, pairingConfirmation, server));
   
    // Create BLE Service
    auto service = server->createService(RemoteInputServiceUuid);
    
    // Create Keyboard Characteristic
    auto keyboardCharacteristic = service->createCharacteristic(
        KeyboardCharUuid,
        NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC
    );
    keyboardCharacteristic->setCallbacks(new KeyboardCallbacks(statusLed, keyboard));
    
    // Create Mouse Characteristic
    auto mouseCharacteristic = service->createCharacteristic(
        MouseCharUuid,
        NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::WRITE_ENC
    );
    mouseCharacteristic->setCallbacks(new MouseCallbacks(statusLed, mouse));
    
    // Create Status Characteristic
    auto statusCharacteristic = service->createCharacteristic(
        StatusCharUuid,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::READ_ENC
    );
    statusCharacteristic->setCallbacks(new StatusCallbacks());
    
    // Start the service
    service->start();

    // Create Device Information Service
    auto disService = server->createService(DisServiceUuid);

    // Add Manufacturer Name characteristic
    auto manufacturerNameChar = disService->createCharacteristic(
        ManufacturerNameUuid,
        NIMBLE_PROPERTY::READ
    );
    manufacturerNameChar->setValue("radugin.com");

    // Add Model Number characteristic
    auto modelNumberChar = disService->createCharacteristic(
        ModelNumberUuid,
        NIMBLE_PROPERTY::READ
    );
    modelNumberChar->setValue("Remote Input Dongle");

    // Add Serial Number characteristic
    auto serialNumberChar = disService->createCharacteristic(
        SerialNumberUuid,
        NIMBLE_PROPERTY::READ
    );
    serialNumberChar->setValue(getDeviceSerialNumber());

    // Add Firmware Revision characteristic
    auto firmwareRevChar = disService->createCharacteristic(
        FirmwareRevUuid,
        NIMBLE_PROPERTY::READ
    );
    firmwareRevChar->setValue("1.0.0");

    // Add Hardware Revision characteristic
    auto hardwareRevChar = disService->createCharacteristic(
        HardwareRevUuid,
        NIMBLE_PROPERTY::READ
    );
    hardwareRevChar->setValue("1.0.0");

    // Start the Device Information Service
    disService->start();

    statusLed.setMode(LedAdvertisingMode);

    // Start advertising
    auto advertising = server->getAdvertising();
    advertising->addServiceUUID(RemoteInputServiceUuid);
    advertising->addServiceUUID(DisServiceUuid);
    advertising->setName(deviceName.c_str());
    advertising->enableScanResponse(true);
    advertising->setPreferredParams(6, 8);
    advertising->start();
    
    Serial.print(deviceName);
    Serial.println(" Dongle is Ready!");
}

void loop() {
    bootButton.update();
    statusLed.loop();
    pairingConfirmation.loop();
    delay(100);
}
