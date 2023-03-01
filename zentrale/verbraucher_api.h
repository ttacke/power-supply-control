#pragma once
#include "base_api.h"

namespace Local {
	class VerbraucherAPI: public BaseAPI {

	using BaseAPI::BaseAPI;

	protected:
		int timestamp;
		Local::Persistenz* persistenz;

		const char* heizung_relay_zustand_seit_filename = "heizung_relay.status";

		const char* wasser_relay_zustand_seit_filename = "wasser_relay.status";

		const char* roller_relay_zustand_seit_filename = "roller_relay.zustand_seit";
		const char* roller_ladestatus_filename = "roller.ladestatus";
		const char* roller_leistung_filename = "roller_leistung.status";
		const char* roller_leistung_log_filename = "roller_leistung.log";

		const char* auto_relay_zustand_seit_filename = "auto_relay.zustand_seit";
		const char* auto_ladestatus_filename = "auto.ladestatus";
		const char* auto_leistung_filename = "auto_leistung.status";
		const char* auto_leistung_log_filename = "auto_leistung.log";

		const char* ueberschuss_leistung_log_filename = "ueberschuss_leistung.log";

		bool _liste_enthaelt_mindestens(int* liste, int mindest_wert, int mindest_anzahl) {
			int anzahl = 0;
			for(int i = 0; i < 5; i++) {
				if(liste[i] >= mindest_wert) {
					anzahl++;
				}
			}
			return anzahl >= mindest_anzahl;
		}

		bool _auto_laden_ist_beendet(Local::Verbraucher& verbraucher) {
			if(
				verbraucher.auto_ladestatus == Local::Verbraucher::Ladestatus::off
				&& verbraucher.auto_relay_ist_an
			) {
				return true;
			}
			if(
				verbraucher.auto_relay_ist_an
				&& !_liste_enthaelt_mindestens(
					verbraucher.auto_ladeleistung_log_in_w,
					verbraucher.auto_benoetigte_ladeleistung_in_w * 0.9,
					3
				)
			) {
				return true;
			}
			return false;
		}

		bool _auto_schalte_solarstatus_automatisch(Local::Verbraucher& verbraucher) {
			if(verbraucher.auto_ladestatus != Local::Verbraucher::Ladestatus::solar) {
				return false;
			}

			bool auto_schalt_mindestdauer_ist_erreicht = timestamp - verbraucher.auto_relay_zustand_seit >= cfg->roller_min_schaltzeit_in_min * 60;
			if(
				verbraucher.auto_relay_ist_an
				&& auto_schalt_mindestdauer_ist_erreicht
				&& !_liste_enthaelt_mindestens(
					verbraucher.ueberschuss_log_in_w,
					verbraucher.auto_benoetigte_ladeleistung_in_w * 0.9,
					3
				)
				&& verbraucher.aktueller_akku_ladenstand_in_promille < 500
			) {
				_schalte_auto_relay(false);
				return true;
			} else if(
				!verbraucher.auto_relay_ist_an
				&& auto_schalt_mindestdauer_ist_erreicht
				&& (
					_liste_enthaelt_mindestens(
						verbraucher.ueberschuss_log_in_w,
						verbraucher.auto_benoetigte_ladeleistung_in_w * 0.9,
						3
					)
					|| verbraucher.aktueller_akku_ladenstand_in_promille > 700
				)
			) {
				_schalte_auto_relay(true);
				return true;
			}
			return false;
		}

		void _ermittle_relay_zustaende(Local::Verbraucher& verbraucher) {
			verbraucher.heizung_relay_ist_an = _netz_relay_ist_an(cfg->heizung_relay_host, cfg->heizung_relay_port);
			verbraucher.heizung_relay_zustand_seit = _lese_zustand_seit(heizung_relay_zustand_seit_filename);
			yield();// ESP-Controller zeit fuer interne Dinge (Wlan z.B.) geben

			verbraucher.wasser_relay_ist_an = _netz_relay_ist_an(cfg->wasser_relay_host, cfg->wasser_relay_port);
			verbraucher.wasser_relay_zustand_seit = _lese_zustand_seit(wasser_relay_zustand_seit_filename);
			yield();// ESP-Controller zeit fuer interne Dinge (Wlan z.B.) geben

			verbraucher.auto_relay_ist_an = _netz_relay_ist_an(cfg->auto_relay_host, cfg->auto_relay_port);
			verbraucher.auto_relay_zustand_seit = _lese_zustand_seit(auto_relay_zustand_seit_filename);
			yield();// ESP-Controller zeit fuer interne Dinge (Wlan z.B.) geben

			verbraucher.roller_relay_ist_an = _shellyplug_ist_an(cfg->roller_relay_host, cfg->roller_relay_port);
			verbraucher.roller_relay_zustand_seit = _lese_zustand_seit(roller_relay_zustand_seit_filename);
			yield();// ESP-Controller zeit fuer interne Dinge (Wlan z.B.) geben
		}

