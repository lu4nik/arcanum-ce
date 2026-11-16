#include <stdio.h>

#include <SDL3/SDL_main.h>

#ifdef SDL_PLATFORM_WINDOWS
#include <direct.h>
#else
#include <unistd.h>
#endif

#include <tig/tig.h>

#include "game/ai.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/descriptions.h"
#include "game/dialog.h"
#include "game/gamelib.h"
#include "game/gmovie.h"
#include "game/gsound.h"
#include "game/highres_config.h"
#include "game/hrp.h"
#include "game/item.h"
#include "game/level.h"
#include "game/light_scheme.h"
#include "game/location.h"
#include "game/magictech.h"
#include "game/map.h"
#include "game/name.h"
#include "game/obj.h"
#include "game/object.h"
#include "game/path.h"
#include "game/player.h"
#include "game/proto.h"
#include "game/roof.h"
#include "game/script.h"
#include "game/scroll.h"
#include "game/spell.h"
#include "game/stat.h"
#include "game/tech.h"
#include "game/wallcheck.h"
#include "ui/charedit_ui.h"
#include "ui/dialog_ui.h"
#include "ui/gameuilib.h"
#include "ui/intgame.h"
#include "ui/iso.h"
#include "ui/logbook_ui.h"
#include "ui/mainmenu_ui.h"
#include "ui/textedit_ui.h"
#include "ui/wmap_rnd.h"
#include "ui/wmap_ui.h"

static void main_loop(void);
static void handle_mouse_scroll(void);
static void handle_wheel_pan(void);
static void handle_auto_save(void);
static void handle_keyboard_scroll(void);
static void build_cmd_line(char* dst, size_t size, int argc, char** argv);

// 0x59A040
static float gamma = 1.0f;

// 0x5CFF00
static int dword_5CFF00;

// 0x401000
/**
 * Local tweak: also write the engine's debug log to
 * ~/Library/Logs/arcanum-ce.log (macOS) or the pref path elsewhere, truncated
 * on every start, so that a crash leaves something to read.
 */
static FILE* main_log_file;
static SDL_LogOutputFunction main_log_default_fn;
static void* main_log_default_userdata;

static void main_log_output(void* userdata, int category, SDL_LogPriority priority, const char* message)
{
    (void)userdata;

    if (main_log_file != NULL) {
        fprintf(main_log_file, "%s\n", message);
        fflush(main_log_file);
    }

    if (main_log_default_fn != NULL) {
        main_log_default_fn(main_log_default_userdata, category, priority, message);
    }
}

static void main_log_to_file(void)
{
    char path[1024];
    const char* home = SDL_getenv("HOME");

#if SDL_PLATFORM_MACOS
    if (home == NULL) {
        return;
    }
    SDL_snprintf(path, sizeof(path), "%s/Library/Logs/arcanum-ce.log", home);
#else
    (void)home;
    char* pref = SDL_GetPrefPath("arcanum-ce", "logs");
    if (pref == NULL) {
        return;
    }
    SDL_snprintf(path, sizeof(path), "%sarcanum-ce.log", pref);
    SDL_free(pref);
#endif

    main_log_file = fopen(path, "w");
    if (main_log_file == NULL) {
        return;
    }

    SDL_GetLogOutputFunction(&main_log_default_fn, &main_log_default_userdata);
    SDL_SetLogOutputFunction(main_log_output, NULL);
}

int main(int argc, char** argv)
{
    TigInitInfo init_info;
    TigVideoScreenshotSettings screenshotter;
    GameInitInfo game_init_info;
    const HighResConfig* highres_config;
    char* pch;
    int value;
    tig_art_id_t cursor_art_id;
    int64_t pc_starting_location;
    char msg[80];

#if SDL_PLATFORM_MACOS
    chdir(SDL_GetBasePath());
#elif SDL_PLATFORM_IOS
    chdir(SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS));
#elif SDL_PLATFORM_ANDROID
    chdir(SDL_GetPrefPath("com.alexbatalov", "arcanumce"));
#endif

    // Convert args array to WinMain-like lpCmdLine.
    char lpCmdLine[260];
    build_cmd_line(lpCmdLine, sizeof(lpCmdLine), argc, argv);

    main_log_to_file();

    highres_config_load();
    highres_config = highres_config_get();

