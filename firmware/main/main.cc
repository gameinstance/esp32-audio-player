/* Copyright (C) 2024  Bogdan-Gabriel Alecu  (GameInstance.com)
 *
 * esp32-audio-player - yet another esp32 audio player
 *
 * This library is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <dirent.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include "esp_http_server.h"
#include "sdmmc_cmd.h"

#include <basics/file.hh>
#include <basics/base64.hh>
#include <audio/flac.hh>
#include <defs.hh>
#include <gpio.hh>
#include <nvs_partition.hh>
#include <wifi.hh>
#include <spi_bus.hh>
#include <spi_sd.hh>
#include <stream_buffer.hh>
#include <player.hh>


//      SPI       GPIO    SD     SDSPI  MMC
// ----------------------------------------
#define SD_CS     22   // DAT3   CS     RES
#define SD_SCK    18   // CLK    SCLK   CLK
#define SD_MOSI   23   // CMD    DI     CMD
#define SD_MISO   19   // DAT0   DO     DAT
#define SD_DET    21   // xxx    xxx    xxx

#define PCM_CLK       14
#define PCM_LE        27
#define PCM_CH1_DATA  26
#define PCM_CH2_DATA  25

#define SRC_RLY  12
#define PWR_RLY  13

static const unsigned char wifi_ssid[32] = "WIFI_AP";
static const unsigned char wifi_pasw[64] = "WIFI_PASS";

static const double frequency_calibration = 0.995428; //ideally: 1, adjusted by trial-and-error;
static const uint16_t buffer_max_size = 4608;
static const uint8_t buffer_max_count = 2;

using player_buffer_type = stream_buffer<stereo_sample_type, buffer_max_size>;
using pcm56_player_type = stereo_player<player_buffer_type>;
using input_file_type = basics::file::input<1024>;
using flac_decoder_type = audio::flac::decoder<input_file_type, buffer_max_size>;

enum class cmd_type: uint8_t {
	idle,
	list,
	play,
	stop,
};
enum class state_type: uint8_t {
	init,
	has_connection,
	has_storage,
	ready = has_storage,
	play,
};
enum class play_mode_type: uint8_t {
	once,
	loop,
	album
};


auto bus_config = esp::io::spi_bus_config{
	.sck_pin  = SD_SCK,  // SD#CLK,
	.mosi_pin = SD_MOSI, // SD#CMD,
	.miso_pin = SD_MISO, // SD#DAT0,
	.quadwp_pin = -1,    // SD#DAT2,
	.quadhd_pin = -1,    // SD#DAT3,
	.max_transfer_size = buffer_max_size,
	.core_affinity = esp::io::cpu_core_affinity::cpu_core_0,
};
auto sd_config = esp::io::spi_sd_config{
	.spi_bus = bus_config,
	.cs_pin = SD_CS,
};
auto player_config = stereo_player_config {
	.clk_gpio      = PCM_CLK,
	.ch1_data_gpio = PCM_CH1_DATA,
	.ch2_data_gpio = PCM_CH2_DATA,
	.le_gpio       = PCM_LE,
};
auto relays_config = pcm56_player::relays_output_config {
	.source_gpio = SRC_RLY,
	.power_gpio = PWR_RLY,
};
auto card_detect_config = pcm56_player::card_detect_input_config {
	.gpio = SD_DET,
};

auto player_buffer = player_buffer_type{};
auto cmd = cmd_type{};
auto state = state_type{};
auto current_dir = std::string{"/"};
auto play_mode = play_mode_type{};
auto play_dir = std::string{"/"};
auto play_file = std::string{};
auto play_path = std::string{};
auto volume = int16_t{0};

pcm56_player::relays_output relays{relays_config};
pcm56_player::card_detect_input card_detect{card_detect_config};


std::string get_next_album_track()
{
	std::string dir_path{sd_config.mount_point};
	dir_path += play_dir;

	DIR *dp = ::opendir(dir_path.c_str());
	if (dp == nullptr)
		throw basics::error{"failed opening dir '%s'", dir_path.c_str()};

	std::vector<std::string> files{};
	struct dirent *ep = nullptr;
	for (;;) {
		ep = ::readdir(dp);
		if (ep == nullptr)
			break;

		if (ep->d_type == DT_DIR)
			continue;

		// TODO: filter unsupported files
		files.emplace_back(ep->d_name);
	}
	::closedir(dp);

	taskYIELD();

	std::sort(files.begin(), files.end());
	size_t i = 0;
	for (; i < files.size() - 1; ++i) {
		if (files[i] == play_file)
			break;
	}

	play_file = files[i + 1];

	auto res = play_dir;
	res += "/";
	res += play_file;

	return res;
}


void prepare_next_track()
{
	if (play_mode == play_mode_type::once) {
		state = state_type::ready;
	} else if (play_mode == play_mode_type::loop) {
		// play_path remains unchanged
	} else /*if (play_mode == play_mode_type::album)*/ {
		try {
			play_path = get_next_album_track();

			vTaskDelay(1000 / portTICK_PERIOD_MS);
		} catch (basics::error& e) {
			state = state_type::ready;

			e.append("player: mode=album");
			e.dump();
		} catch (const std::exception &e) {
			state = state_type::ready;

			std::cerr << "error: mode=album: " << e.what() << std::endl;
			;
		}
	}
}


