/*
    ZEsarUX drivers for the libretro core.

    Modelled on scrsdl2.c and audiosdl2.c, but nothing here touches a window
    or a device: frames land in lr_pixels and samples in lr_audio_ring, and
    retro_run passes both on. The timer never fires by itself; retro_run
    triggers one interrupt per call, which is what paces the emulation.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "debug.h"
#include "screen.h"
#include "audio.h"
#include "timer.h"
#include "settings.h"
#include "zxvision.h"
#include "joystick.h"
#include "realjoystick.h"
#include "charset.h"
#include "utils.h"
#include "cpc.h"
#include "prism.h"
#include "sam.h"
#include "ql.h"
#include "msx.h"
#include "coleco.h"
#include "sg1000.h"
#include "sms.h"
#include "svi.h"
#include "pcw.h"
#include "baseconf.h"
#include "scorpion.h"

#include "lr_drivers.h"

uint32_t *lr_pixels = NULL;
int lr_width = 0;
int lr_height = 0;
int lr_frame_presented = 0;

int16_t lr_audio_ring[LR_AUDIO_RING * 2];
unsigned lr_audio_write = 0;
unsigned lr_audio_read = 0;

/* Timer --------------------------------------------------------------------- */

static int lr_timer_start(void)
{
	return 1;
}

static void lr_timer_stop(void)
{
}

/* Video --------------------------------------------------------------------- */

static int lr_window_width(void)
{
	return screen_get_window_size_width_zoom_border_en();
}

static int lr_window_height(void)
{
	return screen_get_window_size_height_zoom_border_en();
}

static void scrlibretro_putpixel_final_rgb(int x, int y, unsigned int color_rgb)
{
	if (x < 0 || y < 0 || x >= lr_width || y >= lr_height)
		return;
	lr_pixels[y * lr_width + x] = color_rgb | 0xFF000000;
}

static void scrlibretro_putpixel_final(int x, int y, unsigned int color)
{
	scrlibretro_putpixel_final_rgb(x, y, spectrum_colortable[color]);
}

static void scrlibretro_putpixel(int x, int y, unsigned int color)
{
	if (menu_overlay_activo == 0) {
		scrlibretro_putpixel_final(x, y, color);
		return;
	}
	buffer_layer_machine[y * ancho_layer_menu_machine + x] = color;
	screen_putpixel_mix_layers(x, y);
}

static void scrlibretro_putchar_zx8081(int x, int y, z80_byte caracter)
{
	scr_putchar_zx8081_comun(x, y, caracter);
}

static void scrlibretro_putchar_menu(int x, int y, z80_byte caracter, int tinta, int papel)
{
	z80_bit inverse;

	inverse.v = 0;
	if (caracter < 32 || caracter > MAX_CHARSET_GRAPHIC)
		caracter = '?';
	scr_putchar_menu_comun_zoom(caracter, x, y, inverse, tinta, papel, menu_gui_zoom);
}

static void scrlibretro_putchar_footer(int x, int y, z80_byte caracter, int tinta, int papel)
{
	scr_putchar_footer_comun_zoom(caracter, x, y, tinta, papel);
}

static void scrlibretro_nothing(void)
{
}

static void scrlibretro_detectedchar_print(z80_byte caracter GCC_UNUSED)
{
}

static void scrlibretro_messages_debug(char *s)
{
	fprintf(stderr, "%s\n", s);
}

static int scrlibretro_get_menu_width(void)
{
	int max = screen_get_emulated_display_width_no_zoom_border_en();

	max = max / menu_char_width / menu_gui_zoom / screen_reduce_menu_ancho;
	if (max > OVERLAY_SCREEN_MAX_WIDTH)
		max = OVERLAY_SCREEN_MAX_WIDTH;
	return max;
}

static int scrlibretro_get_menu_height(void)
{
	int max = screen_get_emulated_display_height_no_zoom_border_en();

	max = max / menu_char_height / menu_gui_zoom / screen_reduce_menu_alto;
	if (max > OVERLAY_SCREEN_MAX_HEIGTH)
		max = OVERLAY_SCREEN_MAX_HEIGTH;
	return max;
}

static void scrlibretro_refresca_pantalla_solo_driver(void)
{
	lr_frame_presented = 1;
}

/* Same machine dispatch as scrsdl_refresca_pantalla. */
static void scrlibretro_refresca_pantalla(void)
{
	if (sem_screen_refresh_reallocate_layers)
		return;
	sem_screen_refresh_reallocate_layers = 1;

	if (MACHINE_IS_ZX8081) {
		scr_refresca_pantalla_y_border_zx8081();
	} else if (MACHINE_IS_PRISM) {
		screen_prism_refresca_pantalla();
	} else if (MACHINE_IS_TBBLUE) {
		screen_tbblue_refresca_pantalla();
	} else if (MACHINE_IS_SPECTRUM) {
		if (MACHINE_IS_TSCONF) {
			screen_tsconf_refresca_pantalla();
		} else if (MACHINE_IS_BASECONF) {
			baseconf_refresca_pantalla();
		} else if (MACHINE_IS_SCORPION_GMX) {
			scorpiongmx_refresca_pantalla();
		} else if (rainbow_enabled.v == 0) {
			if (border_enabled.v && modificado_border.v) {
				scr_refresca_border();
				modificado_border.v = 0;
			}
			scr_refresca_pantalla_comun();
		} else {
			scr_refresca_pantalla_rainbow_comun_spectrum();
		}
	} else if (MACHINE_IS_Z88) {
		screen_z88_refresca_pantalla();
	} else if (MACHINE_IS_ACE) {
		scr_refresca_pantalla_y_border_ace();
	} else if (MACHINE_IS_CPC) {
		scr_refresca_pantalla_y_border_cpc();
	} else if (MACHINE_IS_PCW) {
		scr_refresca_pantalla_y_border_pcw();
	} else if (MACHINE_IS_SAM) {
		scr_refresca_pantalla_y_border_sam();
	} else if (MACHINE_IS_QL) {
		scr_refresca_pantalla_y_border_ql();
	} else if (MACHINE_IS_MK14) {
		scr_refresca_pantalla_y_border_mk14();
	} else if (MACHINE_IS_MSX) {
		scr_refresca_pantalla_y_border_msx();
	} else if (MACHINE_IS_SVI) {
		scr_refresca_pantalla_y_border_svi();
	} else if (MACHINE_IS_COLECO) {
		scr_refresca_pantalla_y_border_coleco();
	} else if (MACHINE_IS_SG1000) {
		scr_refresca_pantalla_y_border_sg1000();
	} else if (MACHINE_IS_SMS) {
		scr_refresca_pantalla_y_border_sms();
	}

	screen_render_menu_overlay_if_active();
	scrlibretro_refresca_pantalla_solo_driver();

	sem_screen_refresh_reallocate_layers = 0;
}