#if SDL_PLATFORM_MACOS
    // Local tweak: Renderer=1 in HighRes/config.ini presents through Metal
    // instead of OpenGL (overrides the driver hint set in tig video).
    if (highres_config->renderer == 1) {
        SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "metal,opengl", SDL_HINT_OVERRIDE);
    }
#endif

    init_info.texture_width = 1024;
    init_info.texture_height = 1024;
    init_info.flags = 0;

    if (highres_config->show_fps) {
        init_info.flags |= TIG_INITIALIZE_FPS;
    }

    pch = lpCmdLine;
    while (*pch != '\0') {
        *pch = (char)(unsigned char)SDL_tolower(*(unsigned char*)pch);
        pch++;
    }

    if (strstr(lpCmdLine, "-fps") != NULL) {
        init_info.flags |= TIG_INITIALIZE_FPS;
    }

    if (strstr(lpCmdLine, "-nosound") != NULL) {
        init_info.flags |= TIG_INITIALIZE_NO_SOUND;
    }

    pch = strstr(lpCmdLine, "-vidfreed");
    if (pch != NULL) {
        value = atoi(pch + 8);
        if (value > 0) {
            tig_art_cache_set_video_memory_fullness(value);
        }
    }

    if (strstr(lpCmdLine, "-animcatchup") != NULL) {
        anim_catch_up_enable();
    }

    if (strstr(lpCmdLine, "-animdebug") != NULL) {
        anim_debug_enable();
    }

    if (strstr(lpCmdLine, "-norandom") != NULL) {
        wmap_rnd_disable();
    }

    sub_549A70();

    // NOTE: These were original switches to set cheat level. They were removed
    // from Arcanum release version.
    if (strstr(lpCmdLine, "-2680") != NULL) {
        gamelib_cheat_level_set(1);
    }

    if (strstr(lpCmdLine, "-0897") != NULL) {
        gamelib_cheat_level_set(2);
    }

    if (strstr(lpCmdLine, "-4637") != NULL) {
        gamelib_cheat_level_set(3);
    }

    pch = strstr(lpCmdLine, "-pathlimit");
    if (pch != NULL) {
        value = atoi(pch + 10);
        if (value > 0) {
            path_set_limit(value);
        }
    }

    pch = strstr(lpCmdLine, "-pathtimelimit");
    if (pch != NULL) {
        value = atoi(pch + 14); // FIX: Length was wrong (10).
        if (value > 0) {
            path_set_time_limit(value);
        }
    }

    if (strstr(lpCmdLine, "-fullscreen") != NULL) {
        intgame_set_fullscreen();
        intgame_toggle_interface();
    }

    pch = strstr(lpCmdLine, "-patchlvl");
    if (pch != NULL) {
        gamelib_patch_lvl_set(pch + 9);
    }

    init_info.width = highres_config->width;
    init_info.height = highres_config->height;
    init_info.bpp = 32;
    init_info.art_file_path_resolver = name_resolve_path;
    init_info.art_id_reset_func = name_normalize_aid;
    init_info.sound_file_path_resolver = gsound_resolve_path;

    if (highres_config->windowed) {
        init_info.flags |= TIG_INITIALIZE_WINDOWED;
    }

    // NOTE: The `window` switch is borrowed from ToEE.
    if (strstr(lpCmdLine, "-window") != NULL) {
        init_info.flags |= TIG_INITIALIZE_WINDOWED;
    }

    // NOTE: The `geometry` switch is also borrowed from ToEE, but the
    // implementation is different (original implementaion is wrong).
    pch = strstr(lpCmdLine, "-geometry");
    if (pch != NULL) {
        int width;
        int height;

        if (sscanf(pch + 10, "%dx%d", &width, &height) == 2) {
            init_info.width = width;
            init_info.height = height;
        }
    }

    // Specify window name.
    init_info.flags |= TIG_INITIALIZE_SET_WINDOW_NAME;
    init_info.window_name = "Arcanum: Of Steamworks & Magick Obscura - Community Edition";

    if (tig_init(&init_info) != TIG_OK) {
        return EXIT_SUCCESS; // FIXME: Should be `EXIT_FAILURE`.
    }

    // Local tweak: optional xBR upscale filter (toggle in game with F9).
    tig_video_set_smooth(highres_config->smooth);
    tig_video_set_smooth_ui(highres_config->smooth_ui);

    screenshotter.key = SDL_SCANCODE_F12;
    screenshotter.field_4 = 0;
    tig_video_screenshot_set_settings(&screenshotter);

    intgame_set_iso_window_width(init_info.width);
    intgame_set_iso_window_height(init_info.height);
    if (!intgame_create_iso_window(&(game_init_info.iso_window_handle))) {
        tig_exit();
        return EXIT_SUCCESS; // FIXME: Should be `EXIT_FAILURE`.
    }

    tig_mouse_hide();

    game_init_info.editor = false;
    game_init_info.invalidate_rect_func = iso_invalidate_rect;
    game_init_info.draw_func = iso_redraw;

    if (!gamelib_init(&game_init_info)) {
        tig_window_destroy(game_init_info.iso_window_handle);
        tig_exit();
        return EXIT_SUCCESS; // FIXME: Should be `EXIT_FAILURE`.
    }

    if (strstr(lpCmdLine, "-dialogcheck") != NULL) {
        dialog_check();
    }

    if (strstr(lpCmdLine, "-dialognumber") != NULL) {
        dialog_enable_numbers();
    }

    if (strstr(lpCmdLine, "-gendercheck") != NULL) {
        map_enable_gender_check();
    }

    pch = strstr(lpCmdLine, "-scrollfps:");
    if (pch != NULL) {
        value = atoi(pch + 11);
        scroll_fps_set(value);
    } else {
        scroll_fps_set(highres_config->scroll_fps);
    }

    pch = strstr(lpCmdLine, "-scrolldist:");
    if (pch != NULL) {
        value = atoi(pch + 12);
        scroll_distance_set(value);
    } else {
        scroll_distance_set(highres_config->scroll_dist);
    }

    pch = strstr(lpCmdLine, "-mod:");
    if (pch != NULL) {
        gamelib_default_module_name_set(pch + 5);
    }

    tig_art_interface_id_create(0, 0, 0, 0, &cursor_art_id);
    tig_mouse_cursor_set_art_id(cursor_art_id);

    if (!gameuilib_init(&game_init_info)) {
        gameuilib_exit();
        tig_window_destroy(game_init_info.iso_window_handle);
        tig_exit();
        return EXIT_SUCCESS; // FIXME: Should be `EXIT_FAILURE`.
    }

    if (!gamelib_mod_load(gamelib_default_module_name_get())) {
        tig_debug_printf("Error loading default module %s\n",
            gamelib_default_module_name_get());
        // FIXME: Missing graceful shutdown sequence.
        return EXIT_SUCCESS; // FIXME: Should be `EXIT_FAILURE`.
    }

    if (!gameuilib_mod_load()) {
        tig_debug_printf("Error loading UI module data\n");
        // FIXME: Missing graceful shutdown sequence.
        return EXIT_SUCCESS; // FIXME: Should be `EXIT_FAILURE`.
    }

    if (highres_config->intro) {
        gmovie_play(8, GAME_MOVIE_NO_FINAL_FLIP, 0);
    }

    if (!mainmenu_ui_handle()) {
        gameuilib_exit();
        gamelib_exit();
        tig_exit();
        tig_memory_print_stats(TIG_MEMORY_STATS_PRINT_ALL_BLOCKS);
        return EXIT_SUCCESS; // FIXME: Should be `EXIT_FAILURE`.
    }

    if (strstr(lpCmdLine, "-logcheck") != NULL) {
        logbook_ui_check();
    }

    tig_debug_printf("[Beginning Game]\n");

    pc_starting_location = obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION);
    snprintf(msg, sizeof(msg),
        "Player Start Position: x: %d, y: %d",
        (int)LOCATION_GET_X(pc_starting_location),
        (int)LOCATION_GET_Y(pc_starting_location));
    tig_debug_printf("%s\n", msg);

    main_loop();

    gameuilib_mod_unload();
    gamelib_mod_unload();
    gameuilib_exit();
    gamelib_exit();
    tig_exit();
    tig_memory_print_stats(TIG_MEMORY_STATS_PRINT_ALL_BLOCKS);

    return EXIT_SUCCESS;
}

