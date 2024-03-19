/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2024 Hrvoje Cavrak
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "main.h"

#define COORD_PERCENT(P) ((((MAX_SCREEN_COORD-MIN_SCREEN_COORD)*P)/100)+MIN_SCREEN_COORD)

const hotkey_combo_t SWITCH_TO_DESKTOP_1 = {
    .modifier = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTSHIFT,
    .keys = {HID_KEY_ARROW_RIGHT},
    .key_count = 1,
};

const hotkey_combo_t SWITCH_TO_DESKTOP_2 = {
    .modifier = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTSHIFT,
    .keys = {HID_KEY_ARROW_LEFT},
    .key_count = 1,
};

transition_t transitions[] = {
    {
        .output = OUTPUT_A,
        .desktop = DESKTOP_1,
        .to_output = OUTPUT_A,
        .to_desktop = DESKTOP_2,
        .switch_keycombo = SWITCH_TO_DESKTOP_2,
        .edge = LEFT,
        .edge_min = COORD_PERCENT(0),
        .edge_max = COORD_PERCENT(50),
        .to_edge = RIGHT,
        .to_edge_min = COORD_PERCENT(75),
        .to_edge_max = COORD_PERCENT(100),
    },
    {
        .output = OUTPUT_A,
        .desktop = DESKTOP_2,
        .to_output = OUTPUT_A,
        .to_desktop = DESKTOP_1,
        .switch_keycombo = SWITCH_TO_DESKTOP_1,
        .to_edge = LEFT,
        .to_edge_min = COORD_PERCENT(0),
        .to_edge_max = COORD_PERCENT(50),
        .edge = RIGHT,
        .edge_min = COORD_PERCENT(75),
        .edge_max = COORD_PERCENT(100),
    },
    {
        .output = OUTPUT_A,
        .desktop = DESKTOP_1,
        .to_output = OUTPUT_B,
        .to_desktop = DESKTOP_1,
        .edge = LEFT,
        .edge_min = COORD_PERCENT(50),
        .edge_max = COORD_PERCENT(100),
        .to_edge = RIGHT,
        .to_edge_min = COORD_PERCENT(0),
        .to_edge_max = COORD_PERCENT(50),
    },
    {
        .output = OUTPUT_B,
        .desktop = DESKTOP_1,
        .to_output = OUTPUT_A,
        .to_desktop = DESKTOP_1,
        .switch_keycombo = SWITCH_TO_DESKTOP_1,
        .to_edge = LEFT,
        .to_edge_min = COORD_PERCENT(50),
        .to_edge_max = COORD_PERCENT(100),
        .edge = RIGHT,
        .edge_min = COORD_PERCENT(0),
        .edge_max = COORD_PERCENT(50),
    },
    {
        .output = OUTPUT_A,
        .desktop = DESKTOP_2,
        .to_output = OUTPUT_B,
        .to_desktop = DESKTOP_1,
        .edge = BOTTOM,
        .edge_min = COORD_PERCENT(70),
        .edge_max = COORD_PERCENT(100),
        .to_edge = TOP,
        .to_edge_min = COORD_PERCENT(99),
        .to_edge_max = COORD_PERCENT(100),
    },
    {
        .output = OUTPUT_A,
        .desktop = DESKTOP_2,
        .to_output = OUTPUT_B,
        .to_desktop = DESKTOP_1,
        .edge = BOTTOM,
        .edge_min = COORD_PERCENT(25),
        .edge_max = COORD_PERCENT(70),
        .to_edge = TOP,
        .to_edge_min = COORD_PERCENT(0),
        .to_edge_max = COORD_PERCENT(100),
    },
    {
        .output = OUTPUT_B,
        .desktop = DESKTOP_1,
        .to_output = OUTPUT_A,
        .to_desktop = DESKTOP_2,
        .switch_keycombo = SWITCH_TO_DESKTOP_2,
        .to_edge = BOTTOM,
        .to_edge_min = COORD_PERCENT(25),
        .to_edge_max = COORD_PERCENT(70),
        .edge = TOP,
        .edge_min = COORD_PERCENT(0),
        .edge_max = COORD_PERCENT(100),
    },
};

int8_t edge_crossed(const mouse_values_t *values, device_t *state, edge_crossing_t *crossing) {
    int new_x = state->mouse_x + values->move_x;
    int new_y = state->mouse_y + values->move_y;

    /* No switching allowed if explicitly disabled */
    if (state->switch_lock)
        return 0;

    /* Crossed left edge of screen */
    if (new_x < MIN_SCREEN_COORD - JUMP_THRESHOLD) {
        crossing->edge = LEFT;
        crossing->edge_position = state->mouse_y;
        return 1;
    }

    /* Crossed right edge of screen */
    else if (new_x > MAX_SCREEN_COORD + JUMP_THRESHOLD) {
        crossing->edge = RIGHT;
        crossing->edge_position = state->mouse_y;
        return 1;
    }

    /* Crossed top edge of screen */
    else if (new_y > MAX_SCREEN_COORD + JUMP_THRESHOLD) {
        crossing->edge = BOTTOM;
        crossing->edge_position = state->mouse_x;
        return 1;
    }

    /* Crossed bottom edge of screen */
    else if (new_y < MIN_SCREEN_COORD - JUMP_THRESHOLD) {
        crossing->edge = TOP;
        crossing->edge_position = state->mouse_x;
        return 1;
    }

    return 0;
}

