#include "ui/options_ui.h"

#include "game/anim.h"
#include "game/combat.h"
#include "game/gamelib.h"
#include "game/highres_config.h"
#include "game/location.h"
#include "game/mes.h"
#include "game/obj.h"
#include "game/player.h"
#include "game/skill.h"
#include "game/tf.h"
#include "ui/cyclic_ui.h"
#include "ui/gameuilib.h"
#include "tig/scalefx_gl.h"

#define MAX_CONTROLS 10

typedef void (*OptionsUiControlValueGetter)(int* value_ptr, bool* enabled_ptr);
typedef void (*OptionsUiControlValueSetter)(int value);

typedef struct OptionsUiControlInfo {
    /* 0000 */ bool active;
    /* 0004 */ CyclicUiControlType type;
    /* 0008 */ int max_value;
    /* 000C */ const char* debug_name;
    /* 0010 */ const char* mes;
    /* 0014 */ OptionsUiControlValueGetter getter;
    /* 0018 */ OptionsUiControlValueSetter setter;
} OptionsUiControlInfo;

typedef struct OptionsUiControlData {
    /* 0000 */ int id;
    /* 0004 */ bool initialized;
} OptionsUiControlData;

static void options_ui_module_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_difficulty_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_difficutly_set(int value);
static void options_ui_violence_filter_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_violence_filter_set(int value);
static void options_ui_combat_mode_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_combat_mode_set(int value);
static void options_ui_auto_attack_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_auto_attack_set(int value);
static void options_ui_auto_switch_weapons_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_auto_switch_weapons_set(int value);
static void options_ui_awlays_run_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_always_run_set(int value);
static void options_ui_follower_skills_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_follower_skills_set(int value);
static void options_ui_brightness_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_brightness_set(int value);
static void options_ui_text_duration_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_text_duration_set(int value);
static void options_ui_floats_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_floats_set(int value);
static void options_ui_float_speed_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_float_speed_set(int value);
static void options_ui_combat_taunts_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_combat_taunts_set(int value);
static void options_ui_shadows_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_shadows_set(int value);
static void options_ui_uap_rules_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_uap_rules_set(int value);
static void options_ui_scale_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_scale_set(int value);
static void options_ui_smooth_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_smooth_set(int value);
static void options_ui_video_arrays_build(void);
static void options_ui_effects_volume_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_effects_volume_set(int value);
static void options_ui_voice_volume_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_voice_volume_set(int value);
static void options_ui_music_volume_get(int* value_ptr, bool* enabled_ptr);
static void options_ui_music_volume_set(int value);

/**
 * 0x5CCD38
 */
static int options_ui_controls_x[MAX_CONTROLS] = {
    261,
    261,
    261,
    261,
    531,
    531,
    531,
    531,
    // Local addition: a fifth row (only the video tab uses it).
    261,
    531,
};

/**
 * 0x5CCD58
 */
static int options_ui_controls_y[MAX_CONTROLS] = {
    122,
    197,
    272,
    347,
    122,
    197,
    272,
    347,
    422,
    422,
};

/**
 * Defines controls metadata.
 *
 * 0x5CCD78
 */