// 0x401560
void main_loop(void)
{
    int64_t location;
    int64_t pc_obj;
    tig_art_id_t art_id;
    bool enable_profiler = false;
    bool disable_profiler = false;
    bool output_profile_data = false;
    TigMessage message;
    int index;
    TigMouseState mouse_state;
    int64_t mouse_loc;
    char version_str[80];
    char mouse_state_str[20];
    char story_state_str[80];

    // TODO: Figure out if this is really needed.
#if 0
    if (!location_at(400, 200, &location)) {
        return;
    }
#endif

    pc_obj = player_get_local_pc_obj();
    location = obj_field_int64_get(pc_obj, OBJ_F_LOCATION);
    sub_43E770(pc_obj, location, 0, 0);
    location_origin_set(location);

    art_id = obj_field_int32_get(pc_obj, OBJ_F_CURRENT_AID);
    art_id = tig_art_id_frame_set(art_id, 0);
    art_id = tig_art_id_anim_set(art_id, 0);
    object_set_current_aid(pc_obj, art_id);

    object_flags_unset(pc_obj, OF_OFF);
    sub_430460();
    iso_interface_refresh();
    intgame_draw_bar(INTGAME_BAR_HEALTH);
    intgame_draw_bar(INTGAME_BAR_FATIGUE);

    while (1) {
        if (enable_profiler) {
            intgame_message_window_display_str(-1, "Enabling profiler...\n");
            enable_profiler = false;
        }

        if (disable_profiler) {
            intgame_message_window_display_str(-1, "Disabling profiler...\n");
            disable_profiler = false;
        }

        if (output_profile_data) {
            intgame_message_window_display_str(-1, "Outputing profile data...\n");
            output_profile_data = false;
        }

        tig_ping();
        gamelib_ping();
        iso_redraw();
        tig_window_display();

        pc_obj = player_get_local_pc_obj();

        while (tig_message_dequeue(&message) == TIG_OK) {
            if (message.type == TIG_MESSAGE_QUIT
                && mainmenu_ui_confirm_quit() == TIG_WINDOW_MODAL_DIALOG_CHOICE_OK) {
                mainmenu_ui_reset();
                return;
            }

            if (message.type == TIG_MESSAGE_REDRAW) {
                gamelib_redraw();
            }

            intgame_process_event(&message);

            if (gameuilib_wants_mainmenu()) {
                mainmenu_ui_start(MM_TYPE_DEFAULT);
                if (!mainmenu_ui_handle()) {
                    return;
                }
            }

            if (intgame_mode_supports_scrolling(intgame_mode_get())) {
                handle_keyboard_scroll();
            }

            if (message.type == TIG_MESSAGE_KEYBOARD) {
                // CE: Toggle highlight mode when Left Alt is pressed.
                if (message.data.keyboard.scancode == SDL_SCANCODE_LALT) {
                    object_highlight_mode_set(message.data.keyboard.pressed);
                }

                if (!message.data.keyboard.pressed) {
                    switch (message.data.keyboard.scancode) {
                    case SDL_SCANCODE_ESCAPE: {
                        IntgameMode esc_mode = intgame_mode_get();
                        if (esc_mode != INTGAME_MODE_MAIN
                            && esc_mode != INTGAME_MODE_DIALOG
                            && esc_mode != INTGAME_MODE_BARTER) {
                            intgame_mode_set(INTGAME_MODE_MAIN);
                            break;
                        }
                        if (dialog_ui_is_local_pc_in_dialog()
                            || wmap_ui_is_created()
                            || (combat_turn_based_is_active()
                                && player_get_local_pc_obj() != combat_turn_based_whos_turn_get())) {
                            mainmenu_ui_start(MM_TYPE_IN_PLAY_LOCKED);
                        } else {
                            mainmenu_ui_start(MM_TYPE_IN_PLAY);
                        }
                        if (!mainmenu_ui_handle()) {
                            return;
                        }
                        break;
                    }
                    case SDL_SCANCODE_F9:
                        // Local tweak: toggle the xBR upscale filter.
                        tig_video_set_smooth((tig_video_get_smooth() + 1) % 4);
                        break;
                    case SDL_SCANCODE_F10:
                        intgame_toggle_interface();
                        tig_debug_printf("iso_redraw...");
                        iso_redraw();
                        tig_debug_printf("tig_window_display...");
                        tig_window_display();
                        tig_debug_printf("completed.\n");
                        break;
                    case SDL_SCANCODE_O:
                        if (!textedit_ui_is_focused()) {
                            mainmenu_ui_start(MM_TYPE_OPTIONS);
                            if (!mainmenu_ui_handle()) {
                                return;
                            }
                        }
                        break;
                    case SDL_SCANCODE_F7:
                        if (!critter_is_dead(player_get_local_pc_obj())) {
                            if (wmap_ui_is_created()) {
                                wmap_ui_close();
                                tig_ping();
                                gamelib_ping();
                                iso_redraw();
                                tig_window_display();
                            }

                            if (!combat_turn_based_is_active() || player_get_local_pc_obj() == combat_turn_based_whos_turn_get()) {
                                intgame_mode_set(INTGAME_MODE_MAIN);
                                intgame_mode_set(INTGAME_MODE_MAIN);
                                mainmenu_ui_feedback_saving();
                                gamelib_save("SlotAuto", "Auto-Save");
                                mainmenu_ui_feedback_saving_completed();
                            } else {
                                mainmenu_ui_feedback_cannot_save_in_tb();
                            }
                        }
                        break;
                    case SDL_SCANCODE_F8:
                        mainmenu_ui_feedback_loading();
                        sub_543220();
                        mainmenu_ui_feedback_loading_completed();
                        break;
                    case SDL_SCANCODE_F11:
                        if (gamelib_cheat_level_get() >= 3) {
                            for (index = 0; index < SPELL_COUNT; index++) {
                                spell_add(pc_obj, index, true);
                            }

                            stat_base_set(pc_obj, STAT_INTELLIGENCE, 20);
                            stat_base_set(pc_obj, STAT_WILLPOWER, 20);

                            for (index = 0; index < SPELL_COUNT; index++) {
                                spell_add(pc_obj, index, true);
                            }

                            magictech_cheat_mode_on();

                            critter_fatigue_damage_set(pc_obj, 0);
                            iso_interface_refresh();
                            intgame_draw_bar(INTGAME_BAR_HEALTH);
                            intgame_draw_bar(INTGAME_BAR_FATIGUE);
                        }
                        break;
                    default:
                        break;
                    }

                    if (!textedit_ui_is_focused()) {
                        if (gamelib_cheat_level_get() >= 3) {
                            switch (message.data.keyboard.scancode) {
                            case SDL_SCANCODE_H:
                                timeevent_inc_milliseconds(3600000);
                                break;
                            case SDL_SCANCODE_N:
                                object_hp_damage_set(pc_obj, 0);
                                critter_fatigue_damage_set(pc_obj, 0);
                                object_flags_unset(pc_obj, OF_NO_BLOCK | OF_FLAT);
                                critter_decay_timeevent_cancel(pc_obj);
                                break;
                            case SDL_SCANCODE_GRAVE:
                                stat_base_set(pc_obj, STAT_INTELLIGENCE, 20);
                                for (index = 0; index < 8; index++) {
                                    tech_degree_inc(pc_obj, index);
                                }
                                charedit_refresh();
                                break;
                            case SDL_SCANCODE_P:
                                ai_npc_fighting_toggle();
                                break;
                            default:
                                break;
                            }
                        }

                        if (gamelib_cheat_level_get() >= 2) {
                            switch (message.data.keyboard.scancode) {
                            case SDL_SCANCODE_D:
                                if (light_scheme_get() == LIGHT_SCHEME_DEFAULT_LIGHTING) {
                                    light_scheme_set(dword_5CFF00, light_scheme_get_hour());
                                } else {
                                    dword_5CFF00 = light_scheme_get();
                                    light_scheme_set(LIGHT_SCHEME_DEFAULT_LIGHTING, light_scheme_get_hour());
                                }
                                break;
                            case SDL_SCANCODE_Y:
                                critter_give_xp(pc_obj, level_experience_points_to_next_level(pc_obj));
                                break;
                            case SDL_SCANCODE_4:
                                if (tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                                    UiMessage ui_message;

                                    ui_message.type = UI_MSG_TYPE_FEEDBACK;
                                    ui_message.str = "Cheater! Here's $1000!";
                                    ui_display_msg(&ui_message);

                                    int64_t gold_obj;
                                    object_create(sub_4685A0(BP_GOLD),
                                        obj_field_int64_get(pc_obj, OBJ_F_LOCATION),
                                        &gold_obj);
                                    obj_field_int32_set(gold_obj, OBJ_F_GOLD_QUANTITY, 1000);
                                    item_transfer(gold_obj, pc_obj);
                                    sub_4605D0();
                                }
                                break;
                            default:
                                break;
                            }
                        }

                        if (gamelib_cheat_level_get() >= 1) {
                            switch (message.data.keyboard.scancode) {
                            case SDL_SCANCODE_V:
                                gamelib_copy_version(version_str, NULL, NULL);
                                if (tig_video_3d_check_hardware() == TIG_OK) {
                                    strcat(version_str, " [hardware renderer");
                                } else {
                                    strcat(version_str, " [software renderer");
                                }

                                if (tig_video_check_gamma_control() == TIG_OK) {
                                    strcat(version_str, " with gamma support]");
                                } else {
                                    strcat(version_str, " without gamma support]");
                                }
                                intgame_message_window_display_str(-1, version_str);
                                break;
                            case SDL_SCANCODE_E:
                                critter_debug_obj(player_get_local_pc_obj());
                                timeevent_debug_lists();
                                magictech_debug_lists();
                                anim_stats();
                                break;
                            case SDL_SCANCODE_X:
                                tig_mouse_get_state(&mouse_state);
                                location_at(mouse_state.x, mouse_state.y, &mouse_loc);
                                snprintf(mouse_state_str, sizeof(mouse_state_str),
                                    "x: %d, y: %d",
                                    (int)LOCATION_GET_X(mouse_loc),
                                    (int)LOCATION_GET_Y(mouse_loc));
                                tig_debug_printf("%s\n", mouse_state_str);
                                intgame_message_window_display_str(-1, mouse_state_str);
                                break;
                            case SDL_SCANCODE_U:
                                snprintf(story_state_str, sizeof(story_state_str),
                                    "Current Story State: %d",
                                    script_story_state_get());
                                intgame_message_window_display_str(-1, story_state_str);
                                break;
                            case SDL_SCANCODE_LEFTBRACKET:
                                switch (object_blit_flags_get()) {
                                case 0:
                                    object_blit_flags_set(TIG_ART_BLT_BLEND_ALPHA_CONST);
                                    break;
                                case TIG_ART_BLT_BLEND_ALPHA_CONST:
                                    object_blit_flags_set(TIG_ART_BLT_BLEND_ALPHA_STIPPLE_S);
                                    break;
                                case TIG_ART_BLT_BLEND_ALPHA_STIPPLE_S:
                                    object_blit_flags_set(0);
                                    break;
                                }
                                break;
                            case SDL_SCANCODE_RIGHTBRACKET:
                                switch (roof_blit_flags_get()) {
                                case 0:
                                    roof_blit_flags_set(TIG_ART_BLT_BLEND_ALPHA_CONST);
                                    break;
                                case TIG_ART_BLT_BLEND_ALPHA_CONST:
                                    roof_blit_flags_set(TIG_ART_BLT_BLEND_ALPHA_STIPPLE_S);
                                    break;
                                case TIG_ART_BLT_BLEND_ALPHA_STIPPLE_S:
                                    roof_blit_flags_set(0);
                                    break;
                                }
                                break;
                            case SDL_SCANCODE_APOSTROPHE:
                                settings_set_value(&settings, SHADOWS_KEY, 1 - settings_get_value(&settings, SHADOWS_KEY));
                                break;
                            case SDL_SCANCODE_BACKSLASH:
                                wallcheck_set_enabled(!wallcheck_is_enabled());
                                break;
                            case SDL_SCANCODE_KP_7:
                                if (tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                                    disable_profiler = true;
                                }
                                break;
                            case SDL_SCANCODE_KP_8:
                                if (tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                                    enable_profiler = true;
                                }
                                break;
                            case SDL_SCANCODE_KP_9:
                                if (tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                                    output_profile_data = true;
                                }
                                break;
                            case SDL_SCANCODE_G:
                                if (tig_kb_get_modifier(SDL_KMOD_CTRL)) {
                                    gamma = 1.0f;
                                    tig_video_set_gamma(gamma);
                                } else if (tig_kb_get_modifier(SDL_KMOD_SHIFT)) {
                                    if (gamma > 0.1f) {
                                        gamma -= 0.1f;
                                        tig_video_set_gamma(gamma);
                                    }
                                } else {
                                    if (gamma < 1.9f) {
                                        gamma += 0.1f;
                                        tig_video_set_gamma(gamma);
                                    }
                                }
                                break;
                            default:
                                break;
                            }
                        }
                    }
                }
            } else {
                if (mainmenu_ui_is_active()) {
                    if (!mainmenu_ui_handle()) {
                        return;
                    }
                }
            }
        }

        handle_auto_save();

        if (intgame_mode_supports_scrolling(intgame_mode_get())) {
            handle_mouse_scroll();
            handle_wheel_pan();
        } else {
            // Local tweak: drop wheel deltas gathered outside map mode.
            float dx;
            float dy;
            tig_mouse_wheel_take(&dx, &dy);
        }
    }
}

