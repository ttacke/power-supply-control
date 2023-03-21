#pragma once
#include "base_api.h"
#include "service/file_reader.h"
#include "wetter.h"
#include <TimeLib.h>

namespace Local {
	class WettervorhersageAPI: public BaseAPI {

	using BaseAPI::BaseAPI;

	protected:
		const char* hourly_filename = "wetter_stundenvorhersage.json";
		const char* hourly_cache_filename = "wetter_stundenvorhersage.csv";
		const char* dayly_filename = "wetter_tagesvorhersage.json";
		const char* dayly_cache_filename = "wetter_tagesvorhersage.csv";
		const char* hourly_request_uri = "/forecasts/v1/hourly/12hour/%d?apikey=%s&language=de-de&details=true&metric=true";
		const char* dayly_request_uri = "/forecasts/v1/daily/5day/%d?apikey=%s&language=de-de&details=true&metric=true";
		char request_uri[128];

		int zeitpunkt_sonnenuntergang = 0;
		int zeitpunkt_tage_liste[5];
		int solarstrahlung_tage_liste[5];
		int tage_anzahl = 5;
		int zeitpunkt_stunden_liste[12];
		int solarstrahlung_stunden_liste[12];
		int stunden_anzahl = 12;// warum ist sizeof(solarstrahlung_stunden_liste) hier ein OOM Fatal? Ist das doch ein pointer, weil ein array?

		void _reset(int* liste, size_t length) {
			for(int i = 0; i < length; i++) {
				liste[i] = 0;
			}
		}

		void _daten_holen_und_persistieren(Local::Service::FileReader& file_reader, const char* filename, const char* uri) {
			// geht der Abruf schief, wird die vorherige Datei zerstoehrt.
			// Der entstehende Schaden ist nicht relevant genug, um sich darum zu kuemmern
			// TODO das hier mit file_writer ersetzen
			if(!file_reader.open_file_to_overwrite(filename)) {
				return;
			}
			sprintf(request_uri, uri, cfg->accuweather_location_id, cfg->accuweather_api_key);
			web_reader->send_http_get_request(
				"dataservice.accuweather.com",
				80,
				request_uri
			);
			while(web_reader->read_next_block_to_buffer()) {
				memcpy(file_reader.buffer, web_reader->buffer, strlen(web_reader->buffer) + 1);
				file_reader.print_buffer_to_file();
			}
			file_reader.close_file();
			Serial.println("Wetterdaten geschrieben");
		}

