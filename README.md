# Industrial Equipment Monitoring System

An IoT-based industrial equipment monitoring system developed using ESP32 for real-time monitoring of machine health parameters such as temperature, vibration, and current consumption. The system provides fault detection, local alerts, and remote monitoring capabilities to improve equipment reliability and reduce downtime.

---

## Project Overview

Industrial equipment failures can lead to unexpected downtime, maintenance costs, and productivity loss. This project continuously monitors critical machine parameters and alerts operators when abnormal conditions are detected.

The system acquires sensor data, processes it in real time, displays values on an LCD, and provides alert notifications when threshold values are exceeded.

---

## Features

- Real-time equipment health monitoring
- Temperature monitoring
- Vibration monitoring
- Current consumption monitoring
- LCD-based live parameter display
- Fault detection and alert generation
- Buzzer-based warning system
- IoT-based remote monitoring
- Threshold-based alarm management

---

## Hardware Components

- ESP32 Development Board
- ADXL345 Accelerometer Sensor
- ACS712 Current Sensor
- DS18B20 Temperature Sensor
- 16x2 LCD Display
- Buzzer
- DC Geared Motor

---

## Parameters Monitored

### Temperature
Monitors machine temperature to detect overheating conditions.

### Vibration
Detects abnormal vibrations that may indicate mechanical faults, imbalance, or wear.

### Current Consumption
Tracks motor current to identify overloads and electrical abnormalities.

---

## System Operation

1. Sensors continuously acquire machine parameters.
2. ESP32 processes the sensor readings.
3. Values are displayed on the LCD.
4. Thresholds are compared with measured values.
5. Fault conditions trigger alarms and notifications.
6. Equipment health status can be monitored remotely through the IoT dashboard.

---

## Technologies Used

- Embedded C/C++
- ESP32
- Arduino IDE
- Sensor Interfacing
- IoT Monitoring
- Real-Time Data Acquisition

---

## Applications

- Industrial Motor Monitoring
- Predictive Maintenance
- Equipment Health Assessment
- Factory Automation
- Industry 4.0 Systems

---

## Project Structure

```text
Industrial_Equipment_Monitoring_System/
│
├── code/       → ESP32 source code
├── images/     → Hardware setup and dashboard screenshots
├── circuit/    → Circuit diagrams and wiring
└── README.md
```

---

## Future Enhancements

- Cloud database integration
- Mobile application support
- MQTT communication
- Predictive maintenance using Machine Learning
- Historical trend analysis
- Automated maintenance scheduling

---

## Author

Augustin C Abraham

MSc Electronics  
Embedded Systems | IoT | Verilog HDL | Semiconductor Enthusiast