int8_t transition_match(const edge_crossing_t *crossing, const transition_t *transition, device_t *state) {
    if (state->active_output != transition->output) {
        return 0;
    }
    if (state->active_desktop != transition->desktop) {
        return 0;
    }
    if (crossing->edge != transition->edge) {
        return 0;
    }
    if (crossing->edge_position < transition->edge_min || crossing->edge_position > transition->edge_max) {
        return 0;
    }
    return 1;
}

int8_t get_transition_info(const edge_crossing_t *crossing, device_t *state, transition_t *transition) {
    for (int j=0; j<ARRAY_SIZE(transitions); j++) {
        if (transition_match(crossing, &transitions[j], state)) {
            *transition = transitions[j];
            return 1;
        }
    }
            
    return 0;
}

/* Move mouse coordinate 'position' by 'offset', but don't fall off the screen */
int32_t move_and_keep_on_screen(int position, int offset) {
    /* Lowest we can go is 0 */
    if (position + offset < MIN_SCREEN_COORD)
        return MIN_SCREEN_COORD;

    /* Highest we can go is MAX_SCREEN_COORD */
    else if (position + offset > MAX_SCREEN_COORD)
        return MAX_SCREEN_COORD;

    /* We're still on screen, all good */
    return position + offset;
}

void update_mouse_position(device_t *state, mouse_values_t *values) {
    output_t *current    = &state->config.output[state->active_output];
    uint8_t reduce_speed = 0;

    /* Check if we are configured to move slowly */
    if (state->mouse_zoom)
        reduce_speed = MOUSE_ZOOM_SCALING_FACTOR;

    /* Calculate movement */
    int offset_x = values->move_x * (current->speed_x >> reduce_speed);
    int offset_y = values->move_y * (current->speed_y >> reduce_speed);

    /* Update movement */
    state->mouse_x = move_and_keep_on_screen(state->mouse_x, offset_x);
    state->mouse_y = move_and_keep_on_screen(state->mouse_y, offset_y);
}

/* If we are active output, queue packet to mouse queue, else send them through UART */
void output_mouse_report(mouse_abs_report_t *real_report, mouse_abs_report_t *wiggle_report, device_t *state) {
    if (CURRENT_BOARD_IS_ACTIVE_OUTPUT) {
        queue_mouse_report(real_report, state);
       if (wiggle_report != NULL) {
            send_packet((uint8_t *)wiggle_report, MOUSE_REPORT_MSG, MOUSE_REPORT_LENGTH);
        }
        state->last_activity[BOARD_ROLE] = time_us_64();
    } else {
        if (wiggle_report != NULL) {
            queue_mouse_report(wiggle_report, state);
        }
        send_packet((uint8_t *)real_report, MOUSE_REPORT_MSG, MOUSE_REPORT_LENGTH);
    }
}

#define TRANSLATE_POSITION(A, MIN1, MAX1, MIN2, MAX2) ( (MAX2-MIN2) * (A-MIN1) / (MAX1-MIN1) + MIN2)

void switch_screen(const transition_t *transition, device_t *state) {
    mouse_abs_report_t hidden_pointer = {.y = MIN_SCREEN_COORD, .x = MAX_SCREEN_COORD};

    output_mouse_report(&hidden_pointer, NULL, state);
    switch_output(state, transition->to_output);

    state->active_desktop = transition->to_desktop;

    if (transition->switch_keycombo.key_count != 0 || transition->switch_keycombo.modifier != 0) {
       send_keycombo(&transition->switch_keycombo, state);
    }

    // calling switch_output a second time to release keycombo
    // TODO: figure out why calling send key {0} doesn't release keys
    switch_output(state, transition->to_output);

    if (transition->to_edge == LEFT || transition->to_edge == RIGHT) {
        state->mouse_x = (transition->to_edge == LEFT) ? MIN_SCREEN_COORD : MAX_SCREEN_COORD;
        state->mouse_y = TRANSLATE_POSITION(state->mouse_y,
                                            transition->edge_min,
                                            transition->edge_max,
                                            transition->to_edge_min,
                                            transition->to_edge_max);
    } else {
        state->mouse_y = (transition->to_edge == TOP) ? MIN_SCREEN_COORD : MAX_SCREEN_COORD;
        state->mouse_x = TRANSLATE_POSITION(state->mouse_x,
                                            transition->edge_min,
                                            transition->edge_max,
                                            transition->to_edge_min,
                                            transition->to_edge_max);
    }
}

