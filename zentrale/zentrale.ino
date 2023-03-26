#include "config.h"
#include "service/wlan.h"
#include "service/web_reader.h"
#include "web_presenter.h"

Local::Config cfg;
Local::Service::Wlan wlan(cfg.wlan_ssid, cfg.wlan_pwd);
Local::Service::WebReader web_reader(wlan.client);
Local::WebPresenter web_presenter(cfg, wlan);
unsigned long runtime;
unsigned long last_runtime;
const char* daten_filename = "daten.json";

// TODO index.html -> ein iFrame drumherum und das wiederum liest Probleme aus
// und lädt ggf den iFrameInhalt neu: ist das noch noetig? Ist das aktuell ggf stabil genug?
void setup(void) {
	runtime = 0;
	last_runtime = 0;
	Serial.begin(cfg.log_baud);
	for(int i = 0; i < 5; i++) {
		Serial.print('o');
		delay(500);
	}
	Serial.println("Start strom-eigenverbrauch-optimierung Zentrale");
	wlan.connect();

	web_presenter.webserver.add_http_get_handler("/", []() {
		web_presenter.zeige_ui();
	});
	web_presenter.webserver.add_http_get_handler("/change", []() {
		web_presenter.aendere();
	});
	web_presenter.webserver.add_http_get_handler("/daten.json", []() {
		web_presenter.download_file(daten_filename);
	});
	web_presenter.webserver.add_http_get_handler("/debug-erzeuge-daten.json", []() {
		web_presenter.heartbeat(daten_filename);
		web_presenter.download_file(daten_filename);
	});
	web_presenter.webserver.add_http_get_handler("/download_file", []() {
		web_presenter.download_file((const char*) web_presenter.webserver.server.arg("name").c_str());
	});
	web_presenter.webserver.add_http_post_fileupload_handler("/upload_file", []() {
		web_presenter.upload_file();
	});
	web_presenter.webserver.start();
	Serial.printf("Free stack: %u heap: %u\n", ESP.getFreeContStack(), ESP.getFreeHeap());
}

void(* resetFunc) (void) = 0;// Reset the board via software

void loop(void) {
	runtime = millis();// Will overflow after ~50 days, but this is not a problem
	if(last_runtime == 0 || runtime - last_runtime > 60000) {// initial || 1min
		Serial.println("heartbeat!");
		web_presenter.heartbeat(daten_filename);
		last_runtime = runtime;
		return;
	}
	web_presenter.webserver.watch_for_client();
	delay(50);
}