// 0x401F50
void handle_mouse_scroll(void)
{
    TigMouseState mouse_state;
    int width;
    int height;
    int tolerance;

    if (!tig_get_active()) {
        scroll_stop();
        return;
    }

    tig_mouse_get_state(&mouse_state);
    width = hrp_iso_window_width_get();
    height = hrp_iso_window_height_get();
    tolerance = 8;

    if (mouse_state.x < tolerance) {
        if (mouse_state.y < tolerance) {
            scroll_start(SCROLL_DIRECTION_UP_LEFT);
        } else if (mouse_state.y >= height - tolerance) {
            scroll_start(SCROLL_DIRECTION_DOWN_LEFT);
        } else {
            scroll_start(SCROLL_DIRECTION_LEFT);
        }
    } else if (mouse_state.x >= width - tolerance) {
        if (mouse_state.y < tolerance) {
            scroll_start(SCROLL_DIRECTION_UP_RIGHT);
        } else if (mouse_state.y >= height - tolerance) {
            scroll_start(SCROLL_DIRECTION_DOWN_RIGHT);
        } else {
            scroll_start(SCROLL_DIRECTION_RIGHT);
        }
    } else {
        if (mouse_state.y < tolerance) {
            scroll_start(SCROLL_DIRECTION_UP);
        } else if (mouse_state.y >= height - tolerance) {
            scroll_start(SCROLL_DIRECTION_DOWN);
        } else {
            scroll_stop();
        }
    }
}