static OptionsUiControlInfo options_ui_tab_controls_meta[OPTIONS_UI_TAB_COUNT][MAX_CONTROLS] = {
    {
        { true, CYCLIC_UI_CONTROL_TEXT_ARRAY, 0, "Module", "", options_ui_module_get, NULL },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Difficulty", "mes\\OptionsDifficulty.mes", options_ui_difficulty_get, options_ui_difficutly_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Violence Filter", "mes\\OptionsOffOn.mes", options_ui_violence_filter_get, options_ui_violence_filter_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Default Combat Mode", "mes\\OptionsCombatMode.mes", options_ui_combat_mode_get, options_ui_combat_mode_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Auto Attack", "mes\\OptionsOffOn.mes", options_ui_auto_attack_get, options_ui_auto_attack_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Auto Switch Weapons", "mes\\OptionsOffOn.mes", options_ui_auto_switch_weapons_get, options_ui_auto_switch_weapons_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Always Run", "mes\\OptionsOffOn.mes", options_ui_awlays_run_get, options_ui_always_run_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Follower Skills", "mes\\OptionsOffOn.mes", options_ui_follower_skills_get, options_ui_follower_skills_set },
    },
    {
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 9, "Brightness", "", options_ui_brightness_get, options_ui_brightness_set },
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 0, "Text Duration", "", options_ui_text_duration_get, options_ui_text_duration_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Floats", "mes\\OptionsFloats.mes", options_ui_floats_get, options_ui_floats_set },
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 0, "Float Speed", "", options_ui_float_speed_get, options_ui_float_speed_set },
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Combat Taunts", "mes\\OptionsOffOn.mes", options_ui_combat_taunts_get, options_ui_combat_taunts_set },
        // UAP: "Dynamic Shadows" (options_ui.mes 205) toggles the existing
        // "shadows" setting.
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "Dynamic Shadows", "mes\\OptionsOffOn.mes", options_ui_shadows_get, options_ui_shadows_set },
        // Local addition: "UAP Rules" (options_ui.mes 206), see UAP_RULES_KEY.
        { true, CYCLIC_UI_CONTROL_MESSAGE_FILE, 0, "UAP Rules", "mes\\OptionsOffOn.mes", options_ui_uap_rules_get, options_ui_uap_rules_set },
        // Local addition: "Scale" (options_ui.mes 207, applies after a restart)
        // and "Smoothing" (208, immediate); text arrays built in
        // options_ui_start.
        { true, CYCLIC_UI_CONTROL_TEXT_ARRAY, 0, "Scale", "", options_ui_scale_get, options_ui_scale_set },
        { true, CYCLIC_UI_CONTROL_TEXT_ARRAY, 0, "Smoothing", "", options_ui_smooth_get, options_ui_smooth_set },
        { false, 0, 0, "", "", NULL, NULL },
    },
    {
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 0, "Effects", "", options_ui_effects_volume_get, options_ui_effects_volume_set },
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 0, "Voice", "", options_ui_voice_volume_get, options_ui_voice_volume_set },
        { true, CYCLIC_UI_CONTROL_NUMERIC_BAR, 0, "Music", "", options_ui_music_volume_get, options_ui_music_volume_set },
        { false, 0, 0, "", "", NULL, NULL },
        { false, 0, 0, "", "", NULL, NULL },
        { false, 0, 0, "", "", NULL, NULL },
        { false, 0, 0, "", "", NULL, NULL },
        { false, 0, 0, "", "", NULL, NULL },
    },
};

/**
 * Flag indicating if the options UI is displayed in-game.
 *
 * This flag controls availability of the "Module" control.
 *
 * 0x687260
 */
static bool options_ui_in_play;

/**
 * List of available game modules.
 *
 * 0x687268
 */
static GameModuleList options_ui_modlist;

/**
 * "options_ui.mes"
 *
 * 0x687274
 */
static mes_file_handle_t options_ui_mes_file;

/**
 * Runtime data for controls.
 *
 * 0x687278
 */
static OptionsUiControlData options_ui_controls[MAX_CONTROLS];

/**
 * Flag indicating whether the options UI has been started.
 *
 * 0x6872B8
 */
static bool options_ui_started;

/**
 * Flag indicating if the module list is initialized.
 *
 * 0x6872BC
 */
static bool options_ui_modlist_initialized;

// Local addition: the video tab's five compact rows no longer line up with
// the empty slot frames painted into OptionsBase.ART; patch them over with
// plain wood from the same picture and restore it when the tab closes.
static tig_window_handle_t options_ui_window_handle;
static bool options_ui_video_patched;
static void options_ui_blit_base(int x, int src_y, int dst_y, int width, int height);
static void options_ui_video_patch(bool restore);

// Local addition: "Scale" / "Smoothing" choices (see options_ui_video_arrays_build).
#define OPTIONS_UI_MAX_SCALES 8
static char options_ui_scale_text[OPTIONS_UI_MAX_SCALES][48];
static char* options_ui_scale_labels[OPTIONS_UI_MAX_SCALES];
static int options_ui_scale_percent[OPTIONS_UI_MAX_SCALES]; // -1 = manual Width/Height
static int options_ui_scale_count;
static char options_ui_smooth_text[4][32];
static char* options_ui_smooth_labels[4];
static int options_ui_smooth_mode[4];
static int options_ui_smooth_count;