		bool _netz_relay_ist_an(const char* host, int port) {
			web_client->send_http_get_request(
				host,
				port,
				"/relay"
			);
			while(web_client->read_next_block_to_buffer()) {
				if(web_client->find_in_content((char*) "\"ison\":true")) {
					return true;
				}
			}
			return false;
		}

		void _schalte_netz_relay(bool ein, const char* host, int port) {
			web_client->send_http_get_request(
				host,
				port,
				(ein ? "/relay?set_relay=true" : "/relay?set_relay=false")
			);
		}

		bool _shellyplug_ist_an(const char* host, int port) {
			web_client->send_http_get_request(
				host,
				port,
				"/status"
			);
			while(web_client->read_next_block_to_buffer()) {
				if(web_client->find_in_content((char*) "\"ison\":true")) {
					return true;
				}
			}
			return false;
		}

		void _schalte_shellyplug(bool ein, const char* host, int port) {
			web_client->send_http_get_request(
				host,
				port,
				(ein ? "/relay/0?turn=on" : "/relay/0?turn=off")
			);
		}

		int _gib_aktuelle_shellyplug_leistung(const char* host, int port) {
			web_client->send_http_get_request(
				host,
				port,
				"/status"
			);
			while(web_client->read_next_block_to_buffer()) {
				if(web_client->find_in_content((char*) "\"power\":([0-9]+)")) {
					return atoi(web_client->finding_buffer);
				}
			}
			return 0;
		}

		int _gib_auto_benoetigte_ladeleistung_in_w() {
			int leistung = cfg->auto_benoetigte_leistung_gering_in_w;
			if(persistenz->open_file_to_read(auto_leistung_filename)) {
				while(persistenz->read_next_block_to_buffer()) {
					if(persistenz->find_in_content((char*) "([0-9]+)")) {
						int i = atoi(persistenz->finding_buffer);
						if(i > 0) {
							leistung = i;
						}
					}
				}
				persistenz->close_file();
			}
			return leistung;
		}

		void _schalte_roller_relay(bool ein) {
			_schalte_shellyplug(ein, cfg->roller_relay_host, cfg->roller_relay_port);
			if(persistenz->open_file_to_overwrite(roller_relay_zustand_seit_filename)) {
				sprintf(persistenz->buffer, "%d", timestamp);
				persistenz->print_buffer_to_file();
				persistenz->close_file();
			}
		}

		void _schalte_auto_relay(bool ein) {
			_schalte_netz_relay(ein, cfg->auto_relay_host, cfg->auto_relay_port);
			if(persistenz->open_file_to_overwrite(auto_relay_zustand_seit_filename)) {
				sprintf(persistenz->buffer, "%d", timestamp);
				persistenz->print_buffer_to_file();
				persistenz->close_file();
			}
		}

		int _lese_zustand_seit(const char* filename) {
			int seit = 0;
			if(persistenz->open_file_to_read(filename)) {
				while(persistenz->read_next_block_to_buffer()) {
					if(persistenz->find_in_content((char*) "([0-9]+)")) {
						seit = atoi(persistenz->finding_buffer);
					}
				}
				persistenz->close_file();
			}
			return seit;
		}

		int _gib_roller_benoetigte_leistung_in_w() {
			int leistung = cfg->roller_benoetigte_leistung_hoch_in_w;
			if(persistenz->open_file_to_read(roller_leistung_filename)) {
				while(persistenz->read_next_block_to_buffer()) {
					if(persistenz->find_in_content((char*) "([0-9]+)")) {
						int i = atoi(persistenz->finding_buffer);
						if(i > 0) {
							leistung = i;
						}
					}
				}
				persistenz->close_file();
			}
			return leistung;
		}

