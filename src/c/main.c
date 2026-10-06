// Brolly v2.0.0 — main.c
// Complete watchface implementation for Pebble (Aplite/Basalt/Emery)

#include <pebble.h>
#include "gpath_weather.h"
#include <stdlib.h>

// Integer square root (avoids float sqrt and math.h dependency)
static int isqrt_int(int n) {
  if (n <= 0) return 0;
  int x = n;
  int y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + n / x) / 2; }
  return x;
}

// Forward declaration
static void tick_handler(struct tm *tick_time, TimeUnits units_changed);
static void accel_tap_handler(AccelAxisType axis, int32_t direction);

// ─────────────────────────────────────────────────────────────────────────────
// Design constants & scaling
// ─────────────────────────────────────────────────────────────────────────────
#define DESIGN_W 144
#define DESIGN_H 168

static int s_screen_w = 144;
static int s_screen_h = 168;

static inline int POS_X(int px) {
  return (px * s_screen_w) / DESIGN_W;
}
static inline int POS_Y(int py) {
  return (py * s_screen_h) / DESIGN_H;
}

#ifdef PBL_ROUND
// Round-display geometry keeps text, weather and complications inside the
// circular safe area instead of merely scaling the rectangular coordinates.
#define ROUND_REFERENCE_DIAMETER 180
#define ROUND_MARKER_OUTER_INSET   2
#define ROUND_LABEL_INSET         22
#define ROUND_CONTENT_CLEARANCE   12

// Scale round-layout reference pixels against the active circular diameter.
// This keeps Chalk at its original geometry and makes Gabbro adapt to the
// emulator's available circular canvas, including a 260 x 260 display.
static int round_px(int reference_px) {
  int diameter = s_screen_w < s_screen_h ? s_screen_w : s_screen_h;
  return (reference_px * diameter) / ROUND_REFERENCE_DIAMETER;
}

static int round_radius(void) {
  int diameter = s_screen_w < s_screen_h ? s_screen_w : s_screen_h;
  return diameter / 2;
}

static int round_content_radius(int half_extent) {
  int radius = round_radius() - round_px(ROUND_CONTENT_CLEARANCE) - half_extent;
  return radius > 0 ? radius : 1;
}

#endif

#ifdef PBL_COLOR
  #define MONO_COLOR(c) (c)
#else
  #define MONO_COLOR(c) (gcolor_equal((c), GColorBlack) ? GColorBlack : GColorWhite)
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Persist keys
// ─────────────────────────────────────────────────────────────────────────────
#define PERSIST_ICONS    0
#define PERSIST_SETTINGS 1
#define PERSIST_CITY     2
#define PERSIST_TEMP_C   3
#define PERSIST_TEMP_F   4
#define PERSIST_CUSTOM_LOCATION 5
#define PERSIST_SUNRISE_HOUR    6
#define PERSIST_SUNRISE_MINUTE  7
#define PERSIST_SUNSET_HOUR     8
#define PERSIST_SUNSET_MINUTE   9
#define PERSIST_COMPACT_NUMBER_SIZES 10

// AppMessage keys used only for settings snapshot synchronisation.
#define KEY_REQUEST_SETTINGS  163
#define KEY_SETTINGS_SNAPSHOT 164
#define KEY_FORECAST_START_HOUR 166

// ─────────────────────────────────────────────────────────────────────────────
// AppMessage inbox/outbox sizes
// ─────────────────────────────────────────────────────────────────────────────
#define APP_MSG_INBOX_SIZE  512
// A complete settings snapshot is returned to the companion on demand.
#define APP_MSG_OUTBOX_SIZE  512

// ─────────────────────────────────────────────────────────────────────────────
// Shake / seconds timings
// ─────────────────────────────────────────────────────────────────────────────
#define SHAKE_DISPLAY_MS  5000
#define SHAKE_DELAY_MS     500

// ─────────────────────────────────────────────────────────────────────────────
// Hand geometry constants
// ─────────────────────────────────────────────────────────────────────────────
#define FIXED_HAND_BASE_WIDTH   3
#define FIXED_HAND_OUTER_WIDTH  10
#define FIXED_HAND_INNER_WIDTH  5
#define FIXED_HAND_BASE_PX     20

// ─────────────────────────────────────────────────────────────────────────────
// Settings structure
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
  // Background
  GColor background_color;
  // Hour markers
  bool   display_hour_markers;
  GColor hour_marker_color;
  // Minute markers
  bool   display_minor_markers;
  GColor minute_marker_color;
  // Numbers
  // 0=Digital 1=Regular 2=Traditional 3=Thin 4=Tall 5=Roman Sans 6=Roman Serif.
  uint8_t number_font;
  uint8_t number_size;    // 1–5 → {18,22,26,30,36}
  GColor  number_color;
  // Icons
  // Manual weather-icon size ladder (1–5).
  uint8_t icon_size;
  GColor  icon_color;
  // Watch hands
  GColor hour_hand_outer;
  GColor hour_hand_inner;
  GColor min_hand_outer;
  GColor min_hand_inner;
  // Seconds hand
  GColor  seconds_hand_color;
  uint8_t seconds_hand_mode;   // 0=Never 1=Always 2=Shake
  uint8_t seconds_shake_dur;   // seconds (default 10)
  // Date / temperature
  uint8_t date_visible;   // 0=Always 1=Off 2=Shake
  uint8_t temp_visible;   // 0=Always 1=Off 2=Shake
  uint8_t display_mode;   // 0=Both 1=Temp 2=Date 3=None
  uint8_t temp_unit;      // 0=C 1=F
  GColor  date_color;
  GColor  temp_color;
  // Battery ring
  uint8_t battery_ring_threshold;    // 0=off,50,40,30,20,10
  uint8_t battery_center_threshold;  // 0=off,20,10,5
  // Sunrise/sunset markers
  uint8_t sunrise_marker_visible;    // 0=Always 1=Only with icons 2=Off
  GColor  sunrise_marker_color;
  GColor  sunset_marker_color;
  // Bluetooth
  bool   vibrate_bt_disconnect;
  bool   vibrate_bt_reconnect;
  bool   bt_disconnect_min_inner_red;
  GColor bt_disconnect_outer_color;
  GColor bt_disconnect_inner_color;
  // Shake mode (for icons)
  uint8_t shake_mode;   // 0=Show on shake 1=Always show 2=Always hide 3=Side-by-side
  // City name display
  uint8_t city_display_mode; // 0=Off 1=Shake 2=Always
  GColor  city_color;
  // Icon/number colour mode
  uint8_t icon_color_mode;   // 0=Single colour 1=Weather based 2=Rainbow
  // False keeps icons at the current number font's rendered pixel height.
  // Reuses this final legacy byte to preserve persisted Settings layout.
  bool    icon_size_manual_override;
  // Extended hour-marker size: 0=large/current, 1=medium, 2=small.
  // Kept at the end so older persisted Settings records remain compatible.
  uint8_t extended_marker_size;
} Settings;

// ─────────────────────────────────────────────────────────────────────────────
// Global state
// ─────────────────────────────────────────────────────────────────────────────
static Window   *s_window;
static Layer    *s_bg_layer;
static Layer    *s_complication_layer;
static Layer    *s_hour_layer;
static Layer    *s_minute_layer;

// Geometry reused by every window render. Pebble asks the full hierarchy to
// redraw whenever any layer is dirtied, so cache calculations rather than add
// redundant full-screen layers.
static GPoint s_dial_center;
static GPoint s_hour_tip;
static GPoint s_minute_tip;
static GPoint s_second_tip_cache[60];
static GPoint s_second_tail_cache[60];

typedef struct {
  bool valid;
  bool show_date;
  bool show_temp;
  bool show_city;
  char date_text[16];
  char temp_text[16];
  GFont font;
  GRect date_rect;
  GRect temp_rect;
  GRect city_rect;
} ComplicationRenderCache;
static ComplicationRenderCache s_complication_cache;

// Exact visible dial content is cached during the background paint on colour
// targets. The complication layer uses this cache only to choose a clear
// existing lane; it never moves dial labels, icons, or watch hands. B/W uses
// a conservative bound based on the measured largest rendered dial item so
// that the Aplite-class heap retains adequate operating headroom.
#ifndef PBL_BW
typedef struct {
  bool number_valid;
  bool icon_valid;
  GRect number_rect;
  GRect icon_rect;
} DialContentBounds;
static DialContentBounds s_dial_content_bounds[12];
#endif
static bool s_dial_content_bounds_valid = false;

static Settings  s_settings;
static int8_t    s_icons[24];

#define ICON_SIZE_VALUE_MASK             0x0F
#define NUMBERS_VISIBILITY_SHIFT          4
#define NUMBERS_VISIBILITY_MASK           0x30
#define LEGACY_HIDE_DIAGONAL_NUMBERS_MASK 0x80

typedef enum {
  EXTENDED_MARKER_SIZE_LARGE = 0,
  EXTENDED_MARKER_SIZE_MEDIUM = 1,
  EXTENDED_MARKER_SIZE_SMALL = 2
} ExtendedMarkerSize;

typedef enum {
  NUMBERS_VISIBILITY_SHOW_ALL = 0,
  NUMBERS_VISIBILITY_ONLY_CARDINALS = 1,
  NUMBERS_VISIBILITY_SHOW_NONE = 2
} NumbersVisibility;

static uint8_t manual_icon_size(void) {
  uint8_t size = s_settings.icon_size & ICON_SIZE_VALUE_MASK;
  return (size >= 1 && size <= 5) ? size : 3;
}

static NumbersVisibility numbers_visibility(void) {
  uint8_t value = (s_settings.icon_size & NUMBERS_VISIBILITY_MASK) >>
                  NUMBERS_VISIBILITY_SHIFT;
  return (value <= NUMBERS_VISIBILITY_SHOW_NONE)
    ? (NumbersVisibility)value : NUMBERS_VISIBILITY_SHOW_ALL;
}

static bool is_cardinal_hour_position(int hour_position) {
  return hour_position == 0 || hour_position == 3 ||
         hour_position == 6 || hour_position == 9;
}

// Validate persisted and incoming enum/range values in one place. This permits
// a partial older Settings record to retain valid fields while restoring only
// invalid bytes to the documented defaults.
static bool normalize_settings(void) {
  bool changed = false;
  if (s_settings.number_font > 6) {
    s_settings.number_font = 0;
    changed = true;
  }
  if (s_settings.number_size < 1 || s_settings.number_size > 5) {
    s_settings.number_size = 3;
    changed = true;
  }
  uint8_t icon_size = s_settings.icon_size & ICON_SIZE_VALUE_MASK;
  uint8_t visibility = (s_settings.icon_size & NUMBERS_VISIBILITY_MASK) >>
                       NUMBERS_VISIBILITY_SHIFT;
  if (icon_size < 1 || icon_size > 5) {
    icon_size = 3;
    changed = true;
  }
  if (visibility > NUMBERS_VISIBILITY_SHOW_NONE) {
    visibility = NUMBERS_VISIBILITY_SHOW_ALL;
    changed = true;
  }
  uint8_t normalized_icon_size = icon_size |
                                 (visibility << NUMBERS_VISIBILITY_SHIFT);
  if (s_settings.icon_size != normalized_icon_size) {
    s_settings.icon_size = normalized_icon_size;
    changed = true;
  }
  if (s_settings.shake_mode > 3) { s_settings.shake_mode = 0; changed = true; }
  if (s_settings.seconds_hand_mode > 2) { s_settings.seconds_hand_mode = 2; changed = true; }
  if (s_settings.seconds_shake_dur == 0 || s_settings.seconds_shake_dur > 60) {
    s_settings.seconds_shake_dur = 10;
    changed = true;
  }
  if (s_settings.date_visible > 2) { s_settings.date_visible = 0; changed = true; }
  if (s_settings.temp_visible > 2) { s_settings.temp_visible = 0; changed = true; }
  if (s_settings.display_mode > 3) { s_settings.display_mode = 0; changed = true; }
  if (s_settings.temp_unit > 1) { s_settings.temp_unit = 0; changed = true; }
  if (s_settings.sunrise_marker_visible > 2) { s_settings.sunrise_marker_visible = 0; changed = true; }
  if (s_settings.city_display_mode > 2) { s_settings.city_display_mode = 1; changed = true; }
  if (s_settings.icon_color_mode > 3) { s_settings.icon_color_mode = 0; changed = true; }
  if (s_settings.extended_marker_size > EXTENDED_MARKER_SIZE_SMALL) {
    s_settings.extended_marker_size = EXTENDED_MARKER_SIZE_MEDIUM;
    changed = true;
  }
  return changed;
}

static bool number_is_visible(int hour_position) {
  NumbersVisibility visibility = numbers_visibility();
  return visibility == NUMBERS_VISIBILITY_SHOW_ALL ||
         (visibility == NUMBERS_VISIBILITY_ONLY_CARDINALS &&
          is_cardinal_hour_position(hour_position));
}

static bool weather_icons_are_visible(void);

static bool hour_marker_is_extended(int hour_position) {
  if (weather_icons_are_visible()) return false;
  NumbersVisibility visibility = numbers_visibility();
  return visibility == NUMBERS_VISIBILITY_SHOW_NONE ||
         (visibility == NUMBERS_VISIBILITY_ONLY_CARDINALS &&
          !is_cardinal_hour_position(hour_position));
}
static int8_t    s_temp_c = 127; // 127 = No data
static int8_t    s_temp_f = 127;
static char      s_city_name[32] = "";
// Kept on the watch as part of the per-user settings snapshot so a companion
// restart cannot make the settings page lose a custom location.
static char      s_custom_location[64] = "";
static int8_t    s_sunrise_hour = 6,  s_sunrise_min = 0;
static int8_t    s_sunset_hour  = 18, s_sunset_min  = 0;
// Hour represented by s_icons[0]. The companion receives a rolling forecast,
// so the array does not necessarily start at the current watch hour.
static int8_t    s_forecast_start_hour = -1;

static struct tm s_last_time;
static bool      s_bt_connected = true;
static uint8_t   s_battery_pct  = 100;

// Marker cache
static GPoint s_min_marker_outer[60];
static GPoint s_hour_marker_outer[12];

// Perimeter point cache: pre-computed once at init (screen size never changes)
static GPoint s_perimeter_cache[12];

// Consolidated rainbow colour array (shared by all colour-mode-2 branches)
static const GColor8 s_rainbow_colors[12] = {
  { .argb = 0xF0 }, // h=0  red
  { .argb = 0xF8 }, // h=1  orange
  { .argb = 0xFC }, // h=2  yellow
  { .argb = 0xEC }, // h=3  chartreuse
  { .argb = 0xCC }, // h=4  green
  { .argb = 0xCE }, // h=5  spring
  { .argb = 0xCF }, // h=6  cyan
  { .argb = 0xCB }, // h=7  sky blue
  { .argb = 0xC3 }, // h=8  blue
  { .argb = 0xE3 }, // h=9  violet
  { .argb = 0xF3 }, // h=10 magenta
  { .argb = 0xF2 }, // h=11 rose
};

// Select the rolling-forecast-array offset represented by a dial position.
// The array begins at s_forecast_start_hour, not necessarily the current watch
// hour. Without this anchor, an 23:00 update viewed at 01:00 maps 08:00 to
// 06:00 and can show a moon after the sunrise marker.
static int forecast_hour_for_dial_position(int dial_hour, int current_hour) {
  int am_hour = dial_hour;
  int pm_hour = am_hour + 12;
  int target_hour;

  // Open-Meteo's rolling window begins at the current forecast hour. Keep that
  // hour at offset zero for the entire hour rather than skipping twelve hours
  // ahead as soon as the minute changes from 00.
  if (am_hour >= current_hour) {
    target_hour = am_hour;
  } else if (pm_hour >= current_hour) {
    target_hour = pm_hour;
  } else {
    target_hour = am_hour + 24;
  }

  int start_hour = s_forecast_start_hour;
  // Use current-hour behaviour only while retaining an old pre-anchor cache.
  // The first successful update from this version overwrites that cache.
  if (start_hour < 0 || start_hour > 23) start_hour = current_hour;
  if (target_hour < start_hour) target_hour += 24;
  return target_hour - start_hour;
}

// Font cache
static GFont   s_cached_number_font = NULL;
static uint8_t s_cached_font_id     = 255;
static uint8_t s_cached_font_size   = 255;
static bool    s_cached_is_sbs      = false;  // tracks whether cache was built for SBS mode


// Ink-bounds cache for the current number font (measured once per font change).
// These describe, for a given digit string, how much empty padding sits inside
// the text box on each side: the visible ink rectangle relative to the box.
// Index 0..11 matches s_num_strings ("12","1",.."11").
typedef struct {
  int8_t left;   // px from box left  to first ink column
  int8_t top;    // px from box top   to first ink row
  int8_t right;  // px from last ink column to box right
  int8_t bottom; // px from last ink row    to box bottom
  uint8_t box_w; // measured text box width
  uint8_t box_h; // measured text box height
  bool valid;
} InkBounds;
static InkBounds s_ink[12];
static bool s_ink_valid = false;
// Per-group minimum edge padding: the smallest empty-space value on the
// screen-facing side across all digits in that group. Used so all numbers
// in a group share the same perpendicular baseline regardless of glyph shape.
// top_min = min(ib.top)  for h=11,0,1
// bot_min = min(ib.bottom) for h=5,6,7
// lft_min = min(ib.left)  for h=8,9,10
// rgt_min = min(ib.right) for h=2,3,4
static int8_t s_ink_top_min = 0;
static int8_t s_ink_bot_min = 0;
static int8_t s_ink_lft_min = 0;
static int8_t s_ink_rgt_min = 0;
// Per-group max box dimension on the perpendicular axis. Ensures all
// digits in a group use the same box size for the edge-anchor formula,
// so varying SDK box heights/widths do not shift individual digits.
static uint8_t s_ink_top_box_h = 0; // max box_h for top group (h=11,0,1)
static uint8_t s_ink_bot_box_h = 0; // max box_h for bottom group (h=5,6,7)
static uint8_t s_ink_lft_box_w = 0; // max box_w for left group (h=8,9,10)
static uint8_t s_ink_rgt_box_w = 0; // max box_w for right group (h=2,3,4)

// Shake / icon display state
static bool     s_showing_icons   = false;
static AppTimer *s_shake_timer     = NULL;

