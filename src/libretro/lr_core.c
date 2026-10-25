/*
    ZEsarUX - libretro frontend.

    Stands in for main_unix.c. zesarux_main() parses a command line built
    here and sets the machine up, then (built with LIBRETRO) returns instead
    of entering its main loop. retro_run takes the loop's place: it fires one
    timer interrupt and steps cpu_core_loop() until the core is back waiting
    for the next one, which is exactly one emulated frame.

    ROMs and the TBBlue SD image are looked up beside argv[0], which is
    <system dir>/zesarux/zesarux.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"

#include "cpu.h"
#include "debug.h"
#include "start.h"
#include "timer.h"
#include "audio.h"
#include "joystick.h"
#include "utils.h"

#include "lr_drivers.h"

extern int timer_pthread_generada;

#define LR_FPS 50.0
#define LR_SAMPLE_RATE FRECUENCIA_CONSTANTE_NORMAL_SONIDO
#define LR_MAX_WIDTH 1024
#define LR_MAX_HEIGHT 1024
/* Far more cpu_core_loop() calls than any frame takes, even at 28 MHz. */
#define LR_MAX_STEPS 4000000

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static int lr_loaded = 0;
static int lr_geometry_width = 0;
static int lr_geometry_height = 0;

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
	va_list ap;
	(void)level;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

/* Command line ------------------------------------------------------------- */

#define LR_MAX_ARGS 64
static char *lr_argv[LR_MAX_ARGS];
static int lr_argc = 0;

static void add_arg(const char *arg)
{
	if (lr_argc < LR_MAX_ARGS - 1)
		lr_argv[lr_argc++] = strdup(arg);
	lr_argv[lr_argc] = NULL;
}

static void free_args(void)
{
	int i;
	for (i = 0; i < lr_argc; i++)
		free(lr_argv[i]);
	lr_argc = 0;
	lr_argv[0] = NULL;
}

/* Input --------------------------------------------------------------------- */

static int retrok_to_zesarux(unsigned keycode)
{
	if (keycode >= 'a' && keycode <= 'z')
		return keycode;
	if (keycode >= '0' && keycode <= '9')
		return keycode;

	switch (keycode) {
		case RETROK_SPACE: return UTIL_KEY_SPACE;
		case RETROK_RETURN: return UTIL_KEY_ENTER;
		case RETROK_KP_ENTER: return UTIL_KEY_KP_ENTER;
		case RETROK_BACKSPACE: return UTIL_KEY_BACKSPACE;
		case RETROK_TAB: return UTIL_KEY_TAB;
		case RETROK_ESCAPE: return UTIL_KEY_ESC;
		case RETROK_LSHIFT: return UTIL_KEY_SHIFT_L;
		case RETROK_RSHIFT: return UTIL_KEY_SHIFT_R;
		case RETROK_LCTRL: return UTIL_KEY_CONTROL_L;
		case RETROK_RCTRL: return UTIL_KEY_CONTROL_R;
		case RETROK_LALT: return UTIL_KEY_ALT_L;
		case RETROK_RALT: return UTIL_KEY_ALT_R;
		case RETROK_CAPSLOCK: return UTIL_KEY_CAPS_LOCK;
		case RETROK_LEFT: return UTIL_KEY_LEFT;
		case RETROK_RIGHT: return UTIL_KEY_RIGHT;
		case RETROK_UP: return UTIL_KEY_UP;
		case RETROK_DOWN: return UTIL_KEY_DOWN;
		case RETROK_HOME: return UTIL_KEY_HOME;
		case RETROK_END: return UTIL_KEY_END;
		case RETROK_DELETE: return UTIL_KEY_DEL;
		case RETROK_PAGEUP: return UTIL_KEY_PAGE_UP;
		case RETROK_PAGEDOWN: return UTIL_KEY_PAGE_DOWN;
		case RETROK_COMMA: return UTIL_KEY_COMMA;
		case RETROK_PERIOD: return UTIL_KEY_PERIOD;
		case RETROK_MINUS: return '-';
		case RETROK_EQUALS: return '=';
		case RETROK_SEMICOLON: return ';';
		case RETROK_QUOTE: return '\'';
		case RETROK_SLASH: return '/';
		case RETROK_BACKSLASH: return '\\';
		case RETROK_LEFTBRACKET: return '[';
		case RETROK_RIGHTBRACKET: return ']';
		case RETROK_F1: return UTIL_KEY_F1;
		case RETROK_F2: return UTIL_KEY_F2;
		case RETROK_F3: return UTIL_KEY_F3;
		case RETROK_F4: return UTIL_KEY_F4;
		case RETROK_F5: return UTIL_KEY_F5;
		case RETROK_F6: return UTIL_KEY_F6;
		case RETROK_F7: return UTIL_KEY_F7;
		case RETROK_F8: return UTIL_KEY_F8;
		case RETROK_F9: return UTIL_KEY_F9;
		case RETROK_F10: return UTIL_KEY_F10;
		default: return -1;
	}
}

