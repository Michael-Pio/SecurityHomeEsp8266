#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <TZ.h>
#include <FS.h>
#include <LittleFS.h>
#include <CertStoreBearSSL.h>

#define pin_LED1 D5 //Indicates WIFI connection
#define pin_LED2 D6 //Indicates MQTT connection
#define pin_LED3 D7 //Indicates ARM State
#define pin_BUZZ D1 //Buzzer
#define pin_LDR A0


// Update these with values suitable for your network.
const char* ssid = "PioAndroid";
const char* password = "asdfghjkl";
const char* mqtt_server = "5ba3cc9682464c34888197ac60a3b249.s1.eu.hivemq.cloud";

// A single, global CertStore which can be used by all connections.
// Needs to stay live the entire time any of the WiFiClientBearSSLs
// are present.
BearSSL::CertStore certStore;

WiFiClientSecure espClient;
PubSubClient * client;
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (500)
char msg[MSG_BUFFER_SIZE];
int value = 0;

struct DeviceState{
  int Temp = 36,Humid = 60;
  bool isArm = true;
  bool Threat = false;
}deviceState;

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  
  digitalWrite(pin_LED1,HIGH);  //Inidicate that the device is connected

}


void setDateTime() {
  // You can use your own timezone, but the exact time is not used at all.
  // Only the date is needed for validating the certificates.
  configTime(TZ_Europe_Berlin, "pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(100);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println();

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.printf("%s %s", tzname[0], asctime(&timeinfo));
}


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  if(strcmp(topic, "MainGate001/App") == 0){
    //snprintf (msg, MSG_BUFFER_SIZE, "Temperature: %ld ,Humidity: %s ,isArm: %d ,Threat: %d", deviceState.Temp , deviceState.Humid, deviceState.isArm, deviceState.Threat);
    String msg = String(deviceState.Temp) + "," + String(deviceState.Humid) + "," + String(deviceState.isArm) + "," + String(deviceState.Threat);
    Serial.print("Publish message: ");
    Serial.println(msg);
    client->publish("MainGate001/Device/State", msg.c_str());
  }else if(strcmp(topic, "MainGate001/App/isARM") == 0){
    deviceState.isArm = (char)payload[0] == '1' ? true : false;
    snprintf(msg,MSG_BUFFER_SIZE,"value Updated isArm: %d", (char)payload[0] == '1' ? true : false );
    client->publish("MainGate001/Device/State", msg);
  }
}


 
void reconnect() {
  // Loop until we’re reconnected
  while (!client->connected()) {
    Serial.print("Attempting MQTT connection…");
    String clientId = "ESP8266Client - MyClient";
    // Attempt to connect
    // Insert your password
    if (client->connect(clientId.c_str(), "Michael", "Pio@2004")) {
      Serial.println("connected");
      // Once connected, publish an announcement…
      client->publish("MainGate001/Device/State", "hello ,Device is BackOnline");
      // … and resubscribe
      client->subscribe("MainGate001/App");
      client->subscribe("MainGate001/App/isARM");
      digitalWrite(pin_LED2,HIGH);
      
    } else { 
      Serial.print("failed, rc = ");
      Serial.print(client->state());
      Serial.println(" try again in 5 seconds");
      digitalWrite(pin_LED2,LOW);
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup() {
  pinMode(LED_BUILTIN, OUTPUT); 
  pinMode(pin_LED1,OUTPUT);
  pinMode(pin_LED2,OUTPUT);
  pinMode(pin_LED3,OUTPUT);
  pinMode(pin_BUZZ,OUTPUT);
  pinMode(pin_LDR,INPUT);
  delay(100);

  // When opening the Serial Monitor, select 9600 Baud
  Serial.begin(9600);
  delay(500);

  LittleFS.begin();
  setup_wifi();
  setDateTime();

  Serial.begin(9600);

  // you can use the insecure mode, when you want to avoid the certificates
  //espclient->setInsecure();

  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  Serial.printf("Number of CA certs read: %d\n", numCerts);
  if (numCerts == 0) {
    Serial.printf("No certs found. Did you run certs-from-mozilla.py and upload the LittleFS directory before running?\n");
    return; // Can't connect to anything w/o certs!
  }

  BearSSL::WiFiClientSecure *bear = new BearSSL::WiFiClientSecure();
  // Integrate the cert store with this connection
  bear->setCertStore(&certStore);

  client = new PubSubClient(*bear);

  client->setServer(mqtt_server, 8883);
  client->setCallback(callback);
}


void HandleIntrusion(){
  int ldr_value = analogRead(pin_LDR);
  Serial.println(ldr_value);
  if (ldr_value < 500) //Threat detected
  {
    //Check whether it is armed or not 

    if(deviceState.isArm == false){
      digitalWrite(pin_BUZZ , LOW);
      digitalWrite(pin_LED3,HIGH);
      delay(100);
      
    }else{
      //if it is armed
      digitalWrite(pin_BUZZ , HIGH);
      digitalWrite(pin_LED3,LOW);
      delay(4000);
      client->publish("MainGate001/Device/Intruder", "Threat Detected");
      Serial.println("Threat Detected");
      deviceState.Threat = true;
    }
  }else{
    deviceState.Threat = false;
    digitalWrite(pin_BUZZ , LOW);
    digitalWrite(pin_LED3,LOW);
    delay(100);
  }
}

void loop() {
  if (!client->connected()) {

    reconnect();
  }
  digitalWrite(pin_LED2,HIGH);
  client->loop();
  HandleIntrusion();
  
  unsigned long now = millis();
  if (now - lastMsg > 10000) {
    lastMsg = now;
    snprintf (msg, MSG_BUFFER_SIZE, "Alive");
    Serial.print("Publish message: ");
    Serial.println(msg);
    client->publish("MainGate001/Device/State", msg);

  }

}