		void _lies_verbraucher_log(int* liste, const char* log_filename) {
			for(int i = 0; i < 5; i++) {
				liste[i] = 0;
			}
			if(persistenz->open_file_to_read(log_filename)) {
				while(persistenz->read_next_block_to_buffer()) {
					char suche[16] = "";
					for(int i = 0; i < 5; i++) {
						sprintf(suche, ">%d=([0-9]+)<", i);
						if(persistenz->find_in_content((char*) suche)) {
							liste[i] = atoi(persistenz->finding_buffer);
						}
					}
				}
				persistenz->close_file();
			}
		}

		void _schreibe_verbraucher_log(int* liste, int aktuell, const char* log_filename) {
			int tmp;
			for(int i = 0; i < 5; i++) {
				if(i > 0) {
					liste[i - 1] = tmp;
				}
				tmp = liste[i];
			}
			liste[4] = aktuell;

			if(persistenz->open_file_to_overwrite(log_filename)) {
				for(int i = 0; i < 5; i++) {
					sprintf(persistenz->buffer, ">%d=%d<", i, liste[i]);
					persistenz->print_buffer_to_file();
				}
				persistenz->close_file();
			}
		}

		void _lese_ladestatus(Local::Verbraucher::Ladestatus& ladestatus, const char* filename) {
			ladestatus = Local::Verbraucher::Ladestatus::off;
			if(persistenz->open_file_to_read(filename)) {
				while(persistenz->read_next_block_to_buffer()) {
					if(persistenz->find_in_content((char*) "([a-z]+)")) {
						if(strcmp(persistenz->finding_buffer, "solar")) {
							ladestatus = Local::Verbraucher::Ladestatus::solar;
						} else if(strcmp(persistenz->finding_buffer, "solar")) {
							ladestatus = Local::Verbraucher::Ladestatus::force;
						}
						return;
					}
				}
				persistenz->close_file();
			}
		}

	public:
		VerbraucherAPI(
			Local::Config& cfg,
			Local::WebClient& web_client,
			Local::Persistenz& persistenz,
			int timestamp
		): BaseAPI(cfg, web_client), persistenz(&persistenz), timestamp(timestamp) {
		}

		void daten_holen_und_einsetzen(
			Local::Verbraucher& verbraucher,
			Local::ElektroAnlage& elektroanlage,
			Local::Wetter wetter
		) {
			_ermittle_relay_zustaende(verbraucher);

			verbraucher.aktuelle_auto_ladeleistung_in_w = round(elektroanlage.l3_strom_ma / 1000 * 230);
			_lies_verbraucher_log(verbraucher.auto_ladeleistung_log_in_w, auto_leistung_log_filename);
			_schreibe_verbraucher_log(verbraucher.auto_ladeleistung_log_in_w, verbraucher.aktuelle_auto_ladeleistung_in_w, auto_leistung_log_filename);
			verbraucher.auto_benoetigte_ladeleistung_in_w = _gib_auto_benoetigte_ladeleistung_in_w();

			verbraucher.aktuelle_roller_ladeleistung_in_w = _gib_aktuelle_shellyplug_leistung(cfg->roller_relay_host, cfg->roller_relay_port);
			_lies_verbraucher_log(verbraucher.roller_ladeleistung_log_in_w, roller_leistung_log_filename);
			_schreibe_verbraucher_log(verbraucher.roller_ladeleistung_log_in_w, verbraucher.aktuelle_roller_ladeleistung_in_w, roller_leistung_log_filename);
			verbraucher.roller_ladeleistung_in_w = _gib_roller_benoetigte_leistung_in_w();

			verbraucher.aktueller_ueberschuss_in_w = elektroanlage.gib_ueberschuss_in_w();
			_lies_verbraucher_log(verbraucher.ueberschuss_log_in_w, ueberschuss_leistung_log_filename);
			_schreibe_verbraucher_log(verbraucher.ueberschuss_log_in_w, verbraucher.aktueller_ueberschuss_in_w, ueberschuss_leistung_log_filename);

			verbraucher.aktueller_akku_ladenstand_in_promille = elektroanlage.solarakku_ladestand_in_promille;

			_lese_ladestatus(verbraucher.auto_ladestatus, auto_ladestatus_filename);
			_lese_ladestatus(verbraucher.roller_ladestatus, roller_ladestatus_filename);
		}