/**
 * Opens the options UI at a specified tab.
 *
 * NOTE: This function and entire module is a little bit strange, it's
 * implementation does not match other UI modules. This function does not create
 * its own window to display options, instead the window handle is passed by the
 * caller. The caller is responsible for drawing the background. This function
 * only deals with controls.
 *
 * 0x589280
 */
void options_ui_start(OptionsUiTab tab, tig_window_handle_t window_handle, bool in_play)
{
    int index;
    CyclicUiControlInfo control_info;
    OptionsUiControlInfo* meta;
    MesFileEntry mes_file_entry;
    int value;

    // Close fate UI if it's currently visible.
    if (options_ui_started) {
        options_ui_close();
    }

    options_ui_in_play = in_play;

    // Load control titles.
    mes_load("mes\\options_ui.mes", &options_ui_mes_file);

    options_ui_window_handle = window_handle;
    if (tab == OPTIONS_UI_TAB_VIDEO) {
        options_ui_video_patch(false);
    }

    for (index = 0; index < MAX_CONTROLS; index++) {
        meta = &(options_ui_tab_controls_meta[tab][index]);
        if (!meta->active) {
            continue;
        }

        // Initialize cyclic UI control.
        cyclic_ui_control_init(&control_info);

        // Special case - the "Module" control must be turned off when options
        // are opened in the middle of the game.
        if (tab == OPTIONS_UI_TAB_GAME && index == 0) {
            if (options_ui_in_play) {
                options_ui_modlist_initialized = false;
                continue;
            }

            // Load available modules and configure control appropriately.
            gamelib_modlist_create(&options_ui_modlist, 0);
            control_info.text_array = (const char* const*)options_ui_modlist.paths;
            control_info.text_array_size = options_ui_modlist.count;

            options_ui_modlist_initialized = true;
        }

        // Local addition: scale and smoothing choices.
        if (tab == OPTIONS_UI_TAB_VIDEO && meta->getter == options_ui_scale_get) {
            options_ui_video_arrays_build();
            control_info.text_array = (const char* const*)options_ui_scale_labels;
            control_info.text_array_size = options_ui_scale_count;
        } else if (tab == OPTIONS_UI_TAB_VIDEO && meta->getter == options_ui_smooth_get) {
            options_ui_video_arrays_build();
            control_info.text_array = (const char* const*)options_ui_smooth_labels;
            control_info.text_array_size = options_ui_smooth_count;
        }

        // Set up control.
        control_info.type = meta->type;
        control_info.window_handle = window_handle;
        control_info.max_value = meta->max_value;

        // Load control title from the message file.
        mes_file_entry.num = index + 100 * (tab + 1);
        // Fall back to the built-in name for entries the stock file lacks.
        control_info.title = mes_search(options_ui_mes_file, &mes_file_entry)
            ? mes_file_entry.str
            : meta->debug_name;

        // Finish control set up.
        control_info.mes_file_path = meta->mes;
        control_info.value_changed_callback = meta->setter;
        control_info.x = options_ui_controls_x[index];
        control_info.y = options_ui_controls_y[index];

        // Local addition: the video tab has a fifth row; tighten its row
        // spacing (75 -> 66 px) so all five fit above the board's lower edge.
        if (tab == OPTIONS_UI_TAB_VIDEO) {
            control_info.y = 120 + 66 * (index < 8 ? index % 4 : 4);
        }

        // Query initial value.
        if (meta->getter != NULL) {
            meta->getter(&value, &(control_info.enabled));
        } else {
            value = 0;
            control_info.enabled = false;
        }

        // Create the control and set its initial value.
        if (cyclic_ui_control_create(&control_info, &(options_ui_controls[index].id))) {
            options_ui_controls[index].initialized = true;
            cyclic_ui_control_set(options_ui_controls[index].id, value);
        } else {
            options_ui_controls[index].initialized = false;
        }
    }

    // Special case - when options are opened in the middle of the game, center
    // viewport on the player, so that PC lens displays correct surroundings.
    if (options_ui_in_play) {
        location_origin_set(obj_field_int64_get(player_get_local_pc_obj(), OBJ_F_LOCATION));
    }

    options_ui_started = true;
}

