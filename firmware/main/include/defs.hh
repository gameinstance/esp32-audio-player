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

#ifndef PCM56_PLAYER_DEFS
#define PCM56_PLAYER_DEFS

namespace pcm56_player {

/**
* @name pcm56 player definitions
*
* @brief Application specific definitions.
*/


void print_streaminfo(const audio::flac::streaminfo_type &info)
{
	printf("FLAC stream:\n");
	printf("* min_block_size=%u\n", info.min_block_size);
	printf("* max_block_size=%u\n", info.max_block_size);
	printf("* min_frame_size=%lu\n", info.min_frame_size);
	printf("* max_frame_size=%lu\n", info.max_frame_size);
	printf("* sample_rate=%lu\n", info.sample_rate);
	printf("* channel_count=%u\n", info.channel_count);
	printf("* sample_bit_size=%u\n", info.sample_bit_size);
	printf("* sample_count=%llu\n", info.sample_count);
}


static const constexpr char web_page[] = R"rawliteral(
<!DOCTYPE html>
<html>
	<head>
		<title>PCM56-player</title>
		<meta name="viewport" content="width=device-width, initial-scale=1.0">
		<style>
body {
	font-family: Verdana, sans-serif;
}
.head {
	display: grid;
	grid-template-columns: repeat(7, 1fr);
	grid-template-rows: auto auto;
}
.head .title {
	grid-column: 1 / 5;
	margin: 0 0 0.6rem 1rem;
	font-size: max(1.8rem, min(3.8vw, 2.5rem));
	font-weight: bold;
}
.head .volume {
	grid-column: 5 / 8;
	padding: 0.2rem 1rem 0 0;
	text-align: right;
}
#volume {
	font-size: 1.1rem;
	width: 1.3rem;
	text-align: center;
}
.player {
	margin: 0 1rem;
}
.player .control {
	padding: 1rem 0;
	display: inline;
}
.player .status {
	padding: 0.5rem;
	display: inline;
}
.browser {
	padding: 1rem;
}
.browser .path {
	padding: 1rem 1rem;
	background-color: #fff3e4;
	font-size: 1.2rem;
}
.browser #content div {
	margin: 0.3rem 0;
	padding: 0.5rem 1rem;
	background-color: #fbf8f2;
}
.selected {
	background-color: #96A2A6 !important;
	color: white !important;
}
.link {
	cursor: pointer;
	color: #03338f;
}
.on {
	border: 2px solid white;
	color: white !important;
}
button {
	padding: 0.2rem 0.6rem;
	height: 2.2rem;
	font-size: 0.9rem;
	text-align: center;
	border: 2px solid #3b88c3;
	border-radius: 0.3rem;
	background-color: #3b88c3;
	color: white;
	font-weight: bold;
	cursor: pointer;
}
@media screen and (max-width: 480px) {
	.status, .control {
		width: 100%;
	}
}
		</style>
		<script>
function dom_get(id) {
	return document.getElementById(id);
}
function make_subpaths(path) {
	let v = []; let last_i = 0;
	for (let i = 0; i < path.length; ++i) {
		if (path[i] == '/') {
			v.push({abs: path.substr(0, i + 1), rel: path.substr(last_i + 1, i - last_i - 1)});
			last_i = i;
		}
	}
	if (last_i != path.length - 1)
		v.push({abs: path.substr(0, path.length), rel: path.substr(last_i + 1, path.length - last_i)});
	var path_el = dom_get('path');path_el.innerHTML = ''
	for (let i = 0; i < v.length; ++i) {
		let o = document.createElement('span');
		o.classList.add('dir');
		o.innerHTML = (i == 0)? 'Root ' : '/ ' + v[i].rel + ' ';
		if ((v.length == 1) || (i != v.length - 1)) {
			o.classList.add('link');
			o.addEventListener('click', function() {load_dir(v[i].abs);}, false);
		}
		path_el.appendChild(o);
	}
}
var selected, state;
function select(o) {
	if (selected) selected.classList.remove('selected');
	if (o) o.classList.add('selected');
	selected = o;
	console.log('selected', o);
}
function set_dir(path, data) {
	if (path.substr(-1) != '/') path += '/';
	console.log('set_dir', path, data);
	var d = JSON.parse(data);
	d.sort(function(a, b) {if (a.t < b.t) return -1; if (a.t > b.t) return 1; if (a.n < b.n) return -1; if (a.n > b.n) return 1; return 0;});
	make_subpaths(path);
	var cont = dom_get('content');cont.innerHTML = '';
	for (var i = 0; i < d.length; ++i) {
		let file = d[i];
		var o = document.createElement('div');
		o.classList.add('link');
		o.classList.add((file.t == 'd')? 'dir' : 'file');
		o.addEventListener('click', (file.t == 'd')? function() {load_dir(path + file.n);} : function() {call('/play?' + btoa(path + file.n), file.n);select(this);}, false);
		o.innerHTML = ((file.t == 'd')? '[' : '') + file.n + ((file.t == 'd')? ']' : '');
		cont.appendChild(o);
	}
}
function load_dir(dir) {
	if ((dir.substr(-1) == '/') && (dir.length != 1)) dir = dir.substr(0, dir.length - 1);
	http_get('/list?' + btoa(dir), function(req) {set_dir(dir, req.responseText);});
}
function call(url, filename = '') {
	http_get(url, function(req) {dom_get('status').innerHTML = req.responseText + ' ' + filename;});
}
function load_state() {
	http_get('/state', function(req) {state = JSON.parse(req.responseText); set_volume(req); set_mode(req); load_dir(state.dir); dom_get('status').innerHTML = state.status + ' ' + state.file;});
}
function set_volume(req) {
	console.log("set_volume", JSON.parse(req.responseText).volume);
	dom_get('volume').value = JSON.parse(req.responseText).volume;
}
var mode = 'once';
function set_mode(req) {
	mode = JSON.parse(req.responseText).mode;
	console.log("set_mode", mode);
	var s;
	switch (mode) {
		case 'once': s = '1x'; break;
		case 'loop': s = '&#128258;'; break;
		default: s = '&#128257;';
	}
	dom_get('mode').innerHTML = s;
}
function toggle_mode() {
	console.log("toggle_mode", mode);
	var n_mode;
	switch (mode) {
		case 'once': n_mode = 'loop'; break;
		case 'loop': n_mode = 'album'; break;
		default: n_mode = 'once';
	}
	http_get('/mode?' + n_mode, set_mode);
}
function http_get(url, fn = function(req) {}) {
	var req = new XMLHttpRequest();req.addEventListener('load', function() {fn(req);}); req.open('get', url); req.send();
}
window.onload = function() {load_state();};
		</script>
	</head>
	<body>
		<div class="head">
			<div class="title">PCM56 player</div>
			<div class="volume">
				<span>Volume</span>
				<button onClick="http_get('/volume?down', set_volume);">-</button>
				<input id="volume" type="text" value="..." readonly />
				<button onClick="http_get('/volume?up', set_volume);">+</button>
			</div>
		</div>
		<div class="player">
			<div class="control">
				<button id="mode" onClick="toggle_mode();">&#128258;</button>
				<button onclick="call('/stop');select();">Stop</button>
			</div>
			<div class="status">
				<span id="status">...</span>
			</div>
		</div>
		<div class="browser">
			<div class="path">
				<span id="path">...</span>
			</div>
			<div id="content">...</div>
		</div>
	</body>
</html>
)rawliteral";


};  // namespace pcm56_player

#endif // PCM56_PLAYER_DEFS
