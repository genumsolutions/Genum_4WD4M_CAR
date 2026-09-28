// =====================================================================
// Genum_4WD4M_CAR — multimode 4WD4M car firmware (ESP32 + L298N)
// File: WebPage.h — static browser control page served at / (PROGMEM)
// Property of GENUM SOLUTIONS PVT. LTD.
// =====================================================================
#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <pgmspace.h>

// T-40 (round-3): fit-to-viewport, no scroll, no overflow. The single status
// strip shows Status / Mode / Conn (RSSI/uptime/heap dropped per owner);
// the ONLY speed readout sits beside the big slider (the duplicate id="speed"
// meant the WS update hit the first element — the slider span never moved).
// T-42: ESP_CLI stays "work in progress" on the page. T-43: title matches the
// folder-derived project name.
// ROUND-6: added Uptime / Heap / SSID back to the info strip (JSON already
// carried uptime_ms, free_heap, ssid); saved-router <select> now diff-rebuilds
// (no flicker while the list or the page is focused); slider keeps its 100..255
// car-floor range with explicit min/max labels.
// T-67 (round-9): closed select 13px, open option 10px (owner: "too small" vs
// "too big"). T-68 (round-9): landscape media query scaled the whole page into
// the folded-keyboard window.
// ROUND-11 (T-69/T-70): the page is rebuilt MOBILE-FIRST with a single
// `@media (orientation:landscape)` breakpoint — it supersedes the old
// max-height:440 block and the pure-shrink approach (owner: buttons were still
// covering the data in landscape). Portrait = compact 2-row data ribbons above
// the D-pad; landscape = a 3-column stage `[left data | control cross | right
// data]` so the readouts move to the SIDES of the control area and every row
// becomes a flex:1 band so the drive tiles grow as large as the window allows.
// Dropdowns: collapsed selects are now ~44px (portrait) / 38px (landscape) tap
// targets with larger text; the open option items are compact (10px, tight
// line-height) so ALL 9 mode entries are visible without scrolling the list.
// Data readouts share one type scale with tabular numerals. No JS contract
// change: every element id the script addresses is unchanged.
static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset='utf-8'/>
  <meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'/>
  <title>4WD4M Car</title>
  <style>
    *{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
    html,body{height:100%;margin:0;overflow:hidden;background:#111;color:#eee;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,Helvetica,sans-serif;line-height:1.5}
    body{display:flex;justify-content:center}
    .frame{max-width:480px;width:100%;height:100vh;height:100dvh;display:flex;flex-direction:column;padding:4px 10px 6px;overflow:hidden}
    h1{font-size:18px;margin:4px 0 2px;text-align:center;font-weight:700;letter-spacing:-0.02em}
    .top-selects{display:flex;gap:6px;align-items:stretch;margin:6px 0 2px}
    /* T-70: the COLLAPSED selects are the tap targets — tall + larger text. */
    select{flex:1;min-width:0;height:46px;padding:0 10px;border-radius:6px;border:1px solid #555;background:#222;color:#fff;font-size:15px;text-overflow:ellipsis}
    /* T-70: the OPEN option list is compact so all 9 modes are visible at once. */
    select option{font-size:10px;padding:0 4px;line-height:1.25}
    .stage{flex:1;min-height:0;display:flex;flex-direction:column}
    /* Data ribbons (portrait) / side columns (landscape). One type scale. */
    .side{display:flex;flex-wrap:wrap;gap:2px 12px;padding:2px 2px 5px;border-bottom:none;font-size:11px;color:#aab}
    .side span{display:inline-flex;align-items:baseline;gap:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:100%}
    .side b{color:#fff;font-weight:bold;font-variant-numeric:tabular-nums}
    .side-l{justify-content:flex-start;border-bottom:1px solid #333}
    .side-r{justify-content:flex-end}
    .controls{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:0;padding:4px 0}
    .row{display:flex;justify-content:center;align-items:center;margin:4px 0}
    .btn{width:clamp(80px,26vmin,132px);height:clamp(72px,24vmin,124px);margin:4px;padding:0;color:#fff;border:2px solid #666;border-radius:18px;font-size:20px;font-weight:bold;display:flex;align-items:center;justify-content:center;user-select:none;touch-action:none;background:linear-gradient(180deg,#2e2e2e,#1a1a1a);box-shadow:0 3px 0 #000,0 5px 12px rgba(0,0,0,.5);text-shadow:0 1px 2px rgba(0,0,0,.6)}
    .btn.pressed{background:linear-gradient(180deg,#0a0,#060);border-color:#0f0;transform:translateY(2px);box-shadow:0 1px 0 #000}
    .btn.stop{background:linear-gradient(180deg,#8a1a1a,#5a0d0d);border-color:#ff5a5a}
    .btn.stop.pressed{background:#500;border-color:#ff9090}
    .range-wrap{display:flex;align-items:center;gap:8px;width:100%;margin:4px 0 2px}
    .range-wrap input[type=range]{flex:1;-webkit-appearance:none;appearance:none;height:10px;border-radius:5px;border:1px solid #444;background:#333}
    .range-wrap input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:34px;height:34px;border-radius:50%;background:#3b82f6;border:2px solid #fff;box-shadow:0 2px 6px rgba(0,0,0,.5)}
    .range-wrap input[type=range]::-moz-range-thumb{width:32px;height:32px;border-radius:50%;background:#3b82f6;border:2px solid #fff}
    .range-wrap input[type=range]::-moz-range-track{height:10px;border-radius:5px;background:#333}
    .range-wrap .val{min-width:44px;text-align:center;font-size:16px;font-weight:bold;color:#fff;font-variant-numeric:tabular-nums}
    .range-wrap .lim{min-width:26px;text-align:center;font-size:12px;color:#aaa}
    .small{font-size:11px;color:#aaa;margin-top:2px;text-align:center}
     /* ROUND-11 (T-69): LANDSCAPE — data to the sides, buttons max-size.
        The old max-height:440 shrink (T-68) is superseded: the whole page
        takes a 3-column stage so the readouts never sit under the controls.
        Overflow protection: constrain the stage height and allow scrolling
        only if content truly exceeds the viewport. */
     @media (orientation:landscape){
       .frame{max-width:none;padding:2px 6px;overflow:hidden}
       h1{font-size:13px;margin:2px 0 0;font-weight:700}
       .top-selects{margin:4px 0 2px}
       select{height:38px;font-size:13px;padding:0 8px;font-weight:600}
       .stage{flex-direction:row;align-items:stretch;gap:8px;min-height:0;overflow:hidden}
       .side{flex:0 0 clamp(84px,24vw,170px);flex-direction:column;flex-wrap:nowrap;justify-content:center;gap:5px;font-size:11px;color:#aab;border-bottom:none;padding:0;min-width:0;font-weight:500}
       .side span{display:flex;justify-content:space-between;gap:4px}
       .side b{color:#fff;font-weight:600}
       .controls{justify-content:center;align-items:center;padding:2px 0}
       .row{flex:1;width:100%;margin:2px 0;align-items:stretch}
       .btn{flex:1;max-width:150px;height:100%;margin:2px;border-radius:14px;font-size:14px;font-weight:700}
       .range-wrap{margin:2px 0}
       .range-wrap .val{min-width:38px;font-size:13px;font-weight:700}
       .range-wrap .lim{font-size:10px;color:#888}
       .small{font-size:9px;color:#888;margin-top:1px}
    }
  </style>

  <script>
    let ws, lastS=0, lastNetSig='';
    function initWebSocket(){
      ws = new WebSocket("ws://" + window.location.hostname + ":81/");
      ws.onopen = () => {
        document.getElementById("conn").innerText="Connected";
        sendCmd("S");
        sendCmd("REQ_NETW");
      };
      ws.onclose = () => { document.getElementById("conn").innerText="Disconnected"; setTimeout(initWebSocket, 3000); };
      ws.onmessage = (evt) => {
        try {
          let j = JSON.parse(evt.data);
          if(j.status){
            document.getElementById('status').innerText=j.status;
            document.getElementById('mode').innerText=j.mode_name || j.mode;
            document.getElementById('conn').innerText=(j.connected === true || j.connected === 'true') ? 'Connected' : 'Disconnected';
            syncSpeed(j.speed);
            document.getElementById('ip').innerText=j.ip || '--';
            document.getElementById('signal').innerText=(j.signal!=null)?j.signal+'%':'--';
            document.getElementById('ssid').innerText=j.ssid || '--';
            var up=j.uptime_ms||0, upS=Math.floor(up/1000);
            var mm=String(Math.floor(upS/60)).padStart(2,'0');
            var ss=String(upS%60).padStart(2,'0');
            document.getElementById('uptime').innerText=mm+':'+ss;
            document.getElementById('heap').innerText=(j.free_heap!=null)?Math.round(j.free_heap/1024)+' KB':'--';
            // Round-6 flicker fix: rebuild the saved-router selector ONLY when
            // the list actually changed, and never while the user has the
            // dropdown open (open list + rebuild = flicker).
            if(Array.isArray(j.networks)){
              var sel=document.getElementById('r');
              if(sel){
                var cur=sel.value;
                var sig=j.networks.join('\u001f');
                if(sig!==lastNetSig && document.activeElement!==sel){
                  lastNetSig=sig;
                  while(sel.options.length>1) sel.remove(1);
                  j.networks.forEach(function(n){
                    var o=document.createElement('option');
                    o.value=n; o.text=n;
                    if(n===cur) o.selected=true;
                    sel.add(o);
                  });
                }
              }
            }
          }
        } catch(e){ /* ignore non-JSON frames */ }
      };
    }
    function sendCmd(cmd){
      if(ws && ws.readyState===WebSocket.OPEN) ws.send(cmd);
    }
    var holdInterval = null;
    function pressDir(dir, el){
      el.classList.add('pressed');
      var cmd = dir.toUpperCase().charAt(0);
      sendCmd(cmd);
      if (holdInterval) clearInterval(holdInterval);
      holdInterval = setInterval(function(){ sendCmd(cmd); }, 300);
    }
    function releaseDir(el){
      el.classList.remove('pressed');
      if (holdInterval) { clearInterval(holdInterval); holdInterval = null; }
      sendCmd("S");
    }
    function attachHold(id, dir){
      var el = document.getElementById(id);
      if(!el) return;
      el.addEventListener('mousedown', function(e){ e.preventDefault(); pressDir(dir, el); });
      el.addEventListener('mouseup',   function(e){ e.preventDefault(); releaseDir(el); });
      el.addEventListener('mouseleave',function(e){ e.preventDefault(); releaseDir(el); });
      el.addEventListener('touchstart',function(e){ e.preventDefault(); pressDir(dir, el); }, {passive:false});
      el.addEventListener('touchend',  function(e){ e.preventDefault(); releaseDir(el); }, {passive:false});
      el.addEventListener('touchcancel',function(e){ e.preventDefault(); releaseDir(el); }, {passive:false});
    }
    var lastSlider=0;
    // T-51 slider throttle + T-61 (round-8): the DISPLAYED speed value and the
    // slider thumb are always the same number. The car's reported speed (JSON
    // `speed`) drives BOTH: the thumb snaps to the exact car truth so a change
    // made from the app / another browser stays in sync (the old code only
    // moved the number, leaving the thumb at its stale position). The 100..255
    // car floor and the 5-step grid are kept. Not fighting the user: JSON sync
    // is skipped while the slider itself is being dragged (focused).
    function syncSpeed(sp){
      let v = parseInt(sp, 10);
      if(isNaN(v)) return;
      if(v < 100) v = 100;
      if(v > 255) v = 255;
      v = Math.round(v / 5) * 5;
      var s = document.getElementById('s');
      var speedEl = document.getElementById('speed');
      if(speedEl) speedEl.innerText = v;
      if(s && document.activeElement !== s) s.value = v;
    }
    function setSpeed(){
      var now=Date.now();
      if(now-lastSlider<80) return;   // T-51: ~80 ms slider throttle
      lastSlider=now;
      let v=parseInt(document.getElementById('s').value,10);
      if(v < 100) v = 100;
      if(v > 255) v = 255;
      sendCmd("#"+v);
      syncSpeed(v);
    }
    function setMode(){
      let sel = document.getElementById('m');
      sendCmd(sel.value);
    }
    function setRouter(){
      let sel = document.getElementById('r');
      if(sel && sel.value) sendCmd("ROUTERS;USE;"+sel.value);
    }
    window.onload=function(){
      setTimeout(initWebSocket, 300);  // T-51: 300 ms WS startup delay (avoids reconnect burst)
      attachHold('btn-forward','forward');
      attachHold('btn-left','left');
      attachHold('btn-stop','stop');
      attachHold('btn-right','right');
      attachHold('btn-back','backward');
      document.getElementById('m').value = 'MODE_ESP_SERVER';
      setMode();
      document.getElementById('m').addEventListener('change', setMode);
      var r = document.getElementById('r');
      if(r) r.addEventListener('change', setRouter);
      var s = document.getElementById('s');
      if(s) s.addEventListener('input', function(){ setSpeed(); });
    };
  </script>
</head>
<body>
  <div class='frame'>
    <h1>4WD4M Car</h1>
    <div class='top-selects'>
      <select id='m'>
        <option value='MODE_BT'>4WD4M</option>
        <option value='MODE_ESP_SERVER'>ESP32 (WS)</option>
        <option value='MODE_ESP_CLIENT'>ESP32 (CLIENT)</option>
        <option value='MODE_PATH'>PATH</option>
        <option value='MODE_OBS_US'>OBS-US</option>
        <option value='MODE_OBS_IR'>OBS-IR</option>
        <option value='MODE_MANUAL'>MANUAL</option>
        <option value='MODE_AUTO'>AUTO</option>
        <option value='MODE_2WD1M'>2WD1M</option>
      </select>
      <select id='r'>
        <option value='' disabled selected>— Select router —</option>
      </select>
    </div>

    <!-- ROUND-11 (T-69): the stage holds the data SIDES + the control cross.
         Portrait: both ribbons render above the D-pad; landscape: they flank it. -->
    <div class='stage'>
      <aside class='side side-l'>
        <span>Status: <b id='status'>...</b></span>
        <span>Mode: <b id='mode'>...</b></span>
        <span>Signal: <b id='signal'>--</b></span>
        <span>Uptime: <b id='uptime'>--:--</b></span>
      </aside>
      <div class='controls'>
        <div class='row'><div id='btn-forward' class='btn'>FWD</div></div>
        <div class='row'>
          <div id='btn-left' class='btn'>LFT</div>
          <div id='btn-stop' class='btn stop'>STOP</div>
          <div id='btn-right' class='btn'>RGT</div>
        </div>
        <div class='row'><div id='btn-back' class='btn'>BWD</div></div>
      </div>
      <aside class='side side-r'>
        <span>IP: <b id='ip'>--</b></span>
        <span>SSID: <b id='ssid'>--</b></span>
        <span>Heap: <b id='heap'>--</b></span>
        <span>Conn: <b id='conn'>...</b></span>
      </aside>
    </div>

    <div class='range-wrap'>
      <label class='lim'>100</label>
      <input id='s' type='range' min='100' max='255' step='5' value='170' aria-label='Speed'>
      <span id='speed' class='val'></span>
      <label class='lim'>255</label>
    </div>
    <p class='small'>Speed 100–255 (car floor). Hold a button to move; release to stop.</p>
  </div>
</body>
</html>
)rawliteral";

#endif // WEBPAGE_H