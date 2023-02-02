#pragma once
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <Regexp.h>
// https://stackoverflow.com/questions/34078497/esp8266-wificlient-simple-http-get
// https://arduinogetstarted.com/reference/serial-readbytes

namespace Local {
	class WebClient {
	protected:
		WiFiClient wlan_client;
		char old_buffer[32];
		char search_buffer[64];
		int content_length = 0;
		bool remaining_content_after_header = true;
		MatchState match_state;

		bool _send_request(const char* host, const char* request_uri) {
			wlan_client.print("GET ");
			wlan_client.print(request_uri);
			wlan_client.print(" HTTP/1.1\r\nHost: ");
			wlan_client.print(host);
			wlan_client.print("\r\nConnection: close\r\n\r\n");

			int i = 0;
			while(!wlan_client.available()) {
				i++;
				if(i > 1000) {// 10s timeout
					return false;
				}
				delay(10);
			}
			return true;
		}

		bool _content_start_reached() {
			if(find_in_content((char*) "\r\n\r\n(.*)$")) {
				memcpy(buffer, finding_buffer, strlen(finding_buffer) + 1);
				return true;
			}
			return false;
		}

		void _read_content_length_header() {
			if(content_length == 0 && find_in_content((char*) "\r\nContent[-]Length: *([0-9]+)\r\n")) {
				content_length = atoi(finding_buffer);
			}
		}

		void _prepare_search_buffer() {
			int old_buffer_strlen = strlen(old_buffer);
			memcpy(search_buffer, old_buffer, old_buffer_strlen + 1);
			for(int i = 0; i < strlen(buffer) + 1; i++) {
				search_buffer[old_buffer_strlen + i] = buffer[i];
			}
		}

	public:
		const size_t buffer_length = 32;
		char buffer[32];
		char finding_buffer[33];

		WebClient(WiFiClient& wlan_client_param) {
			wlan_client = wlan_client_param;
		}

		bool find_in_content(char* regex) {
			match_state.Target(search_buffer);
			char result = match_state.Match(regex);
			if(result > 0) {
				match_state.GetCapture(finding_buffer, 0);
				return true;
			}
			return false;
		}

		void send_http_get_request(const char* host, const int port, const char* request_uri) {
			remaining_content_after_header = false;
			content_length = 0;
			buffer[0] = '\0';
			if(!wlan_client.connect(host, port)) {
				return;
			}
			if(!_send_request(host, request_uri)) {
				return;
			}

			old_buffer[0] = '\0';
			while(wlan_client.available()) {
				memcpy(old_buffer, buffer, strlen(buffer) + 1);
				int read_length = wlan_client.readBytes(buffer, buffer_length - 1);
				buffer[read_length] = '\0';
				_prepare_search_buffer();

				if(read_length < buffer_length - 1) {
					return;// Irgendwas lief schief
				} else {
					_read_content_length_header();
					if(_content_start_reached()) {
						remaining_content_after_header = true;
						return;
					}
				}
			}
		}

		bool read_next_block_to_buffer() {
			if(content_length <=0) {
				return false;
			}
			if(remaining_content_after_header) {
				remaining_content_after_header = false;
				content_length -= strlen(buffer);
				return true;
			}
			if(wlan_client.available()) {
				int read_length = wlan_client.readBytes(
					buffer,
					std::min((size_t) content_length, (size_t) (buffer_length - 1))
				);
				buffer[read_length] = '\0';
				content_length -= strlen(buffer);
				return true;
			}
			buffer[0] = '\0';
			content_length = 0;
			return false;
		}

		String get(const char* url) {// TODO DEPRECATED
			HTTPClient http;
			http.begin(wlan_client, url);
			http.addHeader("Content-Type", "application/json");
			http.setTimeout(15000);// ms, vermutlich
			int httpCode = http.GET();
			if (httpCode > 0) {
				if (httpCode == HTTP_CODE_OK) {
				  const String payload = http.getString();
				  http.end();
				  return payload;
			  } else {
				Serial.printf("[HTTP] GET failed, code: %d\n", httpCode);
			  }
			} else {
			  Serial.printf("[HTTP] GET failed, error: ");
			  Serial.printf(http.errorToString(httpCode).c_str());
			}
			http.end();
			return "";
		}
	};
}
