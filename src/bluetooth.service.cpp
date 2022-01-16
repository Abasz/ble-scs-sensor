#include <Arduino.h>

#include "NimBLEDevice.h"

#include "globals.h"
#include "bluetooth.service.h"

using std::array;
using std::to_string;

BluetoothService::BluetoothService()
{
}

bool BluetoothService::isDeviceConnected() const
{
    return (*NimBLEDevice::getServer()).getConnectedCount() > 0;
}

void BluetoothService::checkConnectedDevices()
{
    if (isDeviceConnected())
    {
        ledState = HIGH;
    }
    else
    {
        ledState = !ledState;
    }

    digitalWrite(GPIO_NUM_2, ledState);
}

void BluetoothService::setup() const
{
    setupBleDevice();
    setupConnectionIndicatorLed();
}

void BluetoothService::startBLEServer() const
{
    NimBLEDevice::getAdvertising()->start();
    Serial.println("Waiting a client connection to notify...");
}

void BluetoothService::stopServer() const
{
    NimBLEDevice::getAdvertising()->stop();
}

void BluetoothService::setBattery(byte batteryLevel) const
{
    NimBLEDevice::getServer()
        ->getServiceByUUID(BATTERY_SVC_UUID)
        ->getCharacteristic(BATTERY_LEVEL_UUID)
        ->setValue(batteryLevel);
}

void BluetoothService::notifyDragFactor(byte distance, byte dragFactor) const
{

    auto dragFactorCharacteristic = *NimBLEDevice::getServer()
                                         ->getServiceByUUID(CYCLING_SPEED_CADENCE_SVC_UUID)
                                         ->getCharacteristic(DRAG_FACTOR_UUID);
    if (dragFactorCharacteristic.getSubscribedCount() > 0)
    {
        dragFactorCharacteristic.setValue("DF=" + to_string(dragFactor) + ", Dist=" + to_string(distance));
        dragFactorCharacteristic.notify();
    }
}

void BluetoothService::notifyCsc(unsigned long lastRevTime, unsigned int revCount, unsigned long lastStrokeTime, unsigned short strokeCount) const
{
    auto cscCharacteristic = *NimBLEDevice::getServer()
                                  ->getServiceByUUID(CYCLING_SPEED_CADENCE_SVC_UUID)
                                  ->getCharacteristic(CSC_MEASUREMENT_UUID);

    if (cscCharacteristic.getSubscribedCount() > 0)
    {
        // execution time: 11-16 microsec
        // auto start = micros();
        unsigned short revTime = lround(lastRevTime / 1000000.0 * 1024) % UINT16_MAX;
        unsigned short strokeTime = lround(lastStrokeTime / 1000000.0 * 1024) % UINT16_MAX;
        // auto stop = micros();
        // Serial.print("Time stamp calc: ");
        // Serial.println(stop - start);

        // execution time: 0-1 microsec
        // auto start = micros();
        array<uint8_t, 11> temp = {
            FEATURES_FLAG[0],

            static_cast<byte>(revCount),
            static_cast<byte>(revCount >> 8),
            static_cast<byte>(revCount >> 16),
            static_cast<byte>(revCount >> 24),

            static_cast<byte>(revTime),
            static_cast<byte>(revTime >> 8),

            static_cast<byte>(strokeCount),
            static_cast<byte>(strokeCount >> 8),
            static_cast<byte>(strokeTime),
            static_cast<byte>(strokeTime >> 8)};

        // auto stop = micros();
        // Serial.print("data calc: ");
        // Serial.println(stop - start);

        // execution time: 28-35 microsec
        // auto start = micros();
        cscCharacteristic.setValue(temp);
        // auto stop = micros();
        // Serial.print("set value: ");
        // Serial.println(stop - start);

        // execution time: 1000-1600 microsec
        // start = micros();
        cscCharacteristic.notify();
        // stop = micros();
        // Serial.print("notify: ");
        // Serial.println(stop - start);
    }
}

void BluetoothService::setupBleDevice() const
{
    Serial.println("Initializing BLE device");

    NimBLEDevice::init("CSC-Sensor");
    NimBLEDevice::setPower(ESP_PWR_LVL_N8);

    Serial.println("Setting up Server");

    NimBLEDevice::createServer();

    setupServices();
    setupAdvertisment();
}

void BluetoothService::setupServices() const
{
    Serial.println("Setting up BLE Services");
    auto server = NimBLEDevice::getServer();
    auto batteryService = server->createService(BATTERY_SVC_UUID);
    auto cscService = server->createService(CYCLING_SPEED_CADENCE_SVC_UUID);
    auto deviceInfoService = server->createService(DEVICE_INFO_SVC_UUID);

    // Create a BLE Characteristic
    Serial.println("Setting up BLE Characteristics");

    batteryService->createCharacteristic(BATTERY_LEVEL_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    cscService->createCharacteristic(CSC_MEASUREMENT_UUID, NIMBLE_PROPERTY::NOTIFY);

    cscService
        ->createCharacteristic(CSC_FEATURE_UUID, NIMBLE_PROPERTY::READ)
        ->setValue(FEATURES_FLAG.data(), FEATURES_FLAG.size());

    cscService
        ->createCharacteristic(SENSOR_LOCATION_UUID, NIMBLE_PROPERTY::READ)
        ->setValue(&FEATURES_FLAG[1], 1);

    cscService->createCharacteristic(SC_CONTROL_POINT_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::INDICATE);

    cscService->createCharacteristic(DRAG_FACTOR_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

    deviceInfoService
        ->createCharacteristic(MANUFACTURER_NAME_SVC_UUID, NIMBLE_PROPERTY::READ)
        ->setValue("ZOCO BODY FIT");
    deviceInfoService
        ->createCharacteristic(MODEL_NUMBER_SVC_UUID, NIMBLE_PROPERTY::READ)
        ->setValue("AR-C2");
    deviceInfoService
        ->createCharacteristic(SERIAL_NUMBER_SVC_UUID, NIMBLE_PROPERTY::READ)
        ->setValue("20220104");
    deviceInfoService
        ->createCharacteristic(SOFTWARE_NUMBER_SVC_UUID, NIMBLE_PROPERTY::READ)
        ->setValue("0.1.0");

    Serial.println("Starting BLE Service");

    // Start the service
    batteryService->start();
    cscService->start();
    deviceInfoService->start();
    server->start();
}

void BluetoothService::setupAdvertisment() const
{
    auto pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setAppearance(BLE_APPEARANCE_CYCLING_SPEED_CADENCE);
    pAdvertising->addServiceUUID(CYCLING_SPEED_CADENCE_SVC_UUID);
}

void BluetoothService::setupConnectionIndicatorLed() const
{
    pinMode(GPIO_NUM_2, OUTPUT);

    timerAttachInterrupt(ledTimer, connectionLedIndicatorInterrupt, true);
    timerAlarmWrite(ledTimer, 1000000, true);
    timerAlarmEnable(ledTimer);
}