static bool weather_icons_are_visible(void) {
  return s_settings.shake_mode == 1 || s_settings.shake_mode == 3 ||
         (s_settings.shake_mode == 0 && s_showing_icons);
}
static AppTimer *s_shake_delay_timer = NULL;

// Seconds visibility
static bool     s_seconds_visible = false;
static AppTimer *s_seconds_timer   = NULL;

// Service state prevents needless unsubscribe/subscribe churn when settings
// change without altering the required tick resolution or tap behaviour.
static TimeUnits s_tick_units = 0;
static bool      s_accel_tap_subscribed = false;

// Test-mode restore state
static uint8_t   s_test_saved_battery = 0;
static bool      s_test_saved_bt      = true;
static AppTimer *s_test_battery_timer  = NULL;
static AppTimer *s_test_bt_timer       = NULL;

static void test_battery_restore_callback(void *data) {
  s_test_battery_timer = NULL;
  s_battery_pct = s_test_saved_battery;
  layer_mark_dirty(s_minute_layer);
}

static void test_bt_restore_callback(void *data) {
  s_test_bt_timer = NULL;
  s_bt_connected  = s_test_saved_bt;
  layer_mark_dirty(s_minute_layer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Convert 0xRRGGBB integer to GColor
static GColor rgb_to_gcolor(int32_t rgb) {
  if (rgb == -1) return GColorClear;
  return GColorFromRGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Convert Pebble's native 2-bit-per-channel colour back to the 0xRRGGBB form
// used by the settings page and companion configuration messages.
static int32_t gcolor_to_rgb(GColor color) {
  uint8_t argb = color.argb;
  uint8_t r = ((argb >> 4) & 0x03) * 85;
  uint8_t g = ((argb >> 2) & 0x03) * 85;
  uint8_t b = (argb & 0x03) * 85;
  return ((int32_t)r << 16) | ((int32_t)g << 8) | b;
}

// Polar to point (clock-hand angle, 12 o'clock = 0)
static GPoint polar_to_point(GPoint center, int32_t angle, int radius) {
  return GPoint(
    center.x + (int)(sin_lookup(angle) * radius / TRIG_MAX_RATIO),
    center.y - (int)(cos_lookup(angle) * radius / TRIG_MAX_RATIO)
  );
}

// Square perimeter point: where a ray from center at angle hits the rect boundary
// margin_x / margin_y inset the boundary
static GPoint get_perimeter_point(GPoint center, int32_t angle, int margin_x, int margin_y) {
#ifdef PBL_ROUND
  // Inset every radial endpoint slightly so marker and second-hand caps do not
  // clip against the circular edge.
  int radius = round_radius() - margin_x - round_px(ROUND_MARKER_OUTER_INSET);
  return polar_to_point(center, angle, radius);
#else
  int hw = s_screen_w / 2 - margin_x;
  int hh = s_screen_h / 2 - margin_y;

  int32_t sin_a = sin_lookup(angle);
  int32_t cos_a = cos_lookup(angle);

  if (sin_a == 0 && cos_a == 0) return center;

  int32_t t_x = (sin_a != 0) ? (hw * TRIG_MAX_RATIO / abs(sin_a)) : INT32_MAX;
  int32_t t_y = (cos_a != 0) ? (hh * TRIG_MAX_RATIO / abs(cos_a)) : INT32_MAX;
  int32_t t   = (t_x < t_y) ? t_x : t_y;

  return GPoint(
    center.x + (int)(sin_a * t / TRIG_MAX_RATIO),
    center.y - (int)(cos_a * t / TRIG_MAX_RATIO)
  );
#endif
}

// Angle for a clock position (minutes 0–59 or hours 0–11 scaled)
// Return an endpoint precisely along the vector from an outer marker point to
// the dial centre. Recomputing this at draw time keeps all marker axes radial
// after platform-specific pixel rounding and any conditional length changes.
static GPoint marker_inner_toward_center(GPoint center, GPoint outer, int length) {
  int dx = center.x - outer.x;
  int dy = center.y - outer.y;
  int distance = isqrt_int(dx * dx + dy * dy);
  if (distance < 1) return outer;
  return GPoint(outer.x + dx * length / distance,
                outer.y + dy * length / distance);
}

static int standard_hour_marker_length(void) {
#ifdef PBL_ROUND
  return 3;
#else
  return 2;
#endif
}

static int extended_hour_marker_length(void) {
  int length = standard_hour_marker_length() * 12;
  if (s_settings.extended_marker_size == EXTENDED_MARKER_SIZE_MEDIUM) {
    length /= 2;
  } else if (s_settings.extended_marker_size == EXTENDED_MARKER_SIZE_SMALL) {
    length = standard_hour_marker_length();
  }
  return length;
}

static bool angle_has_extended_hour_marker(int32_t angle) {
  // Extend a solar marker only when it lands exactly on a long hour marker,
  // not merely somewhere within that hour's visual sector.
  int32_t normalized_angle = angle % TRIG_MAX_ANGLE;
  if (normalized_angle < 0) normalized_angle += TRIG_MAX_ANGLE;
  for (int hour_position = 0; hour_position < 12; hour_position++) {
    if (normalized_angle == TRIG_MAX_ANGLE * hour_position / 12) {
      return hour_marker_is_extended(hour_position);
    }
  }
  return false;
}

static int32_t angle_for_minute(int minute) {
  return TRIG_MAX_ANGLE * minute / 60;
}

static int32_t angle_for_hour(int hour, int minute) {
  return TRIG_MAX_ANGLE * (hour * 60 + minute) / 720;
}


static void update_current_hand_tips(void) {
  int radius = (s_screen_w < s_screen_h ? s_screen_w : s_screen_h) / 2;
  s_minute_tip = polar_to_point(
    s_dial_center, angle_for_minute(s_last_time.tm_min),
#ifdef PBL_ROUND
    radius * 88 / 100
#else
    radius * 95 / 100
#endif
  );
  s_hour_tip = polar_to_point(
    s_dial_center, angle_for_hour(s_last_time.tm_hour % 12, s_last_time.tm_min),
#ifdef PBL_ROUND
    radius * 55 / 100
#else
    radius * 60 / 100
#endif
  );
}

static void compute_static_hand_geometry(void) {
  s_dial_center = GPoint(s_screen_w / 2, s_screen_h / 2);
  for (int second = 0; second < 60; second++) {
    int32_t angle = angle_for_minute(second);
#ifdef PBL_ROUND
    s_second_tip_cache[second] = get_perimeter_point(
      s_dial_center, angle,
      round_px(ROUND_MARKER_OUTER_INSET),
      round_px(ROUND_MARKER_OUTER_INSET));
#else
    s_second_tip_cache[second] = get_perimeter_point(s_dial_center, angle, 0, 0);
#endif
    s_second_tail_cache[second] = polar_to_point(
      s_dial_center, angle + (TRIG_MAX_ANGLE / 2), POS_Y(18));
  }
  update_current_hand_tips();
}

static void invalidate_complication_cache(void) {
  s_complication_cache.valid = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Marker cache computation
// ─────────────────────────────────────────────────────────────────────────────
static void compute_markers(void) {
  GPoint center = GPoint(s_screen_w / 2, s_screen_h / 2);
#ifdef PBL_ROUND
  int radius = round_radius() - round_px(ROUND_MARKER_OUTER_INSET);
#endif

  // Only outer endpoints are static. Inner endpoints depend on the current
  // marker visibility/extension mode and are calculated when the dial repaints.
  for (int i = 0; i < 60; i++) {
    int32_t angle = angle_for_minute(i);
#ifdef PBL_ROUND
    s_min_marker_outer[i] = polar_to_point(center, angle, radius);
#else
    s_min_marker_outer[i] = get_perimeter_point(center, angle, 0, 0);
#endif
  }

  for (int i = 0; i < 12; i++) {
    int32_t angle = TRIG_MAX_ANGLE * i / 12;
#ifdef PBL_ROUND
    s_hour_marker_outer[i] = polar_to_point(center, angle, radius);
#else
    s_hour_marker_outer[i] = get_perimeter_point(center, angle, 0, 0);
#endif
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Font loading
// ─────────────────────────────────────────────────────────────────────────────
#ifndef PBL_BW
static uint32_t get_font_resource_id(uint8_t font_id, uint8_t size_idx) {
#ifdef PBL_BW
  return 0; // Use system fonts on black-and-white targets
#else
#if defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_CHALK)
  // Basalt's compact rectangle and Chalk's usable circular dial need a smaller
  // optical baseline. Size 3 intentionally equals their former Size 1.
#if defined(PBL_PLATFORM_CHALK)
  // Twelve Roman labels must share a 180px circumference. Preserve five real
  // slider steps, but cap the largest Chalk label at 30px: 36px makes the
  // measured XI/XII/I and VII/VIII/VI ink overlap by construction.
  if (font_id == 6) {
    static const uint32_t chalk_roman_serif_resources[5] = {
      RESOURCE_ID_FONT_ROMAN_SERIF_17, RESOURCE_ID_FONT_ROMAN_SERIF_23,
      RESOURCE_ID_FONT_ROMAN_SERIF_26, RESOURCE_ID_FONT_ROMAN_SERIF_28,
      RESOURCE_ID_FONT_ROMAN_SERIF_30
    };
    return size_idx < 5 ? chalk_roman_serif_resources[size_idx] : 0;
  }
#endif
  static const uint32_t font_resources[7][5] = {
    { RESOURCE_ID_FONT_DIGITAL_14, RESOURCE_ID_FONT_DIGITAL_18,
      RESOURCE_ID_FONT_DIGITAL_18, RESOURCE_ID_FONT_DIGITAL_26,
      RESOURCE_ID_FONT_DIGITAL_30 },
    { RESOURCE_ID_FONT_STANDARD_14, RESOURCE_ID_FONT_STANDARD_18,
      RESOURCE_ID_FONT_STANDARD_18, RESOURCE_ID_FONT_STANDARD_26,
      RESOURCE_ID_FONT_STANDARD_30 },
    { RESOURCE_ID_FONT_TRADITIONAL_17, RESOURCE_ID_FONT_TRADITIONAL_23,
      RESOURCE_ID_FONT_TRADITIONAL_23, RESOURCE_ID_FONT_TRADITIONAL_28,
      RESOURCE_ID_FONT_TRADITIONAL_34 },
    { RESOURCE_ID_FONT_THIN_18, RESOURCE_ID_FONT_SBS_THIN_20,
      RESOURCE_ID_FONT_THIN_22, RESOURCE_ID_FONT_THIN_26,
      RESOURCE_ID_FONT_THIN_31 },
    { RESOURCE_ID_FONT_OVERSIZE_11, RESOURCE_ID_FONT_OVERSIZE_13,
      RESOURCE_ID_FONT_OVERSIZE_15, RESOURCE_ID_FONT_OVERSIZE_18,
      RESOURCE_ID_FONT_OVERSIZE_23 },
    // Roman Sans is rendered geometrically and does not load a font resource.
    { 0, 0, 0, 0, 0 },
    { RESOURCE_ID_FONT_ROMAN_SERIF_17, RESOURCE_ID_FONT_ROMAN_SERIF_23,
      RESOURCE_ID_FONT_ROMAN_SERIF_26, RESOURCE_ID_FONT_ROMAN_SERIF_30,
      RESOURCE_ID_FONT_ROMAN_SERIF_36 },
  };
#else
  // Each row is remapped so the user-selected preferred former size is Size 3.
  // Sizes 1–2 remain smaller; Sizes 4–5 remain larger.
  static const uint32_t font_resources[7][5] = {
    { RESOURCE_ID_FONT_DIGITAL_22, RESOURCE_ID_FONT_DIGITAL_26,
      RESOURCE_ID_FONT_DIGITAL_26, RESOURCE_ID_FONT_DIGITAL_36,
      RESOURCE_ID_FONT_DIGITAL_42 },
    { RESOURCE_ID_FONT_STANDARD_22, RESOURCE_ID_FONT_STANDARD_26,
      RESOURCE_ID_FONT_STANDARD_26, RESOURCE_ID_FONT_STANDARD_36,
      RESOURCE_ID_FONT_STANDARD_42 },
    { RESOURCE_ID_FONT_TRADITIONAL_26, RESOURCE_ID_FONT_TRADITIONAL_28,
      RESOURCE_ID_FONT_TRADITIONAL_28, RESOURCE_ID_FONT_TRADITIONAL_40,
      RESOURCE_ID_FONT_TRADITIONAL_46 },
    { RESOURCE_ID_FONT_THIN_22, RESOURCE_ID_FONT_THIN_26,
      RESOURCE_ID_FONT_THIN_31, RESOURCE_ID_FONT_THIN_36,
      RESOURCE_ID_FONT_THIN_42 },
    { RESOURCE_ID_FONT_OVERSIZE_15, RESOURCE_ID_FONT_OVERSIZE_18,
      RESOURCE_ID_FONT_OVERSIZE_23, RESOURCE_ID_FONT_OVERSIZE_27,
      RESOURCE_ID_FONT_OVERSIZE_32 },
    // Roman Sans is rendered geometrically and does not load a font resource.
    { 0, 0, 0, 0, 0 },
    { RESOURCE_ID_FONT_ROMAN_SERIF_17, RESOURCE_ID_FONT_ROMAN_SERIF_23,
      RESOURCE_ID_FONT_ROMAN_SERIF_26, RESOURCE_ID_FONT_ROMAN_SERIF_30,
      RESOURCE_ID_FONT_ROMAN_SERIF_36 },
  };
#endif
  if (font_id > 6 || size_idx > 4) return 0;
  return font_resources[font_id][size_idx];
#endif
}

static uint32_t get_sbs_font_resource_id(uint8_t font_id, uint8_t size_idx) {
#ifdef PBL_BW
  return 0;
#else
#if defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_CHALK)
  // Keep the same compact optical baseline in side-by-side mode. Size 3 is
  // the former Size 1; two genuinely smaller and two larger steps remain.
  static const uint32_t sbs_font_resources[7][5] = {
    { RESOURCE_ID_FONT_SBS_DIGITAL_10, RESOURCE_ID_FONT_SBS_DIGITAL_12,
      RESOURCE_ID_FONT_SBS_DIGITAL_12, RESOURCE_ID_FONT_SBS_DIGITAL_16,
      RESOURCE_ID_FONT_SBS_DIGITAL_18 },
    { RESOURCE_ID_FONT_SBS_STANDARD_8, RESOURCE_ID_FONT_SBS_STANDARD_10,
      RESOURCE_ID_FONT_SBS_STANDARD_10, RESOURCE_ID_FONT_SBS_STANDARD_16,
      RESOURCE_ID_FONT_SBS_STANDARD_18 },
    { RESOURCE_ID_FONT_SBS_TRADITIONAL_13, RESOURCE_ID_FONT_SBS_TRADITIONAL_15,
      RESOURCE_ID_FONT_SBS_TRADITIONAL_15, RESOURCE_ID_FONT_SBS_TRADITIONAL_19,
      RESOURCE_ID_FONT_SBS_TRADITIONAL_23 },
    { RESOURCE_ID_FONT_SBS_THIN_11, RESOURCE_ID_FONT_SBS_THIN_13,
      RESOURCE_ID_FONT_SBS_THIN_15, RESOURCE_ID_FONT_SBS_THIN_18,
      RESOURCE_ID_FONT_SBS_THIN_20 },
    { RESOURCE_ID_FONT_SBS_OVERSIZE_8, RESOURCE_ID_FONT_SBS_OVERSIZE_9,
      RESOURCE_ID_FONT_SBS_OVERSIZE_11, RESOURCE_ID_FONT_SBS_OVERSIZE_13,
      RESOURCE_ID_FONT_SBS_OVERSIZE_15 },
    { 0, 0, 0, 0, 0 },
    { RESOURCE_ID_FONT_SBS_ROMAN_SERIF_13, RESOURCE_ID_FONT_SBS_ROMAN_SERIF_15,
      RESOURCE_ID_FONT_SBS_ROMAN_SERIF_17, RESOURCE_ID_FONT_SBS_ROMAN_SERIF_19,
      RESOURCE_ID_FONT_SBS_ROMAN_SERIF_23 },
  };
#else
  static const uint32_t sbs_font_resources[7][5] = {
    { RESOURCE_ID_FONT_SBS_DIGITAL_14, RESOURCE_ID_FONT_SBS_DIGITAL_16,
      RESOURCE_ID_FONT_SBS_DIGITAL_16, RESOURCE_ID_FONT_SBS_DIGITAL_20,
      RESOURCE_ID_FONT_SBS_DIGITAL_22 },
    { RESOURCE_ID_FONT_SBS_STANDARD_14, RESOURCE_ID_FONT_SBS_STANDARD_16,
      RESOURCE_ID_FONT_SBS_STANDARD_16, RESOURCE_ID_FONT_SBS_STANDARD_23,
      RESOURCE_ID_FONT_SBS_STANDARD_30 },
    { RESOURCE_ID_FONT_SBS_TRADITIONAL_17, RESOURCE_ID_FONT_SBS_TRADITIONAL_19,
      RESOURCE_ID_FONT_SBS_TRADITIONAL_19, RESOURCE_ID_FONT_SBS_TRADITIONAL_26,
      RESOURCE_ID_FONT_SBS_TRADITIONAL_30 },
    { RESOURCE_ID_FONT_SBS_THIN_15, RESOURCE_ID_FONT_SBS_THIN_18,
      RESOURCE_ID_FONT_SBS_THIN_20, RESOURCE_ID_FONT_SBS_THIN_24,
      RESOURCE_ID_FONT_SBS_THIN_28 },
    { RESOURCE_ID_FONT_SBS_OVERSIZE_11, RESOURCE_ID_FONT_SBS_OVERSIZE_13,
      RESOURCE_ID_FONT_SBS_OVERSIZE_15, RESOURCE_ID_FONT_SBS_OVERSIZE_16,
      RESOURCE_ID_FONT_SBS_OVERSIZE_20 },
    { 0, 0, 0, 0, 0 },
    { RESOURCE_ID_FONT_SBS_ROMAN_SERIF_13, RESOURCE_ID_FONT_SBS_ROMAN_SERIF_15,
      RESOURCE_ID_FONT_SBS_ROMAN_SERIF_17, RESOURCE_ID_FONT_SBS_ROMAN_SERIF_19,
      RESOURCE_ID_FONT_SBS_ROMAN_SERIF_23 },
  };
#endif
  if (font_id > 6 || size_idx > 4) return 0;
  return sbs_font_resources[font_id][size_idx];
#endif
}
#endif // !PBL_BW

// City, date, and temperature retain their original v3.2.0 system font and
// target-specific scale. Number Font selection applies only to dial numerals.
static GFont get_complication_font(void) {
#ifdef PBL_PLATFORM_EMERY
  return fonts_get_system_font(FONT_KEY_GOTHIC_24);
#else
  return fonts_get_system_font(FONT_KEY_GOTHIC_14);
#endif
}

static uint8_t number_font_id(void) {
  return s_settings.number_font <= 6 ? s_settings.number_font : 0;
}

static GFont get_number_font(void) {
  uint8_t fid = number_font_id();
  uint8_t sidx = (s_settings.number_size >= 1 && s_settings.number_size <= 5)
                   ? s_settings.number_size - 1 : 2;
  bool is_sbs = (s_settings.shake_mode == 3);
  if (fid == s_cached_font_id && sidx == s_cached_font_size &&
      is_sbs == s_cached_is_sbs && (fid == 5 || s_cached_number_font)) {
    return s_cached_number_font;
  }
  if (s_cached_number_font) {
#ifndef PBL_BW
    fonts_unload_custom_font(s_cached_number_font);
#endif
    s_cached_number_font = NULL;
  }
#ifdef PBL_BW
  // The legacy targets cannot use Brolly's custom colour-font resources, but
  // Number Size should still change the label scale. These system-font ladders
  // preserve each style's closest available family and use smaller values for
  // the side-by-side layout rather than silently ignoring the setting.
  static const char *bw_fonts[7][5] = {
    { FONT_KEY_LECO_20_BOLD_NUMBERS, FONT_KEY_LECO_28_LIGHT_NUMBERS, FONT_KEY_LECO_28_LIGHT_NUMBERS, FONT_KEY_LECO_36_BOLD_NUMBERS, FONT_KEY_LECO_42_NUMBERS },
    { FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_28 },
    { FONT_KEY_BITHAM_18_LIGHT_SUBSET, FONT_KEY_BITHAM_30_BLACK, FONT_KEY_BITHAM_30_BLACK, FONT_KEY_BITHAM_42_LIGHT, FONT_KEY_BITHAM_42_BOLD },
    { FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_28 },
    { FONT_KEY_BITHAM_18_LIGHT_SUBSET, FONT_KEY_BITHAM_30_BLACK, FONT_KEY_BITHAM_30_BLACK, FONT_KEY_BITHAM_42_LIGHT, FONT_KEY_BITHAM_42_BOLD },
    { FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_28, FONT_KEY_BITHAM_42_LIGHT },
    { FONT_KEY_DROID_SERIF_28_BOLD, FONT_KEY_DROID_SERIF_28_BOLD, FONT_KEY_DROID_SERIF_28_BOLD, FONT_KEY_DROID_SERIF_28_BOLD, FONT_KEY_DROID_SERIF_28_BOLD },
  };
  static const char *bw_sbs_fonts[7][5] = {
    { FONT_KEY_LECO_20_BOLD_NUMBERS, FONT_KEY_LECO_20_BOLD_NUMBERS, FONT_KEY_LECO_20_BOLD_NUMBERS, FONT_KEY_LECO_28_LIGHT_NUMBERS, FONT_KEY_LECO_32_BOLD_NUMBERS },
    { FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_14_BOLD, FONT_KEY_GOTHIC_14_BOLD, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24 },
    { FONT_KEY_BITHAM_18_LIGHT_SUBSET, FONT_KEY_BITHAM_18_LIGHT_SUBSET, FONT_KEY_BITHAM_18_LIGHT_SUBSET, FONT_KEY_DROID_SERIF_28_BOLD, FONT_KEY_BITHAM_42_LIGHT },
    { FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_24 },
    { FONT_KEY_BITHAM_18_LIGHT_SUBSET, FONT_KEY_BITHAM_18_LIGHT_SUBSET, FONT_KEY_BITHAM_30_BLACK, FONT_KEY_BITHAM_30_BLACK, FONT_KEY_BITHAM_42_LIGHT },
    { FONT_KEY_GOTHIC_09, FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_28 },
    { FONT_KEY_DROID_SERIF_28_BOLD, FONT_KEY_DROID_SERIF_28_BOLD, FONT_KEY_DROID_SERIF_28_BOLD, FONT_KEY_DROID_SERIF_28_BOLD, FONT_KEY_DROID_SERIF_28_BOLD },
  };
  s_cached_number_font = fonts_get_system_font(
    (is_sbs ? bw_sbs_fonts : bw_fonts)[fid][sidx]);
#else
  if (fid == 5) {
    s_cached_number_font = NULL;
  } else {
    uint32_t res_id = is_sbs ? get_sbs_font_resource_id(fid, sidx)
                             : get_font_resource_id(fid, sidx);
    if (res_id) {
      s_cached_number_font = fonts_load_custom_font(resource_get_handle(res_id));
    } else {
      s_cached_number_font = fonts_get_system_font(FONT_KEY_GOTHIC_28);
    }
  }
#endif
  s_cached_font_id = fid;
  s_cached_font_size = sidx;
  s_cached_is_sbs = is_sbs;
  s_ink_valid = false;
  return s_cached_number_font;
}

// ─────────────────────────────────────────────────────────────────────────────
// Number positioning helpers
// ─────────────────────────────────────────────────────────────────────────────
static const char *s_num_strings[12] = {
  "12","1","2","3","4","5","6","7","8","9","10","11"
};

static const char *s_roman_num_strings[12] = {
  "XII","I","II","III","IV","V","VI","VII","VIII","IX","X","XI"
};

// Roman Sans is the dedicated Roman-numeral font choice. The
// other five Number Font choices continue to draw standard decimal labels.
static const char *get_number_string(int h) {
  return number_font_id() >= 5 ? s_roman_num_strings[h] : s_num_strings[h];
}

#ifdef PBL_COLOR
// Roman Sans is drawn geometrically rather than through a TTF. This avoids
// platform font-metric rounding and guarantees five genuinely distinct visible
// sizes. Size 3 is the original geometric renderer's Size 1 (15px).
static int roman_sans_height(void) {
#if defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_CHALK)
  // Size 3 matches the former compact Size 1 on Basalt and Chalk.
  static const uint8_t normal_heights[5] = {6, 7, 9, 12, 15};
  static const uint8_t sbs_heights[5] = {5, 6, 8, 10, 12};
#else
  static const uint8_t normal_heights[5] = {9, 12, 15, 19, 23};
  static const uint8_t sbs_heights[5] = {8, 10, 12, 14, 16};
#endif
  uint8_t index = (s_settings.number_size >= 1 && s_settings.number_size <= 5)
                    ? s_settings.number_size - 1 : 2;
  return (s_settings.shake_mode == 3 ? sbs_heights : normal_heights)[index];
}

static int roman_sans_stroke_width(int height) {
  if (height >= 30) return 4;
  if (height >= 22) return 3;
  return 2;
}

static int roman_sans_glyph_advance(char glyph, int height) {
  return glyph == 'I' ? (height * 2 + 2) / 5 : (height * 9 + 5) / 10;
}

static int roman_sans_text_width(const char *text, int height) {
  int width = 0;
  for (const char *p = text; *p; p++) {
    width += roman_sans_glyph_advance(*p, height);
  }
  return width > 0 ? width : 1;
}

static void draw_roman_sans_text(GContext *ctx, const char *text,
                                 GRect bounds, GTextAlignment alignment,
                                 GColor color) {
  int height = roman_sans_height();
  int stroke = roman_sans_stroke_width(height);
  int width = roman_sans_text_width(text, height);
  int x = bounds.origin.x;
  if (alignment == GTextAlignmentCenter) {
    x += (bounds.size.w - width) / 2;
  } else if (alignment == GTextAlignmentRight) {
    x += bounds.size.w - width;
  }

  int inset = stroke / 2;
  int top = bounds.origin.y + inset;
  int bottom = bounds.origin.y + height - 1 - inset;
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, stroke);

  for (const char *p = text; *p; p++) {
    int advance = roman_sans_glyph_advance(*p, height);
    int left = x + inset;
    int right = x + advance - 1 - inset;
    int middle = x + advance / 2;
    if (*p == 'I') {
      graphics_draw_line(ctx, GPoint(middle, top), GPoint(middle, bottom));
    } else if (*p == 'V') {
      graphics_draw_line(ctx, GPoint(left, top), GPoint(middle, bottom));
      graphics_draw_line(ctx, GPoint(middle, bottom), GPoint(right, top));
    } else if (*p == 'X') {
      graphics_draw_line(ctx, GPoint(left, top), GPoint(right, bottom));
      graphics_draw_line(ctx, GPoint(right, top), GPoint(left, bottom));
    }
    x += advance;
  }
}
#endif

static void draw_number_text(GContext *ctx, int hour_position, GFont font,
                             GRect bounds, GTextAlignment alignment,
                             GColor color) {
  const char *text = get_number_string(hour_position);
#ifdef PBL_COLOR
  if (number_font_id() == 5) {
    draw_roman_sans_text(ctx, text, bounds, alignment, color);
    return;
  }
#endif
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, bounds,
                     GTextOverflowModeWordWrap, alignment, NULL);
}

// Cache the exact ink area currently used by a dial numeral. The cached result
// lets date/temp choose a clear established lane without another font render or
// a free-form layout solver.
#ifndef PBL_BW
static void cache_number_ink_bounds(int hour_position, GRect bounds,
                                    GTextAlignment alignment) {
  if (hour_position < 0 || hour_position >= 12) return;

#ifdef PBL_COLOR
  if (number_font_id() == 5) {
    int height = roman_sans_height();
    int width = roman_sans_text_width(get_number_string(hour_position), height);
    int x = bounds.origin.x;
    if (alignment == GTextAlignmentCenter) {
      x += (bounds.size.w - width) / 2;
    } else if (alignment == GTextAlignmentRight) {
      x += bounds.size.w - width;
    }
    int inset = roman_sans_stroke_width(height) / 2;
    s_dial_content_bounds[hour_position].number_rect =
      GRect(x - inset, bounds.origin.y, width + inset * 2, height);
    s_dial_content_bounds[hour_position].number_valid = true;
    return;
  }
#endif

  InkBounds ib = s_ink[hour_position];
  if (!ib.valid) return;
  int width = ib.box_w - ib.left - ib.right;
  int height = ib.box_h - ib.top - ib.bottom;
  if (width < 1 || height < 1) return;
  int x = bounds.origin.x + ib.left;
  if (alignment == GTextAlignmentCenter) {
    x = bounds.origin.x + (bounds.size.w - width) / 2;
  } else if (alignment == GTextAlignmentRight) {
    x = bounds.origin.x + bounds.size.w - width - ib.right;
  }
  s_dial_content_bounds[hour_position].number_rect =
    GRect(x, bounds.origin.y + ib.top, width, height);
  s_dial_content_bounds[hour_position].number_valid = true;
}

static void cache_weather_icon_ink_bounds(int hour_position, GPathIconID icon_id,
                                          int ox, int oy, int size) {
  if (hour_position < 0 || hour_position >= 12) return;
  int min_x, min_y, max_x, max_y;
  if (!weather_icon_visible_bounds(icon_id, size, &min_x, &min_y, &max_x, &max_y)) {
    return;
  }
  s_dial_content_bounds[hour_position].icon_rect = GRect(
    ox + min_x, oy + min_y, max_x - min_x + 1, max_y - min_y + 1);
  s_dial_content_bounds[hour_position].icon_valid = true;
}
#else
static void cache_number_ink_bounds(int hour_position, GRect bounds,
                                    GTextAlignment alignment) {
  (void)hour_position;
  (void)bounds;
  (void)alignment;
}

static void cache_weather_icon_ink_bounds(int hour_position, GPathIconID icon_id,
                                          int ox, int oy, int size) {
  (void)hour_position;
  (void)icon_id;
  (void)ox;
  (void)oy;
  (void)size;
}
#endif


// ─────────────────────────────────────────────────────────────────────────────
// Hand drawing
// The hand is drawn as a single continuous GPath outline:
//   centre → stem → base cap (inward semicircle) → left side →
//   tip cap (outward semicircle) → right side → base cap → centre
// This produces one seamless cohesive shape with no joints.
// The interior gap is never drawn — true hole showing layers below.
// ─────────────────────────────────────────────────────────────────────────────
//
// Pill dimensions (all in pixels, fixed regardless of hand length):
//   2px wall | 5px gap | 2px wall  =  9px total outer width
//   Wall centre = 3.5px from axis  -> use PILL_LINE_OFF = 4 (rounds outward)
//   Cap radius  = PILL_LINE_OFF so the semicircle exactly spans the wall centres
//   Stem width  = 2px, centred on axis, connects at the inner wall edge (2.5px)
#define PILL_LINE_OFF   4    // px from axis to centre of each wall line
#define PILL_CAP_R      4    // semicircle radius = PILL_LINE_OFF
#define PILL_STROKE     2    // wall line and cap stroke width
#define STEM_STROKE     2    // centre stem stroke width
#define PILL_CAP_SEGS   12   // segments per semicircle (12 = smoother at 4px radius)

// Path points: base right + base arc interior + base left + left wall +
//   tip left (dup) + tip arc interior + tip right + right wall = 2*SEGS+6
#define HAND_PATH_PTS  (2 * PILL_CAP_SEGS + 6)

static void draw_inittick_hand(GContext *ctx, GPoint center, GPoint tip,
                                GColor hand_color, GColor inner_color) {
  if (gcolor_equal(hand_color, GColorClear) && gcolor_equal(inner_color, GColorClear)) return;

  int dx = tip.x - center.x;
  int dy = tip.y - center.y;
  int dist = isqrt_int(dx * dx + dy * dy);
  if (dist == 0) return;

  GColor hc = MONO_COLOR(hand_color);

  // Hand axis angle and perpendicular angle
  int32_t ha = atan2_lookup(dx, -dy);
  int32_t pa = ha + (TRIG_MAX_ANGLE / 4);

  // Perpendicular offset to wall line centres
  int ox = (int32_t)PILL_LINE_OFF * sin_lookup(pa) / TRIG_MAX_RATIO;
  int oy = -(int32_t)PILL_LINE_OFF * cos_lookup(pa) / TRIG_MAX_RATIO;
  if (ox == 0 && oy == 0) ox = 1;

  // base_pt: centre of the base cap (POS_Y(FIXED_HAND_BASE_PX) from centre)
  int base_px = POS_Y(FIXED_HAND_BASE_PX);
  GPoint base_pt = GPoint(center.x + dx * base_px / dist,
                          center.y + dy * base_px / dist);

  // ── Centre stem: 2px line from pivot to the inward edge of the base cap ────
  // Stop at base_pt - PILL_CAP_R so the stem does not intrude into the gap
  GPoint stem_end = GPoint(
    base_pt.x - PILL_CAP_R * dx / dist,
    base_pt.y - PILL_CAP_R * dy / dist
  );
  if (!gcolor_equal(hand_color, GColorClear)) {
    graphics_context_set_stroke_color(ctx, hc);
    graphics_context_set_stroke_width(ctx, STEM_STROKE);
    graphics_draw_line(ctx, center, stem_end);
  }

  // ── Pill outline: single continuous polyline ─────────────────────────────────────────────
  // Sequence: base cap (inward) → left side → tip cap (outward) → right side
  // The path is open (not closed back to centre) — stem handles that visually.
  GPoint pts[HAND_PATH_PTS];
  int n = 0;

  // Base cap: inward semicircle, arc from (ha+90°) to (ha+270°).
  // Force first/last points to exact wall endpoints to eliminate integer-rounding gaps.
  pts[n++] = GPoint(base_pt.x + ox, base_pt.y + oy);  // exact right wall start
  for (int i = 1; i < PILL_CAP_SEGS; i++) {
    int32_t a = (ha + (TRIG_MAX_ANGLE / 4))
                + (TRIG_MAX_ANGLE / 2) * i / PILL_CAP_SEGS;
    pts[n++] = GPoint(
      base_pt.x + PILL_CAP_R * sin_lookup(a) / TRIG_MAX_RATIO,
      base_pt.y - PILL_CAP_R * cos_lookup(a) / TRIG_MAX_RATIO
    );
  }
  pts[n++] = GPoint(base_pt.x - ox, base_pt.y - oy);  // exact left wall start

  // Left wall to tip left
  pts[n++] = GPoint(tip.x - ox, tip.y - oy);

  // Tip cap: outward semicircle, arc from (ha-90°) to (ha+90°).
  // Force first/last points to exact wall endpoints.
  pts[n++] = GPoint(tip.x - ox, tip.y - oy);  // exact left wall end (duplicate for join)
  for (int i = 1; i < PILL_CAP_SEGS; i++) {
    int32_t a = (ha - (TRIG_MAX_ANGLE / 4))
                + (TRIG_MAX_ANGLE / 2) * i / PILL_CAP_SEGS;
    pts[n++] = GPoint(
      tip.x + PILL_CAP_R * sin_lookup(a) / TRIG_MAX_RATIO,
      tip.y - PILL_CAP_R * cos_lookup(a) / TRIG_MAX_RATIO
    );
  }
  pts[n++] = GPoint(tip.x + ox, tip.y + oy);  // exact right wall end

  // Right wall back to base right
  pts[n++] = GPoint(base_pt.x + ox, base_pt.y + oy);

  // Draw the pill outline as one connected polyline
  graphics_context_set_stroke_color(ctx, hc);
  graphics_context_set_stroke_width(ctx, PILL_STROKE);
  for (int i = 0; i < n - 1; i++) {
    graphics_draw_line(ctx, pts[i], pts[i + 1]);
  }
  // ── Inner colour line + cap sealing ──────────────────────────────────────────
  // Cap fill circles are only drawn when inner_color is set, so that a
  // transparent inner colour does not leave visible dots at the cap ends.
#define INNER_LINE_WIDTH  4  // 4px — centred in the 6px gap
#define INNER_END_R       2  // 2px end cap radius
  if (!gcolor_equal(inner_color, GColorClear)) {
    GColor ic = MONO_COLOR(inner_color);
    // Seal cap pixel gaps in hand colour first, then paint inner colour on top
    graphics_context_set_fill_color(ctx, hc);
    graphics_fill_circle(ctx, base_pt, PILL_CAP_R);
    graphics_fill_circle(ctx, tip,     PILL_CAP_R);
    // Inner colour line
    graphics_context_set_stroke_color(ctx, ic);
    graphics_context_set_stroke_width(ctx, INNER_LINE_WIDTH);
    graphics_draw_line(ctx, base_pt, tip);
    // Rounded ends on top of cap fills
    graphics_context_set_fill_color(ctx, ic);
    graphics_fill_circle(ctx, base_pt, INNER_END_R);
    graphics_fill_circle(ctx, tip,     INNER_END_R);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Layer update callbacks
// ─────────────────────────────────────────────────────────────────────────────

// Measure the true visible-ink rectangle of each digit string for the current
// number font. We draw each string into a scratch area, capture the frame
// buffer, and scan for lit pixels. Run once per font change inside the paint
// callback (the only place text rendering + framebuffer capture is valid).
// While measuring, the scratch draws are overwritten by the normal paint that
// follows, so nothing is visible to the user.
static void measure_ink_bounds(GContext *ctx, GFont font, int sw, int sh) {
  // A top-left scratch box is clipped by a round framebuffer. For Roman Serif
  // on round watches, scan in the centre of the dial so the measured bounds
  // include every visible serif and letter rather than a clipped fragment.
  const int SW = 80, SH = 80;
#ifdef PBL_ROUND
  const bool round_serif = number_font_id() == 6;
  const int SX = round_serif ? (sw - SW) / 2 : 0;
  const int SY = round_serif ? (sh - SH) / 2 : 0;
#else
  const int SX = 0, SY = 0;
  (void)sw; (void)sh;
#endif

  for (int h = 0; h < 12; h++) {
    InkBounds *ib = &s_ink[h];
    ib->valid = false;

#ifdef PBL_COLOR
    if (number_font_id() == 5) {
      int width = roman_sans_text_width(get_number_string(h),
                                        roman_sans_height());
      ib->box_w = (uint8_t)SW;
      ib->box_h = (uint8_t)SH;
      ib->left = 0;
      ib->top = 0;
      ib->right = (int8_t)(SW - width);
      ib->bottom = (int8_t)(SH - roman_sans_height());
      ib->valid = true;
      continue;
    }
#endif

    // Use the full SW×SH scratch area as both the draw rect and the scan
    // region. The SDK's graphics_text_layout_get_content_size can return
    // a box.h smaller than the actual rendered pixels for some glyphs
    // (e.g. Raleway-Light "5"/"7"), causing those digits to be placed
    // lower than "6". By scanning the full area and using SW/SH as the
    // reference box, all digits share the same fixed frame and the
    // padding values are directly comparable across glyphs.
    ib->box_w = (uint8_t)SW;
    ib->box_h = (uint8_t)SH;

    // Clear scratch area, draw digit in white, top-left aligned.
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(SX, SY, SW, SH), 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, get_number_string(h), font,
                       GRect(SX, SY, SW, SH),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    // Capture and scan the full SW×SH area.
    GBitmap *fb = graphics_capture_frame_buffer(ctx);
    if (!fb) continue;

    int min_x = SW, min_y = SH, max_x = -1, max_y = -1;

    for (int y = 0; y < SH; y++) {
      GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, SY + y);
      for (int x = 0; x < SW; x++) {
        int px = SX + x;
        if (px < row.min_x || px > row.max_x) continue;
        bool lit;
#if defined(PBL_COLOR)
        GColor8 c = (GColor8){ .argb = row.data[px] };
        lit = (c.r || c.g || c.b);
#else
        uint8_t byte = row.data[px >> 3];
        lit = (byte >> (px & 7)) & 1;
#endif
        if (lit) {
          if (x < min_x) min_x = x;
          if (x > max_x) max_x = x;
          if (y < min_y) min_y = y;
          if (y > max_y) max_y = y;
        }
      }
    }
    graphics_release_frame_buffer(ctx, fb);

    if (max_x < 0) {
      // No ink found â fall back to zero padding.
      ib->left = ib->top = ib->right = ib->bottom = 0;
      ib->valid = true;
      continue;
    }

    // Padding from each side of the SWÃSH scratch box to the ink edge.
    ib->left   = (int8_t)min_x;
    ib->top    = (int8_t)min_y;
    ib->right  = (int8_t)(SW - 1 - max_x);
    ib->bottom = (int8_t)(SH - 1 - max_y);
    if (ib->right  < 0) ib->right  = 0;
    if (ib->bottom < 0) ib->bottom = 0;
    ib->valid = true;
  }

  // Compute per-group minimum edge padding. All digits in a group share the
  // same perpendicular baseline: the one set by whichever digit has the LEAST
  // empty space on the screen-facing side (i.e. whose ink extends furthest
  // toward the edge). This prevents glyphs with more internal whitespace from
  // appearing to "float" away from the edge relative to their neighbours.
  s_ink_top_min = 127;  s_ink_top_box_h = 0;
  s_ink_bot_min = 127;  s_ink_bot_box_h = 0;
  s_ink_lft_min = 127;  s_ink_lft_box_w = 0;
  s_ink_rgt_min = 127;  s_ink_rgt_box_w = 0;
  for (int h = 0; h < 12; h++) {
    if (!s_ink[h].valid) continue;
    if (h == 11 || h == 0 || h == 1) {
      if (s_ink[h].top    < s_ink_top_min) s_ink_top_min = s_ink[h].top;
      if (s_ink[h].box_h  > s_ink_top_box_h) s_ink_top_box_h = s_ink[h].box_h;
    } else if (h == 5 || h == 6 || h == 7) {
      if (s_ink[h].bottom < s_ink_bot_min) s_ink_bot_min = s_ink[h].bottom;
      if (s_ink[h].box_h  > s_ink_bot_box_h) s_ink_bot_box_h = s_ink[h].box_h;
    } else if (h == 8 || h == 9 || h == 10) {
      if (s_ink[h].left   < s_ink_lft_min) s_ink_lft_min = s_ink[h].left;
      if (s_ink[h].box_w  > s_ink_lft_box_w) s_ink_lft_box_w = s_ink[h].box_w;
    } else {
      if (s_ink[h].right  < s_ink_rgt_min) s_ink_rgt_min = s_ink[h].right;
      if (s_ink[h].box_w  > s_ink_rgt_box_w) s_ink_rgt_box_w = s_ink[h].box_w;
    }
  }
  if (s_ink_top_min == 127) s_ink_top_min = 0;
  if (s_ink_bot_min == 127) s_ink_bot_min = 0;
  if (s_ink_lft_min == 127) s_ink_lft_min = 0;
  if (s_ink_rgt_min == 127) s_ink_rgt_min = 0;

  // Clear the scratch area so the subsequent normal paint starts clean.
  graphics_context_set_fill_color(ctx, MONO_COLOR(s_settings.background_color));
  graphics_fill_rect(ctx, GRect(SX, SY, SW, SH), 0, GCornerNone);

  s_ink_valid = true;
}

static int displayed_number_height(void) {
  int max_height = 0;
  for (int h = 0; h < 12; h++) {
    if (!s_ink[h].valid) continue;
    int height = s_ink[h].box_h - s_ink[h].top - s_ink[h].bottom;
    if (height > max_height) max_height = height;
  }
  return max_height > 0 ? max_height : 26;
}

#ifdef PBL_ROUND
// Roman Serif has wide horizontal glyph groups (notably VIII and XII) and
// taller serif overshoots than the other dial fonts.  Its round layout must
// be based on the measured *ink*, rather than an icon-sized text frame: a
// frame's top padding otherwise pushes the visible glyph too far outward.
//
// The selected centre radius is bounded twice:
//   1. The ink's furthest corner stays within the circular display.
//   2. Its outward radial edge stops before the inner end of the relevant
//      hour marker, including the marker's 3px stroke and a clear 2px gap.
// This applies to every selected Roman Serif size, including side-by-side.
static GRect round_roman_serif_text_rect(int hour_position, InkBounds ib) {
  int ink_w = ib.box_w - ib.left - ib.right;
  int ink_h = ib.box_h - ib.top - ib.bottom;
  if (ink_w < 1) ink_w = 1;
  if (ink_h < 1) ink_h = 1;

  int half_w = (ink_w + 1) / 2;
  int half_h = (ink_h + 1) / 2;
  int32_t angle = TRIG_MAX_ANGLE * hour_position / 12;
  int radial_half_extent =
    ((abs(sin_lookup(angle)) * half_w) +
     (abs(cos_lookup(angle)) * half_h)) / TRIG_MAX_RATIO;
  int tangent_half_extent =
    ((abs(cos_lookup(angle)) * half_w) +
     (abs(sin_lookup(angle)) * half_h)) / TRIG_MAX_RATIO;

  // Keep the *four corners* of the measured ink inside the circle. This is
  // much less conservative than subtracting its full diagonal from the dial
  // radius, so large Roman labels can occupy their correct outer sectors.
  int safe_circle_radius = round_radius() - round_px(2);
  int edge_sq = safe_circle_radius * safe_circle_radius -
                tangent_half_extent * tangent_half_extent;
  if (edge_sq < 1) edge_sq = 1;
  int radius = isqrt_int(edge_sq) - radial_half_extent;

  if (s_settings.display_hour_markers) {
    int marker_length = hour_marker_is_extended(hour_position)
      ? extended_hour_marker_length() : standard_hour_marker_length();

    // Marker endpoints are line centres.  Three pixels clear the 3px marker
    // stroke and preserve a two-pixel visual separation from numeral ink.
    int marker_limit = round_radius() - marker_length - 3 - radial_half_extent;
    if (radius > marker_limit) radius = marker_limit;
  }

  // Keep a defensive inner lower bound if a future font is unexpectedly large.
  if (radius < round_px(20)) radius = round_px(20);

  GPoint ink_center = polar_to_point(
    s_dial_center, TRIG_MAX_ANGLE * hour_position / 12, radius);

  // The 80px layout frame prevents Pebble's font metrics from clipping a
  // Roman glyph.  The baseline is offset by the measured top padding so the
  // *visible* ink, not its invisible frame, is centred on the radial point.
  return GRect(ink_center.x - 40,
               ink_center.y - ink_h / 2 - ib.top,
               80, 80);
}
#endif

// BG layer: background fill, markers, numbers/icons
static void bg_layer_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int sw = bounds.size.w;
  int sh = bounds.size.h;
  GPoint center = GPoint(sw / 2, sh / 2);

  // Rebuild colour-target visible-content bounds in the same pass that draws
  // the dial. The complication layer is above this layer, so it sees this exact
  // frame without another raster pass.
#ifndef PBL_BW
  memset(s_dial_content_bounds, 0, sizeof(s_dial_content_bounds));
  s_dial_content_bounds_valid = false;
#endif

    // Fill background
  graphics_context_set_fill_color(ctx, MONO_COLOR(s_settings.background_color));
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  // Measure true ink bounds once per font change. Must happen after the
  // background fill (so the scratch area starts clean) but before any markers
  // or icons are drawn (so the scratch-area clear at the end of measurement
  // does not erase already-drawn content in the top-left corner).
  GFont num_font = get_number_font();
  if (!s_ink_valid) {
    measure_ink_bounds(ctx, num_font, sw, sh);
  }
  // Minute markers
  if (s_settings.display_minor_markers) {
    graphics_context_set_stroke_color(ctx, MONO_COLOR(s_settings.minute_marker_color));
    graphics_context_set_stroke_width(ctx, 1);
    for (int i = 0; i < 60; i++) {
      // Use the designed length directly rather than re-measuring a rounded
      // cached segment, so every minute marker starts at the edge and reaches
      // inward by the same distance for its marker class.
      bool is_quarter = (i == 7 || i == 23 || i == 37 || i == 53);
      int marker_length = (sw >= 200) ? (is_quarter ? 10 : 4)
                                      : (is_quarter ? 4 : 2);
      GPoint marker_inner = marker_inner_toward_center(
        s_dial_center, s_min_marker_outer[i], marker_length);
      graphics_draw_line(ctx, s_min_marker_outer[i], marker_inner);
    }
  }

  // Hour markers
  if (s_settings.display_hour_markers) {
    graphics_context_set_stroke_color(ctx, MONO_COLOR(s_settings.hour_marker_color));
    graphics_context_set_stroke_width(ctx, 3);
    for (int i = 0; i < 12; i++) {
      int marker_length = standard_hour_marker_length();
      if (hour_marker_is_extended(i)) {
        marker_length = extended_hour_marker_length();
      }
      GPoint marker_inner = marker_inner_toward_center(
        s_dial_center, s_hour_marker_outer[i], marker_length);
      graphics_draw_line(ctx, s_hour_marker_outer[i], marker_inner);
    }
  }

  // Sunrise / sunset markers
  int sun_inset = (sw >= 200) ? 8 : 5;
#ifdef PBL_ROUND
  sun_inset = round_px(sun_inset);
#endif
  bool show_sun_markers = false;
  if (s_settings.sunrise_marker_visible == 0) {
    show_sun_markers = true;
  } else if (s_settings.sunrise_marker_visible == 1) {
    show_sun_markers = weather_icons_are_visible();
  }
  // else 2 = Off

  if (show_sun_markers) {

    // Sunrise marker
    int sr_total_min = s_sunrise_hour * 60 + s_sunrise_min;
    int cur_total_min = s_last_time.tm_hour * 60 + s_last_time.tm_min;
    int sr_delta = sr_total_min - cur_total_min;
    if (sr_delta <= 0) sr_delta += 24 * 60;
    // The next sunrise is always relevant, even when it is more than 12 hours away.
    if (sr_delta <= 24 * 60) {
      // Keep solar indicators on the same 60-position grid as the dial's
      // hour/minute markers. Each dial step represents 12 real minutes.
      int32_t sr_angle = angle_for_minute(
        s_sunrise_hour * 5 + s_sunrise_min / 12);
      GPoint sr_outer = get_perimeter_point(center, sr_angle, 0, 0);
      int sr_marker_length = sun_inset;
      if (angle_has_extended_hour_marker(sr_angle)) {
        // At an extended diagonal hour position, the sunrise marker follows
        // the same radial length as that hour marker.
        sr_marker_length = extended_hour_marker_length();
      }
      GPoint sr_inner = marker_inner_toward_center(
        center, sr_outer, sr_marker_length);
      graphics_context_set_stroke_color(ctx, MONO_COLOR(s_settings.sunrise_marker_color));
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, sr_outer, sr_inner);
    }

    // Sunset marker
    int ss_total_min = s_sunset_hour * 60 + s_sunset_min;
    int ss_delta = ss_total_min - cur_total_min;
    if (ss_delta <= 0) ss_delta += 24 * 60;
    // The next sunset is always relevant, even when it is more than 12 hours away.
    if (ss_delta <= 24 * 60) {
      int32_t ss_angle = angle_for_minute(
        s_sunset_hour * 5 + s_sunset_min / 12);
      GPoint ss_outer = get_perimeter_point(center, ss_angle, 0, 0);
      int ss_marker_length = sun_inset;
      if (angle_has_extended_hour_marker(ss_angle)) {
        // At an extended diagonal hour position, the sunset marker follows
        // the same radial length as that hour marker.
        ss_marker_length = extended_hour_marker_length();
      }
      GPoint ss_inner = marker_inner_toward_center(
        center, ss_outer, ss_marker_length);
      graphics_context_set_stroke_color(ctx, MONO_COLOR(s_settings.sunset_marker_color));
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, ss_outer, ss_inner);
    }
  }

  // Determine icon size in pixels. By default the icon exactly matches the
  // current number font's tallest rendered numeral; manual Icon Size remains
  // available when the user clears "Same as font size" in settings.
  static const int s_icon_sizes[5] = {18, 22, 26, 30, 36};
  int icon_sz;
  if (s_settings.icon_size_manual_override) {
    int icon_sz_idx = manual_icon_size() - 1;
    icon_sz = s_icon_sizes[icon_sz_idx];
#if defined(PBL_PLATFORM_CHALK)
    // Preserve Chalk's established manual Size 3 reduction.
    if (manual_icon_size() == 3) icon_sz = s_icon_sizes[0];
#endif
  } else {
    icon_sz = displayed_number_height();
  }

  bool draw_icons = false;
  bool side_by_side = (s_settings.shake_mode == 3);
  if (s_settings.shake_mode == 1) {
    draw_icons = true;
  } else if (s_settings.shake_mode == 0) {
    draw_icons = s_showing_icons;
  }
  // shake_mode == 2 = always hide icons
  // shake_mode == 3 = side-by-side (both numbers and icons always visible)

  // In side-by-side mode, synced icons retain the displayed numeral height.
  // Manual icon sizes retain the established compact side-by-side scaling.
  int sbs_icon_sz = s_settings.icon_size_manual_override
                      ? (icon_sz / 2) - 1 : icon_sz;
  if (sbs_icon_sz < 8) sbs_icon_sz = 8;

  for (int h = 0; h < 12; h++) {
    // Numbers Visibility controls numeral drawing. Weather icons shown on
    // shake remain available at every clock position, even without a numeral.
    bool show_number = number_is_visible(h);
    if (side_by_side && show_number) {
      // ── Side-By-Side mode: draw both number and icon at reduced size ──
      // Number uses smallest font (forced in get_number_font).
      // Icon uses sbs_icon_sz.
      // Layout: number at edge, icon adjacent with 2px gap.
#ifndef PBL_ROUND
      GPoint edge = s_perimeter_cache[h];
      int gap = sun_inset * 3 / 2;
#endif

      InkBounds ib = s_ink[h];
      if (!ib.valid) {
        GSize sz = graphics_text_layout_get_content_size(
          get_number_string(h), num_font, GRect(0,0,80,80),
          GTextOverflowModeWordWrap, GTextAlignmentLeft);
        ib.box_w = (uint8_t)sz.w; ib.box_h = (uint8_t)sz.h;
        ib.left = ib.top = ib.right = ib.bottom = 0;
        ib.valid = true;
      }
      int ink_w = ib.box_w - ib.left - ib.right;
      int ink_h = ib.box_h - ib.top  - ib.bottom;
      if (ink_w < 1) ink_w = 1;
      if (ink_h < 1) ink_h = 1;

      // Number position
#ifndef PBL_ROUND
      int rx, ry;
#endif
      GRect text_rect;
#ifdef PBL_ROUND
      if (number_font_id() == 6) {
        text_rect = round_roman_serif_text_rect(h, ib);
      } else {
#if defined(PBL_PLATFORM_CHALK)
      // Chalk numerals use the weather icon's literal polar centre and
      // top-left frame. Roman Sans needs a full-height layout box because its
      // visible ink is shorter than the font's line metrics.
      GPoint icon_center = polar_to_point(
        center, TRIG_MAX_ANGLE * h / 12, round_content_radius(icon_sz / 2));
      int icon_ox = icon_center.x - icon_sz / 2;
      int text_y = number_font_id() == 5
        ? icon_center.y - ink_h / 2 - ib.top
        : icon_center.y - icon_sz / 2;
      text_rect = GRect(icon_ox - (80 - icon_sz) / 2, text_y,
                        80, number_font_id() == 5 ? 80 : icon_sz);
#else
      // Gabbro uses the selected font size for the number's centred frame.
      GPoint icon_center = polar_to_point(
        center, TRIG_MAX_ANGLE * h / 12, round_content_radius(icon_sz / 2));
      int text_y = number_font_id() == 5
        ? icon_center.y - ink_h / 2 - ib.top
        : icon_center.y - icon_sz / 2;
      text_rect = GRect(icon_center.x - 40, text_y,
                        80, number_font_id() == 5 ? 80 : icon_sz);
#endif
      }
#else
      if (h == 11 || h == 0 || h == 1) {
        ry = gap - ib.top;
        rx = edge.x - ink_w / 2 - ib.left;
        if (h == 11) rx += ink_w / 4;
        // Match hour 5's visual width: shift hour 1 left by one quarter of
        // hour 5's measured visible ink rather than hour 1's narrow strokes.
        if (h == 1) {
          InkBounds ib5 = s_ink[5];
          int ink_w_5 = ib5.box_w - ib5.left - ib5.right;
          if (ink_w_5 < 1) ink_w_5 = 1;
          rx -= ink_w_5 / 4;
        }
      } else if (h == 5 || h == 6 || h == 7) {
        ry = sh - gap - 80 + ib.bottom;
        rx = edge.x - ink_w / 2 - ib.left;
        if (h == 7) rx += ink_w / 4;
        if (h == 5) rx -= ink_w / 4;
      } else if (h == 8 || h == 9 || h == 10) {
        rx = gap - ib.left;
        {
          InkBounds ib12 = s_ink[0];
          InkBounds ib6  = s_ink[6];
          int y_12c = gap + (ib12.box_h - ib12.top - ib12.bottom) / 2;
          int y_6c  = sh - gap - (ib6.box_h - ib6.top - ib6.bottom) / 2;
          int cross_y = edge.y;
          if (h == 10)      cross_y = y_12c + (y_6c - y_12c) * 25 / 100;
          else if (h == 8)  cross_y = y_12c + (y_6c - y_12c) * 75 / 100;
          ry = cross_y - ink_h / 2 - ib.top;
        }
      } else {
        rx = sw - gap - 80 + ib.right;
        {
          InkBounds ib12 = s_ink[0];
          InkBounds ib6  = s_ink[6];
          int y_12c = gap + (ib12.box_h - ib12.top - ib12.bottom) / 2;
          int y_6c  = sh - gap - (ib6.box_h - ib6.top - ib6.bottom) / 2;
          int cross_y = edge.y;
          if (h == 2)      cross_y = y_12c + (y_6c - y_12c) * 25 / 100;
          else if (h == 4) cross_y = y_12c + (y_6c - y_12c) * 75 / 100;
          ry = cross_y - ink_h / 2 - ib.top;
        }
      }
#endif
#ifndef PBL_ROUND
      text_rect = GRect(rx, ry, 80, 80);
#endif

      // Draw number
      GColor num_draw_color;
      num_draw_color = (s_settings.icon_color_mode == 2)
        ? MONO_COLOR(s_rainbow_colors[h]) : MONO_COLOR(s_settings.number_color);
      cache_number_ink_bounds(h, text_rect,
#ifdef PBL_ROUND
                               GTextAlignmentCenter
#else
                               GTextAlignmentLeft
#endif
      );
      draw_number_text(ctx, h, num_font, text_rect,
#ifdef PBL_ROUND
                         GTextAlignmentCenter,
#else
                         GTextAlignmentLeft,
#endif
                         num_draw_color);

      // Icon position: adjacent to number with 4px gap
      int iox, ioy;
#ifdef PBL_ROUND
      // On round screens stack the smaller forecast icon inward from the number
      // along the same clock radius, leaving a clear circular perimeter.
      int icon_radius = round_content_radius(ink_h + sbs_icon_sz / 2 + 4);
      GPoint icon_pt = polar_to_point(center, TRIG_MAX_ANGLE * h / 12, icon_radius);
      iox = icon_pt.x - sbs_icon_sz / 2;
      ioy = icon_pt.y - sbs_icon_sz / 2;
#else
      int num_ink_cx = rx + ib.left + ink_w / 2;
      // At diagonal clock positions, place the icon in the inside corner of
      // the numeral: 1→bottom-left, 5→top-left, 7→top-right, 11→bottom-right.
      // This follows the radial path toward the dial centre instead of leaving
      // the icon directly above or below the corner numeral.
      if (h == 1 || h == 5 || h == 7 || h == 11) {
        iox = (h < 6) ? rx + ib.left - 4 - sbs_icon_sz
                      : rx + ib.left + ink_w + 4;
        ioy = (h == 5 || h == 7) ? ry + ib.top - 4 - sbs_icon_sz
                                  : ry + ib.top + ink_h + 4;
      } else if (h == 0) {
        ioy = ry + ib.top + ink_h + 4;
        iox = num_ink_cx - sbs_icon_sz / 2;
      } else if (h == 6) {
        ioy = ry + ib.top - 4 - sbs_icon_sz;
        iox = num_ink_cx - sbs_icon_sz / 2;
      } else if (h == 8 || h == 9 || h == 10) {
        iox = rx + ib.left + ink_w + 4;
        ioy = ry + ib.top + ink_h / 2 - sbs_icon_sz / 2;
      } else {
        iox = rx + ib.left - 4 - sbs_icon_sz;
        ioy = ry + ib.top + ink_h / 2 - sbs_icon_sz / 2;
      }
#endif

      // Resolve this position in the timestamp-anchored rolling forecast.
      int8_t icon_code = s_icons[forecast_hour_for_dial_position(
        h, s_last_time.tm_hour)];
      GPathIconID gpath_id = icon_code_to_gpath(icon_code);

      // Determine icon colour
      GColor icon_draw_color;
      icon_draw_color = (s_settings.icon_color_mode == 2)
        ? MONO_COLOR(s_rainbow_colors[h]) : MONO_COLOR(s_settings.icon_color);
      {
        int draw_icon_sz = sbs_icon_sz;
        if (!s_settings.icon_size_manual_override) {
          weather_icon_match_ink_height(gpath_id, sbs_icon_sz,
                                        &iox, &ioy, &draw_icon_sz);
        }
        cache_weather_icon_ink_bounds(h, gpath_id, iox, ioy, draw_icon_sz);
        if (s_settings.icon_color_mode == 3) {
          draw_weather_icon_shaded(ctx, gpath_id, iox, ioy, draw_icon_sz);
        } else {
          draw_weather_icon(ctx, gpath_id, iox, ioy, draw_icon_sz, icon_draw_color,
                            s_settings.icon_color_mode == 1);
        }
      }

    } else if (draw_icons || (side_by_side && !show_number)) {
      // Resolve this position in the timestamp-anchored rolling forecast.
      int8_t icon_code = s_icons[forecast_hour_for_dial_position(
        h, s_last_time.tm_hour)];
      GPathIconID gpath_id = icon_code_to_gpath(icon_code);

      // Rectangular devices retain their existing edge-anchored icon grid.
      // Round devices use a radial ring so no weather icon enters the clipped
      // corners of the circular display.
      int ox, oy;
#ifdef PBL_ROUND
      GPoint icon_center = polar_to_point(
        center, TRIG_MAX_ANGLE * h / 12, round_content_radius(icon_sz / 2));
      ox = icon_center.x - icon_sz / 2;
      oy = icon_center.y - icon_sz / 2;
#else
      GPoint i_edge = s_perimeter_cache[h];
      int icon_gap = sun_inset * 3 / 2;
      if (h == 11 || h == 0 || h == 1) {
        oy = icon_gap;
        ox = i_edge.x - icon_sz / 2;
        if (h == 11) ox += icon_sz / 4;
        if (h == 1)  ox -= icon_sz / 4;
      } else if (h == 5 || h == 6 || h == 7) {
        oy = sh - icon_gap - icon_sz;
        ox = i_edge.x - icon_sz / 2;
        if (h == 7) ox += icon_sz / 4;
        if (h == 5) ox -= icon_sz / 4;
      } else if (h == 8 || h == 9 || h == 10) {
        ox = icon_gap;
        int y_12c = icon_gap + icon_sz / 2;
        int y_6c  = sh - icon_gap - icon_sz / 2;
        int cross_y = i_edge.y;
        if (h == 10)      cross_y = y_12c + (y_6c - y_12c) * 25 / 100;
        else if (h == 8)  cross_y = y_12c + (y_6c - y_12c) * 75 / 100;
        oy = cross_y - icon_sz / 2;
      } else {
        ox = sw - icon_gap - icon_sz;
        int y_12c = icon_gap + icon_sz / 2;
        int y_6c  = sh - icon_gap - icon_sz / 2;
        int cross_y = i_edge.y;
        if (h == 2)      cross_y = y_12c + (y_6c - y_12c) * 25 / 100;
        else if (h == 4) cross_y = y_12c + (y_6c - y_12c) * 75 / 100;
        oy = cross_y - icon_sz / 2;
      }
#endif

      // Determine icon colour based on mode
      GColor icon_draw_color;
      if (s_settings.icon_color_mode == 2) {
        // Rainbow: colour based on hour position around the dial
        // 12 positions → hues 0..330 in steps of 30
        icon_draw_color = MONO_COLOR(s_rainbow_colors[h]);
      } else {
        icon_draw_color = MONO_COLOR(s_settings.icon_color);
      }
      {
        int draw_icon_sz = icon_sz;
        if (!s_settings.icon_size_manual_override) {
          weather_icon_match_ink_height(gpath_id, icon_sz, &ox, &oy, &draw_icon_sz);
        }
        cache_weather_icon_ink_bounds(h, gpath_id, ox, oy, draw_icon_sz);
        if (s_settings.icon_color_mode == 3) {
          // Line shading mode: hatched fill with weather colours
          draw_weather_icon_shaded(ctx, gpath_id, ox, oy, draw_icon_sz);
        } else {
          draw_weather_icon(ctx, gpath_id, ox, oy, draw_icon_sz, icon_draw_color,
                            s_settings.icon_color_mode == 1);
        }
      }
    } else if (show_number) {
      // Draw number anchored by its TRUE VISIBLE INK edge a constant gap from
      // the nearest screen edge. The cross-axis position comes from the
      // hour-angle perimeter ray so each number lines up under its clock
      // position. The text box itself is offset so that, after the font's
      // internal padding (s_ink[h]), the lit pixels land exactly where we want.
#ifndef PBL_ROUND
      GPoint edge = s_perimeter_cache[h];
#endif

      InkBounds ib = s_ink[h];
      if (!ib.valid) {
        // Defensive fallback: treat the whole box as ink.
        GSize sz = graphics_text_layout_get_content_size(
          get_number_string(h), num_font, GRect(0,0,80,80),
          GTextOverflowModeWordWrap, GTextAlignmentLeft);
        ib.box_w = (uint8_t)sz.w; ib.box_h = (uint8_t)sz.h;
        ib.left = ib.top = ib.right = ib.bottom = 0;
        ib.valid = true;
      }

      // Dimensions of the visible ink itself.
      int ink_w = ib.box_w - ib.left - ib.right;
      int ink_h = ib.box_h - ib.top  - ib.bottom;
      if (ink_w < 1) ink_w = 1;
      if (ink_h < 1) ink_h = 1;

      // Gap from screen edge to visible ink. Must clear the longest marker
      // (4px on Basalt quarter-hour, 10px on Emery) plus a small margin.
#ifndef PBL_ROUND
      int gap = sun_inset * 3 / 2;
#endif

      // Top-left corner of the (untrimmed) text box. We position so that the
      // visible ink edge sits `gap` from the screen edge, and the ink centre
      // sits on the perimeter ray on the cross axis.
#ifndef PBL_ROUND
      int rx, ry;
#endif
      GRect text_rect;

#ifdef PBL_ROUND
      if (number_font_id() == 6) {
        text_rect = round_roman_serif_text_rect(h, ib);
      } else {
#if defined(PBL_PLATFORM_CHALK)
      // Chalk numerals use the weather icon's literal polar centre and
      // top-left frame. Roman Sans needs a full-height layout box because its
      // visible ink is shorter than the font's line metrics.
      GPoint icon_center = polar_to_point(
        center, TRIG_MAX_ANGLE * h / 12, round_content_radius(icon_sz / 2));
      int icon_ox = icon_center.x - icon_sz / 2;
      int text_y = number_font_id() == 5
        ? icon_center.y - ink_h / 2 - ib.top
        : icon_center.y - icon_sz / 2;
      text_rect = GRect(icon_ox - (80 - icon_sz) / 2, text_y,
                        80, number_font_id() == 5 ? 80 : icon_sz);
#else
      // Gabbro uses the selected font size for the number's centred frame.
      GPoint icon_center = polar_to_point(
        center, TRIG_MAX_ANGLE * h / 12, round_content_radius(icon_sz / 2));
      int text_y = number_font_id() == 5
        ? icon_center.y - ink_h / 2 - ib.top
        : icon_center.y - icon_sz / 2;
      text_rect = GRect(icon_center.x - 40, text_y,
                        80, number_font_id() == 5 ? 80 : icon_sz);
#endif
      }
#else
      if (h == 11 || h == 0 || h == 1) {
        ry = gap - ib.top;
        rx = edge.x - ink_w / 2 - ib.left;
        if (h == 11) rx += ink_w / 4;
        // Match hour 5's visual width: shift hour 1 left by one quarter of
        // hour 5's measured visible ink rather than hour 1's narrow strokes.
        if (h == 1) {
          InkBounds ib5 = s_ink[5];
          int ink_w_5 = ib5.box_w - ib5.left - ib5.right;
          if (ink_w_5 < 1) ink_w_5 = 1;
          rx -= ink_w_5 / 4;
        }
      } else if (h == 5 || h == 6 || h == 7) {
        ry = sh - gap - 80 + ib.bottom;
        rx = edge.x - ink_w / 2 - ib.left;
        if (h == 7) rx += ink_w / 4;
        if (h == 5) rx -= ink_w / 4;
      } else if (h == 8 || h == 9 || h == 10) {
        rx = gap - ib.left;
        {
          InkBounds ib12 = s_ink[0];
          InkBounds ib6  = s_ink[6];
          int y_12c = gap + (ib12.box_h - ib12.top - ib12.bottom) / 2;
          int y_6c  = sh - gap - (ib6.box_h - ib6.top - ib6.bottom) / 2;
          int cross_y = edge.y;
          if (h == 10)      cross_y = y_12c + (y_6c - y_12c) * 25 / 100;
          else if (h == 8)  cross_y = y_12c + (y_6c - y_12c) * 75 / 100;
          ry = cross_y - ink_h / 2 - ib.top;
        }
      } else {
        rx = sw - gap - 80 + ib.right;
        {
          InkBounds ib12 = s_ink[0];
          InkBounds ib6  = s_ink[6];
          int y_12c = gap + (ib12.box_h - ib12.top - ib12.bottom) / 2;
          int y_6c  = sh - gap - (ib6.box_h - ib6.top - ib6.bottom) / 2;
          int cross_y = edge.y;
          if (h == 2)      cross_y = y_12c + (y_6c - y_12c) * 25 / 100;
          else if (h == 4) cross_y = y_12c + (y_6c - y_12c) * 75 / 100;
          ry = cross_y - ink_h / 2 - ib.top;
        }
      }
#endif
#ifndef PBL_ROUND
      text_rect = GRect(rx, ry, 80, 80);
#endif
      // Determine number colour based on mode
      GColor num_draw_color;
      if (s_settings.icon_color_mode == 2) {
        // Rainbow: same colour wheel as icons
        num_draw_color = MONO_COLOR(s_rainbow_colors[h]);
      } else {
        num_draw_color = MONO_COLOR(s_settings.number_color);
      }
      cache_number_ink_bounds(h, text_rect,
#ifdef PBL_ROUND
                               GTextAlignmentCenter
#else
                               GTextAlignmentLeft
#endif
      );
      draw_number_text(ctx, h, num_font, text_rect,
#ifdef PBL_ROUND
                         GTextAlignmentCenter,
#else
                         GTextAlignmentLeft,
#endif
                         num_draw_color);
    }
  }
  s_dial_content_bounds_valid = true;
}

// Complication layer: date + temperature
static bool rects_overlap_with_margin(GRect a, GRect b, int margin) {
  return a.origin.x - margin < b.origin.x + b.size.w + margin &&
         a.origin.x + a.size.w + margin > b.origin.x - margin &&
         a.origin.y - margin < b.origin.y + b.size.h + margin &&
         a.origin.y + a.size.h + margin > b.origin.y - margin;
}

#ifndef PBL_BW
// Dial-content collision needs the text's actual horizontal footprint, not the
// deliberately generous half-screen drawing box. This lets colour targets use
// exact ink-aware fallback rather than treating a safe short move as blocked.
static GRect complication_text_collision_rect(const char *text, GFont font,
                                              GRect layout_rect) {
  if (!text || text[0] == '\0') return GRect(0, 0, 0, 0);
  GSize size = graphics_text_layout_get_content_size(
    text, font, GRect(0, 0, layout_rect.size.w, layout_rect.size.h),
    GTextOverflowModeWordWrap, GTextAlignmentCenter);
  int width = size.w;
  if (width < 1) width = 1;
  if (width > layout_rect.size.w) width = layout_rect.size.w;
  return GRect(layout_rect.origin.x + (layout_rect.size.w - width) / 2,
               layout_rect.origin.y, width, layout_rect.size.h);
}

static int point_cross_product(GPoint a, GPoint b, GPoint c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static bool point_in_or_on_rect(GPoint point, GRect rect) {
  return point.x >= rect.origin.x && point.x <= rect.origin.x + rect.size.w &&
         point.y >= rect.origin.y && point.y <= rect.origin.y + rect.size.h;
}

static bool point_on_segment(GPoint a, GPoint b, GPoint point) {
  return point.x >= (a.x < b.x ? a.x : b.x) &&
         point.x <= (a.x > b.x ? a.x : b.x) &&
         point.y >= (a.y < b.y ? a.y : b.y) &&
         point.y <= (a.y > b.y ? a.y : b.y);
}

static bool line_segments_intersect(GPoint a, GPoint b, GPoint c, GPoint d) {
  int ab_c = point_cross_product(a, b, c);
  int ab_d = point_cross_product(a, b, d);
  int cd_a = point_cross_product(c, d, a);
  int cd_b = point_cross_product(c, d, b);
  if (((ab_c > 0 && ab_d < 0) || (ab_c < 0 && ab_d > 0)) &&
      ((cd_a > 0 && cd_b < 0) || (cd_a < 0 && cd_b > 0))) return true;
  if (ab_c == 0 && point_on_segment(a, b, c)) return true;
  if (ab_d == 0 && point_on_segment(a, b, d)) return true;
  if (cd_a == 0 && point_on_segment(c, d, a)) return true;
  if (cd_b == 0 && point_on_segment(c, d, b)) return true;
  return false;
}

// The rendered hand is approximately 10 px wide. Expanding a candidate text
// area by its hand half-width plus a small clearance avoids text under a hand
// without introducing a free-form layout solver.
static bool hand_intersects_text_rect(GPoint hand_tip, GRect text_rect) {
  const int margin = s_screen_w >= 200 ? 7 : 5;
  GRect expanded = GRect(text_rect.origin.x - margin,
                         text_rect.origin.y - margin,
                         text_rect.size.w + margin * 2,
                         text_rect.size.h + margin * 2);
  if (point_in_or_on_rect(s_dial_center, expanded) ||
      point_in_or_on_rect(hand_tip, expanded)) return true;
  GPoint tl = expanded.origin;
  GPoint tr = GPoint(expanded.origin.x + expanded.size.w, expanded.origin.y);
  GPoint br = GPoint(expanded.origin.x + expanded.size.w,
                     expanded.origin.y + expanded.size.h);
  GPoint bl = GPoint(expanded.origin.x, expanded.origin.y + expanded.size.h);
  return line_segments_intersect(s_dial_center, hand_tip, tl, tr) ||
         line_segments_intersect(s_dial_center, hand_tip, tr, br) ||
         line_segments_intersect(s_dial_center, hand_tip, br, bl) ||
         line_segments_intersect(s_dial_center, hand_tip, bl, tl);
}
#endif

static bool complication_candidate_intersects_hands(GRect date_rect, bool show_date,
                                                     GRect temp_rect, bool show_temp) {
#ifdef PBL_BW
  // Aplite retains the conservative dial-content boxes above. Avoid carrying
  // an extra line/rectangle solver into the 24 KB application budget.
  (void)date_rect;
  (void)show_date;
  (void)temp_rect;
  (void)show_temp;
  return false;
#else
  GPoint tips[] = { s_hour_tip, s_minute_tip };
  for (unsigned int i = 0; i < sizeof(tips) / sizeof(tips[0]); i++) {
    if ((show_date && hand_intersects_text_rect(tips[i], date_rect)) ||
        (show_temp && hand_intersects_text_rect(tips[i], temp_rect))) {
      return true;
    }
  }
  return false;
#endif
}

static bool complication_candidate_collides(GRect date_rect, bool show_date,
                                            GRect temp_rect, bool show_temp) {
#ifdef PBL_BW
  // B/W watches retain a compact dynamic bound instead of colour's persistent
  // per-item cache. It is anchored inward from the same screen edge as labels
  // and icons, so wide Roman strings such as VIII are conservatively covered.
  int number_extent = 0;
  for (int h = 0; h < 12; h++) {
    if (!s_ink[h].valid) continue;
    int width = s_ink[h].box_w - s_ink[h].left - s_ink[h].right;
    int height = s_ink[h].box_h - s_ink[h].top - s_ink[h].bottom;
    if (width > number_extent) number_extent = width;
    if (height > number_extent) number_extent = height;
  }
  number_extent += 4;  // measured glyph ink plus the drawn-stroke clearance
  if (number_extent < 32) number_extent = 32;
  int icon_size = manual_icon_size();
  int icon_extent = icon_size == 1 ? 18 : icon_size == 2 ? 22 :
                    icon_size == 3 ? 26 : icon_size == 4 ? 30 : 36;
  int content_extent = weather_icons_are_visible() && icon_extent > number_extent
                         ? icon_extent : number_extent;
  if (content_extent > 64) content_extent = 64;
  int edge_gap = (s_screen_w >= 200 ? 8 : 5) * 3 / 2;
  for (int h = 0; h < 12; h++) {
    if (!weather_icons_are_visible() && !number_is_visible(h)) continue;
    GPoint point = s_perimeter_cache[h];
    int x = point.x - content_extent / 2;
    int y = point.y - content_extent / 2;
    if (h == 11 || h == 0 || h == 1) y = edge_gap;
    else if (h == 5 || h == 6 || h == 7) y = s_screen_h - edge_gap - content_extent;
    else if (h == 8 || h == 9 || h == 10) x = edge_gap;
    else x = s_screen_w - edge_gap - content_extent;
    GRect content = GRect(x, y, content_extent, content_extent);
    if ((show_date && rects_overlap_with_margin(date_rect, content, 2)) ||
        (show_temp && rects_overlap_with_margin(temp_rect, content, 2))) {
      return true;
    }
  }
  return false;
#else
  if (!s_dial_content_bounds_valid) return false;
  int margin = s_screen_w >= 200 ? 3 : 2;
  for (int h = 0; h < 12; h++) {
#ifdef PBL_BW
    bool number_valid = (s_dial_number_valid_mask & (uint16_t)(1U << h)) != 0;
    bool icon_valid = (s_dial_icon_valid_mask & (uint16_t)(1U << h)) != 0;
    GRect number_rect = s_dial_number_bounds[h];
    GRect icon_rect = s_dial_icon_bounds[h];
#else
    DialContentBounds *slot = &s_dial_content_bounds[h];
    bool number_valid = slot->number_valid;
    bool icon_valid = slot->icon_valid;
    GRect number_rect = slot->number_rect;
    GRect icon_rect = slot->icon_rect;
#endif
    if (number_valid &&
        ((show_date && rects_overlap_with_margin(date_rect, number_rect, margin)) ||
         (show_temp && rects_overlap_with_margin(temp_rect, number_rect, margin)))) {
      return true;
    }
    if (icon_valid &&
        ((show_date && rects_overlap_with_margin(date_rect, icon_rect, margin)) ||
         (show_temp && rects_overlap_with_margin(temp_rect, icon_rect, margin)))) {
      return true;
    }
  }
  return false;
#endif
}

static int choose_complication_center(int preferred_cx, int preferred_y,
                                      int *selected_y, int box_w, GFont font,
                                      const char *date_text, bool show_date,
                                      const char *temp_text, bool show_temp) {
  const int center_cx = s_screen_w / 2;
  const int top_y = POS_Y(45);
  const int bottom_y = POS_Y(105);
  bool preferred_top = preferred_y == top_y;
  int candidates_x[4];
  int candidates_y[4];
  int candidate_count = 0;

  // First use the deterministic minute/hour/Roman result. The remaining
  // candidates are bounded safety lanes only: same-lane centre, then the
  // centre and same-side position in the opposite vertical lane. There is no
  // inward nudge and no opposite-corner jump.
  candidates_x[candidate_count] = preferred_cx;
  candidates_y[candidate_count++] = preferred_y;
  if (preferred_cx != center_cx) {
    candidates_x[candidate_count] = center_cx;
    candidates_y[candidate_count++] = preferred_y;
  }
  candidates_x[candidate_count] = center_cx;
  candidates_y[candidate_count++] = preferred_top ? bottom_y : top_y;
  candidates_x[candidate_count] = preferred_cx;
  candidates_y[candidate_count++] = preferred_top ? bottom_y : top_y;

  for (int i = 0; i < candidate_count; i++) {
    int box_x = candidates_x[i] - box_w / 2;
    GRect date_layout = GRect(box_x, candidates_y[i] - 10, box_w, 20);
    int temp_y = show_date ? candidates_y[i] + 8 : candidates_y[i] - 10;
    GRect temp_layout = GRect(box_x, temp_y, box_w, 20);
#ifdef PBL_BW
    // Keep Aplite's calculation-only conservative footprint.
    GRect date_rect = show_date
      ? GRect(candidates_x[i] - 28, date_layout.origin.y, 56, date_layout.size.h)
      : GRect(0, 0, 0, 0);
    GRect temp_rect = show_temp
      ? GRect(candidates_x[i] - 24, temp_layout.origin.y, 48, temp_layout.size.h)
      : GRect(0, 0, 0, 0);
    (void)font;
    (void)date_text;
    (void)temp_text;
#else
    GRect date_rect = complication_text_collision_rect(date_text, font, date_layout);
    GRect temp_rect = complication_text_collision_rect(temp_text, font, temp_layout);
#endif
    if (!complication_candidate_collides(date_rect, show_date, temp_rect, show_temp) &&
        !complication_candidate_intersects_hands(date_rect, show_date,
                                                 temp_rect, show_temp)) {
      *selected_y = candidates_y[i];
      return candidates_x[i];
    }
  }
  // If all bounded safety lanes are occupied, retain the deterministic lane.
  *selected_y = preferred_y;
  return preferred_cx;
}

static void rebuild_complication_cache(void) {
  ComplicationRenderCache *cache = &s_complication_cache;
  int sw = s_screen_w;
  int cur_min = s_last_time.tm_min;
  bool comp_at_top = (cur_min >= 20 && cur_min <= 40);
  int comp_y = comp_at_top ? POS_Y(45) : POS_Y(105);
  bool is_emery = (sw >= 200);
  cache->font = get_complication_font();

  bool mode_date = (s_settings.display_mode == 0 || s_settings.display_mode == 2);
  bool mode_temp = (s_settings.display_mode == 0 || s_settings.display_mode == 1);
  cache->show_date = mode_date && ((s_settings.date_visible == 0) ||
                     (s_settings.date_visible == 2 && s_showing_icons));
  cache->show_temp = mode_temp && ((s_settings.temp_visible == 0) ||
                     (s_settings.temp_visible == 2 && s_showing_icons));
  cache->show_city = false;

  cache->date_text[0] = '\0';
  if (cache->show_date) {
    static const char *day_names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    snprintf(cache->date_text, sizeof(cache->date_text), "%s %d",
             day_names[s_last_time.tm_wday], s_last_time.tm_mday);
  }

  cache->temp_text[0] = '\0';
  if (cache->show_temp) {
    if (s_temp_c == 127 || s_temp_f == 127) {
      snprintf(cache->temp_text, sizeof(cache->temp_text), "--\xc2\xb0");
    } else if (s_settings.temp_unit == 0) {
      snprintf(cache->temp_text, sizeof(cache->temp_text), "%d\xc2\xb0""C", (int)s_temp_c);
    } else {
      snprintf(cache->temp_text, sizeof(cache->temp_text), "%d\xc2\xb0""F", (int)s_temp_f);
    }
  }

  int cur_hour12 = s_last_time.tm_hour % 12;
  int box_w = sw / 2;
  int comp_cx = sw / 2;
  // Corner complications sit at screen_width / 2.75 from each side.
  const int left_comp_cx = (sw * 4) / 11;
  const int right_comp_cx = sw - left_comp_cx;
  if (comp_at_top) {
    if (cur_hour12 == 10 || cur_hour12 == 11) comp_cx = right_comp_cx;
    else if (cur_hour12 == 0 || cur_hour12 == 1) comp_cx = left_comp_cx;
  } else {
    if (cur_hour12 == 4 || cur_hour12 == 5) comp_cx = left_comp_cx;
    else if (cur_hour12 == 6 || cur_hour12 == 7) comp_cx = right_comp_cx;
  }
  // Roman VIII is widest on the left. For hours 4/5, move the complication
  // into the top lane instead of placing it in the lower-left numeral area or
  // beneath the hour hand. At minutes 45-59 the minute hand occupies the
  // upper-left sector, so use top-right; otherwise use top-left.
  if (!comp_at_top && number_font_id() >= 5 &&
      (cur_hour12 == 4 || cur_hour12 == 5)) {
    comp_y = POS_Y(45);
    comp_at_top = true;
    comp_cx = (cur_min >= 45) ? right_comp_cx : left_comp_cx;
  }

  comp_cx = choose_complication_center(comp_cx, comp_y, &comp_y, box_w, cache->font,
                                       cache->date_text, cache->show_date,
                                       cache->temp_text, cache->show_temp);

  int box_x = comp_cx - box_w / 2;
  cache->date_rect = GRect(box_x, comp_y - 10, box_w, 20);
  int temp_y = cache->show_date ? comp_y + 18 - 10 : comp_y - 10;
  cache->temp_rect = GRect(box_x, temp_y, box_w, 20);

  if (s_settings.city_display_mode != 0 && s_city_name[0] != '\0') {
    cache->show_city = (s_settings.city_display_mode == 2) ||
                       (s_settings.city_display_mode == 1 && s_showing_icons);
  }
  if (cache->show_city) {
    int city_y = (comp_y < s_screen_h / 2) ? POS_Y(105) : POS_Y(45);
    int city_safe_margin = POS_X(36);
    int city_safe_w = sw - city_safe_margin * 2;
    if (city_safe_w < POS_X(60)) city_safe_w = POS_X(60);
    GSize natural_size = graphics_text_layout_get_content_size(
      s_city_name, cache->font, GRect(0, 0, sw * 2, 30),
      GTextOverflowModeWordWrap, GTextAlignmentCenter);
    if (natural_size.w > city_safe_w) {
      int city_line_h = is_emery ? 26 : 18;
      cache->city_rect = GRect(city_safe_margin, city_y - city_line_h / 2,
                               city_safe_w, city_line_h * 2);
    } else {
      cache->city_rect = GRect(0, city_y - 10, sw, 20);
    }
  }
  cache->valid = true;
}

static void complication_layer_update(Layer *layer, GContext *ctx) {
  if (!s_complication_cache.valid) rebuild_complication_cache();
  ComplicationRenderCache *cache = &s_complication_cache;

  if (cache->show_date) {
    graphics_context_set_text_color(ctx, MONO_COLOR(s_settings.date_color));
    graphics_draw_text(ctx, cache->date_text, cache->font, cache->date_rect,
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
  if (cache->show_temp) {
    graphics_context_set_text_color(ctx, MONO_COLOR(s_settings.temp_color));
    graphics_draw_text(ctx, cache->temp_text, cache->font, cache->temp_rect,
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
  if (cache->show_city) {
    graphics_context_set_text_color(ctx, MONO_COLOR(s_settings.city_color));
    graphics_draw_text(ctx, s_city_name, cache->font, cache->city_rect,
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

// Hour hand layer
static void hour_layer_update(Layer *layer, GContext *ctx) {
  draw_inittick_hand(ctx, s_dial_center, s_hour_tip,
                     s_settings.hour_hand_outer,
                     s_settings.hour_hand_inner);
}

// Minute hand + seconds hand (below centre rings) + centre cap layer
static void minute_layer_update(Layer *layer, GContext *ctx) {
  GPoint center = s_dial_center;

  // BT disconnect override
  GColor min_outer = s_settings.min_hand_outer;
  GColor min_inner = s_settings.min_hand_inner;
  if (!s_bt_connected && s_settings.bt_disconnect_min_inner_red) {
    min_outer = s_settings.bt_disconnect_outer_color;
    min_inner = s_settings.bt_disconnect_inner_color;
  }

  draw_inittick_hand(ctx, center, s_minute_tip, min_outer, min_inner);

  // ── Seconds hand — drawn here so it appears BELOW the centre rings ──────────
  if (s_seconds_visible) {
    int second = s_last_time.tm_sec % 60;
    GPoint sec_tip = s_second_tip_cache[second];
    GPoint sec_tail = s_second_tail_cache[second];
    graphics_context_set_stroke_color(ctx, MONO_COLOR(s_settings.seconds_hand_color));
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, sec_tail, sec_tip);
  }

  // Centre cap — five concentric circles
  // Battery alert colours
  GColor battery_ring = GColorWhite;
  GColor inner_ring   = s_settings.min_hand_inner;
  GColor dot          = s_settings.hour_hand_outer;

  bool critical_battery = (s_battery_pct <= s_settings.battery_center_threshold &&
                           s_settings.battery_center_threshold > 0);
  // Low applies only above the critical threshold. This makes the visual
  // states mutually exclusive even though a critical percentage is also
  // numerically below the low-battery threshold.
  bool low_battery = !critical_battery &&
                     (s_battery_pct <= s_settings.battery_ring_threshold &&
                      s_settings.battery_ring_threshold > 0);

  // Low battery: only the inner ring turns red. The outer ring and centre dot
  // retain their normal colours.
  if (low_battery) {
    inner_ring = GColorRed;
  }
  // Critical battery: both rings turn red. The centre dot remains normal.
  if (critical_battery) {
    battery_ring = GColorRed;
    inner_ring = GColorRed;
  }

  // r5 outer ring
  graphics_context_set_fill_color(ctx, MONO_COLOR(battery_ring));
  graphics_fill_circle(ctx, center, POS_X(7));
  // r4 black gap
  graphics_context_set_fill_color(ctx, MONO_COLOR(s_settings.background_color));
  graphics_fill_circle(ctx, center, POS_X(5));
  // r3 inner ring
  graphics_context_set_fill_color(ctx, MONO_COLOR(inner_ring));
  graphics_fill_circle(ctx, center, POS_X(4));
  // r2 black gap
  graphics_context_set_fill_color(ctx, MONO_COLOR(s_settings.background_color));
  graphics_fill_circle(ctx, center, POS_X(2));
  // r1 centre dot
  graphics_context_set_fill_color(ctx, MONO_COLOR(dot));
  graphics_fill_circle(ctx, center, POS_X(1));
}



// ─────────────────────────────────────────────────────────────────────────────
// Tick subscription management
// ─────────────────────────────────────────────────────────────────────────────
static void update_tick_subscription(void) {
  bool need_seconds = false;
  if (s_settings.seconds_hand_mode == 1) {
    need_seconds = true;
    s_seconds_visible = true;
  } else if (s_settings.seconds_hand_mode == 2) {
    need_seconds = s_seconds_visible;
  } else {
    s_seconds_visible = false;
  }

  TimeUnits wanted_units = need_seconds ? SECOND_UNIT : MINUTE_UNIT;
  if (s_tick_units == wanted_units) return;
  if (s_tick_units != 0) tick_timer_service_unsubscribe();
  tick_timer_service_subscribe(wanted_units, tick_handler);
  s_tick_units = wanted_units;
}

static void update_accel_tap_subscription(void) {
  bool need_accel = (s_settings.shake_mode == 0 ||
                     s_settings.seconds_hand_mode == 2 ||
                     s_settings.city_display_mode == 1);
  if (need_accel == s_accel_tap_subscribed) return;
  if (need_accel) {
    accel_tap_service_subscribe(accel_tap_handler);
  } else {
    accel_tap_service_unsubscribe();
  }
  s_accel_tap_subscribed = need_accel;
}

static void mark_shake_content_dirty(void) {
  // Only the on-shake icon mode changes the background.
  if (s_settings.shake_mode == 0) layer_mark_dirty(s_bg_layer);

  // Redraw the complication only when its own visibility depends on shaking.
  // Always-visible date/temp text does not need a second paint just because
  // weather icons were briefly shown.
  bool complication_depends_on_shake =
    s_settings.date_visible == 2 ||
    s_settings.temp_visible == 2 ||
    s_settings.city_display_mode == 1;
  if (complication_depends_on_shake) {
    invalidate_complication_cache();
    layer_mark_dirty(s_complication_layer);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shake / icon display timers
// ─────────────────────────────────────────────────────────────────────────────
static void hide_icons_callback(void *data) {
  s_shake_timer = NULL;
  s_showing_icons = false;
  mark_shake_content_dirty();
}

static void show_icons_callback(void *data) {
  s_shake_delay_timer = NULL;
  s_showing_icons = true;
  mark_shake_content_dirty();

  if (s_shake_timer) {
    app_timer_reschedule(s_shake_timer, SHAKE_DISPLAY_MS);
  } else {
    s_shake_timer = app_timer_register(SHAKE_DISPLAY_MS, hide_icons_callback, NULL);
  }
}

static void hide_seconds_callback(void *data) {
  s_seconds_timer = NULL;
  s_seconds_visible = false;
  update_tick_subscription();
  layer_mark_dirty(s_minute_layer);  // seconds drawn inside minute_layer
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick handler
// ─────────────────────────────────────────────────────────────────────────────
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  int prev_min = s_last_time.tm_min;
  int prev_mday = s_last_time.tm_mday;
  bool hour_changed = (tick_time->tm_hour != s_last_time.tm_hour);
  bool date_changed = (tick_time->tm_mday != prev_mday);
  s_last_time = *tick_time;

  if (units_changed & SECOND_UNIT) {
    layer_mark_dirty(s_minute_layer);  // seconds drawn inside minute layer
  }
  if (units_changed & MINUTE_UNIT) {
    update_current_hand_tips();
    layer_mark_dirty(s_minute_layer);
    // Preserve the established five-minute hour-hand redraw cadence. The minute
    // hand still updates every minute, but the hour layer avoids four redundant
    // paints out of every five.
    if ((tick_time->tm_min % 5) == 0) layer_mark_dirty(s_hour_layer);
    // Fixed complication geometry only changes at the established top/bottom
    // boundaries, the hour-dependent corner rules, or a date rollover.
    bool was_mid = (prev_min >= 20 && prev_min <= 40);
    bool now_mid = (tick_time->tm_min >= 20 && tick_time->tm_min <= 40);
    if (was_mid != now_mid || hour_changed || date_changed) {
      invalidate_complication_cache();
      layer_mark_dirty(s_complication_layer);
    }
  }
  if (hour_changed) {
    layer_mark_dirty(s_bg_layer);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Accelerometer tap (shake) handler
// ─────────────────────────────────────────────────────────────────────────────
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  // Seconds hand on shake
  if (s_settings.seconds_hand_mode == 2) {
    // Refresh s_last_time from the system clock so the first draw shows the
    // correct second position, not the stale value from the last minute tick.
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t) s_last_time = *t;

    s_seconds_visible = true;
    update_tick_subscription();
    if (s_seconds_timer) {
      app_timer_reschedule(s_seconds_timer, s_settings.seconds_shake_dur * 1000);
    } else {
      s_seconds_timer = app_timer_register(
        s_settings.seconds_shake_dur * 1000, hide_seconds_callback, NULL);
    }
    layer_mark_dirty(s_minute_layer);  // seconds drawn inside minute layer
  }

  // Timed shake content: icons when configured for On shake, and/or the city
  // name when configured for On shake. City visibility is deliberately
  // independent of whether icons are Always Show, Always Hide, or Side-by-Side.
  // Every shake restarts the full show cycle: cancel any in-flight timers,
  // hide the timed content immediately, then start the 500ms delay before
  // re-showing it. This eliminates any lockout window.
  if (s_settings.shake_mode == 0 || s_settings.city_display_mode == 1) {
    // Cancel hide timer if running
    if (s_shake_timer) {
      app_timer_cancel(s_shake_timer);
      s_shake_timer = NULL;
    }
    // Hide icons immediately so the delay gives a clear "flash off then on" feel
    s_showing_icons = false;
    // Start (or restart) the 500ms delay before showing icons
    if (s_shake_delay_timer) {
      app_timer_reschedule(s_shake_delay_timer, SHAKE_DELAY_MS);
    } else {
      s_shake_delay_timer = app_timer_register(SHAKE_DELAY_MS, show_icons_callback, NULL);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Battery handler
// ─────────────────────────────────────────────────────────────────────────────
static uint8_t battery_visual_state(uint8_t percentage) {
  if (s_settings.battery_center_threshold > 0 &&
      percentage <= s_settings.battery_center_threshold) return 2;
  if (s_settings.battery_ring_threshold > 0 &&
      percentage <= s_settings.battery_ring_threshold) return 1;
  return 0;
}

static void battery_handler(BatteryChargeState charge) {
  uint8_t new_pct = charge.charge_percent;
  if (new_pct == s_battery_pct) return;
  uint8_t old_state = battery_visual_state(s_battery_pct);
  uint8_t new_state = battery_visual_state(new_pct);
  s_battery_pct = new_pct;
  // The centre cap changes only when crossing normal/low/critical boundaries.
  if (old_state != new_state) layer_mark_dirty(s_minute_layer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bluetooth handler
// ─────────────────────────────────────────────────────────────────────────────
static void bt_handler(bool connected) {
  bool was_connected = s_bt_connected;
  if (connected == was_connected) return;
  s_bt_connected = connected;

  if (!connected && was_connected && s_settings.vibrate_bt_disconnect) {
    vibes_long_pulse();
  }
  if (connected && !was_connected && s_settings.vibrate_bt_reconnect) {
    vibes_short_pulse();
  }

  layer_mark_dirty(s_minute_layer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings snapshot response
// ─────────────────────────────────────────────────────────────────────────────
// The companion requests this before opening the configuration page. Returning
// the watch's persisted settings makes the page independent of webview cache.
static void send_settings_snapshot(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;

  dict_write_uint8(out, KEY_SETTINGS_SNAPSHOT, 1);
  dict_write_uint8(out, 40, s_settings.display_hour_markers);
  dict_write_uint8(out, 41, s_settings.display_minor_markers);
  dict_write_uint8(out, 53, s_settings.bt_disconnect_min_inner_red);
  dict_write_uint8(out, 54, s_settings.vibrate_bt_disconnect);
  dict_write_uint8(out, 55, s_settings.vibrate_bt_reconnect);
  dict_write_uint8(out, 107, s_settings.shake_mode);
  dict_write_uint8(out, 110, s_settings.temp_unit);
  dict_write_cstring(out, 113, s_custom_location);
  dict_write_int32(out, 114, gcolor_to_rgb(s_settings.hour_hand_outer));
  dict_write_int32(out, 115, gcolor_to_rgb(s_settings.hour_hand_inner));
  dict_write_int32(out, 116, gcolor_to_rgb(s_settings.min_hand_outer));
  dict_write_int32(out, 117, gcolor_to_rgb(s_settings.min_hand_inner));
  dict_write_uint8(out, 118, s_settings.date_visible);
  dict_write_uint8(out, 119, s_settings.temp_visible);
  dict_write_uint8(out, 121, number_font_id());
  dict_write_int32(out, 126, gcolor_to_rgb(s_settings.background_color));
  dict_write_int32(out, 127, gcolor_to_rgb(s_settings.number_color));
  dict_write_int32(out, 128, gcolor_to_rgb(s_settings.icon_color));
  dict_write_int32(out, 129, gcolor_to_rgb(s_settings.hour_marker_color));
  dict_write_int32(out, 130, gcolor_to_rgb(s_settings.minute_marker_color));
  dict_write_int32(out, 134, gcolor_to_rgb(s_settings.date_color));
  dict_write_int32(out, 135, gcolor_to_rgb(s_settings.temp_color));
  dict_write_int32(out, 136, gcolor_to_rgb(s_settings.bt_disconnect_outer_color));
  dict_write_int32(out, 137, gcolor_to_rgb(s_settings.bt_disconnect_inner_color));
  dict_write_uint8(out, 138, s_settings.battery_ring_threshold);
  dict_write_uint8(out, 139, s_settings.battery_center_threshold);
  dict_write_int32(out, 141, gcolor_to_rgb(s_settings.seconds_hand_color));
  dict_write_uint8(out, 142, s_settings.seconds_hand_mode);
  dict_write_uint8(out, 143, s_settings.seconds_shake_dur);
  dict_write_uint8(out, 147, s_settings.sunrise_marker_visible);
  dict_write_int32(out, 148, gcolor_to_rgb(s_settings.sunrise_marker_color));
  dict_write_int32(out, 149, gcolor_to_rgb(s_settings.sunset_marker_color));
  dict_write_uint8(out, 150, s_settings.number_size);
  dict_write_uint8(out, 151, manual_icon_size());
  dict_write_uint8(out, 165, s_settings.extended_marker_size);
  dict_write_uint8(out, 152, s_settings.icon_size_manual_override ? 0 : 1);
  dict_write_uint8(out, 154, (uint8_t)numbers_visibility());
  dict_write_uint8(out, 153, s_settings.icon_color_mode);
  dict_write_uint8(out, 158, s_settings.display_mode);
  dict_write_uint8(out, 160, s_settings.city_display_mode);
  dict_write_int32(out, 161, gcolor_to_rgb(s_settings.city_color));
  dict_write_end(out);
  app_message_outbox_send();
}

// ─────────────────────────────────────────────────────────────────────────────
// AppMessage handler
// ─────────────────────────────────────────────────────────────────────────────
static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  bool icons_changed = false;
  bool temperature_changed = false;
  bool solar_changed = false;
  bool settings_changed = false;
  bool service_dirty = false;
  bool bg_dirty = false;
  bool complication_dirty = false;
  bool hour_dirty = false;
  bool minute_dirty = false;
  Tuple *t;

  // The phone requests this before opening the settings page. Respond before
  // processing normal weather or settings payloads.
  t = dict_find(iter, KEY_REQUEST_SETTINGS);
  if (t && t->value->int32) {
    send_settings_snapshot();
    return;
  }

  // Weather deltas: omitted fields retain their last good values.
  for (int i = 0; i < 24; i++) {
    t = dict_find(iter, i);
    if (t) {
      int8_t value = (int8_t)t->value->int32;
      if (s_icons[i] != value) {
        s_icons[i] = value;
        icons_changed = true;
      }
    }
  }

  t = dict_find(iter, KEY_FORECAST_START_HOUR);
  if (t) {
    int8_t value = (int8_t)t->value->int32;
    if (value >= 0 && value <= 23 && s_forecast_start_hour != value) {
      s_forecast_start_hour = value;
      bg_dirty = true;
      complication_dirty = true;
    }
  }

  t = dict_find(iter, 58); // KEY_TEMP_C
  if (t) {
    int8_t value = (int8_t)t->value->int32;
    if (s_temp_c != value) { s_temp_c = value; temperature_changed = true; }
  }
  t = dict_find(iter, 59); // KEY_TEMP_F
  if (t) {
    int8_t value = (int8_t)t->value->int32;
    if (s_temp_f != value) { s_temp_f = value; temperature_changed = true; }
  }

  t = dict_find(iter, 25); // KEY_SUNRISE_HOUR
  if (t) {
    int8_t value = (int8_t)t->value->int32;
    if (s_sunrise_hour != value) { s_sunrise_hour = value; solar_changed = true; }
  }
  t = dict_find(iter, 26); // KEY_SUNRISE_MINUTE
  if (t) {
    int8_t value = (int8_t)t->value->int32;
    if (s_sunrise_min != value) { s_sunrise_min = value; solar_changed = true; }
  }
  t = dict_find(iter, 27); // KEY_SUNSET_HOUR
  if (t) {
    int8_t value = (int8_t)t->value->int32;
    if (s_sunset_hour != value) { s_sunset_hour = value; solar_changed = true; }
  }
  t = dict_find(iter, 28); // KEY_SUNSET_MINUTE
  if (t) {
    int8_t value = (int8_t)t->value->int32;
    if (s_sunset_min != value) { s_sunset_min = value; solar_changed = true; }
  }

  if (icons_changed) {
    persist_write_data(PERSIST_ICONS, s_icons, sizeof(s_icons));
    bg_dirty = true;
    complication_dirty = true;
  }
  if (temperature_changed) {
    persist_write_int(PERSIST_TEMP_C, (int32_t)s_temp_c);
    persist_write_int(PERSIST_TEMP_F, (int32_t)s_temp_f);
    complication_dirty = true;
  }
  if (solar_changed) {
    persist_write_int(PERSIST_SUNRISE_HOUR, (int32_t)s_sunrise_hour);
    persist_write_int(PERSIST_SUNRISE_MINUTE, (int32_t)s_sunrise_min);
    persist_write_int(PERSIST_SUNSET_HOUR, (int32_t)s_sunset_hour);
    persist_write_int(PERSIST_SUNSET_MINUTE, (int32_t)s_sunset_min);
    bg_dirty = true;
  }

#define UPDATE_BOOL_SETTING(KEY, FIELD, ON_CHANGE) do { \
  t = dict_find(iter, (KEY)); \
  if (t) { \
    bool value = (bool)t->value->int32; \
    if (s_settings.FIELD != value) { \
      s_settings.FIELD = value; settings_changed = true; ON_CHANGE; \
    } \
  } \
} while (0)

#define UPDATE_U8_SETTING(KEY, FIELD, ON_CHANGE) do { \
  t = dict_find(iter, (KEY)); \
  if (t) { \
    uint8_t value = (uint8_t)t->value->int32; \
    if (s_settings.FIELD != value) { \
      s_settings.FIELD = value; settings_changed = true; ON_CHANGE; \
    } \
  } \
} while (0)

#define UPDATE_COLOR_SETTING(KEY, FIELD, ON_CHANGE) do { \
  t = dict_find(iter, (KEY)); \
  if (t) { \
    GColor value = rgb_to_gcolor(t->value->int32); \
    if (s_settings.FIELD.argb != value.argb) { \
      s_settings.FIELD = value; settings_changed = true; ON_CHANGE; \
    } \
  } \
} while (0)

  UPDATE_BOOL_SETTING(40, display_hour_markers, bg_dirty = true);
  UPDATE_BOOL_SETTING(41, display_minor_markers, bg_dirty = true);
  UPDATE_BOOL_SETTING(53, bt_disconnect_min_inner_red, minute_dirty = true);
  UPDATE_BOOL_SETTING(54, vibrate_bt_disconnect, (void)0);
  UPDATE_BOOL_SETTING(55, vibrate_bt_reconnect, (void)0);
  UPDATE_U8_SETTING(107, shake_mode, bg_dirty = true; complication_dirty = true; service_dirty = true);
  UPDATE_U8_SETTING(110, temp_unit, complication_dirty = true);

  t = dict_find(iter, 113); // KEY_CUSTOM_LOCATION
  if (t && t->type == TUPLE_CSTRING &&
      strncmp(s_custom_location, t->value->cstring, sizeof(s_custom_location)) != 0) {
    strncpy(s_custom_location, t->value->cstring, sizeof(s_custom_location) - 1);
    s_custom_location[sizeof(s_custom_location) - 1] = '\0';
    persist_write_string(PERSIST_CUSTOM_LOCATION, s_custom_location);
  }

  UPDATE_COLOR_SETTING(114, hour_hand_outer, hour_dirty = true; minute_dirty = true);
  UPDATE_COLOR_SETTING(115, hour_hand_inner, hour_dirty = true);
  UPDATE_COLOR_SETTING(116, min_hand_outer, minute_dirty = true);
  UPDATE_COLOR_SETTING(117, min_hand_inner, minute_dirty = true);
  UPDATE_U8_SETTING(118, date_visible, complication_dirty = true);
  UPDATE_U8_SETTING(119, temp_visible, complication_dirty = true);

  t = dict_find(iter, 121); // KEY_NUMBER_FONT
  if (t) {
    uint8_t value = (uint8_t)t->value->int32;
    if (value > 6) value = 0;
    if (s_settings.number_font != value) {
      s_settings.number_font = value;
      s_cached_font_id = 255;
      s_ink_valid = false;
      settings_changed = true;
      bg_dirty = true;
      complication_dirty = true;
    }
  }

  UPDATE_COLOR_SETTING(126, background_color, bg_dirty = true);
  UPDATE_COLOR_SETTING(127, number_color, bg_dirty = true);
  UPDATE_COLOR_SETTING(128, icon_color, bg_dirty = true);
  UPDATE_COLOR_SETTING(129, hour_marker_color, bg_dirty = true);
  UPDATE_COLOR_SETTING(130, minute_marker_color, bg_dirty = true);
  UPDATE_COLOR_SETTING(134, date_color, complication_dirty = true);
  UPDATE_COLOR_SETTING(135, temp_color, complication_dirty = true);
  UPDATE_COLOR_SETTING(136, bt_disconnect_outer_color, minute_dirty = true);
  UPDATE_COLOR_SETTING(137, bt_disconnect_inner_color, minute_dirty = true);
  UPDATE_U8_SETTING(138, battery_ring_threshold, minute_dirty = true);
  UPDATE_U8_SETTING(139, battery_center_threshold, minute_dirty = true);
  UPDATE_COLOR_SETTING(141, seconds_hand_color, minute_dirty = true);

  t = dict_find(iter, 142); // KEY_SECONDS_HAND_MODE
  if (t) {
    uint8_t value = (uint8_t)t->value->int32;
    if (s_settings.seconds_hand_mode != value) {
      s_settings.seconds_hand_mode = value;
      settings_changed = true;
      service_dirty = true;
      minute_dirty = true;
      update_tick_subscription();
    }
  }
  UPDATE_U8_SETTING(143, seconds_shake_dur, (void)0);

  // Test actions remain isolated from persisted settings.
  t = dict_find(iter, 144); // KEY_TEST_BATTERY_ALERT
  if (t && t->value->int32) {
    if (s_test_battery_timer) app_timer_cancel(s_test_battery_timer);
    s_test_saved_battery = s_battery_pct;
    uint8_t low_threshold = s_settings.battery_ring_threshold;
    uint8_t critical_threshold = s_settings.battery_center_threshold;
    if (low_threshold > critical_threshold + 1) {
      s_battery_pct = critical_threshold + (low_threshold - critical_threshold) / 2;
    } else {
      s_battery_pct = low_threshold;
    }
    layer_mark_dirty(s_minute_layer);
    s_test_battery_timer = app_timer_register(3000, test_battery_restore_callback, NULL);
  }

  t = dict_find(iter, 145); // KEY_TEST_BT_DISCONNECT
  if (t && t->value->int32) {
    if (s_test_bt_timer) app_timer_cancel(s_test_bt_timer);
    s_test_saved_bt = s_bt_connected;
    s_bt_connected = false;
    if (s_settings.vibrate_bt_disconnect) vibes_long_pulse();
    layer_mark_dirty(s_minute_layer);
    s_test_bt_timer = app_timer_register(3000, test_bt_restore_callback, NULL);
  }

  t = dict_find(iter, 146); // KEY_TEST_CRITICAL_BATTERY_ALERT
  if (t && t->value->int32) {
    if (s_test_battery_timer) app_timer_cancel(s_test_battery_timer);
    s_test_saved_battery = s_battery_pct;
    s_battery_pct = 1;
    layer_mark_dirty(s_minute_layer);
    s_test_battery_timer = app_timer_register(3000, test_battery_restore_callback, NULL);
  }

  UPDATE_U8_SETTING(147, sunrise_marker_visible, bg_dirty = true);
  UPDATE_COLOR_SETTING(148, sunrise_marker_color, bg_dirty = true);
  UPDATE_COLOR_SETTING(149, sunset_marker_color, bg_dirty = true);

  t = dict_find(iter, 150); // KEY_NUMBER_SIZE
  if (t) {
    uint8_t value = (uint8_t)t->value->int32;
    if (value < 1 || value > 5) value = 3;
    if (s_settings.number_size != value) {
      s_settings.number_size = value;
      s_cached_font_size = 255;
      s_ink_valid = false;
      settings_changed = true;
      bg_dirty = true;
      complication_dirty = true;
    }
  }

  t = dict_find(iter, 151); // KEY_ICON_SIZE
  if (t) {
    uint8_t icon_size = (uint8_t)t->value->int32;
    if (icon_size < 1 || icon_size > 5) icon_size = 3;
    uint8_t packed_size = (s_settings.icon_size & NUMBERS_VISIBILITY_MASK) |
                          icon_size;
    if (s_settings.icon_size != packed_size) {
      s_settings.icon_size = packed_size;
      settings_changed = true;
      bg_dirty = true;
      complication_dirty = true;
    }
  }
  t = dict_find(iter, 152); // KEY_ICON_SIZE_SAME_AS_FONT
  if (t) {
    bool manual_override = ((uint8_t)t->value->int32 == 0);
    if (s_settings.icon_size_manual_override != manual_override) {
      s_settings.icon_size_manual_override = manual_override;
      settings_changed = true;
      bg_dirty = true;
      complication_dirty = true;
    }
  }
  t = dict_find(iter, 154); // KEY_NUMBERS_VISIBILITY
  if (t) {
    uint8_t visibility = (uint8_t)t->value->int32;
    if (visibility > NUMBERS_VISIBILITY_SHOW_NONE) {
      visibility = NUMBERS_VISIBILITY_SHOW_ALL;
    }
    uint8_t packed_size = (s_settings.icon_size & ICON_SIZE_VALUE_MASK) |
                          (visibility << NUMBERS_VISIBILITY_SHIFT);
    if (s_settings.icon_size != packed_size) {
      s_settings.icon_size = packed_size;
      settings_changed = true;
      bg_dirty = true;
      complication_dirty = true;
    }
  }
  t = dict_find(iter, 165); // KEY_EXTENDED_MARKER_SIZE
  if (t) {
    uint8_t value = (uint8_t)t->value->int32;
    if (value > EXTENDED_MARKER_SIZE_SMALL) {
      value = EXTENDED_MARKER_SIZE_MEDIUM;
    }
    if (s_settings.extended_marker_size != value) {
      s_settings.extended_marker_size = value;
      settings_changed = true;
      bg_dirty = true;
    }
  }
  UPDATE_U8_SETTING(158, display_mode, complication_dirty = true);

  t = dict_find(iter, 159); // KEY_CITY_NAME
  if (t && t->type == TUPLE_CSTRING &&
      strncmp(s_city_name, t->value->cstring, sizeof(s_city_name)) != 0) {
    strncpy(s_city_name, t->value->cstring, sizeof(s_city_name) - 1);
    s_city_name[sizeof(s_city_name) - 1] = '\0';
    persist_write_string(PERSIST_CITY, s_city_name);
    complication_dirty = true;
  }

  UPDATE_U8_SETTING(160, city_display_mode, complication_dirty = true; service_dirty = true);
  UPDATE_COLOR_SETTING(161, city_color, complication_dirty = true);
  t = dict_find(iter, 153); // KEY_ICON_COLOR_MODE
  if (t) {
    uint8_t value = (uint8_t)t->value->int32;
    if (value > 3) value = 0;  // retired Pebble-icon test modes fall back safely
    if (s_settings.icon_color_mode != value) {
      s_settings.icon_color_mode = value;
      settings_changed = true;
      bg_dirty = true;
      complication_dirty = true;
    }
  }

#undef UPDATE_BOOL_SETTING
#undef UPDATE_U8_SETTING
#undef UPDATE_COLOR_SETTING

  if (settings_changed) {
    persist_write_data(PERSIST_SETTINGS, &s_settings, sizeof(Settings));
  }
  if (service_dirty) update_accel_tap_subscription();

  if (bg_dirty) layer_mark_dirty(s_bg_layer);
  if (complication_dirty) {
    invalidate_complication_cache();
    layer_mark_dirty(s_complication_layer);
  }
  if (hour_dirty) layer_mark_dirty(s_hour_layer);
  if (minute_dirty) layer_mark_dirty(s_minute_layer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Default settings
// ─────────────────────────────────────────────────────────────────────────────
static void load_default_settings(void) {
  s_settings.background_color       = GColorBlack;
  s_settings.display_hour_markers   = true;
  s_settings.hour_marker_color      = GColorWhite;
  s_settings.display_minor_markers  = true;
  s_settings.minute_marker_color    = GColorFromRGB(0x6b, 0x7f, 0x99);
  s_settings.number_font            = 0;   // Digital
  s_settings.number_size            = 3;
  s_settings.number_color           = GColorWhite;
  s_settings.icon_size              = 3;
  s_settings.icon_size_manual_override = true;
  s_settings.icon_color             = GColorWhite;
  s_settings.hour_hand_outer        = GColorWhite;
  s_settings.hour_hand_inner        = GColorClear;
  s_settings.min_hand_outer         = GColorBlack;
  s_settings.min_hand_inner         = GColorFromRGB(0, 97, 254);
  s_settings.seconds_hand_color     = GColorWhite;
  s_settings.seconds_hand_mode      = 2;   // Shake to show
  s_settings.seconds_shake_dur      = 10;
  s_settings.date_visible           = 0;   // Always
  s_settings.temp_visible           = 0;   // Always
  s_settings.display_mode           = 0;   // Both
  s_settings.temp_unit              = 0;   // °C
  s_settings.date_color             = GColorFromRGB(0x4a, 0x5f, 0x7f);
  s_settings.temp_color             = GColorFromRGB(0x4a, 0x5f, 0x7f);
  s_settings.battery_ring_threshold   = 20;
  s_settings.battery_center_threshold = 10;
  s_settings.sunrise_marker_visible = 0;   // Always
  s_settings.sunrise_marker_color   = GColorFromRGB(0xff, 0x95, 0x00);
  s_settings.sunset_marker_color    = GColorFromRGB(0x00, 0x61, 0xfe);
  s_settings.vibrate_bt_disconnect  = true;
  s_settings.vibrate_bt_reconnect   = false;
  s_settings.bt_disconnect_min_inner_red = true;
  s_settings.bt_disconnect_outer_color   = GColorRed;
  s_settings.bt_disconnect_inner_color   = GColorRed;
  s_settings.shake_mode             = 0;   // Show on shake
  s_settings.city_display_mode      = 1;   // Shake
  s_settings.city_color             = GColorFromRGB(0x00, 0x00, 0xaa);
  s_settings.icon_color_mode        = 0;   // Single colour
  s_settings.icon_size_manual_override = true;
  s_settings.extended_marker_size   = EXTENDED_MARKER_SIZE_MEDIUM;
}

// ─────────────────────────────────────────────────────────────────────────────
// Window load / unload
// ─────────────────────────────────────────────────────────────────────────────
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_screen_w = bounds.size.w;
  s_screen_h = bounds.size.h;

  // Start from defaults before overlaying stored bytes. Older Settings records
  // can be shorter after a future update; this preserves valid legacy fields
  // without leaving an unread tail uninitialised.
  load_default_settings();
  if (persist_exists(PERSIST_SETTINGS)) {
    persist_read_data(PERSIST_SETTINGS, &s_settings, sizeof(Settings));
  }
  bool settings_need_write = false;
  // v3.2.38–v3.2.40 packed Numbers/Numerals into the high bit.
  // Preserve the selected base font while returning to the seven-option font menu.
  if (s_settings.number_font & 0x80) {
    s_settings.number_font &= 0x7F;
    if (s_settings.number_font > 6) s_settings.number_font = 0;
    settings_need_write = true;
  }
  // Migrate v3.2.18–v3.2.36's legacy diagonal-hide bit to the Cardinals-only
  // menu value while retaining the manual icon-size field in the lower nibble.
  if (s_settings.icon_size & LEGACY_HIDE_DIAGONAL_NUMBERS_MASK) {
    s_settings.icon_size = (s_settings.icon_size & ICON_SIZE_VALUE_MASK) |
                          (NUMBERS_VISIBILITY_ONLY_CARDINALS << NUMBERS_VISIBILITY_SHIFT);
    settings_need_write = true;
  }
  // v3.2.33's retired Pebble icon test mode used value 4. Preserve user
  // settings otherwise, but restore that obsolete selection to normal mode.
  if (s_settings.icon_color_mode > 3) {
    s_settings.icon_color_mode = 0;
    settings_need_write = true;
  }
#if defined(PBL_PLATFORM_BASALT) || defined(PBL_PLATFORM_CHALK)
  // Size 3 now matches the former compact Size 1 baseline. Preserve prior
  // small selections at their closest new slider step, once per watch.
  if (!persist_exists(PERSIST_COMPACT_NUMBER_SIZES)) {
    if (s_settings.number_size == 1) {
      s_settings.number_size = 3;
      settings_need_write = true;
    } else if (s_settings.number_size == 2) {
      s_settings.number_size = 4;
      settings_need_write = true;
    } else if (s_settings.number_size >= 4) {
      s_settings.number_size = 5;
      settings_need_write = true;
    }
    // Keep this migration separate from the persisted Settings structure so it
    // cannot replay when a later unrelated setting is saved.
    persist_write_bool(PERSIST_COMPACT_NUMBER_SIZES, true);
  }
#endif
  if (normalize_settings()) settings_need_write = true;
  // Commit all migrations and normalization in one flash write.
  if (settings_need_write) {
    persist_write_data(PERSIST_SETTINGS, &s_settings, sizeof(Settings));
  }

  // Load persisted icons
  if (persist_exists(PERSIST_ICONS)) {
    persist_read_data(PERSIST_ICONS, s_icons, sizeof(s_icons));
  } else {
    memset(s_icons, ICON_UNKNOWN, sizeof(s_icons));
  }
  // Load persisted city name and custom location.
  if (persist_exists(PERSIST_CITY)) {
    persist_read_string(PERSIST_CITY, s_city_name, sizeof(s_city_name));
  }
  if (persist_exists(PERSIST_CUSTOM_LOCATION)) {
    persist_read_string(PERSIST_CUSTOM_LOCATION, s_custom_location, sizeof(s_custom_location));
  }
  // Load persisted temperature
  if (persist_exists(PERSIST_TEMP_C)) {
    s_temp_c = (int8_t)persist_read_int(PERSIST_TEMP_C);
  }
  if (persist_exists(PERSIST_TEMP_F)) {
    s_temp_f = (int8_t)persist_read_int(PERSIST_TEMP_F);
  }
  if (persist_exists(PERSIST_SUNRISE_HOUR)) {
    s_sunrise_hour = (int8_t)persist_read_int(PERSIST_SUNRISE_HOUR);
  }
  if (persist_exists(PERSIST_SUNRISE_MINUTE)) {
    s_sunrise_min = (int8_t)persist_read_int(PERSIST_SUNRISE_MINUTE);
  }
  if (persist_exists(PERSIST_SUNSET_HOUR)) {
    s_sunset_hour = (int8_t)persist_read_int(PERSIST_SUNSET_HOUR);
  }
  if (persist_exists(PERSIST_SUNSET_MINUTE)) {
    s_sunset_min = (int8_t)persist_read_int(PERSIST_SUNSET_MINUTE);
  }

  // Compute marker caches
  compute_markers();

  // Fixed bottom-to-top order: dial, complications, then hands. Dynamic
  // Layering was redundant and has been removed from settings and runtime code.
  s_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_bg_layer, bg_layer_update);
  layer_add_child(root, s_bg_layer);

  s_complication_layer = layer_create(bounds);
  layer_set_update_proc(s_complication_layer, complication_layer_update);
  layer_add_child(root, s_complication_layer);

  s_hour_layer = layer_create(bounds);
  layer_set_update_proc(s_hour_layer, hour_layer_update);
  layer_add_child(root, s_hour_layer);

  s_minute_layer = layer_create(bounds);
  layer_set_update_proc(s_minute_layer, minute_layer_update);
  layer_add_child(root, s_minute_layer);


  // Get initial time
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  if (t) s_last_time = *t;

  // Initial seconds visibility and reusable render geometry.
  s_seconds_visible = (s_settings.seconds_hand_mode == 1);
  compute_static_hand_geometry();
  invalidate_complication_cache();

  // Pre-compute perimeter points once (screen size never changes at runtime)
  GPoint center = GPoint(s_screen_w / 2, s_screen_h / 2);
  for (int h = 0; h < 12; h++) {
    int32_t angle = TRIG_MAX_ANGLE * h / 12;
#ifdef PBL_ROUND
    s_perimeter_cache[h] = get_perimeter_point(center, angle,
                                                round_px(ROUND_LABEL_INSET),
                                                round_px(ROUND_LABEL_INSET));
#else
    s_perimeter_cache[h] = get_perimeter_point(center, angle, 0, 0);
#endif
  }

  // Subscribe to tick timer
  update_tick_subscription();

  // Subscribe to battery and BT
  BatteryChargeState bcs = battery_state_service_peek();
  s_battery_pct = bcs.charge_percent;
  battery_state_service_subscribe(battery_handler);

  s_bt_connected = connection_service_peek_pebble_app_connection();
  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = bt_handler
  });

  // Subscribe to the tap service only when a configured feature needs it.
  update_accel_tap_subscription();

  // Open AppMessage
  app_message_open(APP_MSG_INBOX_SIZE, APP_MSG_OUTBOX_SIZE);
  app_message_register_inbox_received(inbox_received_handler);
}

static void window_unload(Window *window) {
  // Unload only custom resource fonts. PBL_BW uses SDK-owned system fonts.
  if (s_cached_number_font) {
#ifndef PBL_BW
    fonts_unload_custom_font(s_cached_number_font);
#endif
    s_cached_number_font = NULL;
  }
  // Cancel timers
  if (s_shake_timer)       { app_timer_cancel(s_shake_timer);       s_shake_timer = NULL; }
  if (s_shake_delay_timer) { app_timer_cancel(s_shake_delay_timer); s_shake_delay_timer = NULL; }  // kept for safety
  if (s_seconds_timer)     { app_timer_cancel(s_seconds_timer);     s_seconds_timer = NULL; }

  // Unsubscribe
  if (s_tick_units != 0) tick_timer_service_unsubscribe();
  s_tick_units = 0;
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  if (s_accel_tap_subscribed) accel_tap_service_unsubscribe();
  s_accel_tap_subscribed = false;

  // Destroy layers
  layer_destroy(s_minute_layer);
  layer_destroy(s_hour_layer);
  layer_destroy(s_complication_layer);
  layer_destroy(s_bg_layer);
}

// ─────────────────────────────────────────────────────────────────────────────
// App init / deinit
// ─────────────────────────────────────────────────────────────────────────────
static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