		void _lese_stundendaten_und_setze_ein(Local::Service::FileReader& file_reader, int now_timestamp) {
			if(!file_reader.open_file_to_read(hourly_filename)) {
				return;
			}

			int i = -1;
			std::uint8_t findings = 0b0000'0001;
			int letzte_gefundene_zeit = 0;
			while(file_reader.read_next_block_to_buffer() && i < stunden_anzahl) {
				if(file_reader.find_in_buffer((char*) "\"EpochDateTime\":([0-9]+)[,}]")) {
					int zeitpunkt = atoi(file_reader.finding_buffer);
					if(
						letzte_gefundene_zeit != zeitpunkt // nur 1x behandeln
						&& now_timestamp < zeitpunkt + 1800 // Zu altes ueberspringen
					) {
						letzte_gefundene_zeit = zeitpunkt;
						i++;
						if(// das bezieht sich immer auf den Cache
							zeitpunkt_stunden_liste[i] == 0
							||
							zeitpunkt_stunden_liste[i] == zeitpunkt
						) {
							zeitpunkt_stunden_liste[i] = zeitpunkt;
							findings = 0b0000'0000;
						}
					}
				}

				if(
					!(findings & 0b000'0001) // Nur das erste passend zur gefundenen Zeit ermitteln
					// Nur den Ganzzahlwert, Nachkommastellen sind irrelevant
					&& file_reader.find_in_buffer((char*) "\"SolarIrradiance\":{[^}]*\"Value\":([0-9.]+)[,}]")
				) {
					solarstrahlung_stunden_liste[i] = round(atof(file_reader.finding_buffer));
					findings = 0b0000'0001;
				}
			}
			file_reader.close_file();
		}

		int _timestamp_to_date(int timestamp) {
			return day(timestamp) + (month(timestamp) * 100) + (year(timestamp) * 10000);
		}

		void _lese_tagesdaten_und_setze_ein(Local::Service::FileReader& file_reader, int now_timestamp) {
			if(!file_reader.open_file_to_read(dayly_filename)) {
				return;
			}

			int i = -1;
			std::uint8_t findings = 0b0000'0011;
			int letzte_gefundene_zeit = 0;
			int now_date = _timestamp_to_date(now_timestamp);
			zeitpunkt_sonnenuntergang = 0;
			while(file_reader.read_next_block_to_buffer() && i < tage_anzahl) {
				if(
					zeitpunkt_sonnenuntergang == 0
					&& file_reader.find_in_buffer((char*) "\"EpochSet\":([0-9]+)},\"Moon\"")
				) {
					int zeitpunkt = atoi(file_reader.finding_buffer);
					if(zeitpunkt >= now_timestamp || zeitpunkt > now_timestamp - 4 * 3600) {
						zeitpunkt_sonnenuntergang = zeitpunkt;
					}
				}
				if(file_reader.find_in_buffer((char*) "\"EpochDate\":([0-9]+)[,}]")) {
					int zeitpunkt = atoi(file_reader.finding_buffer);
					if(
						letzte_gefundene_zeit != zeitpunkt // nur 1x behandeln
						&& _timestamp_to_date(zeitpunkt) >= now_date // Zu altes ueberspringen
					) {
						letzte_gefundene_zeit = zeitpunkt;
						i++;
						if(// das bezieht sich immer auf den Cache
							zeitpunkt_tage_liste[i] == 0
							||
							zeitpunkt_tage_liste[i] == zeitpunkt
						) {
							zeitpunkt_tage_liste[i] = zeitpunkt;
							solarstrahlung_tage_liste[i] = 0;
							findings = 0b0000'0000;
						}
					}
				}
				// Nur den Ganzzahlwert, Nachkommastellen sind irrelevant
				if(
					!(findings & 0b0000'0001)
					&&
					file_reader.find_in_buffer((char*) "\"SolarIrradiance\":{[^}]*\"Value\":([0-9.]+)[,}]")
				) {
					// Tag & Nacht (das sind 2 gleich lautende Keys)
					solarstrahlung_tage_liste[i] += round(atof(file_reader.finding_buffer));
					findings |= 0b0000'0001;
				}
				if(file_reader.find_in_buffer((char*) "\"Night\":")) {
					// Erst wenn der Nacht-Uebergang gefunden wurde, nochmal auslesen erlauben
					findings = 0b0000'0000;
				}
			}
			file_reader.close_file();
		}

		void _lese_stundencache_und_setze_ein(Local::Service::FileReader& file_reader, int now_timestamp) {
			if(!file_reader.open_file_to_read(hourly_cache_filename)) {
				return;
			}
			int i = 0;
			while(file_reader.read_next_line_to_buffer() && i < stunden_anzahl) {
				if(file_reader.find_in_buffer((char*) "^([0-9]+),([0-9]+)$")) {
					int cache_zeit = atoi(file_reader.finding_buffer);
					if(now_timestamp - 1800 > cache_zeit) {
						continue;// Zu alt, ueberspringen
					}
					zeitpunkt_stunden_liste[i] = cache_zeit;
					file_reader.fetch_next_finding();
					solarstrahlung_stunden_liste[i] = atoi(file_reader.finding_buffer);
					i++;
				}
			}
			file_reader.close_file();
		}

		void _lese_tagescache_und_setze_ein(Local::Service::FileReader& file_reader, int now_timestamp) {
			if(!file_reader.open_file_to_read(dayly_cache_filename)) {
				return;
			}
			int i = 0;
			int now_date = _timestamp_to_date(now_timestamp);
			while(file_reader.read_next_line_to_buffer() && i < tage_anzahl) {
				if(file_reader.find_in_buffer((char*) "^([0-9]+),([0-9]+)$")) {
					int cache_zeit = atoi(file_reader.finding_buffer);
					if(_timestamp_to_date(cache_zeit) < now_date) {
						continue;// Zu alt, ueberspringen
					}
					zeitpunkt_tage_liste[i] = cache_zeit;
					file_reader.fetch_next_finding();
					solarstrahlung_tage_liste[i] = atoi(file_reader.finding_buffer);
					i++;
				}
			}
			file_reader.close_file();
		}

		void _schreibe_stundencache(Local::Service::FileReader& file_reader) {
			if(!file_reader.open_file_to_overwrite(hourly_cache_filename)) {
				return;
			}
			for(int i = 0; i < stunden_anzahl; i++) {
				sprintf(file_reader.buffer, "%d,%d\n", zeitpunkt_stunden_liste[i], solarstrahlung_stunden_liste[i]);
				file_reader.print_buffer_to_file();
			}
			file_reader.close_file();
		}

		void _schreibe_tagescache(Local::Service::FileReader& file_reader) {
			if(!file_reader.open_file_to_overwrite(dayly_cache_filename)) {
				return;
			}
			for(int i = 0; i < tage_anzahl; i++) {
				sprintf(file_reader.buffer, "%d,%d\n", zeitpunkt_tage_liste[i], solarstrahlung_tage_liste[i]);
				file_reader.print_buffer_to_file();
			}
			file_reader.close_file();
		}

	public:
		void stundendaten_holen_und_persistieren(Local::Service::FileReader& file_reader) {
			_daten_holen_und_persistieren(file_reader, hourly_filename, hourly_request_uri);
		}

		void tagesdaten_holen_und_persistieren(Local::Service::FileReader& file_reader) {
			_daten_holen_und_persistieren(file_reader, dayly_filename, dayly_request_uri);
		}

		void persistierte_daten_einsetzen(
			Local::Service::FileReader& file_reader, Local::Wetter& wetter, int now_timestamp
		) {
			_reset(zeitpunkt_stunden_liste, stunden_anzahl);
			_reset(solarstrahlung_stunden_liste, stunden_anzahl);
			wetter.stundenvorhersage_startzeitpunkt = 0;
			for(int i = 0; i < stunden_anzahl; i++) {
				wetter.setze_stundenvorhersage_solarstrahlung(i, 0);
			}

			_lese_stundencache_und_setze_ein(file_reader, now_timestamp);
			_lese_stundendaten_und_setze_ein(file_reader, now_timestamp);
			wetter.stundenvorhersage_startzeitpunkt = zeitpunkt_stunden_liste[0];
			for(int i = 0; i < stunden_anzahl; i++) {
				wetter.setze_stundenvorhersage_solarstrahlung(i, solarstrahlung_stunden_liste[i]);
			}
			_schreibe_stundencache(file_reader);

			_reset(zeitpunkt_tage_liste, tage_anzahl);
			_reset(solarstrahlung_tage_liste, tage_anzahl);
			wetter.tagesvorhersage_startzeitpunkt = 0;
			wetter.zeitpunkt_sonnenuntergang = 0;
			for(int i = 0; i < tage_anzahl; i++) {
				wetter.setze_tagesvorhersage_solarstrahlung(i, 0);
			}

			_lese_tagescache_und_setze_ein(file_reader, now_timestamp);
			_lese_tagesdaten_und_setze_ein(file_reader, now_timestamp);
			wetter.tagesvorhersage_startzeitpunkt = zeitpunkt_tage_liste[0];
			wetter.zeitpunkt_sonnenuntergang = zeitpunkt_sonnenuntergang;
			for(int i = 0; i < tage_anzahl; i++) {
				wetter.setze_tagesvorhersage_solarstrahlung(i, solarstrahlung_tage_liste[i]);
			}
			_schreibe_tagescache(file_reader);
		}
	};
}
