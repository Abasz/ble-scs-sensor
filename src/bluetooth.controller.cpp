#include <Arduino.h>

#include "bluetooth.controller.h"

BluetoothController::BluetoothController() : bluetoothService(BluetoothService())
{
}

bool BluetoothController::isAnyDeviceConnected() const
{
    return bluetoothService.isAnyDeviceConnected();
}

void BluetoothController::checkConnectedDevices()
{
    bluetoothService.checkConnectedDevices();
}

void BluetoothController::begin()
{
    bluetoothService.setup();
    bluetoothService.startBLEServer();
}

void BluetoothController::notifyBattery(byte batteryLevel) const
{
    bluetoothService.notifyBattery(batteryLevel);
}

void BluetoothController::notifyCsc(unsigned long lastRevTime, unsigned int revCount, unsigned long lastStrokeTime, unsigned short strokeCount) const
{
    bluetoothService.notifyCsc(lastRevTime, revCount, lastStrokeTime, strokeCount);
}

void BluetoothController::notifyDragFactor(byte dragFactor) const
{
    auto distance = pow(dragFactor / 2.8, 1.0 / 3.0) * (2.0 * PI) * 10;
    bluetoothService.notifyDragFactor(static_cast<byte>(distance), dragFactor);
}
