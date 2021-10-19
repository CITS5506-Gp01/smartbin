/**
 * MIT License
 * 
 * Copyright (c) 2021 UWA-CITS5506-Gp01
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifdef ESP_ARDUINO

// setup() and loop() forward declared here
#include <Arduino.h>

#include "secrets.h"
#include "component/axp192.h"
#include "component/iot.h"
#include "component/neo6m.h"
#include "component/esp32wifi.h"
#include "component/trafficlight.h"
#include "component/hcsr04.h"
#include "component/mq2.h"
#include "component/scopedswitch.h"
#include "component/tmp36.h"
#include "component/button.h"
#include "component/debuglight.h"
#include "board/tbeamv1.h"
#include "streamextensions.h"

RTC_DATA_ATTR double calibrationDistance = 0.0;

constexpr int EXTERN_PWR = 4;
constexpr int TRIG = 15;
constexpr int ECHO = 39; // VN
constexpr int RED = 14;
constexpr int YELLOW = 13;
constexpr int GREEN = 2;
constexpr int CALIBRATE = 38;
constexpr int TMP = 36; // VP

void setup()
{
    Serial.begin(115200); // USB Monitor
    Axp192 axp;
    Neo6M gps(Serial2);
    HCSR04 ultrasonic(TRIG, ECHO);
    TMP36 thermo(TMP);
    TrafficLight traffic(RED, YELLOW, GREEN);
    DebugLights debug(axp);
    ScopedSwitch scoped_switch(EXTERN_PWR);
    Button calibrate(CALIBRATE);
    sleep(1);

    // Calibration Mode
    // Note: Local calibration only necessary for traffic light display 
    if(calibrate.is_down())
    {
        // Will calibrate in 20 seconds
        for(int i = 0; i < 200; i++)
        {
            long delay = ultrasonic.measure_distance();
            std::optional<double> distance = ultrasonic.get_distance_m();
            if(calibrate.is_down())
            {
                // can realtime demo distances with io38 here
                // calibration will be set after 20 seconds regardless
                if(distance.has_value())
                {
                    traffic.set_max_distance(distance.value());
                }
            }
            traffic.set_distance(distance.value());
            Serial << "dist: " << distance << ", pct: " << traffic.get_percent() << "\n";
            delayMicroseconds(std::max(0l, 100000l - delay));
        }

        calibrationDistance = ultrasonic.get_distance_m().value();
        traffic.set_max_distance(calibrationDistance);
        Serial.printf("Recalibrated distance to %f\n", calibrationDistance);
    }
    else
    {
        if(calibrationDistance == 0.0)
        {
            Serial.printf("Calibration distance not set, traffic module defaulting to %f\n", traffic.get_max_distance());
        }
        else
        {
            traffic.set_max_distance(calibrationDistance);
            Serial.printf("Calibrated distance is %f\n", calibrationDistance);
        }
    }

    try
    {
        // Read Distance
        std::optional<double> distance;
        long delay = ultrasonic.measure_distance();
        distance = ultrasonic.get_distance_m();
        
        if(distance.has_value())
        {
            traffic.set_distance(distance.value());
        }

        Serial << "dist: " << distance << ", pct: " << traffic.get_percent() << "\n";
        delayMicroseconds(std::max(0l, 100000l - delay));

        // Read Temperature
        float temp = thermo.read();
        Serial.printf("deg C: %f\n", temp);

        // Read GPS
        gps.read(10000);
        Serial.printf("sat: %i, lat: %f, long %f\n",
            gps.get().satellites.value(),
            gps.get().location.lat(),
            gps.get().location.lng());

        // Read Battery
        axp.getimpl().debugCharging();
        Serial.printf("batt voltage: %f\n", axp.getimpl().getBattVoltage());
        Serial.printf("batt discharge: %f\n", axp.getimpl().getBattDischargeCurrent());

        // Network Connect
        Esp32WiFi wifi(WIFI_SSID, WIFI_PWD);
        if (!wifi.is_connected())
        {
            throw std::runtime_error("failed to connected to wifi\n");
        }

        // IoT Record
        IoTMySQL iot(wifi.get_client(), DB_HOST, DB_PORT, DB_USR, DB_PWD);
        if(!iot.is_connected())
        {
            throw std::runtime_error("failed to connected to iot platform\n");
        }
        iot.insert_record(
            distance,
            temp,
            axp.get_battery_voltage(),
            gps.get_lat(),
            gps.get_lng()
        );
        Serial.println("Data record posted successfully");
        debug.disable_error();
    }
    catch(std::exception& e)
    {
        Serial.println(e.what());
        debug.enable_error();
    }
}

void loop()
{
    Serial.printf("sleeping for 10s...\n");
    esp_deep_sleep(10000000);
}

#endif