/* Key events are queued and applied in retro_run, since the frontend may
   call the keyboard callback from another thread than the one emulating. */
#define LR_KEY_QUEUE 64
static int key_queue[LR_KEY_QUEUE];
static int key_queue_pressed[LR_KEY_QUEUE];
static unsigned key_queue_len = 0;

static void RETRO_CALLCONV keyboard_cb(bool down, unsigned keycode,
	uint32_t character, uint16_t key_modifiers)
{
	int key;
	(void)character;
	(void)key_modifiers;

	key = retrok_to_zesarux(keycode);
	if (key < 0 || key_queue_len >= LR_KEY_QUEUE)
		return;
	key_queue[key_queue_len] = key;
	key_queue_pressed[key_queue_len] = down ? 1 : 0;
	key_queue_len++;
}

static void apply_keys(void)
{
	unsigned i;
	for (i = 0; i < key_queue_len; i++)
		util_set_reset_key((enum util_teclas)key_queue[i], key_queue_pressed[i]);
	key_queue_len = 0;
}

static int16_t joy_last = 0;

static void apply_joypad(void)
{
	int16_t joy = 0;
	int16_t changed;
	unsigned id;

	for (id = 0; id <= RETRO_DEVICE_ID_JOYPAD_R3; id++)
		if (input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, id))
			joy |= 1 << id;
	changed = joy ^ joy_last;
	joy_last = joy;
	if (!changed)
		return;

#define LR_JOY(bit, set, release) \
	if (changed & (1 << (bit))) { \
		if (joy & (1 << (bit))) set; else release; \
	}
	LR_JOY(RETRO_DEVICE_ID_JOYPAD_UP, joystick_set_up(0), joystick_release_up(0));
	LR_JOY(RETRO_DEVICE_ID_JOYPAD_DOWN, joystick_set_down(0), joystick_release_down(0));
	LR_JOY(RETRO_DEVICE_ID_JOYPAD_LEFT, joystick_set_left(0), joystick_release_left(0));
	LR_JOY(RETRO_DEVICE_ID_JOYPAD_RIGHT, joystick_set_right(0), joystick_release_right(0));
	LR_JOY(RETRO_DEVICE_ID_JOYPAD_B, joystick_set_fire(0, 0), joystick_release_fire(0, 0));
	LR_JOY(RETRO_DEVICE_ID_JOYPAD_A, joystick_set_fire(0, 1), joystick_release_fire(0, 1));
	LR_JOY(RETRO_DEVICE_ID_JOYPAD_Y, joystick_set_fire(0, 2), joystick_release_fire(0, 2));
	LR_JOY(RETRO_DEVICE_ID_JOYPAD_X, joystick_set_fire(0, 3), joystick_release_fire(0, 3));
#undef LR_JOY
}

/* libretro API -------------------------------------------------------------- */

RETRO_API unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
	struct retro_log_callback logging;
	bool no_game = false;

	environ_cb = cb;
	if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
		log_cb = logging.log;
	else
		log_cb = fallback_log;
	cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

RETRO_API void retro_init(void)
{
	if (!log_cb)
		log_cb = fallback_log;
}

RETRO_API void retro_deinit(void)
{
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
	memset(info, 0, sizeof(*info));
	info->library_name = "ZEsarUX";
	info->library_version = EMULATOR_VERSION;
	info->valid_extensions = "nex|snx|sna|z80|zsf|tap|tzx|pzx|mmc|img";
	info->need_fullpath = true;
	info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
	memset(info, 0, sizeof(*info));
	info->geometry.base_width = lr_width;
	info->geometry.base_height = lr_height;
	info->geometry.max_width = LR_MAX_WIDTH;
	info->geometry.max_height = LR_MAX_HEIGHT;
	info->geometry.aspect_ratio = 4.0f / 3.0f;
	info->timing.fps = LR_FPS;
	info->timing.sample_rate = LR_SAMPLE_RATE;
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
	(void)port;
	(void)device;
}

