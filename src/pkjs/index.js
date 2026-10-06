// Brolly v2.0.0 — pkjs/index.js
// PebbleKit JS: weather fetch (Open-Meteo) + config bridge
//
// IMPORTANT: Pebble.sendAppMessage() requires NUMERIC keys only.
// String key names are NOT accepted. All message keys are mapped to their
// numeric IDs from package.json messageKeys.

'use strict';

// ─────────────────────────────────────────────────────────────────────────────
// Icon code constants (must match C defines)
// ─────────────────────────────────────────────────────────────────────────────
var ICON = {
  UNKNOWN:          0,
  UNDEFINED:        1,
  CLEAR:            2,
  CLEAR_N:          3,
  PARTLY_CLOUDY:    4,
  PARTLY_CLOUDY_N:  5,
  MOSTLY_CLOUDY:    6,
  MOSTLY_CLOUDY_N:  7,
  CLOUDY:           8,
  CLOUDY_N:         9,
  CHANCE_FLURRIES:  10,
  FLURRIES:         11,
  CHANCE_FLURRIES_N:12,
  FLURRIES_N:       13,
  CHANCE_RAIN:      14,
  RAIN:             15,
  CHANCE_RAIN_N:    16,
  RAIN_N:           17,
  CHANCE_SLEET:     18,
  SLEET:            19,
  CHANCE_SLEET_N:   20,
  SLEET_N:          21,
  CHANCE_SNOW:      22,
  SNOW:             23,
  CHANCE_SNOW_N:    24,
  SNOW_N:           25,
  CHANCE_TSTORMS:   26,
  TSTORMS:          27,
  CHANCE_TSTORMS_N: 28,
  TSTORMS_N:        29,
  FOG:              30,
  HAZE:             31,
  FOG_N:            32,
  HAZE_N:           33
};

// ─────────────────────────────────────────────────────────────────────────────
// Numeric message key IDs (from package.json messageKeys)
// ALL Pebble.sendAppMessage calls must use these numbers, not string names.
// ─────────────────────────────────────────────────────────────────────────────
var KEY = {
  // Weather / icons (0–23 = icon slots)
  ICON_0:  0,  ICON_1:  1,  ICON_2:  2,  ICON_3:  3,
  ICON_4:  4,  ICON_5:  5,  ICON_6:  6,  ICON_7:  7,
  ICON_8:  8,  ICON_9:  9,  ICON_10: 10, ICON_11: 11,
  ICON_12: 12, ICON_13: 13, ICON_14: 14, ICON_15: 15,
  ICON_16: 16, ICON_17: 17, ICON_18: 18, ICON_19: 19,
  ICON_20: 20, ICON_21: 21, ICON_22: 22, ICON_23: 23,
  SUNRISE_HOUR:   25,
  SUNRISE_MINUTE: 26,
  SUNSET_HOUR:    27,
  SUNSET_MINUTE:  28,
  // Alerts / BT
  BT_DISCONNECT_MIN_INNER_RED: 53,
  VIBRATE_BT_DISCONNECT:       54,
  VIBRATE_BT_RECONNECT:        55,
  // Temperature
  TEMP_C: 58,
  TEMP_F: 59,
  // Settings
  SHAKE_MODE:              107,
  TEMP_UNIT:               110,
  CUSTOM_LOCATION:         113,
  HOUR_HAND_OUTER:         114,
  HOUR_HAND_INNER:         115,
  MIN_HAND_OUTER:          116,
  MIN_HAND_INNER:          117,
  DATE_VISIBLE:            118,
  TEMP_VISIBLE:            119,
  NUMBER_FONT:             121,
  BACKGROUND_COLOR:        126,
  NUMBER_COLOR:            127,
  ICON_COLOR:              128,
  HOUR_MARKER_COLOR:       129,
  MINUTE_MARKER_COLOR:     130,
  DATE_COLOR:              134,
  TEMP_COLOR:              135,
  BT_DISCONNECT_OUTER_COLOR: 136,
  BT_DISCONNECT_INNER_COLOR: 137,
  BATTERY_RING_THRESHOLD:  138,
  BATTERY_CENTER_THRESHOLD:139,
  SECONDS_HAND_COLOR:      141,
  SECONDS_HAND_MODE:       142,
  SECONDS_SHAKE_DUR:       143,
  // Test buttons
  TEST_BATTERY_ALERT:          144,
  TEST_BT_DISCONNECT:          145,
  TEST_CRITICAL_BATTERY_ALERT: 146,
  // Sunrise/sunset markers
  SUNRISE_MARKER_VISIBLE: 147,
  SUNRISE_MARKER_COLOR:   148,
  SUNSET_MARKER_COLOR:    149,
  // Appearance
  NUMBER_SIZE:    150,
  ICON_SIZE:      151,
  ICON_SIZE_SAME_AS_FONT: 152,
  NUMBERS_VISIBILITY: 154,
  ICON_COLOR_MODE:153,
  DISPLAY_MODE:   158,
  // City name display
  CITY_NAME:         159,
  CITY_DISPLAY_MODE: 160,
  CITY_COLOR:        161,
  COMPLICATION_LAYER:162,
  EXTENDED_MARKER_SIZE: 165,
  FORECAST_START_HOUR: 166,
  // Settings snapshot synchronisation
  REQUEST_SETTINGS:  163,
  SETTINGS_SNAPSHOT: 164,
  // Markers
  DISPLAY_HOUR_MARKERS:  40,
  DISPLAY_MINOR_MARKERS: 41
};

