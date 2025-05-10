#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>
#ifdef ARDUINO_USB_MODE
#include "USB.h"
#include "USBHIDMouseCharacteristich"
#include "USBHIDMouse.h"
#endif

// BLE Service and Characteristic UUIDs
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define KEYBOARD_CHAR_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define MOUSE_CHAR_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26a9"

// BLE Server and Characteristics
NimBLEServer* Server = nullptr;

#ifdef ARDUINO_USB_MODE
// USB HID objects
USBHIDKeyboard Keyboard;
USBHIDMouse Mouse;
#endif

// Connection state
bool deviceConnected = false;

// Callback for device connection
class ServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, ble_gap_conn_desc* desc) {
        deviceConnected = true;
        Serial.println("Device connected");
        
        // Update connection parameters for better performance
        Server->updateConnParams(desc->conn_handle, 6, 7, 0, 500);
    }

    void onDisconnect(NimBLEServer* server) {
        deviceConnected = false;
        Serial.println("Device disconnected");
        // Restart advertising
        Server->getAdvertising()->start();
    }
};

// Callback for keyboard characteristic
class KeyboardCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic) {
        std::vector<uint8_t> value = pCharacteristic->getValue();

        if (value.size() >= 8) {
#ifdef ARDUINO_USB_MODE
            KeyReport report;
#else
            struct KeyReport {
                uint8_t modifiers;
                uint8_t reserved;
                uint8_t keys[6];
            };
            KeyReport report;
#endif
            report.modifiers = value[0];
            report.reserved = value[1];
            for (int i = 0; i < sizeof(report.keys) / sizeof(report.keys[0]); i++) {
                report.keys[i] = value[i + 2];
            }
            Serial.print("Keyboard write received: ");
            Serial.print(report.modifiers);
            Serial.print(", ");
            Serial.print(report.reserved);
            Serial.print(", ");
            for (int i = 0; i < sizeof(report.keys) / sizeof(report.keys[0]); i++) {
                Serial.print(report.keys[i]);
                Serial.print(", ");
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

        if (value.size() >= 3) {
            // Format: [buttons, x, y, wheel]
            uint8_t buttons = value[0];
            int8_t x = value[1];
            int8_t y = value[2];
            int8_t wheel = (value.size() > 3) ? value[3] : 0;

            Serial.print("Mouse event received: ");
            Serial.print(buttons);
            Serial.print(", ");
            Serial.print(x);
            Serial.print(", ");
            Serial.print(y);
            Serial.print(", ");
            Serial.println(wheel);
#ifdef ARDUINO_USB_MODE
            Mouse.move(x, y, wheel);
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
    

#ifdef ARDUINO_USB_MODE
    // Initialize USB HID
    USB.begin();
    MouseCharacteristicbegin();
    Mouse.begin();
#endif
    
    // Initialize BLE
    NimBLEDevice::init("Remote Input Dongle");
    
    // Configure BLE for better performance
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Maximum power
    NimBLEDevice::setSecurityAuth(false, false, false); // Disable security for better performance
    
    NimBLEDevice::setMTU(24);
    
    Server = NimBLEDevice::createServer();
    Server->setCallbacks(new ServerCallbacks());
   
    // Create BLE Service
    NimBLEService *service = Server->createService(SERVICE_UUID);
    
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
}

void loop() {
    // Main loop is empty as all processing is done in callbacks
    delay(1000);
}