RETRO_API void retro_reset(void)
{
	if (lr_loaded)
		hard_reset_cpu();
}

static int has_extension(const char *path, const char *ext)
{
	const char *dot = strrchr(path, '.');
	return dot && !strcasecmp(dot + 1, ext);
}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
	const char *system_dir = NULL;
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
	struct retro_keyboard_callback kb = { keyboard_cb };
	char path[4096];

	if (!game || !game->path)
		return false;
	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
		return false;
	environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb);
	if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) || !system_dir)
		system_dir = ".";

	free_args();
	snprintf(path, sizeof(path), "%s/zesarux/zesarux", system_dir);
	add_arg(path);
	add_arg("--noconfigfile");
	add_arg("--vo"); add_arg("libretro");
	add_arg("--ao"); add_arg("libretro");
	add_arg("--machine"); add_arg("tbblue");
	add_arg("--nowelcomemessage");
	add_arg("--nosplash");
	add_arg("--disablefooter");
	add_arg("--disable-autoframeskip");
	add_arg("--frameskip"); add_arg("0");
	add_arg("--zoom"); add_arg("1");
	add_arg("--realvideo");
	add_arg("--joystickemulated"); add_arg("Kempston");
	add_arg("--tbblue-autoconfigure-sd-already-asked");

	if (has_extension(game->path, "mmc") || has_extension(game->path, "img")) {
		add_arg("--enable-mmc");
		add_arg("--enable-divmmc-ports");
		add_arg("--mmc-no-persistent-writes");
		add_arg("--mmc-file"); add_arg(game->path);
	} else {
		snprintf(path, sizeof(path), "%s/zesarux/tbblue.mmc", system_dir);
		if (si_existe_archivo(path)) {
			add_arg("--enable-mmc");
			add_arg("--enable-divmmc-ports");
			add_arg("--mmc-no-persistent-writes");
			add_arg("--mmc-file"); add_arg(path);
		}
		add_arg(game->path);
	}

	if (zesarux_main(lr_argc, lr_argv) != 0) {
		log_cb(RETRO_LOG_ERROR, "zesarux_main failed\n");
		return false;
	}

	lr_loaded = 1;
	lr_geometry_width = lr_width;
	lr_geometry_height = lr_height;
	return true;
}

RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
	(void)type;
	(void)info;
	(void)num;
	return false;
}

RETRO_API void retro_unload_game(void)
{
	lr_loaded = 0;
}

static void run_frame(void)
{
	long steps;

	timer_trigger_interrupt();
	for (steps = 0; steps < LR_MAX_STEPS; steps++) {
		cpu_core_loop();
		if (esperando_tiempo_final_t_estados.v && !timer_pthread_generada)
			return;
	}
	log_cb(RETRO_LOG_WARN, "frame did not end after %d steps\n", LR_MAX_STEPS);
}

static void push_audio(void)
{
	static int16_t out[LR_AUDIO_RING * 2];
	unsigned frames = lr_audio_write - lr_audio_read;
	unsigned i;

	for (i = 0; i < frames; i++) {
		unsigned pos = (lr_audio_read + i) % LR_AUDIO_RING;
		out[i * 2] = lr_audio_ring[pos * 2];
		out[i * 2 + 1] = lr_audio_ring[pos * 2 + 1];
	}
	lr_audio_read += frames;
	if (frames)
		audio_batch_cb(out, frames);
}

RETRO_API void retro_run(void)
{
	if (!lr_loaded)
		return;

	input_poll_cb();
	apply_keys();
	apply_joypad();

	run_frame();

	if (lr_width != lr_geometry_width || lr_height != lr_geometry_height) {
		struct retro_system_av_info av;
		retro_get_system_av_info(&av);
		environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &av.geometry);
		lr_geometry_width = lr_width;
		lr_geometry_height = lr_height;
	}

	video_cb(lr_pixels, lr_width, lr_height, lr_width * sizeof(uint32_t));
	push_audio();
}

RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
RETRO_API bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_PAL; }
RETRO_API void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