void check_screen_switch(const mouse_values_t *values, device_t *state) {
    edge_crossing_t crossing;
    if (!edge_crossed(values, state, &crossing)) {
        return;
    }

    transition_t transition;
    if (!get_transition_info(&crossing, state, &transition)) {
        return;
    }

    switch_screen(&transition, state);
}

void extract_report_values(uint8_t *raw_report, device_t *state, mouse_values_t *values) {
    /* Interpret values depending on the current protocol used. */
    if (state->mouse_dev.protocol == HID_PROTOCOL_BOOT) {
        hid_mouse_report_t *mouse_report = (hid_mouse_report_t *)raw_report;

        values->move_x  = mouse_report->x;
        values->move_y  = mouse_report->y;
        values->wheel   = mouse_report->wheel;
        values->buttons = mouse_report->buttons;
        return;
    }

    /* If HID Report ID is used, the report is prefixed by the report ID so we have to move by 1 byte */
    if (state->mouse_dev.uses_report_id)
        raw_report++;

    values->move_x  = get_report_value(raw_report, &state->mouse_dev.move_x);
    values->move_y  = get_report_value(raw_report, &state->mouse_dev.move_y);
    values->wheel   = get_report_value(raw_report, &state->mouse_dev.wheel);
    values->buttons = get_report_value(raw_report, &state->mouse_dev.buttons);
}

mouse_abs_report_t create_mouse_report(device_t *state, mouse_values_t *values) {
    mouse_abs_report_t abs_mouse_report = {.buttons = values->buttons,
                                           .x       = state->mouse_x,
                                           .y       = state->mouse_y,
                                           .wheel   = values->wheel,
                                           .pan     = 0};
    return abs_mouse_report;
}

mouse_abs_report_t create_mouse_wiggle_report(int y_value) {
    mouse_abs_report_t wiggle_mouse_report = {.buttons = 0,
                                              .x       = MAX_SCREEN_COORD,
                                              .y       = y_value,
                                              .wheel   = 0,
                                              .pan     = 0};
    return wiggle_mouse_report;
}

void send_mouse_wiggle(device_t *state) {
    int y_value = (rand() % (MAX_SCREEN_COORD - MIN_SCREEN_COORD)) + MIN_SCREEN_COORD;
    mouse_abs_report_t wiggle_report = create_mouse_wiggle_report(y_value);
    if (CURRENT_BOARD_IS_ACTIVE_OUTPUT) {
        send_packet((uint8_t *)&wiggle_report, MOUSE_WIGGLE_MSG, MOUSE_REPORT_LENGTH);
    } else {
        queue_mouse_report(&wiggle_report, state);
    }
}

void process_mouse_report(uint8_t *raw_report, int len, device_t *state) {
    mouse_values_t values = {0};

    /* Interpret the mouse HID report, extract and save values we need. */
    extract_report_values(raw_report, state, &values);

    /* Calculate and update mouse pointer movement. */
    update_mouse_position(state, &values);

    /* Create the report for the output PC based on the updated values */
    mouse_abs_report_t real_report = create_mouse_report(state, &values);
    mouse_abs_report_t wiggle_report = create_mouse_wiggle_report(state->mouse_y);

    /* Move the mouse, depending where the output is supposed to go */
    output_mouse_report(&real_report, &wiggle_report, state);

    /* We use the mouse to switch outputs, the logic is in check_screen_switch() */
    check_screen_switch(&values, state);
}

/* ==================================================== *
 * Mouse Queue Section
 * ==================================================== */

void process_mouse_queue_task(device_t *state) {
    mouse_abs_report_t report = {0};

    /* We need to be connected to the host to send messages */
    if (!state->tud_connected)
        return;

    /* Peek first, if there is anything there... */
    if (!queue_try_peek(&state->mouse_queue, &report))
        return;

    /* If we are suspended, let's wake the host up */
    if (tud_suspended())
        tud_remote_wakeup();

    /* ... try sending it to the host, if it's successful */
    bool succeeded = tud_hid_abs_mouse_report(
        REPORT_ID_MOUSE, report.buttons, report.x, report.y, report.wheel, report.pan);

    /* ... then we can remove it from the queue */
    if (succeeded)
        queue_try_remove(&state->mouse_queue, &report);
}

void queue_mouse_report(mouse_abs_report_t *report, device_t *state) {
    /* It wouldn't be fun to queue up a bunch of messages and then dump them all on host */
    if (!state->tud_connected)
        return;

    queue_try_add(&state->mouse_queue, report);
}
