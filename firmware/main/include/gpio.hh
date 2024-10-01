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

#ifndef PCM56_PLAYER_GPIO
#define PCM56_PLAYER_GPIO

#include "soc/gpio_reg.h"
#include "driver/gpio.h"


namespace pcm56_player {

/**
* @name pcm56 player gpio
*
* @brief Application specific definitions for power and signals relays output gpio class as well as
*        for card detect input gpio.
*/


struct relays_output_config {
	int8_t source_gpio;
	int8_t power_gpio;
};

class relays_output {
public:
	using config_type = relays_output_config;

	explicit relays_output(config_type &config)
		: _config{config}, _bitmask{1ull << _config.source_gpio | 1ull << _config.power_gpio}
	{
		gpio_config_t gpio_conf{};
		gpio_conf.intr_type = GPIO_INTR_DISABLE;
		gpio_conf.mode = GPIO_MODE_OUTPUT;
		gpio_conf.pin_bit_mask = _bitmask;
		gpio_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
		gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
		gpio_config(&gpio_conf);
	}
	relays_output(const relays_output&) = delete;
	relays_output(relays_output&& other) = delete;

	relays_output& operator=(const relays_output&) = delete;
	relays_output& operator=(relays_output&& other) = delete;

	~relays_output()
	{
		gpio_reset_pin((gpio_num_t)_config.source_gpio);
		gpio_reset_pin((gpio_num_t)_config.power_gpio);
	}

	void set(bool on = true)
	{
		if (!on)
			REG_WRITE(GPIO_OUT_W1TC_REG, _bitmask);
		else
			REG_WRITE(GPIO_OUT_W1TS_REG, _bitmask);
	}

private:
	config_type &_config;
	uint64_t _bitmask;
};


struct card_detect_input_config {
	int8_t gpio;
};

class card_detect_input {
public:
	using config_type = card_detect_input_config;

	explicit card_detect_input(config_type &config)
		: _config{config}, _bitmask{1ull << _config.gpio}
	{
		gpio_config_t gpio_conf{};
		gpio_conf.intr_type = GPIO_INTR_DISABLE;
		gpio_conf.mode = GPIO_MODE_INPUT;
		gpio_conf.pin_bit_mask = _bitmask;
		gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
		gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
		gpio_config(&gpio_conf);
	}
	card_detect_input(const card_detect_input&) = delete;
	card_detect_input(card_detect_input&& other) = delete;

	card_detect_input& operator=(const card_detect_input&) = delete;
	card_detect_input& operator=(card_detect_input&& other) = delete;

	~card_detect_input()
	{
		gpio_reset_pin((gpio_num_t)_config.gpio);
	}

	bool card_present()
	{
		return (gpio_get_level((gpio_num_t)_config.gpio) == 0);
	}

private:
	config_type &_config;
	uint64_t _bitmask;
};


};  // namespace pcm56_player

#endif // PCM56_PLAYER_GPIO