// 0x402010
void handle_keyboard_scroll(void)
{
    if (tig_kb_is_key_pressed(SDL_SCANCODE_UP)) {
        if (tig_kb_is_key_pressed(SDL_SCANCODE_LEFT)) {
            scroll_start(SCROLL_DIRECTION_UP_LEFT);
        } else if (tig_kb_is_key_pressed(SDL_SCANCODE_RIGHT)) {
            scroll_start(SCROLL_DIRECTION_UP_RIGHT);
        } else {
            scroll_start(SCROLL_DIRECTION_UP);
        }
    } else if (tig_kb_is_key_pressed(SDL_SCANCODE_DOWN)) {
        if (tig_kb_is_key_pressed(SDL_SCANCODE_LEFT)) {
            scroll_start(SCROLL_DIRECTION_DOWN_LEFT);
        } else if (tig_kb_is_key_pressed(SDL_SCANCODE_RIGHT)) {
            scroll_start(SCROLL_DIRECTION_DOWN_RIGHT);
        } else {
            scroll_start(SCROLL_DIRECTION_DOWN);
        }
    } else if (tig_kb_is_key_pressed(SDL_SCANCODE_LEFT)) {
        scroll_start(SCROLL_DIRECTION_LEFT);
    } else if (tig_kb_is_key_pressed(SDL_SCANCODE_RIGHT)) {
        scroll_start(SCROLL_DIRECTION_RIGHT);
    }
}

