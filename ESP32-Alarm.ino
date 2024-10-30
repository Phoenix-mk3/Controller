#include <Wire.h> // i2c kommunikasjon
#include <WiFi.h> // wifi tilkobling med esp32
#include <PubSubClient.h> //for MQTT kommunikasjon
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <time.h> // Bibliotek for RTC (Real time clock)

const char* ssid = "your_SSID"; // Brukernavn og passord på nettverket
const char* password = "your_PASSWORD";
// MQTT Broker settings
const char* mqtt_server = "your_MQTT_broker_address"; // Dette blir IP adressen på Rasberry Pi'en/HUB'en.
const int mqtt_port = 1883; // MQTT er standard gjennom port 1883
const char* mqtt_user = "your_MQTT_username";  // Optional
const char* mqtt_password = "your_MQTT_password";  // Optional

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;  // Juster for riktig tidssone
const int daylightOffset_sec = 3600;

Adafruit_BME680 bme; // Sensorobjekt for BME688

WiFiClient espClient;  // WiFi-objekt for å håndtere ESP32s WiFi-tilkobling
PubSubClient client(espClient); // PubSubClient-objekt for MQTT-kommunikasjon, bruker WiFi-tilkoblingen
const int LIGHT_PIN = 13; // Endre etter hvilken pinne lyslenken er koblet til
String alarmTime = "";  // Lagre alarmtidspunkt lokalt

long lastPublishTime = 0;
char msg[100];

// Function to connect to WiFi
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP()); // Skriver ut IP-adressen til ESP32 på nettverket
}

void callback(char* topic, byte* payload, unsigned int length) { // Forbedret callbacken og gjort den lettere å forstå
  Serial.print("Message arrived [");
  Serial.print(topic); 
  Serial.print("] ");

  // Forenklet metode for å konvertere payload til String
  String message((char*)payload, length);
  Serial.println(message);

  if (String(topic) == "esp32/alarmtime") {
    alarmTime = message;
    Serial.println("Alarm set for: " + alarmTime);
  } else if (String(topic) == "esp32/alarmoff") {
    digitalWrite(LIGHT_PIN, LOW);
    Serial.println("Alarm turned off.");
  }
}
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");
      client.publish("status/esp32", "ESP32 connected"); // Abonner på nødvendige topics på nytt
      client.subscribe("esp32/commands");
      client.subscribe("esp32/alarmtime");
      client.subscribe("esp32/alarmoff");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  pinMode(LIGHT_PIN, OUTPUT);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);// Configure time with NTP

  if (!bme.begin()) { // Initialiserer BME sensor
    Serial.println("Could not find a valid BME688 sensor, check wiring!");
    while (1);
  }

  // Set up BME688 sensor
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150); // 320*C for 150 ms
  
}

void loop() {
  if (!client.connected()) { // Reconnect MQTT hvis den ikke er tilkoblet
    reconnect();
  }
  client.loop(); // Hold MQTT-tilkoblingen aktiv
  // publiserer data hvert 2 sek
  long now = millis();
  if (now - lastPublishTime > 2000) {
    lastPublishTime = now;

    if (!bme.performReading()) {
      Serial.println("Failed to perform reading from BME688 sensor");
      return;
    }

     // Konstruer meldingen med sensor-data i JSON-format
    String payload = "{\"temperature\": ";
    payload += String(bme.temperature);
    payload += ", \"humidity\": ";
    payload += String(bme.humidity);
    payload += ", \"pressure\": ";
    payload += String(bme.pressure / 100.0);  // Konverter fra Pascal til hPa
    payload += ", \"gas_resistance\": ";
    payload += String(bme.gas_resistance / 1000.0);  // Konverter til kOhm
    payload += "}";

      // Konverter String til char-array
    payload.toCharArray(msg, 100);

    // Publish data to the MQTT topic. Denne som går inn i MQTT in noden. 
    Serial.print("Publishing message: ");
    Serial.println(msg);
    client.publish("sensors/bme688", msg);
  }
   // Get the current time from the built-in RTC
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    // Check if the current time matches the alarm time
    if (!alarmTime.isEmpty()) {
      int alarmHour = alarmTime.substring(0, 2).toInt();
      int alarmMinute = alarmTime.substring(3, 5).toInt();

      if (timeinfo.tm_hour == alarmHour && timeinfo.tm_min == alarmMinute) {
        digitalWrite(LIGHT_PIN, HIGH);  // Skru på lyslenken (alarm)
        Serial.println("Alarm triggered!");
      }
    }
  } 
}

