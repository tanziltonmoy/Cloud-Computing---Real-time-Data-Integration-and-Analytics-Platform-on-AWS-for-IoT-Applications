#include <MQUnifiedsensor.h>
#include "secrets.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"

#define Board                   ("ESP-32")
#define Pin                     (36)  // ADC pin for MQ-3 sensor
#define ServoPin                (15)   // PWM capable pin for servo control
#define Type                    ("MQ-3")
#define Voltage_Resolution      (5)
#define ADC_Bit_Resolution      (12)
#define RatioMQ3CleanAir        (60)
#define AWS_IOT_PUBLISH_TOPIC   "esp32/publish"
#define AWS_IOT_SUBSCRIBE_TOPIC "esp32/subscribe"

MQUnifiedsensor MQ3(Board, Voltage_Resolution, ADC_Bit_Resolution, Pin, Type);

const int freq = 50; // Servo frequency (50Hz is standard)
const int servoChannel = 0; // Channel to use for PWM (0-15)
const int resolution = 10; // Timer resolution in bits (ESP32 supports 1-16 bits resolutions)
const int minPulseWidth = 544; // Minimum pulse width for servo in microseconds
const int maxPulseWidth = 2400; // Maximum pulse width for servo in microseconds
WiFiClientSecure net = WiFiClientSecure();
PubSubClient client(net);

void connectAWS() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Configure WiFiClientSecure to use the AWS IoT device credentials
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // Connect to the MQTT broker on the AWS endpoint
  client.setServer(AWS_IOT_ENDPOINT, 8883);
  Serial.println("Connecting to AWS IoT");
  while (!client.connect(THINGNAME)) {
    Serial.print(".");
    delay(100);
  }
  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }
  Serial.println("AWS IoT Connected!");
}

void setup() {
  Serial.begin(115200);
  connectAWS();
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
}
void loop() {
  if (!client.connected()) {
    Serial.println("Disconnected from AWS IoT. Attempting to reconnect...");
    connectAWS();
  }
  client.loop(); // This keeps the MQTT connection alive and handles incoming messages.

  MQ3.update(); // Update the MQ3 sensor reading
  float alcoholPPM = MQ3.readSensor(); // Read the current sensor value
  float alcoholmgpc = alcoholPPM * 15.0; // Convert the PPM to mg/dL or your desired unit
  
  // Prepare and publish the sensor data to AWS IoT
  publishMQ3Data(alcoholmgpc);

  // Decide and set the servo angle based on alcohol concentration
  if (alcoholmgpc < 50) {
    Serial.println("Status: Normal");
    setServoAngle(0); // Set servo to indicate "Normal" status
  } else {
    Serial.println("Status: Illegal to drive");
    setServoAngle(90); // Set servo to indicate "Illegal to drive" status
  }

  delay(5000); // A delay to prevent flooding AWS IoT with messages. Adjust as needed.
}

void publishMQ3Data(float alcoholmgpc) {
  if (!client.connected()) {
    Serial.println("Reconnecting to AWS IoT...");
    connectAWS();
  }

  StaticJsonDocument<200> doc;
  doc["alcohol_mg_per_cubic"] = alcoholmgpc;
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer); // Serialize the JSON document to a buffer
  
  if (client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer)) {
    Serial.print("Publish success: ");
    Serial.println(jsonBuffer);
  } else {
    Serial.println("Publish failed");
  }
}


void setServoAngle(int angle) {
  int dutyCycle = map(angle, 0, 180, minPulseWidth, maxPulseWidth) * pow(2, resolution) / 20000;
  ledcWrite(servoChannel, dutyCycle);
}