/**
 * Loads the selected game module.
 *
 * 0x589430
 */
bool options_ui_load_module(void)
{
    unsigned int selected;

    if (!options_ui_started
        || !options_ui_modlist_initialized
        || options_ui_in_play) {
        return true;
    }

    selected = cyclic_ui_control_get(options_ui_controls[0].id);
    if (selected == options_ui_modlist.selected) {
        return true;
    }

    if (!gamelib_mod_load(options_ui_modlist.paths[selected])
        || !gameuilib_mod_load()) {
        tig_debug_printf("Can't load module %s\n", options_ui_modlist.paths[selected]);
        return false;
    }

    return true;
}

/**
 * Closes the options UI.
 *
 * 0x5894C0
 */
void options_ui_close(void)
{
    int index;

    if (!options_ui_started) {
        return;
    }

    // Destroy initialized controls.
    for (index = 0; index < MAX_CONTROLS; index++) {
        if (options_ui_controls[index].initialized) {
            cyclic_ui_control_destroy(options_ui_controls[index].id, false);
            options_ui_controls[index].initialized = false;
        }
    }

    // Destroy module list.
    if (options_ui_modlist_initialized) {
        gamelib_modlist_destroy(&options_ui_modlist);
        options_ui_modlist_initialized = false;
    }

    mes_unload(options_ui_mes_file);

    if (options_ui_video_patched) {
        options_ui_video_patch(true);
    }

    options_ui_started = false;
}

/**
 * Handles button press event for the options UI.
 *
 * This function delegates handling to the cyclic UI system. Returns `true`
 * if the specified button is one of left/right buttons (in this case the
 * event should be considered as processed), `false` otherwise.
 *
 * 0x589530
 */
bool options_ui_handle_button_pressed(tig_button_handle_t button_handle)
{
    // Delegate button handling to the cyclic UI system.
    return cyclic_ui_handle_button_pressed(button_handle);
}

/**
 * Called to retrieve the initial value and state of "Module".
 *
 * 0x589540
 */
void options_ui_module_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = options_ui_modlist.selected;
    *enabled_ptr = !options_ui_in_play;
}

/**
 * Called to retrieve the initial value and state of "Difficulty".
 *
 * 0x589560
 */
void options_ui_difficulty_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, DIFFICULTY_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Difficulty" is changed.
 *
 * 0x589580
 */
void options_ui_difficutly_set(int value)
{
    settings_set_value(&settings, DIFFICULTY_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Violence Filter".
 *
 * 0x5895A0
 */
void options_ui_violence_filter_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, VIOLENCE_FILTER_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Violence Filter" is changed.
 *
 * 0x5895D0
 */
void options_ui_violence_filter_set(int value)
{
    settings_set_value(&settings, VIOLENCE_FILTER_KEY, value ? 1 : 0);
}

/**
 * Called to retrieve the initial value and state of "Combat Mode".
 *
 * 0x589610
 */
void options_ui_combat_mode_get(int* value_ptr, bool* enabled_ptr)
{
    if (combat_is_turn_based()) {
        *value_ptr = settings_get_value(&settings, FAST_TURN_BASED_KEY) ? 2 : 1;
    } else {
        *value_ptr = 0;
    }
    *enabled_ptr = true;
}

/**
 * Called when the value of "Combat Mode" is changed.
 *
 * 0x5896A0
 */
void options_ui_combat_mode_set(int value)
{
    switch (value) {
    case 0:
        settings_set_value(&settings, TURN_BASED_KEY, 0);
        settings_set_value(&settings, FAST_TURN_BASED_KEY, 0);
        break;
    case 1:
        settings_set_value(&settings, TURN_BASED_KEY, 1);
        settings_set_value(&settings, FAST_TURN_BASED_KEY, 0);
        break;
    case 2:
        settings_set_value(&settings, TURN_BASED_KEY, 1);
        settings_set_value(&settings, FAST_TURN_BASED_KEY, 1);
        break;
    }
}

/**
 * Called to retrieve the initial value and state of "Auto Attack".
 *
 * 0x589710
 */
void options_ui_auto_attack_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = combat_auto_attack_get(player_get_local_pc_obj());
    *enabled_ptr = true;
}

/**
 * Called when the value of "Auto Attack" is changed.
 *
 * 0x589740
 */
void options_ui_auto_attack_set(int value)
{
    combat_auto_attack_set(value);
}

/**
 * Called to retrieve the initial value and state of "Auto Switch Weapons".
 *
 * 0x589750
 */
void options_ui_auto_switch_weapons_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = combat_auto_switch_weapons_get(player_get_local_pc_obj());
    *enabled_ptr = true;
}