// ─────────────────────────────────────────────────────────────────────────────
// WMO weather code → icon code mapping
// ─────────────────────────────────────────────────────────────────────────────
function wmoToIcon(code, isDay) {
  var d = isDay ? 1 : 0;
  switch (code) {
    case 0:  return d ? ICON.CLEAR        : ICON.CLEAR_N;
    case 1:  return d ? ICON.PARTLY_CLOUDY: ICON.PARTLY_CLOUDY_N;
    case 2:  return d ? ICON.MOSTLY_CLOUDY: ICON.MOSTLY_CLOUDY_N;
    case 3:  return d ? ICON.CLOUDY       : ICON.CLOUDY_N;
    case 45: case 48: return d ? ICON.FOG : ICON.FOG_N;
    case 51: case 53: case 55:
    case 56: case 57:
    case 61: case 63: case 65:
    case 66: case 67:
    case 80: case 81: case 82:
      return d ? ICON.RAIN : ICON.RAIN_N;
    case 71: case 73: case 75:
    case 77:
    case 85: case 86:
      return d ? ICON.SNOW : ICON.SNOW_N;
    case 95: case 96: case 99:
      return d ? ICON.TSTORMS : ICON.TSTORMS_N;
    default: return ICON.UNKNOWN;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Stored settings
// ─────────────────────────────────────────────────────────────────────────────
var s_customLocation = '';
var s_useLatLon = false;
var s_storedLat = null;
var s_storedLon = null;
var s_resolvedCityName = '';
var s_weatherIntervalId = null;
var s_currentWeatherInterval = 60; // default 60 mins
// Avoid overlapping GPS/network work and duplicate watch payloads when app
// lifecycle events and interval refreshes happen close together.
var s_weatherFetchInFlight = false;
var s_weatherFetchQueued = false;
var s_lastWeatherPayload = null;
var s_pendingWeatherPayloadSignature = null;
var CUSTOM_LOCATION_STORAGE_KEY = 'custom_location';
var FULL_SETTINGS_STORAGE_KEY = 'brolly_full_settings_v1';
var s_fullSettings = null;
var s_waitingForSettingsSnapshot = false;
var s_settingsSnapshotTimer = null;

// Numeric watch snapshot fields mapped back to the settings-page property names.
var SNAPSHOT_FIELD_MAP = {
  40: 'KEY_DISPLAY_HOUR_MARKERS', 41: 'KEY_DISPLAY_MINOR_MARKERS',
  53: 'KEY_BT_DISCONNECT_MIN_INNER_RED', 54: 'KEY_VIBRATE_BT_DISCONNECT',
  55: 'KEY_VIBRATE_BT_RECONNECT', 107: 'KEY_SHAKE_MODE', 110: 'KEY_TEMP_UNIT',
  113: 'KEY_CUSTOM_LOCATION', 114: 'KEY_HOUR_HAND_OUTER', 115: 'KEY_HOUR_HAND_INNER',
  116: 'KEY_MIN_HAND_OUTER', 117: 'KEY_MIN_HAND_INNER', 118: 'KEY_DATE_VISIBLE',
  119: 'KEY_TEMP_VISIBLE', 121: 'KEY_NUMBER_FONT', 126: 'KEY_BACKGROUND_COLOR',
  127: 'KEY_NUMBER_COLOR', 128: 'KEY_ICON_COLOR', 129: 'KEY_HOUR_MARKER_COLOR',
  130: 'KEY_MINUTE_MARKER_COLOR', 134: 'KEY_DATE_COLOR', 135: 'KEY_TEMP_COLOR',
  136: 'KEY_BT_DISCONNECT_OUTER_COLOR', 137: 'KEY_BT_DISCONNECT_INNER_COLOR',
  138: 'KEY_BATTERY_RING_THRESHOLD', 139: 'KEY_BATTERY_CENTER_THRESHOLD',
  141: 'KEY_SECONDS_HAND_COLOR', 142: 'KEY_SECONDS_HAND_MODE',
  143: 'KEY_SECONDS_SHAKE_DUR', 147: 'KEY_SUNRISE_MARKER_VISIBLE',
  148: 'KEY_SUNRISE_MARKER_COLOR', 149: 'KEY_SUNSET_MARKER_COLOR',
  150: 'KEY_NUMBER_SIZE', 151: 'KEY_ICON_SIZE',
  152: 'KEY_ICON_SIZE_SAME_AS_FONT', 153: 'KEY_ICON_COLOR_MODE',
  154: 'KEY_NUMBERS_VISIBILITY',
  165: 'KEY_EXTENDED_MARKER_SIZE',
  158: 'KEY_DISPLAY_MODE', 160: 'KEY_CITY_DISPLAY_MODE', 161: 'KEY_CITY_COLOR',
  162: 'KEY_COMPLICATION_LAYER'
};


// Build this once rather than reconstructing it for every settings save.
var SETTINGS_KEY_MAP = {
  KEY_BT_DISCONNECT_MIN_INNER_RED: KEY.BT_DISCONNECT_MIN_INNER_RED,
  KEY_VIBRATE_BT_DISCONNECT:       KEY.VIBRATE_BT_DISCONNECT,
  KEY_VIBRATE_BT_RECONNECT:        KEY.VIBRATE_BT_RECONNECT,
  KEY_SHAKE_MODE:                  KEY.SHAKE_MODE,
  KEY_TEMP_UNIT:                   KEY.TEMP_UNIT,
  KEY_CUSTOM_LOCATION:             KEY.CUSTOM_LOCATION,
  KEY_HOUR_HAND_OUTER:             KEY.HOUR_HAND_OUTER,
  KEY_HOUR_HAND_INNER:             KEY.HOUR_HAND_INNER,
  KEY_MIN_HAND_OUTER:              KEY.MIN_HAND_OUTER,
  KEY_MIN_HAND_INNER:              KEY.MIN_HAND_INNER,
  KEY_DATE_VISIBLE:                KEY.DATE_VISIBLE,
  KEY_TEMP_VISIBLE:                KEY.TEMP_VISIBLE,
  KEY_NUMBER_FONT:                 KEY.NUMBER_FONT,
  KEY_BACKGROUND_COLOR:            KEY.BACKGROUND_COLOR,
  KEY_NUMBER_COLOR:                KEY.NUMBER_COLOR,
  KEY_ICON_COLOR:                  KEY.ICON_COLOR,
  KEY_HOUR_MARKER_COLOR:           KEY.HOUR_MARKER_COLOR,
  KEY_MINUTE_MARKER_COLOR:         KEY.MINUTE_MARKER_COLOR,
  KEY_DATE_COLOR:                  KEY.DATE_COLOR,
  KEY_TEMP_COLOR:                  KEY.TEMP_COLOR,
  KEY_BT_DISCONNECT_OUTER_COLOR:   KEY.BT_DISCONNECT_OUTER_COLOR,
  KEY_BT_DISCONNECT_INNER_COLOR:   KEY.BT_DISCONNECT_INNER_COLOR,
  KEY_BATTERY_RING_THRESHOLD:      KEY.BATTERY_RING_THRESHOLD,
  KEY_BATTERY_CENTER_THRESHOLD:    KEY.BATTERY_CENTER_THRESHOLD,
  KEY_SECONDS_HAND_COLOR:          KEY.SECONDS_HAND_COLOR,
  KEY_SECONDS_HAND_MODE:           KEY.SECONDS_HAND_MODE,
  KEY_SECONDS_SHAKE_DUR:           KEY.SECONDS_SHAKE_DUR,
  KEY_SUNRISE_MARKER_VISIBLE:      KEY.SUNRISE_MARKER_VISIBLE,
  KEY_SUNRISE_MARKER_COLOR:        KEY.SUNRISE_MARKER_COLOR,
  KEY_SUNSET_MARKER_COLOR:         KEY.SUNSET_MARKER_COLOR,
  KEY_NUMBER_SIZE:                 KEY.NUMBER_SIZE,
  KEY_ICON_SIZE:                   KEY.ICON_SIZE,
  KEY_ICON_SIZE_SAME_AS_FONT:      KEY.ICON_SIZE_SAME_AS_FONT,
  KEY_NUMBERS_VISIBILITY:       KEY.NUMBERS_VISIBILITY,
  KEY_ICON_COLOR_MODE:             KEY.ICON_COLOR_MODE,
  KEY_DISPLAY_MODE:                KEY.DISPLAY_MODE,
  KEY_CITY_DISPLAY_MODE:           KEY.CITY_DISPLAY_MODE,
  KEY_CITY_COLOR:                  KEY.CITY_COLOR,
  KEY_COMPLICATION_LAYER:          KEY.COMPLICATION_LAYER,
  KEY_EXTENDED_MARKER_SIZE:        KEY.EXTENDED_MARKER_SIZE,
  KEY_DISPLAY_HOUR_MARKERS:        KEY.DISPLAY_HOUR_MARKERS,
  KEY_DISPLAY_MINOR_MARKERS:       KEY.DISPLAY_MINOR_MARKERS
};

function normalizedSettingValue(key, value) {
  if (key === 'KEY_CUSTOM_LOCATION') return String(value || '');
  if (value === true) return 1;
  if (value === false) return 0;
  var parsed = parseInt(value, 10);
  return isNaN(parsed) ? value : parsed;
}

function loadFullSettings() {
  if (s_fullSettings) return s_fullSettings;
  try {
    var raw = localStorage.getItem(FULL_SETTINGS_STORAGE_KEY);
    s_fullSettings = raw ? JSON.parse(raw) : {};
  } catch (e) {
    console.log('Stored settings parse error: ' + e);
    s_fullSettings = {};
  }
  return s_fullSettings;
}

function persistFullSettings(patch) {
  var current = loadFullSettings();
  var next = {};
  var key;

  // Keep only real configuration fields; test buttons must never become a
  // user's next-open settings state.
  Object.keys(current).forEach(function(k) {
    if (k.indexOf('KEY_') === 0 && k.indexOf('KEY_TEST_') !== 0) next[k] = current[k];
  });
  Object.keys(patch || {}).forEach(function(k) {
    if (k.indexOf('KEY_') === 0 && k.indexOf('KEY_TEST_') !== 0) next[k] = patch[k];
  });

  if (next.KEY_WEATHER_INTERVAL === undefined) {
    next.KEY_WEATHER_INTERVAL = s_currentWeatherInterval || 60;
  }
  var nextJson = JSON.stringify(next);
  if (nextJson !== JSON.stringify(current)) {
    localStorage.setItem(FULL_SETTINGS_STORAGE_KEY, nextJson);
  }
  s_fullSettings = next;
  return next;
}

function decodeWatchSettingsSnapshot(payload) {
  var patch = {};
  var existing = loadFullSettings();
  Object.keys(SNAPSHOT_FIELD_MAP).forEach(function(numericKey) {
    if (!payload.hasOwnProperty(numericKey)) return;
    var settingKey = SNAPSHOT_FIELD_MAP[numericKey];
    // Earlier watchface versions did not store custom locations on the watch.
    // Keep the companion's known legacy value until a new save explicitly
    // writes it to the upgraded watchface.
    if (settingKey === 'KEY_CUSTOM_LOCATION' &&
        !payload[numericKey] && existing.KEY_CUSTOM_LOCATION) return;
    patch[settingKey] = payload[numericKey];
  });
  return persistFullSettings(patch);
}

function openConfigurationPage(settings) {
  var snapshot = settings || loadFullSettings();
  var configUrl = 'https://themott27.github.io/Brolly-v3-Settings/v3.4.7/' +
                  '#settings=' + encodeURIComponent(JSON.stringify(snapshot));
  Pebble.openURL(configUrl);
}

// Keep custom-location state durable across PebbleKit JS restarts and clear
// coordinate/city caches whenever the user selects a different place.
function setCustomLocation(location) {
  var normalized = String(location || '').trim();
  // Snapshot restores can repeat the same location during an active companion
  // session. Keep its resolved coordinates instead of forcing another geocode.
  if (normalized === s_customLocation) return;
  s_customLocation = normalized;
  s_resolvedCityName = '';
  s_useLatLon = false;
  s_storedLat = null;
  s_storedLon = null;
  if (normalized) {
    localStorage.setItem(CUSTOM_LOCATION_STORAGE_KEY, normalized);
  } else {
    localStorage.removeItem(CUSTOM_LOCATION_STORAGE_KEY);
  }
}

// Pebble AppMessage has one active delivery at a time. Queue every outgoing
// message and retry transient failures so settings and weather cannot race.
var s_messageQueue = [];
var s_messageSending = false;
var APP_MESSAGE_MAX_RETRIES = 3;
var APP_MESSAGE_RETRY_MS = 1000;

function enqueueAppMessage(message, label, onSuccess, onFailure) {
  if (!message || Object.keys(message).length === 0) return;
  s_messageQueue.push({
    message: message,
    label: label || 'AppMessage',
    attempts: 0,
    onSuccess: onSuccess || null,
    onFailure: onFailure || null
  });
  sendNextAppMessage();
}

function sendNextAppMessage() {
  if (s_messageSending || s_messageQueue.length === 0) return;

  var item = s_messageQueue[0];
  s_messageSending = true;
  Pebble.sendAppMessage(item.message, function() {
    console.log(item.label + ' sent to watch');
    s_messageQueue.shift();
    s_messageSending = false;
    if (item.onSuccess) item.onSuccess();
    sendNextAppMessage();
  }, function(e) {
    item.attempts++;
    if (item.attempts <= APP_MESSAGE_MAX_RETRIES) {
      var delay = APP_MESSAGE_RETRY_MS * item.attempts;
      console.log(item.label + ' send failed; retry ' + item.attempts + ' in ' + delay + 'ms: ' + JSON.stringify(e));
      setTimeout(function() {
        s_messageSending = false;
        sendNextAppMessage();
      }, delay);
    } else {
      console.log(item.label + ' send failed after retries: ' + JSON.stringify(e));
      s_messageQueue.shift();
      s_messageSending = false;
      if (item.onFailure) item.onFailure(e);
      sendNextAppMessage();
    }
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// Geocoding helper
// ─────────────────────────────────────────────────────────────────────────────
function geocodeCity(cityName, callback) {
  var url = 'https://geocoding-api.open-meteo.com/v1/search?name=' +
            encodeURIComponent(cityName) + '&count=1&language=en&format=json';
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.onload = function() {
    if (xhr.status === 200) {
      try {
        var data = JSON.parse(xhr.responseText);
        if (data.results && data.results.length > 0) {
          var r = data.results[0];
          var resolvedParts = [r.name || cityName, r.country || ''];
          s_resolvedCityName = resolvedParts.filter(function(part) {
            return part && part.length > 0;
          }).join(', ');
          callback(null, r.latitude, r.longitude);
        } else {
          callback('No results for: ' + cityName);
        }
      } catch (e) {
        callback('Geocode parse error: ' + e);
      }
    } else {
      callback('Geocode HTTP error: ' + xhr.status);
    }
  };
  xhr.onerror = function() { callback('Geocode network error'); };
  xhr.send();
}

// Reverse geocode lat/lon to city name using Nominatim (OpenStreetMap)
function reverseGeocode(lat, lon, callback) {
  var url = 'https://nominatim.openstreetmap.org/reverse?lat=' +
            lat + '&lon=' + lon + '&format=json&zoom=10&accept-language=en';
  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.setRequestHeader('Accept-Language', 'en');
  xhr.onload = function() {
    if (xhr.status === 200) {
      try {
        var data = JSON.parse(xhr.responseText);
        var addr = data.address || {};
        var city = addr.city || addr.town || addr.village || addr.hamlet || addr.county || '';
        var locationParts = [city, addr.country || ''];
        callback(null, locationParts.filter(function(part) {
          return part && part.length > 0;
        }).join(', '));
      } catch (e) {
        callback(null, '');
      }
    } else {
      callback(null, '');
    }
  };
  xhr.onerror = function() { callback(null, ''); };
  xhr.send();
}

// ─────────────────────────────────────────────────────────────────────────────
// IP geolocation fallback
// ─────────────────────────────────────────────────────────────────────────────
function ipGeolocate(callback) {
  var xhr = new XMLHttpRequest();
  xhr.open('GET', 'https://ipapi.co/json/', true);
  xhr.onload = function() {
    if (xhr.status === 200) {
      try {
        var data = JSON.parse(xhr.responseText);
        callback(null, data.latitude, data.longitude);
      } catch (e) {
        callback('IP geo parse error: ' + e);
      }
    } else {
      callback('IP geo HTTP error: ' + xhr.status);
    }
  };
  xhr.onerror = function() { callback('IP geo network error'); };
  xhr.send();
}

// ─────────────────────────────────────────────────────────────────────────────
// Fetch weather from Open-Meteo and send to watch
// ─────────────────────────────────────────────────────────────────────────────
function fetchWeather(lat, lon, complete) {
  // A rolling horizon begins at the current forecast hour, so each dial slot
  // can represent the next occurrence of that clock position rather than a
  // time earlier today. `timezone=auto` follows the selected weather location.
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + lat +
    '&longitude=' + lon +
    '&hourly=weather_code,is_day' +
    '&current=temperature_2m' +
    '&daily=sunrise,sunset' +
    '&forecast_hours=36' +
    '&forecast_days=2' +
    '&timezone=auto';

  var xhr = new XMLHttpRequest();
  xhr.open('GET', url, true);
  xhr.onload = function() {
    if (xhr.status === 200) {
      try {
        var data = JSON.parse(xhr.responseText);
        processWeatherData(data);
      } catch (e) {
        console.log('Weather parse error: ' + e);
      }
    } else {
      console.log('Weather HTTP error: ' + xhr.status);
    }
    complete();
  };
  xhr.onerror = function() {
    console.log('Weather network error');
    complete();
  };
  xhr.send();
}

function nextDailyTime(times, locationNow) {
  if (!Array.isArray(times) || times.length === 0) return null;
  for (var i = 0; i < times.length; i++) {
    if (typeof times[i] === 'string' && times[i] >= locationNow) return times[i];
  }
  return typeof times[0] === 'string' ? times[0] : null;
}

function parseLocalTime(value) {
  if (typeof value !== 'string' || value.indexOf('T') < 0) return null;
  var parts = value.split('T')[1].split(':');
  var hour = parseInt(parts[0], 10);
  var minute = parseInt(parts[1], 10);
  return (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59)
    ? { hour: hour, minute: minute } : null;
}

function processWeatherData(data) {
  data = data || {};
  var hourly = data.hourly || {};
  var codes = hourly.weather_code;
  var isDays = hourly.is_day;
  var hourlyTimes = hourly.time;
  var forecastStart = Array.isArray(hourlyTimes)
    ? parseLocalTime(hourlyTimes[0]) : null;
  var hasHourly = Array.isArray(codes) && Array.isArray(isDays) &&
                  codes.length >= 24 && isDays.length >= 24 &&
                  forecastStart !== null;
  var icons = new Array(24);

  // forecast_hours starts at the current forecast hour, not midnight. The
  // watch maps each dial position to the next AM/PM occurrence within this
  // rolling window, which keeps the evening and overnight dial accurate.
  if (hasHourly) {
    for (var h = 0; h < 24; h++) {
      icons[h] = wmoToIcon(codes[h], isDays[h] === 1);
    }
  }

  var current = data.current || {};
  var locationNow = typeof current.time === 'string' ? current.time : '';
  var rawTemp = Number(current.temperature_2m);
  var tempC = Math.round(rawTemp);
  var tempF = Math.round(tempC * 9 / 5 + 32);
  var hasTemp = isFinite(tempC);

  // Open-Meteo returns daily values in the selected location's local time.
  // Use the next actual solar event so an evening update does not restore a
  // sunrise that has already passed.
  var daily = data.daily || {};
  var sunrise = parseLocalTime(nextDailyTime(daily.sunrise, locationNow));
  var sunset = parseLocalTime(nextDailyTime(daily.sunset, locationNow));

  // Build message using NUMERIC keys only
  var msg = {};
  if (hasHourly) {
    for (var k = 0; k < 24; k++) {
      msg[k] = icons[k]; // KEY_ICON_0..23 = numeric keys 0..23
    }
    // Map the dial from Open-Meteo's actual rolling-window start. Otherwise,
    // for example, a 23:00 update viewed at 01:00 maps 08:00 to 06:00.
    msg[KEY.FORECAST_START_HOUR] = forecastStart.hour;
  } else {
    console.log('Weather response missing a complete timestamped hourly forecast; retaining last icons');
  }
  if (hasTemp) {
    msg[KEY.TEMP_C] = tempC;
    msg[KEY.TEMP_F] = tempF;
  }
  if (sunrise) {
    msg[KEY.SUNRISE_HOUR] = sunrise.hour;
    msg[KEY.SUNRISE_MINUTE] = sunrise.minute;
  }
  if (sunset) {
    msg[KEY.SUNSET_HOUR] = sunset.hour;
    msg[KEY.SUNSET_MINUTE] = sunset.minute;
  }
  // Send city name if we have one
  if (s_resolvedCityName !== undefined) {
    msg[KEY.CITY_NAME] = s_resolvedCityName.substring(0, 31);
  }

  var signature = JSON.stringify(msg);
  if (signature === s_pendingWeatherPayloadSignature) {
    console.log('Weather update already queued; skipping duplicate');
    return;
  }

  // The first update after a companion restart is complete. Later updates carry
  // only changed fields; the watch keeps its last good values for omitted keys.
  var delta = {};
  Object.keys(msg).forEach(function(key) {
    if (!s_lastWeatherPayload || s_lastWeatherPayload[key] !== msg[key]) {
      delta[key] = msg[key];
    }
  });
  if (Object.keys(delta).length === 0) {
    console.log('Weather unchanged; skipping watch update');
    return;
  }

  s_pendingWeatherPayloadSignature = signature;
  enqueueAppMessage(delta, 'Weather', function() {
    s_lastWeatherPayload = msg;
    if (s_pendingWeatherPayloadSignature === signature) {
      s_pendingWeatherPayloadSignature = null;
    }
  }, function() {
    if (s_pendingWeatherPayloadSignature === signature) {
      s_pendingWeatherPayloadSignature = null;
    }
  });
}
// ─────────────────────────────────────────────────────────────────────────────
// Location resolution
// ─────────────────────────────────────────────────────────────────────────────
function resolveLocation(callback) {
  // 1. Stored GPS coords
  if (s_useLatLon && s_storedLat !== null && s_storedLon !== null) {
    callback(null, s_storedLat, s_storedLon);
    return;
  }

  // 2. Custom location string
  if (s_customLocation && s_customLocation.trim().length > 0) {
    var loc = s_customLocation.trim();
    // Check if it's "lat,lon"
    var parts = loc.split(',');
    if (parts.length === 2 && !isNaN(parseFloat(parts[0])) && !isNaN(parseFloat(parts[1]))) {
      callback(null, parseFloat(parts[0]), parseFloat(parts[1]));
    } else {
      // Use cached geocode result if available for the same city
      if (s_storedLat !== null && s_storedLon !== null && s_useLatLon) {
        callback(null, s_storedLat, s_storedLon);
      } else {
        geocodeCity(loc, function(err, lat, lon) {
          if (!err) {
            // Cache the geocoded coordinates to avoid re-geocoding every 30 min
            s_storedLat = lat;
            s_storedLon = lon;
            s_useLatLon = true;
          }
          callback(err, lat, lon);
        });
      }
    }
    return;
  }

  // 3. GPS via navigator.geolocation
  if (navigator.geolocation) {
    navigator.geolocation.getCurrentPosition(
      function(pos) {
        s_storedLat = pos.coords.latitude;
        s_storedLon = pos.coords.longitude;
        s_useLatLon = true;
        // Reverse geocode to get city name
        reverseGeocode(s_storedLat, s_storedLon, function(err2, cityName) {
          if (cityName) { s_resolvedCityName = cityName; } else { s_resolvedCityName = ""; }
          callback(null, s_storedLat, s_storedLon);
        });
      },
      function(err) {
        console.log('GPS error: ' + err.message + ', falling back to IP');
        // 4. IP fallback
        ipGeolocate(function(err2, lat, lon) {
          if (!err2) {
            // Cache the successful IP fallback for subsequent scheduled updates
            // in this companion session, avoiding repeated GPS timeouts.
            s_storedLat = lat;
            s_storedLon = lon;
            s_useLatLon = true;
            reverseGeocode(lat, lon, function(err3, cityName) {
              if (cityName) { s_resolvedCityName = cityName; } else { s_resolvedCityName = ""; }
              callback(null, lat, lon);
            });
          } else {
            callback(err2);
          }
        });
      },
      { timeout: 15000 }
    );
  } else {
    // 4. IP fallback
    ipGeolocate(function(err2, lat, lon) {
      if (!err2) {
        // Cache the successful IP fallback for subsequent scheduled updates in
        // this companion session.
        s_storedLat = lat;
        s_storedLon = lon;
        s_useLatLon = true;
        reverseGeocode(lat, lon, function(err3, cityName) {
          if (cityName) { s_resolvedCityName = cityName; } else { s_resolvedCityName = ""; }
          callback(null, lat, lon);
        });
      } else {
        callback(err2);
      }
    });
  }
}

function finishWeatherFetch() {
  s_weatherFetchInFlight = false;
  if (s_weatherFetchQueued) {
    s_weatherFetchQueued = false;
    doWeatherFetch();
  }
}

function doWeatherFetch() {
  if (s_weatherFetchInFlight) {
    s_weatherFetchQueued = true;
    return;
  }
  s_weatherFetchInFlight = true;
  resolveLocation(function(err, lat, lon) {
    if (err) {
      console.log('Location error: ' + err);
      finishWeatherFetch();
      return;
    }
    fetchWeather(lat, lon, finishWeatherFetch);
  });
}

function updateWeatherInterval(mins) {
  if (mins < 15) mins = 30; // safety minimum
  if (mins === s_currentWeatherInterval && s_weatherIntervalId !== null) return;
  console.log('Updating weather interval to ' + mins + ' minutes');
  s_currentWeatherInterval = mins;
  if (s_weatherIntervalId !== null) {
    clearInterval(s_weatherIntervalId);
  }
  s_weatherIntervalId = setInterval(doWeatherFetch, mins * 60 * 1000);
}

// ─────────────────────────────────────────────────────────────────────────────
// Config / settings bridge
// ─────────────────────────────────────────────────────────────────────────────
function sendSettingsToWatch(settings, previousSettings) {
  // Test buttons are deliberately one-key messages and never enter saved state.
  if (settings.KEY_TEST_BATTERY_ALERT) {
    var msg144 = {};
    msg144[KEY.TEST_BATTERY_ALERT] = 1;
    enqueueAppMessage(msg144, 'Test low battery alert');
    return {};
  }
  if (settings.KEY_TEST_BT_DISCONNECT) {
    var msg145 = {};
    msg145[KEY.TEST_BT_DISCONNECT] = 1;
    enqueueAppMessage(msg145, 'Test Bluetooth disconnect');
    return {};
  }
  if (settings.KEY_TEST_CRITICAL_BATTERY_ALERT) {
    var msg146 = {};
    msg146[KEY.TEST_CRITICAL_BATTERY_ALERT] = 1;
    enqueueAppMessage(msg146, 'Test critical battery alert');
    return {};
  }

  previousSettings = previousSettings || {};
  var completeSettings = persistFullSettings(settings);
  var changedSettings = {};
  var msg = {};

  Object.keys(SETTINGS_KEY_MAP).forEach(function(strKey) {
    if (!completeSettings.hasOwnProperty(strKey)) return;
    var nextValue = normalizedSettingValue(strKey, completeSettings[strKey]);
    var hadPrevious = previousSettings.hasOwnProperty(strKey);
    var previousValue = hadPrevious
      ? normalizedSettingValue(strKey, previousSettings[strKey]) : undefined;
    if (!hadPrevious || previousValue !== nextValue) {
      changedSettings[strKey] = nextValue;
    }
    // Always send the complete saved configuration. The watchface can be
    // reloaded or replaced while the companion's cached snapshot remains
    // intact; comparing against that cache would make the first Save a no-op
    // and leave the watch on defaults.
    msg[SETTINGS_KEY_MAP[strKey]] = nextValue;
  });

  if (changedSettings.KEY_CUSTOM_LOCATION !== undefined) {
    setCustomLocation(changedSettings.KEY_CUSTOM_LOCATION);
  }
  if (completeSettings.KEY_WEATHER_INTERVAL !== undefined) {
    var interval = parseInt(completeSettings.KEY_WEATHER_INTERVAL, 10);
    var previousInterval = previousSettings.KEY_WEATHER_INTERVAL !== undefined
      ? parseInt(previousSettings.KEY_WEATHER_INTERVAL, 10) : undefined;
    if (previousInterval !== interval) {
      changedSettings.KEY_WEATHER_INTERVAL = interval;
      localStorage.setItem('weather_interval', interval);
      updateWeatherInterval(interval);
    }
  }

  if (Object.keys(msg).length > 0) {
    enqueueAppMessage(msg, 'Settings changes');
  } else {
    console.log('Settings unchanged; skipping watch update');
  }
  return changedSettings;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pebble event handlers
// ─────────────────────────────────────────────────────────────────────────────
Pebble.addEventListener('ready', function(e) {
  console.log('PebbleKit JS ready');
  // Restore the complete per-user snapshot before the first weather request.
  var restored = loadFullSettings();
  var storedInterval = restored.KEY_WEATHER_INTERVAL !== undefined
    ? restored.KEY_WEATHER_INTERVAL : localStorage.getItem('weather_interval');
  if (storedInterval) {
    s_currentWeatherInterval = parseInt(storedInterval, 10);
  }
  var storedLocation = restored.KEY_CUSTOM_LOCATION !== undefined
    ? restored.KEY_CUSTOM_LOCATION : localStorage.getItem(CUSTOM_LOCATION_STORAGE_KEY);
  if (storedLocation !== null && storedLocation !== undefined) {
    setCustomLocation(storedLocation);
  }
  // Migrate users of earlier versions, which stored only these two JS-side
  // values, into the complete per-user settings snapshot.
  persistFullSettings({
    KEY_WEATHER_INTERVAL: s_currentWeatherInterval,
    KEY_CUSTOM_LOCATION: storedLocation || ''
  });
  doWeatherFetch();
  updateWeatherInterval(s_currentWeatherInterval);
});

Pebble.addEventListener('appmessage', function(e) {
  var payload = e.payload || {};
  if (payload[KEY.SETTINGS_SNAPSHOT]) {
    var snapshot = decodeWatchSettingsSnapshot(payload);
    if (snapshot.KEY_CUSTOM_LOCATION !== undefined) {
      setCustomLocation(snapshot.KEY_CUSTOM_LOCATION);
    }
    if (snapshot.KEY_WEATHER_INTERVAL !== undefined) {
      updateWeatherInterval(parseInt(snapshot.KEY_WEATHER_INTERVAL, 10));
    }
    if (s_waitingForSettingsSnapshot) {
      s_waitingForSettingsSnapshot = false;
      if (s_settingsSnapshotTimer) clearTimeout(s_settingsSnapshotTimer);
      s_settingsSnapshotTimer = null;
      openConfigurationPage(snapshot);
    }
    return;
  }
  console.log('AppMessage from watch: ' + JSON.stringify(payload));
});

function requestSettingsSnapshot() {
  var request = {};
  request[KEY.REQUEST_SETTINGS] = 1;
  enqueueAppMessage(request, 'Settings snapshot request');
}

Pebble.addEventListener('showConfiguration', function(e) {
  if (s_waitingForSettingsSnapshot) return;

  var cachedSnapshot = loadFullSettings();
  if (Object.keys(cachedSnapshot).length > 0) {
    // The companion already holds the user's complete saved configuration, so
    // open Settings immediately. Refresh it from the watch in the background
    // so a later opening also reflects any direct watch-side changes.
    openConfigurationPage(cachedSnapshot);
    requestSettingsSnapshot();
    return;
  }

  // First run (or a legacy upgrade) has no local full snapshot. Briefly wait
  // for the watch's authoritative values, then preserve the safe fallback.
  s_waitingForSettingsSnapshot = true;
  s_settingsSnapshotTimer = setTimeout(function() {
    if (!s_waitingForSettingsSnapshot) return;
    console.log('Initial settings snapshot timed out; opening fallback settings');
    s_waitingForSettingsSnapshot = false;
    s_settingsSnapshotTimer = null;
    openConfigurationPage(loadFullSettings());
  }, 650);

  requestSettingsSnapshot();
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e.response && e.response.length > 0) {
    try {
      var payload = JSON.parse(decodeURIComponent(e.response));

      var isTestAction = payload.KEY_TEST_BATTERY_ALERT ||
                         payload.KEY_TEST_BT_DISCONNECT ||
                         payload.KEY_TEST_CRITICAL_BATTERY_ALERT;
      var previousSettings = loadFullSettings();
      var changedSettings = sendSettingsToWatch(payload, previousSettings);

      // Preserve the immediate refresh behavior, but only when a weather-source
      // setting truly changed. Unit changes need no fetch because both C/F values
      // are already maintained on the watch.
      var weatherSourceChanged = changedSettings.KEY_CUSTOM_LOCATION !== undefined ||
                                 changedSettings.KEY_WEATHER_INTERVAL !== undefined;
      if (!isTestAction && weatherSourceChanged) doWeatherFetch();

    } catch (err) {
      console.log('Config parse error: ' + err);
    }
  }
});