static z80_byte scrlibretro_lee_puerto(z80_byte puerto_h GCC_UNUSED, z80_byte puerto_l GCC_UNUSED)
{
	return 255;
}

int scrlibretro_init(void)
{
	debug_printf(VERBOSE_INFO, "Init libretro Video Driver");

	screen_este_driver_permite_ext_desktop = 0;

	scr_putpixel = scrlibretro_putpixel;
	scr_putpixel_final = scrlibretro_putpixel_final;
	scr_putpixel_final_rgb = scrlibretro_putpixel_final_rgb;
	scr_get_menu_width = scrlibretro_get_menu_width;
	scr_get_menu_height = scrlibretro_get_menu_height;
	scr_putchar_zx8081 = scrlibretro_putchar_zx8081;
	scr_debug_registers = scrlibretro_nothing;
	scr_messages_debug = scrlibretro_messages_debug;
	scr_putchar_menu = scrlibretro_putchar_menu;
	scr_putchar_footer = scrlibretro_putchar_footer;
	scr_set_fullscreen = scrlibretro_nothing;
	scr_reset_fullscreen = scrlibretro_nothing;
	scr_z88_cpc_load_keymap = scrlibretro_nothing;
	scr_detectedchar_print = scrlibretro_detectedchar_print;
	scr_update_window_title = NULL;
	scr_get_text_clipboard = NULL;
	scr_tiene_colores = 1;
	screen_refresh_menu = 1;

	lr_width = lr_window_width();
	lr_height = lr_window_height();
	free(lr_pixels);
	lr_pixels = calloc((size_t)lr_width * lr_height, sizeof(uint32_t));
	if (lr_pixels == NULL)
		return 1;

	scr_reallocate_layers_menu(lr_width, lr_height);
	modificado_border.v = 1;

	timer_add_timer_to_top(available_timers, TIMER_LIBRETRO, "libretro",
		lr_timer_start, lr_timer_stop);

	scr_set_driver_name("libretro");
	return 0;
}

static void scrlibretro_end(void)
{
	debug_printf(VERBOSE_INFO, "Closing libretro video driver");
}

static void scrlibretro_actualiza_tablas_teclado(void)
{
}

int set_scrdriver_libretro(void)
{
	scr_refresca_pantalla = scrlibretro_refresca_pantalla;
	scr_refresca_pantalla_solo_driver = scrlibretro_refresca_pantalla_solo_driver;
	scr_init_pantalla = scrlibretro_init;
	scr_end_pantalla = scrlibretro_end;
	scr_lee_puerto = scrlibretro_lee_puerto;
	scr_actualiza_tablas_teclado = scrlibretro_actualiza_tablas_teclado;
	return 0;
}

/* Audio --------------------------------------------------------------------- */

int audiolibretro_init(void)
{
	debug_printf(VERBOSE_INFO, "Init libretro Audio Driver, %d Hz", FRECUENCIA_SONIDO);
	audio_set_driver_name("libretro");
	return 0;
}

/* One frame of signed 8 bit stereo, AUDIO_BUFFER_SIZE samples per channel. */
static void audiolibretro_send_frame(char *buffer)
{
	int i;

	if (audio_playing.v == 0)
		audio_playing.v = 1;

	for (i = 0; i < AUDIO_BUFFER_SIZE; i++) {
		unsigned pos = lr_audio_write % LR_AUDIO_RING;

		if (lr_audio_write - lr_audio_read >= LR_AUDIO_RING)
			lr_audio_read++;
		lr_audio_ring[pos * 2] = (int16_t)(buffer[i * 2] * 256);
		lr_audio_ring[pos * 2 + 1] = (int16_t)(buffer[i * 2 + 1] * 256);
		lr_audio_write++;
	}
}

static int audiolibretro_thread_finish(void)
{
	return 0;
}

static void audiolibretro_end(void)
{
	audio_playing.v = 0;
}

static void audiolibretro_get_buffer_info(int *buffer_size, int *current_size)
{
	*buffer_size = LR_AUDIO_RING;
	*current_size = (int)(lr_audio_write - lr_audio_read);
}

static int audiolibretro_can_record_input(void)
{
	return 0;
}

int set_audiodriver_libretro(void)
{
	audio_init = audiolibretro_init;
	audio_send_frame = audiolibretro_send_frame;
	audio_thread_finish = audiolibretro_thread_finish;
	audio_end = audiolibretro_end;
	audio_get_buffer_info = audiolibretro_get_buffer_info;
	audio_can_record_input = audiolibretro_can_record_input;
	return 0;
}
