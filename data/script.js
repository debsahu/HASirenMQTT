// node-minify --compressor uglify-es --input 'script.js' --output 'script.min.js'

var wsc;

document.addEventListener("DOMContentLoaded", function(){
    wsc = new ReconnectingWebSocket('ws://' + window.location.hostname + ':81');
    wsc.timeoutInterval = 3000;

    wsc.onopen = function (e) {
        console.log("WebSocketClient connected:", e);
    }

    wsc.onmessage = function (data, flags, number) {
        console.log('WebSocketClient message #${number}: ', data);
        var switchstatus = JSON.parse(data.data);
        if(switchstatus.siren1 == "ON") {
            document.getElementById("onoff1").checked = true;
        } else {
            document.getElementById("onoff1").checked = false;
        }
    }

    document.getElementById("onoff1").onchange = function () {
        var JsonData;
        if (document.getElementById("onoff1").checked) {
            JsonData = { siren: 1, state: "ON" };
        } else {
            JsonData = { siren: 1, state: "OFF" };
        }
        wsc.send(JSON.stringify(JsonData));
    }

});