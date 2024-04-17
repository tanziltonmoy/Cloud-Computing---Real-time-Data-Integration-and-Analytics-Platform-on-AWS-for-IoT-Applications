#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <MQUnifiedsensor.h>
#include <PubSubClient.h>
#include <Preferences.h> // Library to store preferences in non-volatile storage
#include "secrets.h"


// Define hardware settings
#define Board "ESP-32"
#define Pin 36  // ADC pin for MQ-3 sensor
#define Type "MQ-3"
#define Voltage_Resolution 5
#define ADC_Bit_Resolution 12
#define RatioMQ3CleanAir 60

// Servo settings
#define ServoPin 15  // PWM capable pin for servo control
#define freq 50
#define servoChannel 0
#define resolution 10
#define minPulseWidth 544
#define maxPulseWidth 2400


// Define constants for servo positions
const int SERVO_ON = 0;
const int SERVO_OFF = 90;

Preferences preferences;

int ignitionState;  // State of ignition, 0 = ON, 90 = OFF
int previousState = 90;   // Default to OFF so the initial change gets applied
String status;            // Status of driving legality

// WiFi and API settings
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
const char* googleApiUrl = "";
const char* shadowTopic = "$aws/things/ESP32_Alcohol/shadow/update";


// AWS IoT Core MQTT settings
WiFiClientSecure net;
PubSubClient client(net);
const char* awsEndpoint = AWS_IOT_ENDPOINT;
const int awsPort = 8883;
const char* mqttTopic = "esp32/publish";
const char* topic = "esp32/control";


// Initialize the MQ3 sensor
MQUnifiedsensor MQ3(Board, Voltage_Resolution, ADC_Bit_Resolution, Pin, Type);


void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println(message);

    StaticJsonDocument<256> doc;
    deserializeJson(doc, message);

    if (doc.containsKey("servo_angle")) {
        int receivedAngle = doc["servo_angle"];

        // Directly update ignition state and status based on received angle
        ignitionState = receivedAngle;
        status = (ignitionState == SERVO_ON) ? "Normal" : "Illegal to drive";

        // Update the status and servo angle variables
        int servoAngle = ignitionState; // Directly use the ignition state for the servo angle

        // Publish the updated data
        String locationData = getLocationData();
        publishData(0, status, servoAngle, locationData);

        preferences.putInt("ignition", ignitionState);
        preferences.putString("status", status);
        setServoAngle(servoAngle); // Apply servo angle directly from the payload
    }
}


void connectAWS() {
    net.setCACert(AWS_CERT_CA);
    net.setCertificate(AWS_CERT_CRT);
    net.setPrivateKey(AWS_CERT_PRIVATE);
    client.setServer(awsEndpoint, awsPort);

    while (!client.connected()) {
        Serial.print("Connecting to AWS IoT...");
        if (client.connect(THINGNAME)) {
            Serial.println("Connected");
            client.subscribe(topic, 1);  // Subscribe with QoS 1
            client.setCallback(callback);
        } else {
            Serial.print("Failed, rc=");
            Serial.println(client.state());
            delay(500);
        }
    }
}


void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
  Serial.println("Connected to WiFi");
  connectAWS();  // This now includes subscribing to the topic and setting the callback
  MQ3.setRegressionMethod(1);
  MQ3.setA(0.3934); MQ3.setB(-1.504);
  MQ3.init();

  // PWM setup for the servo
  ledcSetup(servoChannel, freq, resolution);
  ledcAttachPin(ServoPin, servoChannel);

  Serial.print("Calibrating please wait.");
  float calcR0 = 0;
  for(int i = 1; i <= 50; i++) {
    MQ3.update();
    calcR0 += MQ3.calibrate(RatioMQ3CleanAir);
    Serial.print(".");
  }
  MQ3.setR0(calcR0 / 50);
  MQ3.setR0(5);
  Serial.println("  done!.");

  preferences.begin("app", false);
  ignitionState = SERVO_ON; // Set initial state to normal driving
  preferences.putInt("ignition", ignitionState);
  preferences.putString("status", status);
  Serial.println("System initialized with servo at: " + String(ignitionState) + " and status: " + status);

}

void loop() {
    if (!client.connected()) {
        connectAWS();
    }
    client.loop();

    MQ3.update();
    float alcoholPPM = MQ3.readSensor();
    float alcoholmgpc = alcoholPPM * 15.0;

    Serial.print("Raw Alcohol PPM: ");
    Serial.println(alcoholPPM);
    Serial.print("Calculated Alcohol mg/cubic: ");
    Serial.println(alcoholmgpc);

    if (isnan(alcoholmgpc) || isinf(alcoholmgpc)) {
        alcoholmgpc = 20; // Assume high value for error
    }

    if (ignitionState == SERVO_ON) {
        // Check if a message payload was received to set servo angle to 0
        if (alcoholmgpc < 20) {
            status = "Normal";
            setServoAngle(0);
        } else {
            status = "Illegal to drive";
            setServoAngle(90);
        }
    }

    String locationData = getLocationData();
    publishData(alcoholmgpc, status, (status == "Normal") ? 0 : 90, locationData);

    if (ignitionState != previousState) {
        preferences.putInt("ignition", ignitionState);
        preferences.putString("status", status);
        previousState = ignitionState; // Save the last state after updating
    }

    delay(500); // Slight delay
}




void setServoAngle(int angle) {
    int dutyCycle = map(angle, 0, 180, minPulseWidth, maxPulseWidth) * pow(2, resolution) / 20000;
    ledcWrite(servoChannel, dutyCycle);
}

String getLocationData() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(googleApiUrl);
        http.addHeader("Content-Type", "application/json");

        DynamicJsonDocument json(1024);
        JsonArray wifiAccessPoints = json.createNestedArray("wifiAccessPoints");
        int n = WiFi.scanNetworks();
        for (int i = 0; i < n; ++i) {
            JsonObject wifi = wifiAccessPoints.createNestedObject();
            wifi["macAddress"] = WiFi.BSSIDstr(i);
            wifi["signalStrength"] = WiFi.RSSI(i);
        }

        String requestBody;
        serializeJson(json, requestBody);
        int httpResponseCode = http.POST(requestBody);

        if (httpResponseCode > 0) {
            String response = http.getString();
            http.end();
            return response;
        } else {
            http.end();
            return "{}"; // Return empty JSON if error
        }
    }
    return "{}";
}

void publishData(float alcoholmgpc, String status, int servoAngle, String locationData) {
    StaticJsonDocument<512> doc;
    doc["alcohol_mg_per_cubic"] = alcoholmgpc;
    doc["status"] = status;
    doc["servo_angle"] = servoAngle;
    doc["location"] = locationData;

    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);
    if (client.publish(mqttTopic, jsonBuffer)) {
        Serial.println("Publish success");
        Serial.println(jsonBuffer);
    } else {
        Serial.println("Publish failed");
    }
}