/**
 * Called when the value of "Auto Switch Weapons" is changed.
 *
 * 0x589780
 */
void options_ui_auto_switch_weapons_set(int value)
{
    combat_auto_switch_weapons_set(value);
}

/**
 * Called to retrieve the initial value and state of "Always Run".
 *
 * 0x589790
 */
void options_ui_awlays_run_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = get_always_run(player_get_local_pc_obj());
    *enabled_ptr = true;
}

/**
 * Called when the value of "Always Run" is changed.
 *
 * 0x5897C0
 */
void options_ui_always_run_set(int value)
{
    set_always_run(value);
}

/**
 * Called to retrieve the initial value and state of "Follower Skills".
 *
 * 0x5897D0
 */
void options_ui_follower_skills_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = get_follower_skills(player_get_local_pc_obj());
    *enabled_ptr = true;
}

/**
 * Called when the value of "Follower Skills" is changed.
 *
 * 0x589800
 */
void options_ui_follower_skills_set(int value)
{
    set_follower_skills(value);
}

/**
 * Called to retrieve the initial value and state of "Brightness".
 *
 * 0x589810
 */
void options_ui_brightness_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, BRIGHTNESS_KEY);
    *enabled_ptr = tig_video_check_gamma_control() == TIG_OK;
}

/**
 * Called when the value of "Brightness" is changed.
 *
 * 0x589840
 */
