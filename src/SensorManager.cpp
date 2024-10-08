// Sensor Abstraction: Consider creating a base Sensor class with derived classes for different sensor types. 
// This would make it easier to add new types of sensors in the future.
// Calibration: For the moisture sensors, we might want to add a calibration mechanism.

#include "SensorManager.h"

SensorManager::SensorManager(ConfigManager& configManager)
    : configManager(configManager), 
    
      logger(Logger::instance()), 
      sensorTaskHandle(nullptr) {
        sizeMoistureData();
      }

void SensorManager::sensorTaskFunction(void* pvParameters) {
    SensorManager* manager = static_cast<SensorManager*>(pvParameters);
    int systemSize = manager->configManager.getHwConfig().systemSize.value();
    while (true) {
        manager->updateSensorData();
        uint32_t updateInterval = manager->configManager.getSwConfig().sensorUpdateInterval.value();
        vTaskDelay(pdMS_TO_TICKS(updateInterval));
    }
}

void SensorManager::startSensorTask() {
    if (sensorTaskHandle == nullptr) {
        xTaskCreatePinnedToCore(
            sensorTaskFunction,
            "SensorTask",
            4096,
            this,
            1,
            &sensorTaskHandle,
            1
        );
        logger.log("SensorManager", LogLevel::INFO, "Sensor task started");
    } else {
        logger.log("SensorManager", LogLevel::WARNING, "Sensor task already running");
    }
}

void SensorManager::setupFloatSwitch() {
    const auto& hwConfig = configManager.getHwConfig();
    floatSwitchPin = hwConfig.floatSwitchPin.value();
    pinMode(floatSwitchPin, INPUT_PULLUP);
    logger.log("SensorManager", LogLevel::INFO, "Float switch setup on pin %d", floatSwitchPin);
}

void SensorManager::setupSensors() {
    const auto& hwConfig = configManager.getHwConfig();
    Wire.begin(hwConfig.sdaPin.value(), hwConfig.sclPin.value());
    logger.log("SensorManager", LogLevel::INFO, "I2C initialized on SDA: %d, SCL: %d", hwConfig.sdaPin.value(), hwConfig.sclPin.value());

    if (!bmp.begin()) {
        logger.log("SensorManager", LogLevel::ERROR, "Could not find a valid BMP085 sensor, check wiring!");
        while (1) {}
    }
    logger.log("SensorManager", LogLevel::INFO, "BMP085 sensor initialized");

    for (size_t i = 0; i < hwConfig.systemSize.value(); ++i) {
        const auto& sensorConfig = configManager.getSensorConfig(i);
        const auto& sensorPins = configManager.getHwConfig().moistureSensorPins;
        if (sensorConfig.sensorEnabled) {
            pinMode(hwConfig.moistureSensorPins[i], INPUT);
            logger.log("SensorManager", LogLevel::INFO, "Moisture sensor %zu enabled on pin %d", i, sensorPins[i]);
        }
    }
}

void SensorManager::updateSensorData() {
    std::unique_lock<std::shared_mutex> lock(dataMutex);
    const auto& hwConfig = configManager.getHwConfig();
    
    for (size_t i = 0; i < hwConfig.systemSize.value(); ++i) {
        const bool sensorEnabled = configManager.getSensorConfig(i).sensorEnabled.value();
        if (sensorEnabled) {
            data.moisture[i] = readMoistureSensor(hwConfig.moistureSensorPins[i]);
        }
    }

    data.temperature = bmp.readTemperature();
    const auto& swConfig = configManager.getSwConfig();
    data.temperature += swConfig.tempOffset.value();
    data.pressure = bmp.readPressure() / 100.0F;
    data.waterLevel = checkWaterLevel();

    logger.log("SensorManager", LogLevel::DEBUG, "Sensor data updated: Temp: %.2f°C, Pressure: %.2f hPa, Water Level: %s", 
               data.temperature, data.pressure, data.waterLevel ? "OK" : "Low");
}

const SensorData& SensorManager::getSensorData() const {
    std::shared_lock<std::shared_mutex> lock(dataMutex);
    return data;
}

float SensorManager::readMoistureSensor(int sensorPin) {
    const int SAMPLES = 10;
    float sum = 0;
    for (int i = 0; i < SAMPLES; i++) {
        sum += analogRead(sensorPin);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    float average = sum / SAMPLES;
    // change the map function to match your sensor's range
    float moisturePercentage = map(average, 2592, 975, 0, 100);
    logger.log("SensorManager", LogLevel::DEBUG, "Moisture sensor on pin %d read: %.2f%%", sensorPin, moisturePercentage);
    return moisturePercentage;
}

bool SensorManager::checkWaterLevel() {
    vTaskDelay(pdMS_TO_TICKS(10));  // Delay for switch debounce if needed
    bool waterLevel = digitalRead(floatSwitchPin);
    logger.log("SensorManager", LogLevel::DEBUG, "Water level check: %s", waterLevel ? "Low" : "OK");
    return waterLevel;
}

void SensorManager::sizeMoistureData() {
    const auto size = configManager.getHwConfig().systemSize.value();
    data.moisture.resize(size);
}

TaskHandle_t SensorManager::getTaskHandle() const {
    return sensorTaskHandle;
}