void play_track()
{
	std::string file_path{sd_config.mount_point};
	file_path += play_path;
	input_file_type file_istream{file_path.data()};
	flac_decoder_type flac_decoder{file_istream};
	std::cout << "player: track=" << file_path << std::endl;

	flac_decoder.decode_marker();
	while (flac_decoder.state() != audio::flac::decoder_state::has_metadata)
		flac_decoder.decode_metadata();
	auto info = flac_decoder.streaminfo();
	pcm56_player::print_streaminfo(info);

	int sample_rshift = info.sample_bit_size - player_sample_bit_size;
	std::cout << "player: sample rshifting by " << sample_rshift << " bits\n";

	player_buffer.reset();

	{
		pcm56_player_type player{player_config, player_buffer, info.sample_rate, frequency_calibration};

		auto have_block = false;
		for (;;) {
			if (cmd == cmd_type::stop) {
				cmd = cmd_type::idle;
				state = state_type::ready;
				std::cout << "player: cmd=stop" << std::endl;

				break;
			}

			if (cmd == cmd_type::play) {
				cmd = cmd_type::idle;
				state = state_type::play;
				std::cout << "player: cmd=play" << std::endl;

				break;
			}

			if (!have_block) {
				flac_decoder.decode_audio();
				have_block = true;

				continue;
			}

			// have_block
			if (!player_buffer.need_data()) {
				taskYIELD();

				continue;
			}

			int rshift = sample_rshift - volume;

			if (rshift == 0) {
				for (auto i = size_t{0}; i < flac_decoder.block_size(); ++i)
					player_buffer.template put<task_operation>(stereo_sample_type{
						(player_sample_type)flac_decoder.block_data()[0][i],
										(player_sample_type)flac_decoder.block_data()[1][i]});
			} else if (rshift >= 0) {
				for (auto i = size_t{0}; i < flac_decoder.block_size(); ++i)
					player_buffer.template put<task_operation>(stereo_sample_type{
						(player_sample_type)(flac_decoder.block_data()[0][i] >> rshift),
										(player_sample_type)(flac_decoder.block_data()[1][i] >> rshift)});
			} else { // (rshift < 0)
				for (auto i = size_t{0}; i < flac_decoder.block_size(); ++i)
					player_buffer.template put<task_operation>(stereo_sample_type{
						(player_sample_type)(flac_decoder.block_data()[0][i] << -rshift),
										(player_sample_type)(flac_decoder.block_data()[1][i] << -rshift)});
			}
			have_block = false;

			if (flac_decoder.state() == audio::flac::decoder_state::complete)
				break;

			taskYIELD();
		}
	}

	if ((state == state_type::play) && (flac_decoder.state() == audio::flac::decoder_state::complete))
		prepare_next_track();
}


httpd_uri_t main_page_handler = {
	.uri = "/",
	.method = HTTP_GET,
	.handler = [] (httpd_req_t *req) -> esp_err_t {
		return httpd_resp_send(req, pcm56_player::web_page, HTTPD_RESP_USE_STRLEN);
	},
	.user_ctx = nullptr
};

httpd_uri_t list_handler = {
	.uri = "/list",
	.method = HTTP_GET,
	.handler = [] (httpd_req_t *req) -> esp_err_t {
		try {
			std::string dir_path = std::string{req->uri}.substr(6);  // HACK: parse params properly
			dir_path = basics::base64::decode(dir_path.c_str(), dir_path.size());
			std::cout << "http_ui: GET /list " << req->uri << "; dir=" << dir_path << std::endl;

			if (!card_detect.card_present())
				throw basics::error{"sd_card: no card present"};

			current_dir = dir_path;
			dir_path = sd_config.mount_point + dir_path;

			DIR *dp = nullptr;
			dp = ::opendir(dir_path.c_str());
			if (dp == nullptr)
				throw basics::error{"httpd: failed opening dir '%s'", dir_path.c_str()};

			bool first{true};
			struct dirent *ep = nullptr;

			std::stringstream ostream{};

			httpd_resp_set_type(req, "application/json");
			ostream << "[";
			for (;;) {
				ep = ::readdir(dp);
				if (ep == nullptr)
					break;

				ostream << (first? "" : ",")
						<< "{\"t\":\"" << ((ep->d_type == DT_DIR)? "d" : "f") << "\",\"n\":\""
						<< ep->d_name << "\"}";
				first = false;
			}
			::closedir(dp);

			ostream << "]";
			return httpd_resp_sendstr(req, ostream.str().c_str());

		} catch (...) {
			return httpd_resp_sendstr(req, "error");
		}
	},
	.user_ctx = nullptr
};

