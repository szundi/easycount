var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var REQUEST_TIMEOUT_MS = 12000;
var RESPONSE_TEXT_MAX_BYTES = 240;
var requestStarted = false;
var requestFinished = false;

function getActionUrl() {
  var settings;
  try {
    settings = JSON.parse(localStorage.getItem('clay-settings')) || {};
  } catch (error) {
    console.log('Invalid saved settings: ' + error.message);
    return '';
  }

  if (typeof settings.ACTION_URL !== 'string') {
    return '';
  }
  return settings.ACTION_URL.replace(/^\s+|\s+$/g, '');
}

function sendResult(status, value, attempt) {
  var message = { STATUS: status };
  if (typeof value === 'string') {
    message.VALUE = value;
  }

  Pebble.sendAppMessage(
    message,
    function() {},
    function() {
      if (attempt < 2) {
        setTimeout(function() {
          sendResult(status, value, attempt + 1);
        }, 250);
      }
    }
  );
}

function finish(status, value) {
  if (requestFinished) {
    return;
  }
  requestFinished = true;
  sendResult(status, value, 0);
}

function utf8CharacterSize(text, index) {
  var first = text.charCodeAt(index);
  if (first < 0x80) {
    return { bytes: 1, units: 1 };
  }
  if (first < 0x800) {
    return { bytes: 2, units: 1 };
  }
  if (first >= 0xd800 && first <= 0xdbff && index + 1 < text.length) {
    var second = text.charCodeAt(index + 1);
    if (second >= 0xdc00 && second <= 0xdfff) {
      return { bytes: 4, units: 2 };
    }
  }
  return { bytes: 3, units: 1 };
}

function utf8CutIndex(text, maxBytes) {
  var bytes = 0;
  var index = 0;
  while (index < text.length) {
    var character = utf8CharacterSize(text, index);
    if (bytes + character.bytes > maxBytes) {
      break;
    }
    bytes += character.bytes;
    index += character.units;
  }
  return index;
}

function truncateUtf8(text, maxBytes) {
  var index = utf8CutIndex(text, maxBytes);
  if (index === text.length) {
    return text;
  }

  return text.substring(0, utf8CutIndex(text, maxBytes - 3)) + '\u2026';
}

function responseTextForWatch(responseText) {
  var text = String(responseText).replace(/^\s+|\s+$/g, '');
  return truncateUtf8(text, RESPONSE_TEXT_MAX_BYTES);
}

function performRequest() {
  var actionUrl = getActionUrl();
  if (!actionUrl) {
    finish(-1);
    return;
  }

  var request = new XMLHttpRequest();
  request.onload = function() {
    var status = request.status || 0;
    if (status >= 200 && status < 300) {
      finish(status, responseTextForWatch(request.responseText));
      return;
    }
    finish(status);
  };
  request.onerror = function() {
    finish(0);
  };
  request.ontimeout = function() {
    finish(0);
  };
  request.open('GET', actionUrl, true);
  request.timeout = REQUEST_TIMEOUT_MS;
  request.send(null);
}

Pebble.addEventListener('showConfiguration', function() {
  Pebble.openURL(clay.generateUrl());
});

Pebble.addEventListener('webviewclosed', function(event) {
  if (event && event.response) {
    try {
      clay.getSettings(event.response, false);
    } catch (error) {
      console.log('Could not save settings: ' + error.message);
    }
  }

  Pebble.sendAppMessage({ CONFIG_DONE: 1 });
});

Pebble.addEventListener('appmessage', function(event) {
  if (!event.payload.REQUEST || requestStarted) {
    return;
  }
  requestStarted = true;
  performRequest();
});
