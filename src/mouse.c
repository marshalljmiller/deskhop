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

hotkey_combo_t SWITCH_TO_DESKTOP_1 = {
    .modifier = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTSHIFT,
    .keys = {HID_KEY_ARROW_RIGHT},
    .key_count = 1,
};

hotkey_combo_t SWITCH_TO_DESKTOP_2 = {
    .modifier = KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_LEFTSHIFT,
    .keys = {HID_KEY_ARROW_LEFT},
    .key_count = 1,
};

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

/* Calculate and return Y coordinate when moving from screen out_from to screen out_to */
int16_t scale_y_coordinate(int screen_from, int screen_to, device_t *state) {
    output_t *from = &state->config.output[screen_from];
    output_t *to   = &state->config.output[screen_to];

    int size_to   = to->border.bottom - to->border.top;
    int size_from = from->border.bottom - from->border.top;

    /* If sizes match, there is nothing to do */
    if (size_from == size_to)
        return state->mouse_y;

    /* Moving from smaller ==> bigger screen
       y_a = top + (((bottom - top) * y_b) / HEIGHT) */

    if (size_from > size_to) {
        return to->border.top + ((size_to * state->mouse_y) / MAX_SCREEN_COORD);
    }

    /* Moving from bigger ==> smaller screen
       y_b = ((y_a - top) * HEIGHT) / (bottom - top) */

    if (state->mouse_y < from->border.top)
        return MIN_SCREEN_COORD;

    if (state->mouse_y > from->border.bottom)
        return MAX_SCREEN_COORD;

    return ((state->mouse_y - from->border.top) * MAX_SCREEN_COORD) / size_from;
}

void switch_desktop(device_t *state, int output_from, int output_to) {
    if (output_to == DESKTOP_1) {
        send_keycombo(&SWITCH_TO_DESKTOP_1, state);
    } else {
        send_keycombo(&SWITCH_TO_DESKTOP_2, state);
    }
    state->mouse_y = (output_to == DESKTOP_1) ? MIN_SCREEN_COORD : MAX_SCREEN_COORD;
    state->active_desktop = output_to;
    switch_output(state, state->active_output);
}

void switch_screen(device_t *state, int new_x, int output_from, int output_to) {
    mouse_abs_report_t hidden_pointer = {.y = MIN_SCREEN_COORD, .x = MAX_SCREEN_COORD};

    output_mouse_report(&hidden_pointer, NULL, state);
    switch_output(state, output_to);
    state->mouse_x = (output_to == OUTPUT_A) ? MIN_SCREEN_COORD : MAX_SCREEN_COORD;
    state->mouse_y = scale_y_coordinate(output_from, output_to, state);
}

void check_screen_switch(const mouse_values_t *values, device_t *state) {
    int new_x = state->mouse_x + values->move_x;
    int new_y = state->mouse_y + values->move_y;

    /* No switching allowed if explicitly disabled */
    if (state->switch_lock)
        return;

    /* End of screen left switches screen A->B  TODO: make configurable */
    if (new_x < MIN_SCREEN_COORD - JUMP_THRESHOLD && state->active_output == OUTPUT_A) {
        switch_screen(state, new_x, OUTPUT_A, OUTPUT_B);
    }

    /* End of screen right switches screen B->A  TODO: make configurable */
    else if (new_x > MAX_SCREEN_COORD + JUMP_THRESHOLD && state->active_output == OUTPUT_B) {
        switch_screen(state, new_x, OUTPUT_B, OUTPUT_A);
    }

    /* Switch from Desktop 2->1 */
    else if (new_y > MAX_SCREEN_COORD && state->active_output == OUTPUT_A && state->active_desktop == DESKTOP_2) {
        switch_desktop(state, DESKTOP_2, DESKTOP_1);
    }

    /* Switch from Desktop 1->2 */
    else if (new_y < MIN_SCREEN_COORD && state->active_output == OUTPUT_A && state->active_desktop == DESKTOP_1) {
        switch_desktop(state, DESKTOP_1, DESKTOP_2);
    }
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
