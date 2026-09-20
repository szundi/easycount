#include <pebble.h>

#define WATCHDOG_TIMEOUT_MS 15000
#define SPINNER_FRAME_MS 100
#define SPINNER_DOT_COUNT 8
#define SUCCESS_DISPLAY_MS 1400
#define ERROR_DISPLAY_MS 1400

typedef enum {
  UI_STATE_LOADING,
  UI_STATE_SUCCESS,
  UI_STATE_ERROR,
} UiState;

static Window *s_window;
static Layer *s_icon_layer;
static TextLayer *s_text_layer;
static TextLayer *s_value_layer;
static AppTimer *s_watchdog_timer;
static AppTimer *s_spinner_timer;
static UiState s_ui_state = UI_STATE_LOADING;
static uint8_t s_spinner_frame;
static bool s_finished;
static bool s_configuration_launch;
static char s_status_text[20];
static char s_value_text[20];

static const GPoint s_spinner_offsets[SPINNER_DOT_COUNT] = {
  { 0, -24 },
  { 17, -17 },
  { 24, 0 },
  { 17, 17 },
  { 0, 24 },
  { -17, 17 },
  { -24, 0 },
  { -17, -17 },
};

static void prv_icon_update_proc(Layer *layer, GContext *ctx) {
  if (s_configuration_launch) {
    return;
  }

  const GRect bounds = layer_get_bounds(layer);
  const int16_t center_x = bounds.size.w / 2;
  const int16_t center_y = s_ui_state == UI_STATE_SUCCESS
      ? bounds.size.h * 34 / 100
      : bounds.size.h / 2 - 16;

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 7);

  if (s_ui_state == UI_STATE_LOADING) {
    for (uint8_t index = 0; index < SPINNER_DOT_COUNT; index++) {
      const GPoint offset = s_spinner_offsets[index];
      const uint16_t radius = index == s_spinner_frame ? 5 : 2;
      graphics_fill_circle(ctx,
                           GPoint(center_x + offset.x, center_y + offset.y),
                           radius);
    }
  } else if (s_ui_state == UI_STATE_SUCCESS) {
    graphics_draw_line(ctx, GPoint(center_x - 32, center_y),
                       GPoint(center_x - 10, center_y + 22));
    graphics_draw_line(ctx, GPoint(center_x - 10, center_y + 22),
                       GPoint(center_x + 35, center_y - 25));
  } else {
    graphics_draw_line(ctx, GPoint(center_x - 27, center_y - 27),
                       GPoint(center_x + 27, center_y + 27));
    graphics_draw_line(ctx, GPoint(center_x + 27, center_y - 27),
                       GPoint(center_x - 27, center_y + 27));
  }
}

static void prv_exit(void *context) {
  window_stack_pop_all(false);
}

static void prv_spinner_tick(void *context) {
  s_spinner_timer = NULL;
  if (s_ui_state != UI_STATE_LOADING) {
    return;
  }

  s_spinner_frame = (s_spinner_frame + 1) % SPINNER_DOT_COUNT;
  layer_mark_dirty(s_icon_layer);
  s_spinner_timer = app_timer_register(SPINNER_FRAME_MS,
                                        prv_spinner_tick, NULL);
}

static void prv_finish(UiState state, int32_t status_code,
                       int32_t response_value) {
  if (s_finished) {
    return;
  }
  s_finished = true;

  if (s_watchdog_timer) {
    app_timer_cancel(s_watchdog_timer);
    s_watchdog_timer = NULL;
  }
  if (s_spinner_timer) {
    app_timer_cancel(s_spinner_timer);
    s_spinner_timer = NULL;
  }

  s_ui_state = state;
  layer_mark_dirty(s_icon_layer);
  APP_LOG(APP_LOG_LEVEL_INFO, "Request result: %ld", (long) status_code);

  if (state == UI_STATE_SUCCESS) {
    const GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
    snprintf(s_value_text, sizeof(s_value_text), "%ld", (long) response_value);
    text_layer_set_text(s_value_layer, s_value_text);
    layer_set_hidden(text_layer_get_layer(s_value_layer), false);
    layer_set_frame(text_layer_get_layer(s_text_layer),
                    GRect(0, bounds.size.h * 80 / 100,
                          bounds.size.w, 36));
    text_layer_set_text(s_text_layer, "OK");
    vibes_short_pulse();
    exit_reason_set(APP_EXIT_ACTION_PERFORMED_SUCCESSFULLY);
    app_timer_register(SUCCESS_DISPLAY_MS, prv_exit, NULL);
    return;
  }

  if (status_code > 0) {
    snprintf(s_status_text, sizeof(s_status_text), "HTTP %ld", (long) status_code);
  } else {
    snprintf(s_status_text, sizeof(s_status_text), "Hiba %ld", (long) status_code);
  }
  text_layer_set_text(s_text_layer, s_status_text);
  vibes_long_pulse();
  app_timer_register(ERROR_DISPLAY_MS, prv_exit, NULL);
}

