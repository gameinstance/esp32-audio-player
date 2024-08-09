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

#ifndef PCM56_PLAYER_PLAYER
#define PCM56_PLAYER_PLAYER

#include "esp_attr.h"
#include "soc/gpio_reg.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "stream_buffer.hh"

/**
* @name player
*
* @brief ESP32 GPIO interfacing objects for dual PCM56 (stereo) DAC chips
*/


struct stereo_player_config {
	// GPIOs 0..31 only
	int8_t clk_gpio;
	int8_t ch1_data_gpio;
	int8_t ch2_data_gpio;
	int8_t le_gpio;
};


class dac_gpio {
public:
	dac_gpio(const stereo_player_config &config)
		: _config{config},
		  _clk_bitmask{1ul << _config.clk_gpio},
		  _ch1_data_bitmask{1ul << _config.ch1_data_gpio},
		  _ch2_data_bitmask{1ul << _config.ch2_data_gpio},
		  _le_bitmask{1ul << _config.le_gpio},
		  _set_bitmask{0}, _reset_bitmask{0}
	{
		// PCM56 gpio setup
		gpio_config_t pcm_gpio_conf = {};
		pcm_gpio_conf.intr_type = GPIO_INTR_DISABLE;
		pcm_gpio_conf.mode = GPIO_MODE_OUTPUT;
		pcm_gpio_conf.pin_bit_mask = (_clk_bitmask
									| _ch1_data_bitmask
									| _ch2_data_bitmask
									| _le_bitmask);
		pcm_gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
		pcm_gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
		gpio_config(&pcm_gpio_conf);
	}
	~dac_gpio() {
		gpio_reset_pin((gpio_num_t)_config.clk_gpio);
		gpio_reset_pin((gpio_num_t)_config.ch1_data_gpio);
		gpio_reset_pin((gpio_num_t)_config.ch2_data_gpio);
		gpio_reset_pin((gpio_num_t)_config.le_gpio);
	}

	inline void set_samples_and_enable(int16_t &ch1_val, int16_t &ch2_val)
	{
		for (int i{15}, mask{1 << i}; i >= 0; --i, mask >>= 1) {
			_set_bitmask = 0;
			_reset_bitmask = 0;

			if (i == 14)
				_set_bitmask |= _le_bitmask;             // LE set

			if (ch1_val & mask)
				_set_bitmask |= _ch1_data_bitmask;
			else
				_reset_bitmask |= _ch1_data_bitmask;

			if (ch2_val & mask)
				_set_bitmask |= _ch2_data_bitmask;
			else
				_reset_bitmask |= _ch2_data_bitmask;

			REG_WRITE(GPIO_OUT_W1TC_REG, _reset_bitmask);
			REG_WRITE(GPIO_OUT_W1TS_REG, _set_bitmask);

			REG_WRITE(GPIO_OUT_W1TS_REG, _clk_bitmask);  // CLK set
			REG_WRITE(GPIO_OUT_W1TC_REG, _clk_bitmask);  // CLK reset
		}

		REG_WRITE(GPIO_OUT_W1TC_REG, _le_bitmask);       // LE reset
	};

private:
	const stereo_player_config &_config;
	uint32_t _clk_bitmask;
	uint32_t _ch1_data_bitmask;
	uint32_t _ch2_data_bitmask;
	uint32_t _le_bitmask;
	uint32_t _set_bitmask;
	uint32_t _reset_bitmask;
};


struct stereo_sample_type {
	int16_t channel_0;
	int16_t channel_1;
};

using player_sample_type = int16_t;

static constexpr const uint8_t player_channel_count = 2;
static constexpr const uint8_t player_sample_bit_size = 16;
static constexpr const size_t player_sample_rate = 44100;
static constexpr const size_t player_oversampling = 1;  // esp32 cannot handle more in || w/ other tasks
static constexpr const uint64_t _timer_resolution_hz = 40000000; // 40MHz

template<typename BUFFER>
class stereo_player {
public:
	using config_type = stereo_player_config;
	using dac_gpio_type = dac_gpio;

	stereo_player(const config_type &config, BUFFER &stream_buffer,
							size_t sample_rate = player_sample_rate, double frequency_calibration = 1)
		: _config{config},
		  _gpio{_config},
		  _context{.buffer{stream_buffer}, .gpio{_gpio}, .play_cnt{player_oversampling}, .stereo_sample{}},
		  _gptimer{nullptr}
	{
		printf("%s: Starting timer @ %zu samples/second\n", _tag, sample_rate);
		gptimer_config_t timer_config = {
			.clk_src = GPTIMER_CLK_SRC_DEFAULT,
			.direction = GPTIMER_COUNT_UP,
			.resolution_hz = _timer_resolution_hz,
			.intr_priority = 3,  // highest priority
			.flags{}
		};
		ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &_gptimer));
		printf("%s: Timer created", _tag);

		gptimer_event_callbacks_t cbs = {
			.on_alarm = &stereo_player<BUFFER>::play_data,
		};
		ESP_ERROR_CHECK(gptimer_register_event_callbacks(_gptimer, &cbs, &_context));

		printf("%s: Enable timer", _tag);
		ESP_ERROR_CHECK(gptimer_enable(_gptimer));

		printf("%s: Start timer", _tag);
		gptimer_alarm_config_t alarm_config = {
			.alarm_count = (uint64_t)(_timer_resolution_hz * frequency_calibration / (sample_rate * player_oversampling)),
			.reload_count = 0,
			.flags {
				.auto_reload_on_alarm = true
			}
		};
		ESP_ERROR_CHECK(gptimer_set_alarm_action(_gptimer, &alarm_config));
		ESP_ERROR_CHECK(gptimer_start(_gptimer));
	}
	~stereo_player()
	{
		ESP_ERROR_CHECK(gptimer_stop(_gptimer));

		ESP_ERROR_CHECK(gptimer_disable(_gptimer));

		ESP_ERROR_CHECK(gptimer_del_timer(_gptimer));
	}

	static bool IRAM_ATTR NOINLINE_ATTR play_data(gptimer_handle_t timer, const gptimer_alarm_event_data_t */*ev_data*/, void *user_ctx)
	{
		_context_type *context = (_context_type *)user_ctx;

		auto value = context->buffer.template get<isr_operation>();
		if (value)
			context->gpio.set_samples_and_enable(value->channel_0, value->channel_1);

		return true;
	}

private:
	static const constexpr char *_tag = "stereo_player";

	struct _context_type {
		BUFFER &buffer;
		dac_gpio_type &gpio;
		size_t play_cnt;
		stereo_sample_type stereo_sample;
	};

	const config_type &_config;
	dac_gpio_type _gpio;
	_context_type _context;
	gptimer_handle_t _gptimer;
};


#endif // PCM56_PLAYER_PLAYER