void build_cmd_line(char* dst, size_t size, int argc, char** argv)
{
    int idx;
    char* src;

    for (idx = 1; idx < argc && size > 1; idx++) {
        if (idx != 1) {
            *dst++ = ' ';
            size--;
        }

        src = argv[idx];
        while (*src != '\0' && size > 1) {
            *dst++ = *src++;
            size--;
        }
    }

    *dst = '\0';
}

// Local tweak: pan the map with two-finger trackpad swipes (or the wheel)
// while the cursor is over the game world. Over interface windows the wheel
// keeps its usual meaning (scrollbars). WheelScroll in HighRes/config.ini sets
// pixels per wheel unit; 0 disables, negative inverts.
/**
 * Local tweak: periodic autosave.
 *
 * Every `AutoSave` real minutes (HighRes/config.ini, 0 = off) the game is saved
 * into one of two rotating slots ("SlotAut1"/"SlotAut2"), so neither the F7
 * quick save nor a just-made autosave is overwritten. Only saves at a calm
 * moment: on the map, out of combat, no menu/dialog/world map, PC alive. If
 * the moment is not calm, it retries on the next frame.
 */
static void handle_auto_save(void)
{
    static Uint64 last_save_ms;
    static int next_slot;
    const HighResConfig* config = highres_config_get();
    int64_t pc_obj;
    Uint64 now;

    if (config->auto_save <= 0) {
        return;
    }

    now = SDL_GetTicks();
    if (last_save_ms == 0) {
        // Count from the first frame of play, not from a save.
        last_save_ms = now;
        return;
    }

    if (now - last_save_ms < (Uint64)config->auto_save * 60 * 1000) {
        return;
    }

    pc_obj = player_get_local_pc_obj();
    if (pc_obj == OBJ_HANDLE_NULL
        || mainmenu_ui_is_active()
        || textedit_ui_is_focused()
        || wmap_ui_is_created()
        || intgame_mode_get() != INTGAME_MODE_MAIN
        || critter_is_dead(pc_obj)
        || combat_turn_based_is_active()
        || combat_critter_is_combat_mode_active(pc_obj)) {
        return;
    }

    mainmenu_ui_feedback_saving();
    if (gamelib_save(next_slot == 0 ? "SlotAut1" : "SlotAut2", "Auto-Save")) {
        next_slot = 1 - next_slot;
    }
    mainmenu_ui_feedback_saving_completed();
    last_save_ms = now;
}

