#include "config.h"
#include "wlan.h"
#include "web_client.h"
#include "web_presenter.h"

Local::Config cfg;
Local::Wlan wlan(cfg.wlan_ssid, cfg.wlan_pwd);
Local::WebClient web_client(wlan.client);
Local::WebPresenter web_presenter(cfg, wlan);

void setup(void) {
	Serial.begin(cfg.log_baud);
	delay(10);
	Serial.println();
	Serial.println();
	Serial.println("Setup ESP");
	wlan.connect();

	web_presenter.webserver.add_page("/master/", []() {
		web_presenter.zeige_ui();
	});
	web_presenter.webserver.add_page("/master/daten.json", []() {
		web_presenter.zeige_daten(true);
	});

	web_presenter.webserver.add_page("/", []() {
		web_presenter.zeige_ui();
	});
	web_presenter.webserver.add_page("/daten.json", []() {
		web_presenter.zeige_daten(false);
	});

	web_presenter.webserver.add_page("/download_file", []() {
		web_presenter.download_file();
	});
	web_presenter.webserver.start();
	Serial.printf("Free stack: %u heap: %u\n", ESP.getFreeContStack(), ESP.getFreeHeap());
}

void loop(void) {
	web_presenter.webserver.watch_for_client();
	delay(1000);// Ein bisschen ausgebremst ist er immer noch schnell genug
}
