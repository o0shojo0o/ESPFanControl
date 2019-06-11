#include <DHTesp.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClient.h>

//// MQTT Config
String mqttServer = "<IP>";
String mqttMasterTopic = "Haus/Buero/ESPFanControl/";
int mqttPort = 1883;

// Wifi Config
const char *ssid = "";
const char *password = "";

volatile int interruptCounter; //counter use to detect hall sensor in fan
int lastRPM;
int RPM; //variable used to store computed value of fan RPM
unsigned long previousmills;
unsigned long previousmillsTemp;
int currentPrecent = 0;
int targetPrecent = 25;
int lastPrecent;
float temp;
float lastTemp;
float humidity;
float lastHumidity;

#define pwmOutputPin D3
#define tachoPin D4
#define calculationPeriod 1000 //Number of milliseconds over which to interrupts
#define DHTTYPE DHT22
#define DHTGROUND D6
const int DHTPin = D7;

WiFiClient espClient;
PubSubClient client(espClient);
DHTesp dht;

void setup()
{
	Serial.begin(115200);

	previousmills = 0;
	interruptCounter = 0;
	lastRPM = 1;
	RPM = 0;

	analogWriteFreq(25000);
	pinMode(tachoPin, INPUT_PULLUP);
	pinMode(pwmOutputPin, OUTPUT);
	pinMode(DHTGROUND, OUTPUT);

	digitalWrite(DHTGROUND, LOW);

	attachInterrupt(tachoPin, handleInterrupt, RISING);

	while (WiFi.waitForConnectResult() != WL_CONNECTED)
	{
		WiFi.begin(ssid, password);
		Serial.println("WiFi failed, retrying.");
	}

	client.setServer(mqttServer.c_str(), mqttPort);
	client.setCallback(callback);
	Serial.println(F("MQTT started"));

	dht.setup(DHTPin, DHTesp::DHT22);
}

void loop()
{
	if (!client.connected())
	{
		MqttReconnect();
	}
	client.loop();

	if ((millis() - previousmills) > calculationPeriod)
	{
		detachInterrupt(tachoPin);
		computeFanSpeed();
		displayFanSpeed();
		if (RPM != lastRPM)
		{
			client.publish((mqttMasterTopic + "fanRPM").c_str(), String(RPM).c_str());
			lastRPM = RPM;
		}

		if (currentPrecent != lastPrecent)
		{
			client.publish((mqttMasterTopic + "fanSpeed").c_str(), String(targetPrecent).c_str());
			lastPrecent = currentPrecent;
		}

		previousmills = millis();
		interruptCounter = 0;
		attachInterrupt(tachoPin, handleInterrupt, RISING);
	}

	if (targetPrecent != currentPrecent)
	{
		analogWrite(pwmOutputPin, CalcPWM(targetPrecent));
		currentPrecent = targetPrecent;
	}

	if ((millis() - previousmillsTemp) > 10 * 1000)
	{
		client.publish((mqttMasterTopic + "temperature").c_str(), String(dht.getTemperature()).c_str());
		client.publish((mqttMasterTopic + "humidity").c_str(), String(dht.getHumidity()).c_str());
		previousmillsTemp = millis();
	}
}

void ICACHE_RAM_ATTR handleInterrupt()
{ //This is the function called by the interrupt
	interruptCounter++;
}

void computeFanSpeed()
{
	//interruptCounter counts 2 pulses per revolution of the fan over a one second period
	RPM = (0.5) * (interruptCounter / (calculationPeriod / 1000)) * 60;
}

void displayFanSpeed()
{
	Serial.print(RPM, DEC); //Prints the computed fan speed to the serial monitor
	Serial.println(" RPM"); //Prints " RPM" and a new line to the serial monitor
}

int CalcPWM(int precent)
{
	return map(targetPrecent, 0, 100, 0, 1023);
}

void callback(char *topic, byte *payload, unsigned int length)
{
	payload[length] = '\0';
	String s_payload = String((char *)payload);
	String channel = String(topic);
	channel.replace(mqttMasterTopic, "");

	if (channel.equals("setFanSpeed"))
	{
		targetPrecent = s_payload.toInt();
	}
}

String GetChipID()
{
	return String(ESP.getChipId());
}

void MqttReconnect()
{
	// Loop until we're reconnected
	while (!client.connected())
	{
		// Attempt to connect
		if (client.connect(("ESPFanControl_" + GetChipID()).c_str()))
		{
			// ... and resubscribe
			client.subscribe((mqttMasterTopic + "setFanSpeed").c_str());
			// ... and publish
			client.publish((mqttMasterTopic + "chipid").c_str(), GetChipID().c_str(), true);
			client.publish((mqttMasterTopic + "ip").c_str(), WiFi.localIP().toString().c_str(), true);
		}
		else
		{
			// Wait 5 seconds before retrying
			delay(5000);
		}
	}
}