static void handle_wheel_pan(void)
{
    static float rest_x;
    static float rest_y;
    float dx;
    float dy;

    tig_mouse_wheel_take(&dx, &dy);
    if (dx == 0.0f && dy == 0.0f) {
        return;
    }

    int speed = highres_config_get()->wheel_scroll;
    if (speed == 0 || !tig_get_active()) {
        return;
    }

    TigMouseState mouse_state;
    tig_mouse_get_state(&mouse_state);

    TigRect rects[64];
    int num_rects = tig_window_overlay_rects(hrp_iso_window_width_get(), hrp_iso_window_height_get(), rects, 64);
    for (int index = 0; index < num_rects; index++) {
        if (mouse_state.x >= rects[index].x
            && mouse_state.y >= rects[index].y
            && mouse_state.x < rects[index].x + rects[index].width
            && mouse_state.y < rects[index].y + rects[index].height) {
            return;
        }
    }

    // Document-style, following the system scroll direction: content moves
    // with the fingers.
    rest_x += -dx * (float)speed;
    rest_y += dy * (float)speed;
    int64_t px = (int64_t)rest_x;
    int64_t py = (int64_t)rest_y;
    rest_x -= (float)px;
    rest_y -= (float)py;
    if (px != 0 || py != 0) {
        scroll_by(px, py);
    }
}
