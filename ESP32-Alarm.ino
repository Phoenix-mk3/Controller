#include <Wire.h> // i2c kommunikasjon
#include <WiFi.h> // wifi tilkobling med esp32
#include <PubSubClient.h> //for MQTT kommunikasjon
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>

const char* ssid = "your_SSID"; // Brukernavn og passord på nettverket
const char* password = "your_PASSWORD";
// MQTT Broker settings
const char* mqtt_server = "your_MQTT_broker_address"; // Dette blir IP adressen på Rasberry Pi'en/HUB'en.
const int mqtt_port = 1883; // MQTT er standard gjennom port 1883
const char* mqtt_user = "your_MQTT_username";  // Optional
const char* mqtt_password = "your_MQTT_password";  // Optional

Adafruit_BME680 bme; // Sensorobjekt for BME688

WiFiClient espClient;  // WiFi-objekt for å håndtere ESP32s WiFi-tilkobling
PubSubClient client(espClient); // PubSubClient-objekt for MQTT-kommunikasjon, bruker WiFi-tilkoblingen
const int LIGHT_PIN = 13; // Endre etter hvilken pinne lyslenken er koblet til
String alarmTime = "";  // Lagre alarmtidspunkt lokalt

long lastMsg = 0;
char msg[100];
int value = 0;

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

void callback(char* topic, byte* payload, unsigned int length) { // Callback funksjon for innkommende meldinger fra MQTT servern. 
  Serial.print("Message arrived [");
  Serial.print(topic); // Skriver ut topic og meldingen hvor payload er meldingen.
  Serial.print("] ");
  // Omgjør payload til en String og skriver den ut
  String message;
  for (int i = 0; i < length; i++) {
    char c = (char)payload[i];
    Serial.print(c);
    message += c;   
    }
  Serial.println();
  // Hvis melding fra topic "esp32/alarmtime", lagre alarmtidspunktet
  if (String(topic) == "esp32/alarmtime") {
    alarmTime = message;
    Serial.println("Alarm set for: " + alarmTime);
  }
  // Hvis melding fra topic "esp32/alarmoff", slå av alarmen (lyslenken)
  if (String(topic) == "esp32/alarmoff") {
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
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.subscribe("esp32/alarmtime");
  client.subscribe("esp32/alarmoff");

  if (!bme.begin()) {
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
  if (now - lastMsg > 2000) {
    lastMsg = now;

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
  // Overvåk alarmtidspunktet og skru på lyslenken når alarmen skal gå av
  if (millis() >= convertTimeToMillis(alarmTime)) {
    digitalWrite(LIGHT_PIN, HIGH);  // Skru på lyslenken (alarm)
    Serial.println("Alarm triggered!");
  }
}

long convertTimeToMillis(String time) {
  // Forventet format: "HH:MM" (f.eks. "07:30")
  int hours = time.substring(0, 2).toInt();
  int minutes = time.substring(3, 5).toInt();
  return (hours * 3600000L) + (minutes * 60000L);  // Konverterer timer og minutter til millisekunder
}