void options_ui_brightness_set(int value)
{
    settings_set_value(&settings, BRIGHTNESS_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Text Duration".
 *
 * 0x589860
 */
void options_ui_text_duration_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, TEXT_DURATION_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Text Duration" is changed.
 *
 * 0x589880
 */
void options_ui_text_duration_set(int value)
{
    settings_set_value(&settings, TEXT_DURATION_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Floats".
 *
 * 0x5898A0
 */
void options_ui_floats_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = tf_level_get();
    *enabled_ptr = true;
}

/**
 * Called when the value of "Floats" is changed.
 *
 * 0x5898C0
 */
void options_ui_floats_set(int value)
{
    if (tf_level_get() != value) {
        tf_level_set(value);
    }
}

/**
 * Called to retrieve the initial value and state of "Float Speed".
 *
 * 0x5898E0
 */
void options_ui_float_speed_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, FLOAT_SPEED_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Float Speed" is changed.
 *
 * 0x589900
 */
void options_ui_float_speed_set(int value)
{
    settings_set_value(&settings, FLOAT_SPEED_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Combat Taunts".
 *
 * 0x589920
 */
void options_ui_combat_taunts_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, COMBAT_TAUNTS_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Combat Taunts" is changed.
 *
 * 0x589940
 */
void options_ui_combat_taunts_set(int value)
{
    settings_set_value(&settings, COMBAT_TAUNTS_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Dynamic Shadows" (UAP).
 */
void options_ui_shadows_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, SHADOWS_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Dynamic Shadows" (UAP) is changed.
 */
void options_ui_shadows_set(int value)
{
    settings_set_value(&settings, SHADOWS_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Effects".
 *
 * 0x589960
 */
void options_ui_effects_volume_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, EFFECTS_VOLUME_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Effects" is changed.
 *
 * 0x589980
 */
void options_ui_effects_volume_set(int value)
{
    settings_set_value(&settings, EFFECTS_VOLUME_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Voice".
 *
 * 0x5899A0
 */
void options_ui_voice_volume_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, VOICE_VOLUME_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Voice" is changed.
 *
 * 0x5899C0
 */
void options_ui_voice_volume_set(int value)
{
    settings_set_value(&settings, VOICE_VOLUME_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "Music".
 *
 * 0x5899E0
 */
void options_ui_music_volume_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, MUSIC_VOLUME_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "Music" is changed.
 *
 * 0x589A00
 */
void options_ui_music_volume_set(int value)
{
    settings_set_value(&settings, MUSIC_VOLUME_KEY, value);
}

/**
 * Called to retrieve the initial value and state of "UAP Rules".
 */
void options_ui_uap_rules_get(int* value_ptr, bool* enabled_ptr)
{
    *value_ptr = settings_get_value(&settings, UAP_RULES_KEY);
    *enabled_ptr = true;
}

/**
 * Called when the value of "UAP Rules" is changed.
 */
void options_ui_uap_rules_set(int value)
{
    settings_set_value(&settings, UAP_RULES_KEY, value);
}

// Local addition: label for a value of the new video controls, from
// options_ui.mes (290 Auto, 291 Off) with an English fallback.
static const char* options_ui_local_label(int num, const char* fallback)
{
    MesFileEntry mes_file_entry;

    mes_file_entry.num = num;
    if (options_ui_mes_file != MES_FILE_HANDLE_INVALID
        && mes_search(options_ui_mes_file, &mes_file_entry)
        && mes_file_entry.str[0] != '\0') {
        return mes_file_entry.str;
    }

    return fallback;
}

static void options_ui_video_arrays_build(void)
{
    static const int factors[] = { 150, 200, 250, 300, 400 };
    const HighResConfig* config = highres_config_get();
    int pw;
    int ph;
    int w;
    int h;
    int index;
    bool have_pixels = tig_video_screen_pixels(&pw, &ph);

    options_ui_scale_count = 0;

    if (config->width > 0 && config->height > 0) {
        snprintf(options_ui_scale_text[options_ui_scale_count], sizeof(options_ui_scale_text[0]),
            "%dx%d", config->width, config->height);
        options_ui_scale_percent[options_ui_scale_count++] = -1;
    }

    if (have_pixels) {
        tig_video_logical_size_for(pw, ph, 0, &w, &h);
        snprintf(options_ui_scale_text[options_ui_scale_count], sizeof(options_ui_scale_text[0]),
            "%s (%dx%d)", options_ui_local_label(290, "Auto"), w, h);
    } else {
        snprintf(options_ui_scale_text[options_ui_scale_count], sizeof(options_ui_scale_text[0]),
            "%s", options_ui_local_label(290, "Auto"));
    }
    options_ui_scale_percent[options_ui_scale_count++] = 0;

    if (have_pixels) {
        for (index = 0; index < (int)SDL_arraysize(factors) && options_ui_scale_count < OPTIONS_UI_MAX_SCALES; index++) {
            tig_video_logical_size_for(pw, ph, factors[index], &w, &h);
            // Skip factors that would fall under 800x600 (clamped).
            if (w * factors[index] / 100 > pw + 2 || h * factors[index] / 100 > ph + 2) {
                continue;
            }
            if (factors[index] % 100 == 0) {
                snprintf(options_ui_scale_text[options_ui_scale_count], sizeof(options_ui_scale_text[0]),
                    "x%d (%dx%d)", factors[index] / 100, w, h);
            } else {
                snprintf(options_ui_scale_text[options_ui_scale_count], sizeof(options_ui_scale_text[0]),
                    "x%d,%d (%dx%d)", factors[index] / 100, factors[index] % 100 / 10, w, h);
            }
            options_ui_scale_percent[options_ui_scale_count++] = factors[index];
        }
    }

    for (index = 0; index < options_ui_scale_count; index++) {
        options_ui_scale_labels[index] = options_ui_scale_text[index];
    }

    options_ui_smooth_count = 0;
    snprintf(options_ui_smooth_text[options_ui_smooth_count], sizeof(options_ui_smooth_text[0]), "%s", options_ui_local_label(291, "Off"));
    options_ui_smooth_mode[options_ui_smooth_count++] = 0;
    snprintf(options_ui_smooth_text[options_ui_smooth_count], sizeof(options_ui_smooth_text[0]), "xBR");
    options_ui_smooth_mode[options_ui_smooth_count++] = 1;
    if (tig_video_has_xbrz()) {
        snprintf(options_ui_smooth_text[options_ui_smooth_count], sizeof(options_ui_smooth_text[0]), "xBRZ");
        options_ui_smooth_mode[options_ui_smooth_count++] = 2;
    }
    if (tig_scalefx_available()) {
        snprintf(options_ui_smooth_text[options_ui_smooth_count], sizeof(options_ui_smooth_text[0]), "ScaleFX");
        options_ui_smooth_mode[options_ui_smooth_count++] = 3;
    }

    for (index = 0; index < options_ui_smooth_count; index++) {
        options_ui_smooth_labels[index] = options_ui_smooth_text[index];
    }
}

void options_ui_scale_get(int* value_ptr, bool* enabled_ptr)
{
    const HighResConfig* config = highres_config_get();
    int index;

    *value_ptr = 0;
    *enabled_ptr = true;

    for (index = 0; index < options_ui_scale_count; index++) {
        if (options_ui_scale_percent[index] == -1) {
            // Manual Width/Height in the config is the current choice.
            *value_ptr = index;
            return;
        }
        if (options_ui_scale_percent[index] == config->scale) {
            *value_ptr = index;
        }
    }
}

void options_ui_scale_set(int value)
{
    if (value < 0 || value >= options_ui_scale_count || options_ui_scale_percent[value] == -1) {
        return;
    }

    // Takes effect on the next start.
    highres_config_save_int("Width", 0);
    highres_config_save_int("Height", 0);
    highres_config_save_int("Scale", options_ui_scale_percent[value]);
}

void options_ui_smooth_get(int* value_ptr, bool* enabled_ptr)
{
    int mode = tig_video_get_smooth();
    int index;

    *value_ptr = 0;
    *enabled_ptr = true;
    for (index = 0; index < options_ui_smooth_count; index++) {
        if (options_ui_smooth_mode[index] == mode) {
            *value_ptr = index;
        }
    }
}

void options_ui_smooth_set(int value)
{
    if (value < 0 || value >= options_ui_smooth_count) {
        return;
    }

    tig_video_set_smooth(options_ui_smooth_mode[value]);
    highres_config_save_int("Smooth", options_ui_smooth_mode[value]);
}

// Window coordinates; OptionsBase.ART starts 41 px below the window top
// (see cyclic_ui_draw_empty_slot) and is 800x400.
void options_ui_blit_base(int x, int src_y, int dst_y, int width, int height)
{
    TigArtBlitInfo blit_info;
    TigRect src_rect;
    TigRect dst_rect;
    tig_art_id_t art_id;

    tig_art_interface_id_create(669, 0, 0, 0, &art_id);

    src_rect.x = x;
    src_rect.y = src_y - 41;
    src_rect.width = width;
    src_rect.height = height;

    dst_rect.x = x;
    dst_rect.y = dst_y;
    dst_rect.width = width;
    dst_rect.height = height;

    blit_info.flags = 0;
    blit_info.art_id = art_id;
    blit_info.src_rect = &src_rect;
    blit_info.dst_rect = &dst_rect;
    tig_window_blit_art(options_ui_window_handle, &blit_info);
}

void options_ui_video_patch(bool restore)
{
    static const int columns[] = { 261, 531 };
    static const int gaps[] = { 176, 242, 308, 374 };
    int col;
    int gap;

    for (col = 0; col < 2; col++) {
        if (restore) {
            options_ui_blit_base(columns[col], 120, 120, 213, 321);
            continue;
        }

        // Gaps between the compact rows: wood from the gap between the
        // original first and second slots.
        for (gap = 0; gap < 4; gap++) {
            options_ui_blit_base(columns[col], 179, gaps[gap], 213, 10);
        }
    }

    if (!restore) {
        // Right column has no fifth control: cover the bottom of the
        // original fourth slot frame with the wood below it.
        options_ui_blit_base(531, 404, 384, 213, 20);
    }

    options_ui_video_patched = !restore;
}
