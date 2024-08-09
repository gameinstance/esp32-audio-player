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

#ifndef STREAM_BUFFER
#define STREAM_BUFFER

#include <stdio.h>
#include <stdexcept>
#include <optional>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

/**
* @name stream_buffer
*
* @brief buffering objects serving a non-blocking ISR consumer
*/


class task_operation;
class isr_operation;


template<size_t MAX_SIZE, size_t TRIGGER = 1>
class stream_buffer_rtos {
public:
	stream_buffer_rtos();
	~stream_buffer_rtos();

	template<typename OPERATION_POLICY>
	uint8_t get();
	template<typename OPERATION_POLICY>
	bool put(char value);


private:
	StreamBufferHandle_t _stream_buffer;

	template<typename OPERATION_POLICY>
	inline size_t _get_data(void *buffer, size_t buffer_size);
	template<typename OPERATION_POLICY>
	inline size_t _put_data(void *data, size_t size);
};


template<typename VALUE_TYPE, size_t MAX_SIZE>
class stream_buffer {
public:
	stream_buffer();

	void reset();

	template<typename OPERATION_POLICY>
	inline std::optional<VALUE_TYPE> get();

	template<typename OPERATION_POLICY>
	inline void put(VALUE_TYPE value);

	inline bool need_data();

private:
	static const constexpr size_t _max_count = 2;

	VALUE_TYPE _buffer[_max_count][MAX_SIZE];
	uint16_t _limit[_max_count];
	uint8_t _read_idx;
	uint16_t _read_pos;
	uint8_t _write_idx;
	bool _need_data;
};


/******************************************************************************************************/

class task_operation {
public:
	static size_t receive(StreamBufferHandle_t stream_buffer, void *buffer, size_t buffer_size);
	static size_t send(StreamBufferHandle_t stream_buffer, void *data, size_t size);
};

class isr_operation {
public:
	static size_t receive(StreamBufferHandle_t stream_buffer, void *buffer, size_t buffer_size);
	static size_t send(StreamBufferHandle_t stream_buffer, void *data, size_t size);
};


size_t task_operation::receive(StreamBufferHandle_t stream_buffer, void *buffer, size_t buffer_size)
{
	return xStreamBufferReceive(stream_buffer, buffer, buffer_size, 0);
}


size_t task_operation::send(StreamBufferHandle_t stream_buffer, void *data, size_t size)
{
	return xStreamBufferSend(stream_buffer, data, size, 0);
}


size_t isr_operation::receive(StreamBufferHandle_t stream_buffer, void *buffer, size_t buffer_size)
{
	return xStreamBufferReceiveFromISR(stream_buffer, buffer, buffer_size, nullptr);
}


size_t isr_operation::send(StreamBufferHandle_t stream_buffer, void *data, size_t size)
{
	return xStreamBufferSendFromISR(stream_buffer, data, size, nullptr);
}


template<size_t MAX_SIZE, size_t TRIGGER>
stream_buffer_rtos<MAX_SIZE, TRIGGER>::stream_buffer_rtos()
	: _stream_buffer{xStreamBufferCreate(MAX_SIZE, TRIGGER)}
{
	if (_stream_buffer == nullptr)
		throw std::runtime_error("stream_buffer_rtos: allocation/init failure");
}


template<size_t MAX_SIZE, size_t TRIGGER>
stream_buffer_rtos<MAX_SIZE, TRIGGER>::~stream_buffer_rtos()
{
	vStreamBufferDelete(_stream_buffer);
}


template<size_t MAX_SIZE, size_t TRIGGER>
template<typename OPERATION_POLICY>
uint8_t stream_buffer_rtos<MAX_SIZE, TRIGGER>::get()
{
	auto res = uint8_t{0};

	_get_data<OPERATION_POLICY>(&res, 1);

	return res;
}


template<size_t MAX_SIZE, size_t TRIGGER>
template<typename OPERATION_POLICY>
bool stream_buffer_rtos<MAX_SIZE, TRIGGER>::put(char value)
{
	return (_put_data<OPERATION_POLICY>(&value, 1) == 1);
}


template<size_t MAX_SIZE, size_t TRIGGER>
template<typename OPERATION_POLICY>
inline size_t stream_buffer_rtos<MAX_SIZE, TRIGGER>::_get_data(void *buffer, size_t buffer_size)
{
	return OPERATION_POLICY::receive(_stream_buffer, buffer, buffer_size);
}


template<size_t MAX_SIZE, size_t TRIGGER>
template<typename OPERATION_POLICY>
inline size_t stream_buffer_rtos<MAX_SIZE, TRIGGER>::_put_data(void *data, size_t size)
{
	return OPERATION_POLICY::send(_stream_buffer, data, size);
}


template<typename VALUE_TYPE, size_t MAX_SIZE>
stream_buffer<VALUE_TYPE, MAX_SIZE>::stream_buffer()
	: _buffer{}, _limit{MAX_SIZE, MAX_SIZE}, _read_idx{0}, _read_pos{0}, _write_idx{1},
		_need_data{true}
{
	memset(_buffer, 0, sizeof(_buffer));
}


template<typename VALUE_TYPE, size_t MAX_SIZE>
void stream_buffer<VALUE_TYPE, MAX_SIZE>::reset()
{
	memset(_buffer, 0, sizeof(_buffer));

	for (size_t i = 0; i < _max_count; ++i)
		_limit[i] = MAX_SIZE;

	_read_idx = 0;
	_read_pos = 0;
	_write_idx = 1;

	_need_data = true;
}


template<typename VALUE_TYPE, size_t MAX_SIZE>
template<typename OPERATION_POLICY>
inline std::optional<VALUE_TYPE> stream_buffer<VALUE_TYPE, MAX_SIZE>::get()
{
	if (_read_pos >= _limit[_read_idx]) {
		_read_idx = (_read_idx + 1) % _max_count;
		_read_pos = 0;

		_write_idx = (_read_idx + 1) % _max_count;

		_need_data = true;
	}

	return _buffer[_read_idx][_read_pos++];
}


template<typename VALUE_TYPE, size_t MAX_SIZE>
template<typename OPERATION_POLICY>
inline void stream_buffer<VALUE_TYPE, MAX_SIZE>::put(VALUE_TYPE value)
{
	if (_limit[_write_idx] >= MAX_SIZE)
		return;

	_buffer[_write_idx][_limit[_write_idx]++] = value;
}


template<typename VALUE_TYPE, size_t MAX_SIZE>
inline bool stream_buffer<VALUE_TYPE, MAX_SIZE>::need_data()
{
	if (_need_data) {
		_limit[_write_idx] = 0;

		_need_data = false;

		return true;
	}

	return false;
}


#endif // STREAM_BUFFER
