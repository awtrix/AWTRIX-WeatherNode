#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "node-conf.h"
#include "ccs811.h"
#include "ClosedCube_HDC1080.h"
#include "Adafruit_BMP280.h"
#include <Wire.h>
#include <FS.h>

WiFiClient espClient;
PubSubClient client(espClient);
CCS811 ccs811;
Adafruit_BMP280 bmp280;
ClosedCube_HDC1080 hdc1080;

unsigned long lastMsg = 0;
float fVoltage;
int i;
int perc = 100;
float fVoltageMatrix[22][2] = {
    {4.2, 100},
    {4.15, 95},
    {4.11, 90},
    {4.08, 85},
    {4.02, 80},
    {3.98, 75},
    {3.95, 70},
    {3.91, 65},
    {3.87, 60},
    {3.85, 55},
    {3.84, 50},
    {3.82, 45},
    {3.80, 40},
    {3.79, 35},
    {3.77, 30},
    {3.75, 25},
    {3.73, 20},
    {3.71, 15},
    {3.69, 10},
    {3.61, 5},
    {3.27, 0},
    {0, 0}};

void sendDataAndSleep()
{
    //Start I2C and Sensors
    Wire.begin();
    ccs811.set_i2cdelay(50);
    ccs811.begin();
    ccs811.start(CCS811_MODE_1SEC);
    bmp280.begin(0x76);
    hdc1080.begin(0x40);

    //Build JSON
    DynamicJsonDocument doc(300);
    doc["Name"] = nodename;
    float temp = bmp280.readTemperature();
    double hum = hdc1080.readHumidity();
    doc["Temp"] = temp;
    doc["Hum"] = hum;
    doc["Pres"] = (bmp280.readPressure() / 100);
    ccs811.set_envdata(temp, hum);
    uint16_t eco2, etvoc, errstat, raw;
    ccs811.read(&eco2, &etvoc, &errstat, &raw);
    if (errstat == CCS811_ERRSTAT_OK)
    {
        doc["CO2"] = eco2;
        doc["PPM"] = etvoc;
    }
    doc["Volt"] = fVoltage;
    doc["BatPerc"] = perc;
    String JS;
    serializeJson(doc, JS);

    //Send JSON to Server
    if (debug)
        Serial.println("Sending message to Server");
    Serial.println(JS.c_str());
    client.publish("awtrixnode/weather/data", JS.c_str());
    delay(200);
    //Deepsleep
    if (debug)
        Serial.println("Going to Sleep now");
    ESP.deepSleep(300e6);
}

void saveSettings()
{
    DynamicJsonDocument doc(1024);
    doc["ssid"] = ssid;
    doc["password"] = password;
    doc["server"] = awtrix_server;
    doc["nodename"] = nodename;
    doc["icon"] = iconID;
    doc["sleep"] = sleepinterval;
    Serial.println("save settings");
    File configFile = SPIFFS.open("/settings.json", "w+");
    serializeJson(doc, configFile);
    configFile.close();
}

void loadSettings()
{
    //if file not exists
    if (SPIFFS.exists("/settings.json"))
    {
        Serial.println("loading settings");
        File configFile = SPIFFS.open("/settings.json", "r");
        // Allocate a buffer to store contents of the file.
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, configFile);
        if (!error)
        {
            ssid = doc["ssid"];
            password = doc["password"];
            awtrix_server = doc["server"];
            nodename = doc["nodename"];
            iconID = doc["icon"];
            sleepinterval = doc["sleep"];
        }
        else
        {
            Serial.println(F("Error: deserializeJson"));
            Serial.println(error.c_str());
        }
        configFile.close();
    }
    else
    {
        Serial.println("loading default");
        saveSettings();
    }
    if (debug)
        Serial.println(nodename);
}
void callback(char *topic, byte *payload, unsigned int length)
{
    int last = String(topic).lastIndexOf(F("/")) + 1;
    String channel = String(topic).substring(last);
    if (channel.equals(F("newData")))
    {
        if (debug)
            Serial.println("got message");
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload);
        if (!error)
        {
            if (doc.containsKey("ssid"))
            {
                ssid = doc["ssid"];
            }
            if (doc.containsKey("password"))
            {
                password = doc["password"];
            }
            if (doc.containsKey("server"))
            {
                awtrix_server = doc["server"];
            }
            if (doc.containsKey("nodename"))
            {
                nodename = doc["nodename"];
            }
            if (doc.containsKey("icon"))
            {
                iconID = doc["icon"];
            }
            if (doc.containsKey("sleep"))
            {
                sleepinterval = doc["sleep"];
            }
            saveSettings();
        }
        else
        {
            if (debug)
            {
                Serial.print(F("deserializeJson() failed: \r\n"));
                Serial.println(error.c_str());
            }
        }
    }
}

void setup()
{
    Serial.begin(115200);
    SPIFFS.begin();
    loadSettings();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    if (debug)
        Serial.println("Connecting to Wifi");
    Serial.println(ssid);
    Serial.println(password);
    while (WiFi.status() != WL_CONNECTED)
    {
        if (debug)
            Serial.println("...");
        delay(500);
    }
    if (debug)
        Serial.println("Wifi connected");
    client.setServer(awtrix_server, 7001);
    if (debug)
        Serial.println("Connecting to server");
    if (client.connect(nodename))
    {
        if (debug)
            Serial.println("Connected to server");
        client.subscribe("awtrixnode/weather/#", 1);
        client.setCallback(callback);
    }
    else
    {
        if (debug)
            Serial.println("cannot connect to server");
    }

    lastMsg = millis();
    int nVoltageRaw = analogRead(A0);
    fVoltage = (float)nVoltageRaw * 0.00486;
    for (i = 20; i >= 0; i--)
    {
        if (fVoltageMatrix[i][0] >= fVoltage)
        {
            perc = fVoltageMatrix[i + 1][1];
            break;
        }
    }
}

void loop()
{
    client.loop();
    unsigned long now = millis();
    if (now - lastMsg > 5000)
    {
        sendDataAndSleep();
    }
}