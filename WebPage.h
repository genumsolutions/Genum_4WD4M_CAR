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
    .frame{max-width:1100px;width:100%;height:100vh;height:100dvh;display:flex;flex-direction:column;padding:4px 10px 6px;overflow:hidden}
    h1{font-size:18px;margin:4px 0 2px;text-align:center;font-weight:700;letter-spacing:-0.02em}
    .top-selects{display:flex;gap:6px;align-items:stretch;margin:6px 0 2px}
    /* T-70: the COLLAPSED selects are the tap targets — tall + larger text. */
    select{flex:1;min-width:0;height:46px;padding:0 10px;border-radius:6px;border:1px solid #555;background:#222;color:#fff;font-size:15px;text-overflow:ellipsis}
    /* T-70: the OPEN option list is compact so all 9 modes are visible at once. */
    select option{font-size:10px;padding:0 4px;line-height:1.25}
    .stage{flex:1;min-height:0;display:flex;flex-direction:column;overflow:hidden}
    /* Data ribbons (portrait) / side columns (landscape). One type scale. */
    .side{display:flex;flex-wrap:wrap;gap:2px 12px;padding:2px 2px 5px;border-bottom:none;font-size:11px;color:#aab}
    .side span{display:inline-flex;align-items:baseline;gap:4px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:100%}
    .side b{color:#fff;font-weight:bold;font-variant-numeric:tabular-nums}
    .side-l{justify-content:flex-start;border-bottom:1px solid #333}
    .side-r{justify-content:flex-end}
    .controls{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;min-height:0;padding:4px 0}
    /* S-15 (owner 2026-11-23: *"add one more d-pad in its own d-pad … and the
       website should mirror the function of two d-pad just like the 2wd1m mode of
       the apps remote screen … also do same for the 4wd4m car too"*): TWO pads
       side by side in the middle of the stage, matching the app's DualDpad. The
       data columns on either side tighten to donate the space; nothing else
       moves. */
    /* S-16 (owner 2026-11-26): the crosses sat in the middle of the stage, out of
       thumb reach while holding the phone at the ends. Each pad owns its half and
       sits at that half's outer edge; buttons keep their size. */
    .pads{display:flex;flex:1;min-height:0;width:100%;align-items:stretch;justify-content:space-between;gap:clamp(6px,2vw,18px);padding:0 clamp(2px,2vw,18px);box-sizing:border-box}
    .pad{display:flex;flex-direction:column;flex:1;min-width:0;max-width:430px;align-items:stretch}
    /* The 4WD4M twin is present but INERT, exactly like enabledZones(false) in
       the app: one full cross on the left, a second pad drawn so the layout
       matches the 2WD1M/deck shape, and no command can ever come from it. */
    .pad.inert{opacity:.26;pointer-events:none;filter:grayscale(1)}
    .row{display:flex;justify-content:center;align-items:center;margin:4px 0}
    .speed-row{margin:6px 0 2px}
    .btn{width:clamp(80px,26vmin,132px);height:clamp(72px,24vmin,124px);margin:4px;padding:0;color:#fff;border:2px solid #666;border-radius:18px;font-size:20px;font-weight:bold;display:flex;align-items:center;justify-content:center;user-select:none;touch-action:none;background:linear-gradient(180deg,#2e2e2e,#1a1a1a);box-shadow:0 3px 0 #000,0 5px 12px rgba(0,0,0,.5);text-shadow:0 1px 2px rgba(0,0,0,.6)}
    .btn.pressed{background:linear-gradient(180deg,#0a0,#060);border-color:#0f0;transform:translateY(2px);box-shadow:0 1px 0 #000}
    .btn.stop{background:linear-gradient(180deg,#8a1a1a,#5a0d0d);border-color:#ff5a5a}
    .btn.stop.pressed{background:#500;border-color:#ff9090}
    .range-wrap{display:flex;align-items:center;gap:8px;width:100%;margin:4px 0 2px}
    .range-wrap input[type=range]{flex:1;min-width:0;-webkit-appearance:none;appearance:none;height:10px;border-radius:5px;border:1px solid #444;background:#333}
    .range-wrap input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:34px;height:34px;border-radius:50%;background:#3b82f6;border:2px solid #fff;box-shadow:0 2px 6px rgba(0,0,0,.5)}
    .range-wrap input[type=range]::-moz-range-thumb{width:32px;height:32px;border-radius:50%;background:#3b82f6;border:2px solid #fff}
    .range-wrap input[type=range]::-moz-range-track{height:10px;border-radius:5px;background:#333}
    .range-wrap .val{min-width:44px;text-align:center;font-size:16px;font-weight:bold;color:#fff;font-variant-numeric:tabular-nums}
    .range-wrap .lim{min-width:26px;text-align:center;font-size:12px;color:#aaa}
    .small{font-size:11px;color:#aaa;margin-top:2px;text-align:center}
    /* S-14/S-15 orientation ruling (owner 2026-11-23: *"dont have potrait at all
       only gave landscape only by default"*): there IS no portrait layout. A
       portrait window gets this notice and nothing else, so the page can never
       render its controls in an arrangement the owner did not approve. */
    .rotate{display:none;flex-direction:column;align-items:center;justify-content:center;gap:10px;height:100vh;height:100dvh;padding:24px;text-align:center}
    .rotate svg{width:72px;height:72px;opacity:.85}
    .rotate b{font-size:17px}
    .rotate span{font-size:12px;color:#aaa;max-width:34ch}
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
        /* U-142: a long SSID / IP / REPLY= used to grow past this column instead
           of ellipsising, because a flex item defaults to min-width:auto. Clip
           and ellipsise so every value stays inside the pane. */
        .side span{display:flex;justify-content:space-between;gap:4px;min-width:0;overflow:hidden;white-space:nowrap;text-overflow:ellipsis}
        .side b{color:#fff;font-weight:600;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;text-align:right}
        .controls{justify-content:center;align-items:center;padding:2px 0}
        .row{flex:1;width:100%;margin:2px 0;align-items:stretch}
        .speed-row{flex:0 0 auto;margin:4px 0 2px}
        /* S-15: smaller max width per tile so two pads fit the middle band on a
           phone in landscape; heights still come from the flex rows, so the
           tiles grow with the window on a tablet or a desktop. */
        .btn{flex:1;max-width:120px;height:100%;margin:2px;border-radius:14px;font-size:14px;font-weight:700}
        .range-wrap{margin:0}
        .range-wrap .val{min-width:38px;font-size:13px;font-weight:700}
        .range-wrap .lim{font-size:10px;color:#888}
        .small{font-size:9px;color:#888;margin-top:1px}
      }
@media (orientation:portrait){
        /* The rotate notice is the whole portrait page. */
        .frame{display:none}
        .rotate{display:flex}
      }
  </style>

  <script>
    let ws, lastS=0, lastNetSig='', lastCapsSig='';
    // U-81: render the car's own answer, in the car's own vocabulary, and never
    // invent a success. `ROUTERS;ADDED;x` / `WIFICFG;OK;x` are confirmations;
    // `ROUTERS;FULL` / `…;Reserved` / `…;Password too long` / `…;SSID length`
    // / `…;Syntax` / `…;Not saved` are refusals and are shown AS refusals.
    // A NACK means the car did not accept the mode that was just tapped.
    function note(txt, good){
      var el = document.getElementById('note');
      if(!el) return;
      el.innerText = txt;
      el.style.color = good ? '#0a7a4a' : '#b3261e';
    }
    function handleTextLine(raw){
      var l = raw.trim();
      if(!l) return;
      // NACK;E=UNKNOWN_MODE;ARG=<token>
      if(l.indexOf('NACK;') === 0 || l.indexOf('NACK:') === 0){
        var arg = (l.split(/[;:]/)[2] || '').replace(/^ARG=/,'');
        note('The car refused mode "' + (arg || '?') + '" - not supported by this car.', false);
        return;
      }
      // STATE;...;REPLY=<the answer to what this page asked>
      var eq = l.indexOf('REPLY=');
      if(eq < 0) return;
      var r = l.slice(eq + 6).trim();
      if(!r) return;
      if(r.indexOf('ROUTERS;ADDED;') === 0){
        // Only a SAVE is confirmed here. The firmware confirms a switch with
        // ROUTERS;USED, so this must not claim one - the page would be telling
        // the user their phone is about to change networks on a promise.
        note('Saved on the car: ' + r.slice(14) + '.', true);
        return;
      }
      if(r.indexOf('ROUTERS;USED;') === 0){
        note('Car switched to ' + r.slice(13) + '.', true);
        return;
      }
      if(r.indexOf('ROUTERS;CLEARED') === 0){ note('All saved routers cleared.', true); return; }
      if(r.indexOf('ROUTERS;DELETED;') === 0){ note('Removed ' + r.slice(16) + '.', true); return; }
      if(r.indexOf('ROUTERS;SCAN;STARTED') === 0){ note('Scanning for networks...', true); return; }
      // U-95 Phase 2: the car answers BUSY when it did NOT start a scan (one
      // already in flight, or the STA is mid-join). A refusal must be shown
      // in the refusal colour - the old page would have waited forever on a
      // result that was never coming.
      if(r.indexOf('ROUTERS;SCAN;BUSY') === 0){ note('The car is busy - try the scan again in a moment.', false); return; }
      if(r.indexOf('ROUTERS;SCAN;CANCELLED') === 0){ note('Scan cancelled.', false); return; }
      if(r.indexOf('ROUTERS;FULL') === 0){
        note('The car already has 6 routers - delete one first.', false); return;
      }
      if(r.indexOf('ROUTERS;ERROR;') === 0){
        // The reason is the last field and is already human-readable
        // (Reserved / Password too long / SSID length / Syntax). `Not saved:x`
        // carries a colon and the name; read it as a sentence, not as
        // "refused: Not saved:x".
        var why = r.slice(14);
        var colon = why.indexOf(':');
        if(/^not saved/i.test(why) && colon >= 0){
          note('The car has not saved ' + why.slice(colon + 1).trim() + ' yet.', false);
          return;
        }
        note('Car refused: ' + why, false); return;
      }
      // U-92: the car answers WIFICFG;STORED;<ssid> (WebServerComm.cpp) —
      // the page used to look for WIFICFG;OK; and so NEVER confirmed a save
      // made through "Add router…".
      if(r.indexOf('WIFICFG;STORED;') === 0){ note('Saved on the car: ' + r.slice(15), true); return; }
      if(r.indexOf('WIFICFG;') === 0){
        var w = r.slice(8);
        if(w.indexOf('ERROR;') === 0) w = w.slice(6);
        note('Car refused: ' + w, false); return;
      }
      // A whole router list rides REPLY= too; the 1 s JSON networks array is
      // what actually repaints the dropdown, so there is nothing to say.
    }
    // U-95 Phase 3 (power): the old reconnect was a bare setTimeout(init, 3000)
    // — no backoff, no cap, no visibility handling — so a car whose AP is down
    // was re-dialled every 3 s forever, including from a background tab the
    // user had forgotten. The schedule now doubles 1s -> 15s, resets the moment
    // a socket opens, and a hidden tab schedules nothing at all: the
    // visibilitychange handler reconnects when the user actually comes back.
    var wsDelay = 0, wsTimer = null;
    function wsScheduleReconnect(){
      wsDelay = (wsDelay === 0) ? 1000 : Math.min(wsDelay * 2, 15000);
      if (wsTimer) clearTimeout(wsTimer);
      wsTimer = setTimeout(function(){ wsTimer = null; initWebSocket(); }, wsDelay);
    }
    function initWebSocket(){
      // One live (or opening) socket only — visibilitychange and the timer can
      // race, and two sockets double every 1 Hz frame on the wire.
      if (ws && ws.readyState <= 1) return;
      ws = new WebSocket("ws://" + window.location.hostname + ":81/");
      ws.onopen = () => {
        wsDelay = 0;
        if (wsTimer) { clearTimeout(wsTimer); wsTimer = null; }
        document.getElementById("conn").innerText="Connected";
        switchNote = '';
        sendCmd("S");
        sendCmd("REQ_NETW");
      };
      ws.onclose = () => {
        // U-79: during a router switch the drop is EXPECTED (the hotspot moves
        // to the router's channel). Say so instead of a bare "Disconnected".
        document.getElementById("conn").innerText = switchNote || "Disconnected";
        if (document.visibilityState === 'hidden') return;  // visibilitychange owns the return
        wsScheduleReconnect();
      };
      ws.onmessage = (evt) => {
        // U-81: the car now also sends TEXT lines on this socket — the STATE
        // line (with the router/WiFi answer in REPLY=), CAPS;, NETW;, SCAN; and
        // NACK;E=UNKNOWN_MODE. That egress was a sink until U-81, so every
        // answer this page asked for was discarded by the car and the page
        // could only ever guess. Text first, JSON second.
        if (typeof evt.data === 'string' && evt.data.charAt(0) !== '{') {
          handleTextLine(evt.data);
          return;
        }
        try {
          let j = JSON.parse(evt.data);
          if(j.status){
            document.getElementById('status').innerText=j.status;
            document.getElementById('mode').innerText=j.mode_name || j.mode;
            syncMode(j.mode);   // F-57: keep the dropdown equal to car truth
            // U-90 (owner override of the U-79 page gate): *"the mode toggle
            // doesnt allows all other comming soon modes to change through the
            // drop down menu, all those are dim please allow those comming
            // soon too that changes the modes on the car too"*. Every mode in
            // the registry is now SELECTABLE, coming-soon included - the car
            // answers for itself: an unknown token still earns
            // NACK;E=UNKNOWN_MODE and a refusal shows in the note area (F-62).
            // ESP_SER/ESP_CLI are gone from the registry entirely and simply
            // never appear. The (coming soon) label suffix stays so a stub
            // mode is never mistaken for a finished one - only the DISABLE
            // state is gone. Honest consequence, known to the owner: entering
            // a stub mode gates the drive controls until 4WD4M is picked
            // again; switching back is one dropdown tap.
            if (typeof j.caps === 'string') {
              var caps = {};
              j.caps.split(';').forEach(function (p) {
                var kv = p.split(':'); if (kv[0]) caps[kv[0]] = kv[1];
              });
              var msel = document.getElementById('m');
              // U-83: the mode list is rebuilt ONLY when the registry actually
              // changes, and never while the dropdown is open.
              //
              // This loop used to reassign o.text and o.disabled on every 1 Hz
              // frame. Writing to an <option> while its popup is open makes the
              // browser dismiss the popup, so the list flickered and taps landed
              // on the wrong row - the owner saw a mode chooser that could not
              // be used. The saved-router list below already guarded this with a
              // signature + activeElement check; the mode list did not, so the
              // one control that matters most was the one that fought the user.
              // Each write is also now conditional, so an unchanged registry
              // touches nothing at all.
              var csig = j.caps + '\u001f' + msel.options.length;
              if (msel && csig !== lastCapsSig && document.activeElement !== msel) {
                lastCapsSig = csig;
                for (var i = 0; i < msel.options.length; i++) {
                  var o = msel.options[i], canon = o.getAttribute('data-canon');
                  if (!canon) continue;
                  // U-90: information only - the option is NEVER disabled.
                  var txt = o.getAttribute('data-label') + (caps[canon] === 'LIVE' ? '' : ' (coming soon)');
                  if (o.disabled) o.disabled = false;
                  if (o.text !== txt) o.text = txt;
                }
              }
            }
            document.getElementById('conn').innerText=(j.connected === true || j.connected === 'true') ? 'Connected' : 'Disconnected';
            syncSpeed(j.speed);
            document.getElementById('ip').innerText=j.ip || '--';
            document.getElementById('signal').innerText=(j.signal!=null)?j.signal+'%':'--';
            document.getElementById('ssid').innerText=j.ssid || '--';
            // U-95 Phase 3: the fields the page used to ignore entirely — the
            // car's own hotspot name (j.ap) and the directed-broadcast address
            // Phase 3 made the car compute (WiFi.softAPBroadcastIP()).
            var apEl=document.getElementById('apname'); if(apEl) apEl.innerText=j.ap||'--';
            var bcEl=document.getElementById('bcast'); if(bcEl) bcEl.innerText=j.broadcast_ip||'--';
            var up=j.uptime_ms||0, upS=Math.floor(up/1000);
            var mm=String(Math.floor(upS/60)).padStart(2,'0');
            var ss=String(upS%60).padStart(2,'0');
            document.getElementById('uptime').innerText=mm+':'+ss;
            document.getElementById('heap').innerText=(j.free_heap!=null)?Math.round(j.free_heap/1024)+' KB':'--';
            // U-95/F-79: show the restart record only when there is one. The
            // gate is crashCount > 0, NOT the presence of the field — an absent
            // field on an un-updated car must render as "no data", never as a
            // fabricated zero. Phase (the operation the car died in) is appended
            // when present because the reason alone is rarely specific enough.
            var rstW=document.getElementById('rst-wrap');
            if(rstW){
              var cc=(j.crash_count!=null)?Number(j.crash_count):0;
              if(cc>0){
                rstW.style.display='';
                var rstTxt=(j.crash_count+'x '+ (j.reset_reason||'UNKNOWN'));
                if(j.last_crash_phase) rstTxt+=' / '+j.last_crash_phase;
                document.getElementById('rst').innerText=rstTxt;
              } else {
                rstW.style.display='none';
              }
            }
            // Round-6 flicker fix: rebuild the saved-router selector ONLY when
            // the list actually changed, and never while the user has the
            // dropdown open (open list + rebuild = flicker).
            if(Array.isArray(j.networks)){
              var sel=document.getElementById('r');
              if(sel){
                // U-92: the joined network rides in the signature too, so a
                // switch repaints the list even though the saved names are
                // unchanged.
                var sig=j.networks.join('\u001f') + '\u001f' + (j.ssid || '');
                if(sig!==lastNetSig && document.activeElement!==sel){
                  lastNetSig=sig;
                  while(sel.options.length>1) sel.remove(1);
                  // U-90 (F-57 applied to the router list): never preselect a
                  // network nothing has confirmed. The old rebuild re-selected
                  // whatever the select happened to show, which ASSERTED a
                  // switch request that was never made; the dropdown must sit
                  // on its neutral placeholder until the user acts on it.
                  j.networks.forEach(function(n){
                    var o=document.createElement('option');
                    o.value=n; o.text=n;
                    sel.add(o);
                  });
                  // U-79: the add-new-network entry, rebuilt with the list
                  // (kept OUT of the static markup so the diff-rebuild owns
                  // every option after the placeholder).
                  var add=document.createElement('option');
                  add.value='__add__'; add.text='＋ Add router…';
                  sel.add(add);
                  // U-98: add delete router option
                  var del=document.createElement('option');
                  del.value='__delete__'; del.text='🗑 Delete router…';
                  sel.add(del);
                  // U-92 (owner: *"webpage doesnt show the router name but
                  // shows select router"*): the placeholder DISPLAYS the
                  // network the car reports it is on - car truth, never an
                  // asserted request (F-57).
                  if(sel.options.length > 0){
                    sel.options[0].text = j.ssid ? ('Now on: ' + j.ssid) : '— Select router —';
                  }
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
    // U-79: the 1 s status broadcast used to snap the thumb back to the
    // car's LAST reported speed while the user was still dragging — on touch
    // devices the range input is often NOT document.activeElement during a
    // drag, so the old focus guard missed and the slider fought the user
    // ("speed control not working properly"). An explicit interaction flag
    // (plus a short tail) is the guard that actually holds.
    var speedDragging = false, lastDragAt = 0;
    function syncSpeed(sp){
      let v = parseInt(sp, 10);
      if(isNaN(v)) return;
      if(v < 100) v = 100;
      if(v > 255) v = 255;
      v = Math.round(v / 5) * 5;
      var s = document.getElementById('s');
      var speedEl = document.getElementById('speed');
      if(speedEl) speedEl.innerText = v;
      if(s && !speedDragging && (Date.now() - lastDragAt) > 1500 &&
         document.activeElement !== s) s.value = v;
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
      if (!sel || !sel.value) return;   // F-57: never send the empty "unknown" option
      sendCmd(sel.value);
    }
    // F-57: mirror the car's REPORTED mode into the dropdown, so the control a
    // user reads agrees with the #mode readout. Never while the user has it
    // open or focused - same anti-flicker / anti-fighting-the-user rule the
    // saved-router dropdown uses below.
    function syncMode(canon){
      if (!canon) return;
      let sel = document.getElementById('m');
      if (!sel || document.activeElement === sel) return;
      for (let i=0; i<sel.options.length; i++){
        if (sel.options[i].getAttribute('data-canon') === canon){
          if (sel.value !== sel.options[i].value) sel.value = sel.options[i].value;
          return;
        }
      }
    }
    // U-79: ONE place for every network. The dropdown already lists the
    // saved routers (car truth via REQ_NETW). "Add router…" stores a new
    // pair with WIFICFG — which SAVES and switches in one command — and the
    // switching note tells the user what happens to this page when the car
    // hops networks (the hotspot link drops by design; the page reconnects
    // automatically once the phone follows the car onto the router).
    var switchNote = '';
    function escHtml(s){
      return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;');
    }
    function useNetwork(name){
      sendCmd('ROUTERS;USE;' + name);
      switchNote = 'Switching to ' + name + '… this page reconnects when this phone joins ' + name + '.';
      var c = document.getElementById('conn');
      if (c) c.innerText = 'Switching…';
    }
    function setRouter(){
      let sel = document.getElementById('r');
      if(!sel || !sel.value) return;
      if(sel.value === '__add__'){
        let ssid = prompt('Router name (SSID):');
        if(!ssid){ sel.value=''; return; }
        let pass = prompt('Router password (leave empty for an open network):', '') || '';
        sendCmd('WIFICFG;'+ssid+';'+pass);
        switchNote = 'Saving + switching to '+ssid+'… this page reconnects when this phone joins '+ssid+'.';
      } else if(sel.value === '__delete__'){
        let ssid = prompt('Router name to delete:');
        if(!ssid){ sel.value=''; return; }
        if(confirm('Delete '+ssid+' from car?')) sendCmd('ROUTERS;DEL;'+ssid);
      } else {
        useNetwork(sel.value);
      }
      document.getElementById('conn').innerText = 'Switching…';
      sel.value = '';
    }
    window.onload=function(){
      setTimeout(initWebSocket, 300);  // T-51: 300 ms WS startup delay (avoids reconnect burst)
      attachHold('btn-forward','forward');
      attachHold('btn-left','left');
      attachHold('btn-stop','stop');
      attachHold('btn-right','right');
      attachHold('btn-back','backward');
      // U-95 Phase 3 (power): a tab that was hidden schedules no reconnect
      // (onclose above); coming back reconnects IMMEDIATELY, no penalty delay —
      // the old page sat "Disconnected" for up to 3 s after every app switch.
      document.addEventListener('visibilitychange', function(){
        if(document.visibilityState !== 'visible') return;
        if(ws && ws.readyState <= 1) return;
        if(wsTimer){ clearTimeout(wsTimer); wsTimer = null; }
        wsDelay = 0;
        initWebSocket();
      });
      // F-57: do NOT preselect a mode. The dropdown starts on the empty
      // "— current mode —" option and is filled in by syncMode() once the car
      // reports, so the page never shows a mode the car is not actually in.
      // (Preselecting is also what made the page look like it was driving the
      // car; the earlier fix stopped it SENDING the value, but the display was
      // still lying.)
      document.getElementById('m').addEventListener('change', setMode);
      var r = document.getElementById('r');
      if(r) r.addEventListener('change', setRouter);
      var s = document.getElementById('s');
      if(s){
        s.addEventListener('input', function(){ speedDragging = true; lastDragAt = Date.now(); setSpeed(); });
        s.addEventListener('change', function(){ setSpeed(); speedDragging = false; lastDragAt = Date.now(); });
      }
    };
  </script>
</head>
<body>
  <!-- S-14/S-15: the ONLY portrait surface. Everything else is landscape-only
       per the owner's ruling; there is no stacked fallback to keep in sync. -->
  <div class='rotate'>
    <svg viewBox='0 0 24 24' fill='none' stroke='#eee' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'>
      <rect x='7' y='2' width='10' height='20' rx='2'/>
      <path d='M4 12a8 8 0 0 1 2-5.3'/>
      <path d='M2.5 5.5 4 7l1.5-1.5'/>
    </svg>
    <b>Turn the phone to landscape</b>
    <span>This control page only runs in landscape. Rotate the device and it
      comes back on its own &#8212; nothing has to be reloaded.</span>
  </div>
  <div class='frame'>
    <h1>4WD4M Car</h1>
    <div class='top-selects'>
      <select id='m'>
        <!-- F-57: the first option is the "car has not told us yet" state and is
             what the page shows until the car reports. The old page hardcoded
             MODE_ESP_SERVER as the selected value, so it displayed "ESP32 (WS)"
             while the car was really in 4WD4M - the same page showed the true
             mode in the #mode readout and a different one here (F-53). A
             dropdown must not assert a state nothing has confirmed. -->
        <option value='' selected>— current mode —</option>
        <!-- value is the WIRE token and must keep its 'MODE_' prefix:
             ModeManager::handleCommand (ModeManager.cpp:302) matches the
             5-char prefix "MODE_" and hands the remainder to
             setModeFromString, so MODE_MANUAL -> "MANUAL" -> Mode::MANUAL and
             MODE_PATH -> "PATH" -> Mode::PATH. VERIFIED ON THE CAR (U-82):
             MODE_MANUAL/MODE_PATH/MODE_AUTO all switch the mode.
             data-canon is the CANONICAL token the car reports back as j.mode
             and is what syncMode() and the CAPS-disable path match against.
             data-label is only the display text (OBS-US vs OBS_US).
             The gate tools/check-webpage-js.mjs checks that every data-canon is
             a real MODE_REGISTRY token, so this list cannot drift. -->
        <!-- U-79: ESP32 (WS) / ESP32 (CLIENT) are REMOVED — the webserver
             mode is gone from the car (no second Wi-Fi setup to fall into).
             U-90 (owner override): the remaining modes are ALL selectable,
             coming-soon included; the (coming soon) suffix the 1 Hz CAPS sync
             adds is information, never a disable. -->
        <option value='MODE_BT' data-canon='4WD4M' data-label='4WD4M'>4WD4M</option>
        <option value='MODE_PATH'      data-canon='PATH'    data-label='PATH'>PATH</option>
        <option value='MODE_OBS_US'    data-canon='OBS_US'  data-label='OBS-US'>OBS-US</option>
        <option value='MODE_OBS_IR'    data-canon='OBS_IR'  data-label='OBS-IR'>OBS-IR</option>
        <option value='MODE_MANUAL'    data-canon='MAN'     data-label='MANUAL'>MANUAL</option>
        <option value='MODE_AUTO'      data-canon='AUTO'    data-label='AUTO'>AUTO</option>
        <option value='MODE_2WD1M'     data-canon='2WD1M'   data-label='2WD1M'>2WD1M</option>
      </select>
      <select id='r'>
        <option value='' disabled selected>— Select router —</option>
        <!-- U-79: ONE place for every network — the car fills this with the
             saved routers + its own hotspot, plus an "Add router…" entry
             (appended by the rebuild script, which owns the option list). -->
      </select>
    </div>

    <!-- U-81: the CAR'S ANSWER to what this page asked it to do.
         Before this the page sent WIFICFG / ROUTERS;USE and simply printed
         "Switching…", which is a promise, not a fact: a REFUSED add (registry
         FULL, a reserved name, an over-long password/SSID) and a success looked
         identical, and the 1 s status JSON overwrote the text every second so
         even the promise was gone. The car answers `…;REPLY=<line>` on the
         STATE line (ModeManager.cpp:535) and `NACK;E=…` for a refused mode;
         handleTextLine() below consumes both (F-62). -->
    <p id='note' class='small' role='status' aria-live='polite'></p>

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
        <!-- S-15: the app's DualDpad for 4WD4M is a full cross on the LEFT and a
             second pad on the right that is drawn but inert (enabledZones(false)
             → right: []). Mirrored here exactly: same zones, same C-stops-
             everything semantics (the centre is STOP), and the right pad carries
             no handler at all — it is markup + styling, never a control. -->
        <div class='pads'>
          <div class='pad'>
            <div class='row'><div id='btn-forward' class='btn'>FWD</div></div>
            <div class='row'>
              <div id='btn-left' class='btn'>LFT</div>
              <div id='btn-stop' class='btn stop'>STOP</div>
              <div id='btn-right' class='btn'>RGT</div>
            </div>
            <div class='row'><div id='btn-back' class='btn'>BWD</div></div>
          </div>
          <div class='pad inert' aria-hidden='true'>
            <div class='row'><div class='btn'>FWD</div></div>
            <div class='row'>
              <div class='btn'>LFT</div>
              <div class='btn stop'>STOP</div>
              <div class='btn'>RGT</div>
            </div>
            <div class='row'><div class='btn'>BWD</div></div>
          </div>
        </div>
        <div class='row speed-row'>
          <div class='range-wrap'>
            <label class='lim'>100</label>
            <input id='s' type='range' min='100' max='255' step='5' value='170' aria-label='Speed'>
            <span id='speed' class='val'></span>
            <label class='lim'>255</label>
          </div>
        </div>
      </div>
<aside class='side side-r'>
        <span>IP: <b id='ip'>--</b></span>
        <span>SSID: <b id='ssid'>--</b></span>
        <span>Hotspot: <b id='apname'>--</b></span>
        <span>Bcast: <b id='bcast'>--</b></span>
        <span>Heap: <b id='heap'>--</b></span>
        <span>Conn: <b id='conn'>...</b></span>
        <span id='rst-wrap' style='display:none'>Last restart: <b id='rst'>--</b></span>
      </aside>
    </div>

    <p class='small'>Speed 100&#8211;255 (car floor). Hold a button to move; release to stop.</p>
  </div>
</body>
</html>
)rawliteral";

#endif // WEBPAGE_H