httpd_uri_t play_handler = {
	.uri = "/play",
	.method = HTTP_GET,
	.handler = [] (httpd_req_t *req) -> esp_err_t {
		try {
			play_path = std::string{req->uri}.substr(6);  // TODO: create a param parser
			play_path = basics::base64::decode(play_path.c_str(), play_path.size());
			std::cout << "http_ui: GET " << req->uri << "; file=" << play_path << std::endl;

			if (!card_detect.card_present())
				return httpd_resp_send(req, "[error: no card]", HTTPD_RESP_USE_STRLEN);

			{
				std::string file_path{sd_config.mount_point};
				file_path += play_path;
				basics::file::input<512> file_istream{file_path.data()};
				auto streaminfo = audio::flac::decode_metadata(file_istream);

				if (streaminfo.channel_count > player_channel_count)
					return httpd_resp_send(req, "[error: not stereo]", HTTPD_RESP_USE_STRLEN);
				if (streaminfo.sample_rate > player_sample_rate)
					return httpd_resp_send(req, "[error: bad rate]", HTTPD_RESP_USE_STRLEN);
			}

			auto pos = play_path.rfind('/');
			play_file = (pos == std::string::npos)? play_path : play_path.substr(pos + 1);
			play_dir = play_path.substr(0, pos);

			cmd = cmd_type::play;

			return httpd_resp_send(req, "play", HTTPD_RESP_USE_STRLEN);
		} catch (basics::error& e) {
			e.append("bad header");
			e.dump();

			return httpd_resp_send(req, "[error: bad header]", HTTPD_RESP_USE_STRLEN);
		} catch (/*basics::error &e*/...) {
			std::cout << "error: cannot play" << std::endl;

			return httpd_resp_send(req, "[error: cannot play]", HTTPD_RESP_USE_STRLEN);
		}
	},
	.user_ctx = nullptr
};

httpd_uri_t stop_handler = {
	.uri = "/stop",
	.method = HTTP_GET,
	.handler = [] (httpd_req_t *req) -> esp_err_t {
		cmd = cmd_type::stop;

		std::cout << "http_ui: GET " << req->uri << std::endl;
		return httpd_resp_send(req, "stop", HTTPD_RESP_USE_STRLEN);
	},
	.user_ctx = nullptr
};

httpd_uri_t volume_handler = {
	.uri = "/volume",
	.method = HTTP_GET,
	.handler = [] (httpd_req_t *req) -> esp_err_t {
		httpd_resp_set_type(req, "application/json");

		if (std::string{req->uri}.substr(8) == "up") {
			if (volume < 1)
				++volume;
		} else {
			if (volume > -6)
				--volume;
		}

		std::stringstream ostream{};
		ostream << "{\"volume\":" << volume << "}";

		std::cout << "http_ui: GET " << req->uri << " " << ostream.str() << std::endl;
		return httpd_resp_sendstr(req, ostream.str().c_str());
	},
	.user_ctx = nullptr
};

httpd_uri_t mode_handler = {
	.uri = "/mode",
	.method = HTTP_GET,
	.handler = [] (httpd_req_t *req) -> esp_err_t {
		httpd_resp_set_type(req, "application/json");

		auto mode = std::string{req->uri}.substr(6);

		if (mode == "once") {
			play_mode = play_mode_type::once;
		} else if (mode == "loop") {
			play_mode = play_mode_type::loop;
		} else /*if (mode == "album")*/ {
			play_mode = play_mode_type::album;
		}

		std::stringstream ostream{};
		ostream << "{\"mode\":\"" << mode << "\"}";

		std::cout << "http_ui: GET " << req->uri << " " << ostream.str() << std::endl;
		return httpd_resp_sendstr(req, ostream.str().c_str());
	},
	.user_ctx = nullptr
};

