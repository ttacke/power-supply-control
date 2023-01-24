#include "config.h"
#include "wlan.h"
#include "webserver.h"
#include "web_client.h"
#include "formatierer.h"
#include "elektro_anlage.h"
#include "wechselrichter_leser.h"
#include "smartmeter_leser.h"

Local::Config cfg;
Local::Wlan wlan(cfg.wlan_ssid, cfg.wlan_pwd);
Local::Webserver webserver(80);
Local::WebClient web_client(wlan.client);
/* TODO
Einstellung beim Bauen:
MMU -> 16kb + 48b IRAM

Display erstellen und HTML dort ausgeben
Berechnungen erfolgen in ElekrtoAnlage
Wechselrichter-IP in der Config, die URL im Leser (die ist eh fix an die Struktur gebunden)

5,5 Zeilen hat das Display in ordentlicher größe
=========
Überschuss: 1.500 W <- enthält auch das Laden der Batterie
SolarBatt:     72 %
Auto Laden: 54% [nur Überschuss] [normal] <- Ü ist inaktiv, wenn nicht sinnvoll
Vorhersage: 8:15 - 17:02 [Überschuss wird erwartet] <- woher kriegen wir so einen Wert?
Last [Netz 2000W + Batterieladestrom (wenn geladen, nicht, wenn entladen)] <-- noch unklar
=========
*/

void setup(void) {
	Serial.begin(cfg.log_baud);
	Serial.println("\nSetup ESP");
	wlan.connect();
	webserver.add_page("/", []() {
//		Serial.println("Free RAM");
//		Serial.println(ESP.getFreeHeap(),DEC); // Freien Speicher zeigen
		_erzeuge_website();
//		Serial.println("Free RAM");
//		Serial.println(ESP.getFreeHeap(),DEC); // Freien Speicher zeigen
	});
	webserver.start();
}

void loop(void) {
	webserver.watch_for_client();
}

void s(String content) {// Schmutzige Abkuerzung
	webserver.server.sendContent(content);
}

void _erzeuge_website() {
	Local::ElektroAnlage elektroanlage;

	Local::WechselrichterLeser wechselrichter_leser(cfg, web_client);
	wechselrichter_leser.daten_holen_und_einsetzen(elektroanlage);

	Local::SmartmeterLeser smartmeter_leser(cfg, web_client);
	smartmeter_leser.daten_holen_und_einsetzen(elektroanlage);

	webserver.server.setContentLength(CONTENT_LENGTH_UNKNOWN);
	webserver.server.send(200, "text/html", "");
	s("<html><head><title>Sonnenrech 19</title>"
		"<script type=\"text/javascript\">"
		"var anzahl_fehler = 0;\n"
		"function setze_lokale_daten() {\n"
		"  var max_i = document.getElementById('max_i_value').innerHTML * 1;\n"
		"  if(!document.max_i || document.max_i < max_i) {\n"
		"   document.max_i = max_i * 1;\n"
		"   document.max_i_phase = document.getElementById('max_i_phase').innerHTML;\n"
		"   var now = new Date();\n"
		"   document.max_i_uhrzeit = now.getHours() + ':' + (now.getMinutes() < 10 ? '0' : '') + now.getMinutes();\n"
		"  }\n"
		"  document.getElementById('marker').innerHTML = document.max_i_uhrzeit;\n"
		"  document.getElementById('max_i').innerHTML = 'max I=' + document.max_i + 'A&nbsp(' + document.max_i_phase + ')&nbsp;';\n"
		"}\n"
	);
	s("function reload() {\n"
		"  var xhr = new XMLHttpRequest();\n"
		"  xhr.onreadystatechange = function(args) {\n"
		"    if(this.readyState == this.DONE) {\n"
		"      if(this.status == 200) {\n"
		"        anzahl_fehler = 0;\n"
		"        document.getElementsByTagName('html')[0].innerHTML = this.responseText;\n"
		"        setze_lokale_daten();\n"
		"        document.getElementsByTagName('body')[0].className = '';\n"
		"      } else {\n"
		"        anzahl_fehler++;\n"
		"        document.getElementById('max_i').innerHTML = '';\n"
		"        document.getElementById('fehler').innerHTML = ' ' + anzahl_fehler + ' FEHLER!';\n"
		"        document.getElementsByTagName('body')[0].className = 'hat_fehler';\n"
		"      }\n"
		"    }\n"
		"  };\n"
		"  xhr.open('GET', '/', true);\n"
		"  xhr.timeout = 30 * 1000;\n"
		"  xhr.send();\n"
		"}\n"
	);
	s("setInterval(reload, " + (String) cfg.refresh_display_interval + " * 1000);\n"
		"</script>"
		"<style>"
		"body{padding:0.5rem;padding-top:0.5rem;}"//transform:rotate(180deg); geht im Kindle nicht
		"*{font-family:sans-serif;font-weight:bold;color:#000;}"
		"td{font-size:5rem;line-height:1.1;}"
		"table{width:100%;}"
		"tr.last td {border-top:5px solid #000;}"
		".zahl{text-align:right;padding-left:1rem;}"
		".label{font-size:2rem;}"
		".einheit{padding-left:0.3rem;}"
		".markerline td{text-align:center;font-size:2.5rem;}"
		".markerline td span{color:#999;}"
		".markerline td #max_i{}"
		".markerline td #marker{}"
		".speicher_aus td{color:#999;}"
		".hat_fehler *{color:#999;}"
		".hat_fehler .markerline td span{color:#000;text-decoration:underline;}"
		"</style>"
	);
	s("</head><body onclick=\"reload();\" onload=\"setze_lokale_daten();\">"
		"<div style=\"display:none\"><span id=max_i_value>" + Local::Formatierer::wert_als_k_mit_1stellen(elektroanlage.max_i_ma()) + "</span><span id=max_i_phase>" + elektroanlage.max_i_phase() + "</span></div>"
		"<table cellspacing=0>"
		"<tr><td class=label>Solar</td><td class=zahl>" + Local::Formatierer::wert_als_k(false, elektroanlage.solar_wh) + "</td><td class=\"label einheit\">W</span></td></tr>"
		"<tr><td class=label>+Akku<br/></td><td class=zahl>" + Local::Formatierer::wert_als_k(true, elektroanlage.solarakku_wh) + "</td><td class=\"label einheit\">W</span></td></tr>"
		"<tr><td class=label>+Netz</td><td class=zahl>" + Local::Formatierer::wert_als_k(true, elektroanlage.netz_wh) + "</td><td class=\"label einheit\">W</span></td></tr>"
		"<tr class=last><td class=label>=Last</td><td class=zahl>" + Local::Formatierer::wert_als_k(false, elektroanlage.verbraucher_wh * -1) + "</td><td class=\"label einheit\">W</span></td></tr>"
		"<tr class=markerline><td colspan=3><span id=max_i></span><span id=marker>starte...</span><span id=fehler></span></td></tr>"
		"<tr class=\"speicher " + (elektroanlage.solarakku_ist_an ? "speicher_status" : "") + "\"><td class=label>Speicher</td><td class=zahl>" + Local::Formatierer::wert_als_k(false, cfg.speicher_groesse / 100 * elektroanlage.solarakku_ladestand_prozent) + "</td><td class=\"label einheit\">Wh</span></td></tr>"
		"</table>"
		"</body></html>"
	);
}