static void prv_watchdog_timeout(void *context) {
  s_watchdog_timer = NULL;
  prv_finish(UI_STATE_ERROR, 0, 0);
}

static void prv_inbox_received(DictionaryIterator *iterator, void *context) {
  if (s_configuration_launch) {
    if (dict_find(iterator, MESSAGE_KEY_CONFIG_DONE)) {
      window_stack_pop_all(false);
    }
    return;
  }

  Tuple *status_tuple = dict_find(iterator, MESSAGE_KEY_STATUS);
  if (!status_tuple) {
    prv_finish(UI_STATE_ERROR, -2, 0);
    return;
  }

  const int32_t status_code = status_tuple->value->int32;
  const bool is_success = status_code >= 200 && status_code < 300;
  if (is_success) {
    Tuple *value_tuple = dict_find(iterator, MESSAGE_KEY_VALUE);
    if (!value_tuple) {
      prv_finish(UI_STATE_ERROR, -5, 0);
      return;
    }
    prv_finish(UI_STATE_SUCCESS, status_code, value_tuple->value->int32);
    return;
  }

  prv_finish(UI_STATE_ERROR, status_code, 0);
}

static void prv_send_request(void) {
  DictionaryIterator *iterator;
  AppMessageResult result = app_message_outbox_begin(&iterator);
  if (result != APP_MSG_OK) {
    prv_finish(UI_STATE_ERROR, -4, 0);
    return;
  }

  dict_write_uint8(iterator, MESSAGE_KEY_REQUEST, 1);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    prv_finish(UI_STATE_ERROR, -4, 0);
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(window_layer);

  s_icon_layer = layer_create(bounds);
  layer_set_update_proc(s_icon_layer, prv_icon_update_proc);
  layer_add_child(window_layer, s_icon_layer);

  s_value_layer = text_layer_create(
      GRect(0, bounds.size.h * 52 / 100, bounds.size.w, 48));
  text_layer_set_background_color(s_value_layer, GColorClear);
  text_layer_set_font(s_value_layer,
                      fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_value_layer, GTextAlignmentCenter);
  layer_set_hidden(text_layer_get_layer(s_value_layer), true);
  layer_add_child(window_layer, text_layer_get_layer(s_value_layer));

  const int16_t text_y = s_configuration_launch
      ? (bounds.size.h - 36) / 2
      : bounds.size.h / 2 + 24;
  s_text_layer = text_layer_create(GRect(0, text_y, bounds.size.w, 36));
  text_layer_set_background_color(s_text_layer, GColorClear);
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_text_layer, GTextAlignmentCenter);
  text_layer_set_text(s_text_layer,
                      s_configuration_launch ? "Beállítás..." : "Pillanat...");
  layer_add_child(window_layer, text_layer_get_layer(s_text_layer));
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_text_layer);
  text_layer_destroy(s_value_layer);
  layer_destroy(s_icon_layer);
}

static void prv_init(void) {
  s_configuration_launch = launch_reason() == APP_LAUNCH_PHONE;

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, false);

  app_message_register_inbox_received(prv_inbox_received);
  const AppMessageResult result = app_message_open(64, 32);
  if (result != APP_MSG_OK) {
    prv_finish(UI_STATE_ERROR, -3, 0);
    return;
  }

  if (s_configuration_launch) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Configuration launch: request suppressed");
    return;
  }

  s_spinner_timer = app_timer_register(SPINNER_FRAME_MS,
                                        prv_spinner_tick, NULL);
  s_watchdog_timer = app_timer_register(WATCHDOG_TIMEOUT_MS,
                                         prv_watchdog_timeout, NULL);
  prv_send_request();
}

static void prv_deinit(void) {
  if (s_spinner_timer) {
    app_timer_cancel(s_spinner_timer);
  }
  if (s_watchdog_timer) {
    app_timer_cancel(s_watchdog_timer);
  }
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