httpd_uri_t state_handler = {
	.uri = "/state",
	.method = HTTP_GET,
	.handler = [] (httpd_req_t *req) -> esp_err_t {
		httpd_resp_set_type(req, "application/json");

		std::stringstream ostream{};
		ostream << "{\"status\":\"" << ((state == state_type::init)? "starting..." :
										(state == state_type::has_connection)? "no sd-card" :
										(state == state_type::ready)? "ready" : "playing") << "\","
				<< "\"dir\":\""  << ((state == state_type::play)? play_dir : current_dir) << "\","
				<< "\"file\":\"" << ((state == state_type::play)? play_file : "") << "\","
				<< "\"mode\":\"" << ((play_mode == play_mode_type::once)? "once" :
									 (play_mode == play_mode_type::loop)? "loop" : "album") << "\","
				<< "\"volume\":" << volume << "}";

		std::cout << "http_ui: GET " << req->uri << " : " << ostream.str() << std::endl;
		return httpd_resp_sendstr(req, ostream.str().c_str());
	},
	.user_ctx = nullptr
};

httpd_handle_t setup_server(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_handle_t server = nullptr;

	if (httpd_start(&server, &config) == ESP_OK) {
		httpd_register_uri_handler(server, &main_page_handler);
		httpd_register_uri_handler(server, &list_handler);
		httpd_register_uri_handler(server, &play_handler);
		httpd_register_uri_handler(server, &stop_handler);
		httpd_register_uri_handler(server, &volume_handler);
		httpd_register_uri_handler(server, &mode_handler);
		httpd_register_uri_handler(server, &state_handler);
	}

	return server;
}


void player_main()
{
	state = state_type::ready;

	for (;;) {
		if (!card_detect.card_present()) {
			std::cout << "player: SD card removed!" << std::endl;

			break;
		}

		if (state == state_type::ready) {
			if (cmd == cmd_type::play) {
				cmd = cmd_type::idle;

				state = state_type::play;
			}
		}

		if (state == state_type::play) {
			relays.set(true);

			try {
				play_track();
			} catch (basics::error& e) {
				e.append("player failure");
				e.dump();

				state = state_type::ready;

				vTaskDelay(1000 / portTICK_PERIOD_MS);
			} catch (const std::exception &e) {
				std::cerr << "player failure: " << e.what() << std::endl;
				throw;
			}
		}

		if (state != state_type::play)
			relays.set(false);

		vTaskDelay(25 / portTICK_PERIOD_MS);
	}
}


void user_main()
{
	for (;;) {
		try {
			state = state_type::has_connection;

			if (!card_detect.card_present())
				throw basics::error{"no SD card present"};

			esp::io::spi_bus bus{bus_config};
			sdmmc_host_t host = SDSPI_HOST_DEFAULT();
			esp::io::spi_sd_deps sd_deps{host};
			esp::io::spi_sd sd{sd_config, sd_deps};

			state = state_type::has_storage;

			sdmmc_card_print_info(stdout, sd.card());
			std::cout << "user: system ready" << std::endl;

			taskYIELD();

			player_main();
		} catch (basics::error& e) {
			e.append("user failure");
			e.dump();
		} catch (std::exception &e) {
			std::cerr << "user failure: " << e.what() << std::endl;
		}

		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}


// wifi task   -> core 1 : menuconfig → Component config → Wi-Fi
// tcp/ip task -> core 1 : menuconfig → Component config → LWIP
// main task   -> core 0 : menuconfig → Component config → ESP System Settings → Main task core affinity
// interrupt watchdog on : menuconfig → Component config → ESP System Settings → [-] Interrupt watchdog
// task watchdog timer on: menuconfig → Component config → ESP System Settings → [-] Enable Task Watchdog Timer
// esp timer   -> core 0 : menuconfig → Component Config → High resolution timer (esp_timer) → esp_timer task core affinity (CPU0)
// isr timer   -> core 0 : menuconfig → Component Config → High resolution timer (esp_timer) → timer interrupt core affinity (CPU0)
// main stack -> 4600    : menuconfig → Component Config → ESP System settings → Main task stack size (changed from 3584 to 4600)
// CPU freq. -> 240MHz   : menuconfig → Component Config → ESP System settings → CPU frequency (changed from 160MHz to 240MHz)
extern "C" void app_main(void)
{
	for (;;) {
		try {
			state = state_type::init;
			play_mode = play_mode_type::album;

			esp::storage::nvs_partition nvs{};
			esp::io::wifi_sta wifi{wifi_ssid, wifi_pasw};
			setup_server();

			std::cout << "app: networking ready" << std::endl;

			taskYIELD();

			user_main();
		} catch (basics::error& e) {
			e.append("app failure");
			e.dump();
		} catch (std::exception &e) {
			std::cerr << "app failure: " << e.what() << std::endl;
		}

		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}