		void fuehre_lastmanagement_aus(Local::Verbraucher& verbraucher) {
			// TODO status==force && relay aus -> status aus + schreiben in Datei
			// verbraucher.auto_relay_ist_an / verbraucher.roller_relay_ist_an

			// TODO Auto/Roller/wasser/heiz: ist in den letzten 5min irgendwas geschalten worden? Wenn ja, raus!

			// TODO alles arbeitet einfach parallel, ohne wissen des anderen
			if(_auto_laden_ist_beendet(verbraucher)) {
				setze_auto_ladestatus(Local::Verbraucher::Ladestatus::off);
				return;
			}
			if(_auto_schalte_solarstatus_automatisch(verbraucher)) {
				return;
			}
		}

		void setze_roller_ladestatus(Local::Verbraucher::Ladestatus status) {
			char stat[6];
			if(status == Local::Verbraucher::Ladestatus::force) {
				_schalte_roller_relay(true);
				strcpy(stat, "force");
			} else if(status == Local::Verbraucher::Ladestatus::solar) {
				_schalte_roller_relay(false);
				strcpy(stat, "solar");
			} else {
				_schalte_roller_relay(false);
				strcpy(stat, "off");
			}
			if(persistenz->open_file_to_overwrite(roller_ladestatus_filename)) {
				sprintf(persistenz->buffer, "%s", stat);
				persistenz->print_buffer_to_file();
				persistenz->close_file();
			}
			if(persistenz->open_file_to_overwrite(roller_leistung_log_filename)) {
				persistenz->close_file();
			}
		}

		void setze_auto_ladestatus(Local::Verbraucher::Ladestatus status) {
			char stat[6];
			if(status == Local::Verbraucher::Ladestatus::force) {
				_schalte_auto_relay(true);
				strcpy(stat, "force");
			} else if(status == Local::Verbraucher::Ladestatus::solar) {
				_schalte_auto_relay(false);
				strcpy(stat, "solar");
			} else {
				_schalte_auto_relay(false);
				strcpy(stat, "off");
			}
			if(persistenz->open_file_to_overwrite(auto_ladestatus_filename)) {
				sprintf(persistenz->buffer, "%s", stat);
				persistenz->print_buffer_to_file();
				persistenz->close_file();
			}
			if(persistenz->open_file_to_overwrite(auto_leistung_log_filename)) {
				persistenz->close_file();
			}
		}

		void wechsle_auto_ladeleistung() {
			int auto_benoetigte_ladeleistung_in_w = _gib_auto_benoetigte_ladeleistung_in_w();
			if(auto_benoetigte_ladeleistung_in_w == cfg->auto_benoetigte_leistung_hoch_in_w) {
				auto_benoetigte_ladeleistung_in_w = cfg->auto_benoetigte_leistung_gering_in_w;
			} else {
				auto_benoetigte_ladeleistung_in_w = cfg->auto_benoetigte_leistung_hoch_in_w;
			}
			if(persistenz->open_file_to_overwrite(auto_leistung_filename)) {
				sprintf(persistenz->buffer, "%d", auto_benoetigte_ladeleistung_in_w);
				persistenz->print_buffer_to_file();
				persistenz->close_file();
			}
		}

		void wechsle_roller_ladeleistung() {
			int roller_benoetigte_leistung_in_w = _gib_roller_benoetigte_leistung_in_w();
			if(roller_benoetigte_leistung_in_w == cfg->roller_benoetigte_leistung_hoch_in_w) {
				roller_benoetigte_leistung_in_w = cfg->roller_benoetigte_leistung_gering_in_w;
			} else {
				roller_benoetigte_leistung_in_w = cfg->roller_benoetigte_leistung_hoch_in_w;
			}
			if(persistenz->open_file_to_overwrite(roller_leistung_filename)) {
				sprintf(persistenz->buffer, "%d", roller_benoetigte_leistung_in_w);
				persistenz->print_buffer_to_file();
				persistenz->close_file();
			}
		}
	};
}