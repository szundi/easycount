var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var REQUEST_TIMEOUT_MS = 12000;
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
  if (typeof value === 'number') {
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

function parseResponseValue(responseText) {
  var text = String(responseText).replace(/^\s+|\s+$/g, '');
  if (!/^[+-]?\d+$/.test(text)) {
    return null;
  }

  var value = Number(text);
  if (value < -2147483648 || value > 2147483647) {
    return null;
  }
  return value;
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
      var value = parseResponseValue(request.responseText);
      if (value === null) {
        finish(-5);
        return;
      }
      finish(status, value);
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
