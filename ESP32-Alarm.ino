#include <Wire.h> // i2c kommunikasjon
#include <WiFi.h> // wifi tilkobling med esp32
#include <PubSubClient.h> //for MQTT kommunikasjon
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <time.h> // Bibliotek for RTC (Real time clock)
#include <Adafruit_NeoPixel.h>

const char* ssid = "YesWeCanWiFi"; // Brukernavn og passord på nettverket
const char* password = "nYdd,MZzz9j,ye";
// MQTT Broker settings
const char* mqtt_server = "192.168.1.229"; // Dette blir IP adressen på Rasberry Pi'en/HUB'en.
const int mqtt_port = 1883; // MQTT er standard gjennom port 1883

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;  // Juster for riktig tidssone
const int daylightOffset_sec = 3600;

Adafruit_BME680 bme; // Sensorobjekt for BME688

WiFiClient espClient;  // WiFi-objekt for å håndtere ESP32s WiFi-tilkobling
PubSubClient client(espClient); // PubSubClient-objekt for MQTT-kommunikasjon, bruker WiFi-tilkoblingen
// NeoPixel setup
#define LED_PIN 18       // Pin where NeoPixel is connected
#define LED_COUNT 50     // Number of LEDs in the strip

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
String alarmTime = "";  // Lagre alarmtidspunkt lokalt

bool isAlarmTriggered = false;  // Variabel for å holde styr på om alarmen er aktivert
bool isLEDOn = false;  // Variabel for å spore om LED-lysene allerede er på
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

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  String message((char*)payload, length);
  Serial.println(message);

  if (String(topic) == "esp32/alarmtime") {
    alarmTime = message;
    isAlarmTriggered = false; // Tilbakestill alarmutløst status
    Serial.println("Alarm set for: " + alarmTime);
  } else if (String(topic) == "esp32/alarmoff") {
    if (message.equalsIgnoreCase("off")) {
      Serial.println("Turning LEDs OFF");
      setAllPixels(0, 0, 0); // Turn off all LEDs
      isAlarmTriggered = false; // Tilbakestill alarmutløst status slik at den kan utløses igjen
      isLEDOn = false; // Sett lysene som av
    } else {
      Serial.println("Received alarmoff message but it's not 'OFF': " + message);
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client")) {
      Serial.println("connected");
      client.publish("status/esp32", "ESP32 connected"); // Abonner på nødvendige topics på nytt
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

void setAllPixels(uint8_t r, uint8_t g, uint8_t b) {
  // Reduser lysstyrken ved å dele med 2, eller bruk en annen faktor
  r /= 2;
  g /= 2;
  b /= 2;

  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  
  // Initialize NeoPixel strip
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); // Configure time with NTP

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
  
  // publiserer data hvert 5. sekund
  long now = millis();
  if (now - lastPublishTime > 5000) {
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

    // Publish data to the MQTT topic
    Serial.print("Publishing message: ");
    Serial.println(msg);
    client.publish("sensor/data", msg);
  }

  // Get the current time from the built-in RTC
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    // Check if the current time matches the alarm time
    if (!alarmTime.isEmpty() && !isAlarmTriggered) {
      int alarmHour = alarmTime.substring(0, 2).toInt();
      int alarmMinute = alarmTime.substring(3, 5).toInt();

      // Inkluder sekunder i sjekken for å unngå kontinuerlig utløsing av alarmen
      if (timeinfo.tm_hour == alarmHour && timeinfo.tm_min == alarmMinute && timeinfo.tm_sec == 0 && !isLEDOn) {
        // Turn on LEDs and set alarm as triggered
        Serial.println("Turning LEDs ON");
        setAllPixels(255, 255, 255); // Set all LEDs to white
        isAlarmTriggered = true;  // Sett alarmen som utløst
        isLEDOn = true; // Sett lysene som på
      }
    }
  }
}
