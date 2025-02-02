#include "sdl_events.h"

#include <SDL.h>

#include "debugger.h"
#include "display.h"
#include "glue.h"
#include "imgui/imgui_impl_sdl2.h"
#include "joystick.h"
#include "keyboard.h"
#include "options.h"
#include "overlay/overlay.h"
#include "i2c.h"
#include "timing.h"
#include "vera/sdcard.h"

#ifdef __APPLE__
#	define LSHORTCUT_KEY SDL_SCANCODE_LGUI
#	define RSHORTCUT_KEY SDL_SCANCODE_RGUI
#else
#	define LSHORTCUT_KEY SDL_SCANCODE_LCTRL
#	define RSHORTCUT_KEY SDL_SCANCODE_RCTRL
#endif

bool mouse_captured = false;

bool sdl_events_update()
{
	static bool cmd_down = false;
	static bool alt_down = false;

	bool mouse_state_change = false;
	bool do_capture_mouse   = false;
	bool do_release_mouse   = false;

	const uint32_t event_handling_start_us = timing_total_microseconds_realtime();
	SDL_Window *   window                  = display_get_window();
	const ImVec4   display_rect            = display_get_rect();
	const int      display_middle_x        = (int)(display_rect.x + display_rect.z) / 2;
	const int      display_middle_y        = (int)(display_rect.y + display_rect.w) / 2;

	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_QUIT:
				return false;

			case SDL_JOYDEVICEADDED:
				joystick_add(event.jdevice.which);
				break;

			case SDL_JOYDEVICEREMOVED:
				joystick_remove(event.jdevice.which);
				break;

			default:
				break;
		}

		ImGui_ImplSDL2_ProcessEvent(&event);

		if (!display_focused || ImGui::GetIO().WantTextInput) {
			do_release_mouse = true;
			continue;
		}

		switch (event.type) {
			case SDL_CONTROLLERBUTTONDOWN:
				joystick_button_down(event.cbutton.which, event.cbutton.button);
				break;

			case SDL_CONTROLLERBUTTONUP:
				joystick_button_up(event.cbutton.which, event.cbutton.button);
				break;

			case SDL_KEYDOWN: {
				bool consumed = false;
				if (event.key.keysym.sym == SDLK_F12) {
					Show_cpu_monitor = true;
					Show_disassembler = true;
					debugger_pause_execution();
				}
				if (!Options.no_keybinds) {
					if (cmd_down) {
						switch (event.key.keysym.sym) {
							case SDLK_s:
								machine_dump("user keyboard request");
								consumed = true;
								break;
							case SDLK_r:
								machine_reset();
								consumed = true;
								break;
							case SDLK_v:
								keyboard_add_text(SDL_GetClipboardText());
								consumed = true;
								break;
							case SDLK_f:
							case SDLK_RETURN:
								display_toggle_fullscreen();
								consumed = true;
								break;
							case SDLK_PLUS:
							case SDLK_EQUALS:
								machine_toggle_warp();
								consumed = true;
								break;
							case SDLK_a:
								sdcard_attach();
								consumed = true;
								break;
							case SDLK_d:
								sdcard_detach();
								consumed = true;
								break;
							case SDLK_m:
								if (mouse_captured) {
									do_release_mouse = true;
								} else {
									do_capture_mouse = true;
								}
								consumed = true;
								break;
						}
					}
					if (cmd_down && alt_down) {
						switch (event.key.keysym.sym) {
							case SDLK_b:
								Show_breakpoints = true;
								consumed         = true;
								break;
							case SDLK_c:
								Show_cpu_monitor = true;
								consumed         = true;
								break;
							case SDLK_d:
								Show_disassembler = true;
								consumed          = true;
								break;
							case SDLK_s:
								Show_symbols_list = true;
								consumed          = true;
								break;
							case SDLK_w:
								Show_watch_list = true;
								consumed        = true;
								break;
						}
					}
					if (event.key.keysym.scancode == LSHORTCUT_KEY || event.key.keysym.scancode == RSHORTCUT_KEY) {
						cmd_down = true;
					}
					if (event.key.keysym.scancode == SDL_SCANCODE_LALT || event.key.keysym.scancode == SDL_SCANCODE_RALT) {
						alt_down = true;
					}
				}
				if (!consumed) {
					keyboard_add_event(true, event.key.keysym.scancode);
				}
				break;
			}

			case SDL_KEYUP:
				if (event.key.keysym.scancode == LSHORTCUT_KEY || event.key.keysym.scancode == RSHORTCUT_KEY) {
					cmd_down = false;
				}
				if (event.key.keysym.scancode == SDL_SCANCODE_LALT || event.key.keysym.scancode == SDL_SCANCODE_RALT) {
					alt_down = false;
				}
				keyboard_add_event(false, event.key.keysym.scancode);
				break;

			case SDL_MOUSEBUTTONDOWN:
				// A bit hacky way to exclude resize hitboxes
				if (
					ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow &&
					event.button.x >= display_rect.x && event.button.x < display_rect.z &&
					event.button.y >= display_rect.y && event.button.y < display_rect.w
				) {
					if (!mouse_captured) {
						do_capture_mouse = true;
					}
					mouse_state_change = true;
					switch (event.button.button) {
						case SDL_BUTTON_LEFT:
							mouse_button_down(0);
							break;
						case SDL_BUTTON_RIGHT:
							mouse_button_down(1);
							break;
						case SDL_BUTTON_MIDDLE:
							mouse_button_down(2);
							break;
					}
				}
				break;

			case SDL_MOUSEBUTTONUP:
				mouse_state_change = true;
				switch (event.button.button) {
					case SDL_BUTTON_LEFT:
						mouse_button_up(0);
						break;
					case SDL_BUTTON_RIGHT:
						mouse_button_up(1);
						break;
					case SDL_BUTTON_MIDDLE:
						mouse_button_up(2);
						break;
				}
				break;

			// Stub for mouse wheel support on emu side.
			// does nothing just yet
			case SDL_MOUSEWHEEL:
				// mouse_state_change = true; // uncomment when code activated
				if (event.wheel.y != 0) {
					// mouse Z axis change
				}
				if (event.wheel.x != 0) {
					// mouse W axis change
				}
				break;

			case SDL_MOUSEMOTION:
				if (mouse_captured) {
					mouse_state_change = true;
					mouse_move(event.motion.xrel, event.motion.yrel);
					SDL_WarpMouseInWindow(window, display_middle_x, display_middle_y);
				}
			    break;

			default:
				break;
		}
	}

	if (do_capture_mouse) {
		mouse_captured = true;
		SDL_WarpMouseInWindow(window, display_middle_x, display_middle_y);
	}
	if (do_release_mouse) {
		mouse_captured = false;
	}

	const uint32_t event_handling_end_us = timing_total_microseconds_realtime();
	display_refund_render_time(event_handling_end_us - event_handling_start_us);

	if (mouse_state_change) {
		mouse_send_state();
	}
	return true;
}
