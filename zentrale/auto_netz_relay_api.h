#pragma once
#include "base_api.h"

namespace Local {
	class AutoNetzRelayAPI: public BaseAPI {

	using BaseAPI::BaseAPI;

	public:
		bool ist_force_aktiv() {
			return _ist_an(cfg->auto_laden_host, cfg->auto_laden_port);
		}

		bool ist_solar_aktiv() {
			return false;
		}

		void schalte(bool ein) {
			_schalte(cfg->auto_laden_host, cfg->auto_laden_port, ein);
		}

		int gib_aktuelle_ladeleistung_in_w() {
			return 3700;
		}
	};
}
