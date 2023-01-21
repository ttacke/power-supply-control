#pragma once
#include <WiFiClient.h>
#include <ESP8266WiFi.h>

class Wlan {
public:
	WiFiClient client;
	const char* ssid;
	const char* pwd;
	Wlan(const char* ssid_param, const char* pwd_param) {
		ssid = ssid_param;
		pwd = pwd_param;
	}
	void connect() {
		Serial.println("Try to conect to WLAN " + (String) ssid);
		WiFi.mode(WIFI_STA);
		WiFi.begin(ssid, pwd);
		while (WiFi.status() != WL_CONNECTED) {
			delay(500);
			Serial.print(".");
		}
		Serial.println("\nWLAN Connected, IP address: " + WiFi.localIP().toString() + "\n");
	}
};
