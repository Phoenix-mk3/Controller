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
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]); // innholdet i meldingen er bytes. så den blir konvertert til tekst
  }
  Serial.println();
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");
      client.publish("status/esp32", "ESP32 connected");
      client.subscribe("esp32/commands");
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
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // publiserer data hvert 2 sek
  long now = millis();
  if (now - lastMsg > 2000) {
    lastMsg = now;

    if (!bme.performReading()) {
      Serial.println("Failed to perform reading from BME688 sensor");
      return;
    }

    // Construct the message payload
    String payload = "{\"temperature\": ";
    payload += String(bme.temperature);
    payload += ", \"humidity\": ";
    payload += String(bme.humidity);
    payload += ", \"pressure\": ";
    payload += String(bme.pressure / 100.0);  // Convert from Pascals to hPa
    payload += ", \"gas_resistance\": ";
    payload += String(bme.gas_resistance / 1000.0);  // Convert to kOhms
    payload += "}";

    // Convert String to char array
    payload.toCharArray(msg, 100);

    // Publish data to the MQTT topic. Denne som går inn i MQTT in noden. 
    Serial.print("Publishing message: ");
    Serial.println(msg);
    client.publish("sensors/bme688", msg);
  }
}
