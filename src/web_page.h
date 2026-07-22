#pragma once
#include <Arduino.h>

// Single self-contained page. No CDN, no build step, no app install.
// Hold-to-move sends /api/nudge every 120 ms; the firmware's deadman stops
// the desk if those stop arriving, so a dropped connection or a locked phone
// cannot leave the desk running.

static const char PAGE_INDEX[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#111318">
<title>Desk</title>
<style>
:root{
  --bg:#f4f5f7; --card:#fff; --fg:#15171c; --muted:#6b7280;
  --line:#e3e5ea; --accent:#2f6fed; --stop:#c0392b; --ok:#1e9e6a;
}
@media (prefers-color-scheme:dark){
  :root{ --bg:#0e1014; --card:#171a21; --fg:#eef1f6; --muted:#9aa3b2;
         --line:#262b35; --accent:#4b86ff; --stop:#e2564a; --ok:#35c48d; }
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--fg);
  font:16px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;
  padding:max(16px,env(safe-area-inset-top)) 16px max(16px,env(safe-area-inset-bottom));
  display:flex;justify-content:center}
.wrap{width:100%;max-width:420px}
h1{font-size:13px;letter-spacing:.09em;text-transform:uppercase;color:var(--muted);
   margin:0 0 14px;font-weight:600}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;
  padding:20px;margin-bottom:14px}
.readout{text-align:center}
.h{font-size:60px;font-weight:250;letter-spacing:-.03em;line-height:1;
   font-variant-numeric:tabular-nums}
.h small{font-size:24px;color:var(--muted);margin-left:2px}
.sub{color:var(--muted);font-size:13px;margin-top:8px;min-height:19px}
.dot{display:inline-block;width:7px;height:7px;border-radius:50%;
  background:var(--muted);margin-right:6px;vertical-align:1px}
.dot.live{background:var(--ok)}
.dot.err{background:var(--stop)}
.btns{display:grid;gap:12px}
button{font:inherit;color:var(--fg);background:var(--card);
  border:1px solid var(--line);border-radius:14px;padding:0;cursor:pointer;
  touch-action:manipulation;user-select:none;transition:transform .06s,filter .12s}
button:active{transform:scale(.975);filter:brightness(1.12)}
button:disabled{opacity:.4;cursor:not-allowed}
.move{height:96px;font-size:19px;font-weight:600;background:var(--accent);
  color:#fff;border:none;display:flex;align-items:center;justify-content:center;gap:10px}
.move svg{width:26px;height:26px}
.stop{height:62px;font-size:17px;font-weight:700;background:var(--stop);
  color:#fff;border:none;letter-spacing:.04em}
.row{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
.row button{height:52px;font-size:15px;font-weight:600}
input[type=range]{width:100%;margin:6px 0 2px;accent-color:var(--accent)}
.scale{display:flex;justify-content:space-between;color:var(--muted);font-size:12px}
.lbl{font-size:12px;text-transform:uppercase;letter-spacing:.07em;
  color:var(--muted);font-weight:600;margin-bottom:10px}
.hint{color:var(--muted);font-size:12px;text-align:center;margin-top:14px}
</style></head><body><div class="wrap">

<h1>Standing Desk</h1>

<div class="card readout">
  <div class="h"><span id="hv">--</span><small>&quot;</small></div>
  <div class="sub"><span class="dot" id="dot"></span><span id="st">connecting</span></div>
</div>

<div class="card btns">
  <button class="move" id="up">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
      stroke-linecap="round" stroke-linejoin="round"><path d="M12 19V5M5 12l7-7 7 7"/></svg>
    RAISE
  </button>
  <button class="stop" id="stop">STOP</button>
  <button class="move" id="dn">
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
      stroke-linecap="round" stroke-linejoin="round"><path d="M12 5v14M5 12l7 7 7-7"/></svg>
    LOWER
  </button>
</div>

<div class="card">
  <div class="lbl">Position</div>
  <input type="range" id="sl" min="0" max="100" value="0" step="1">
  <div class="scale"><span id="lo">--</span><span id="pv">--%</span><span id="hi">--</span></div>
</div>

<div class="card">
  <div class="lbl">Presets</div>
  <div class="row">
    <button data-p="1">1</button><button data-p="2">2</button>
    <button data-p="3">3</button><button data-p="4">4</button>
  </div>
</div>

<div class="hint">Hold raise or lower to move. Release to stop.</div>

<script>
const $=i=>document.getElementById(i);
let held=null, timer=null, dragging=false, lastPoll=0;

function post(p){ return fetch(p,{method:'POST'}).catch(()=>{}); }

// Hold-to-move. Repeats while held; the firmware stops the desk on its own
// if these stop arriving, so we never rely on the release event landing.
function hold(btn,dir){
  const start=e=>{ e.preventDefault(); if(held) return;
    held=dir; post('/api/nudge?dir='+dir);
    timer=setInterval(()=>post('/api/nudge?dir='+dir),120); };
  const end=()=>{ if(!held) return;
    clearInterval(timer); held=null; post('/api/stop'); };
  btn.addEventListener('pointerdown',start);
  ['pointerup','pointercancel','pointerleave'].forEach(ev=>
    btn.addEventListener(ev,end));
  window.addEventListener('blur',end);
}
hold($('up'),'up'); hold($('dn'),'down');

$('stop').addEventListener('click',()=>post('/api/stop'));
document.querySelectorAll('[data-p]').forEach(b=>
  b.addEventListener('click',()=>post('/api/preset?n='+b.dataset.p)));

$('sl').addEventListener('input',()=>{ dragging=true; $('pv').textContent=$('sl').value+'%'; });
$('sl').addEventListener('change',()=>{
  post('/api/goto?pct='+$('sl').value);
  setTimeout(()=>dragging=false,600);
});

function fmt(v){ return v==null?'--':v.toFixed(1)+'"'; }

async function poll(){
  try{
    const r=await fetch('/api/state',{cache:'no-store'});
    const s=await r.json();
    $('hv').textContent = s.height==null ? '--' : s.height.toFixed(1);
    $('lo').textContent = fmt(s.min); $('hi').textContent = fmt(s.max);
    if(s.pct!=null && !dragging && !held){
      $('sl').value=Math.round(s.pct); $('pv').textContent=Math.round(s.pct)+'%';
    }
    $('dot').className='dot'+(s.link?' live':' err');
    $('st').textContent = s.fault ? ('stopped: '+s.fault)
      : s.motion==='up' ? 'raising' : s.motion==='down' ? 'lowering'
      : !s.link ? 'no link to desk'
      : s.max==null ? 'reading desk limits...' : 'ready';
    $('sl').disabled = s.max==null;
  }catch(e){
    $('dot').className='dot err'; $('st').textContent='no connection';
  }
}
setInterval(poll, 500); poll();
</script></div></body></html>)HTML";
