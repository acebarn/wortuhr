#include <cstring>

#include "wordclock/WebApi.h"

// Die Fallback-Seite.
//
// Liegt im Flash, nicht im Dateisystem: eine Fallback-Seite, die erst
// hochgeladen werden muss, ist kein Fallback. Sie erzeugt ihr Formular aus
// /api/schema -- dadurch gibt es weiterhin nur eine Beschreibung der
// Einstellungen, und eine neue Zeile im Schema erscheint hier von selbst.
//
// Bewusst ohne Framework, ohne externe Nachladung: im AP-Modus gibt es kein
// Internet, aus dem sich etwas holen liesse.

namespace wordclock {
namespace {

const char kIndexHtml[] =
    R"HTML(<!doctype html><html lang=de><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Wortuhr</title>
<style>
:root{color-scheme:light dark;--bg:#111;--fg:#eee;--mut:#999;--line:#333;--acc:#ffb43c}
@media(prefers-color-scheme:light){:root{--bg:#fafafa;--fg:#111;--mut:#666;--line:#ddd}}
*{box-sizing:border-box}
body{margin:0;padding:1rem;font:15px/1.5 system-ui,sans-serif;background:var(--bg);color:var(--fg);
     max-width:34rem;margin-inline:auto}
h1{font-size:1.3rem;margin:.2rem 0 1rem}
h2{font-size:.8rem;text-transform:uppercase;letter-spacing:.08em;color:var(--mut);
   margin:1.6rem 0 .4rem;border-bottom:1px solid var(--line);padding-bottom:.3rem}
.row{display:flex;align-items:center;gap:.75rem;padding:.4rem 0}
.row label{flex:1;min-width:0}
.row input,.row select{flex:0 0 9.5rem;background:transparent;color:var(--fg);
  border:1px solid var(--line);border-radius:.35rem;padding:.35rem .5rem;font:inherit}
.row input[type=range]{flex:0 0 7rem;padding:0}
.row input[type=color]{flex:0 0 3rem;padding:.1rem;height:2rem}
.row .val{flex:0 0 2.6rem;text-align:right;color:var(--mut);font-variant-numeric:tabular-nums}
button{font:inherit;padding:.5rem .9rem;border-radius:.4rem;border:1px solid var(--line);
  background:var(--acc);color:#111;cursor:pointer}
button.ghost{background:transparent;color:var(--fg)}
button.danger{background:transparent;color:#e05252;border-color:#e05252}
#bar{position:sticky;bottom:0;background:var(--bg);border-top:1px solid var(--line);
  padding:.75rem 0;display:flex;gap:.5rem;align-items:center;margin-top:1rem}
#msg{color:var(--mut);flex:1;font-size:.85rem}
#status{font-size:.85rem;color:var(--mut);line-height:1.7}
#status b{color:var(--fg);font-weight:500}
#nav{margin-top:.5rem;font-size:.85rem}
#nav a{color:var(--mut)}
.dot{display:inline-block;width:.55rem;height:.55rem;border-radius:50%;margin-right:.4rem}
</style>
<h1>Wortuhr</h1>
<div id=status>lade&hellip;</div>
<div id=nav><a href="/panel">Uhr ansehen</a></div>
<div id=form></div>
<div id=bar>
  <span id=msg></span>
  <button class=ghost onclick=reboot()>Neustart</button>
  <button onclick=save()>Speichern</button>
</div>
<script>
let schema=null;
const $=s=>document.querySelector(s);
const j=(u,o)=>fetch(u,o).then(r=>r.json());
const hex=n=>'#'+(n>>>0).toString(16).padStart(6,'0');
const num=s=>parseInt(s.replace('#',''),16);

function field(it,val){
  const row=document.createElement('div');row.className='row';
  const lab=document.createElement('label');lab.textContent=it.label;row.append(lab);
  let el;
  if(it.type==='bool'){el=document.createElement('input');el.type='checkbox';el.checked=!!val;
    el.style.flex='0 0 auto'}
  else if(it.type==='color'){el=document.createElement('input');el.type='color';el.value=hex(val)}
  else if(it.type==='choice'){el=document.createElement('select');
    it.options.forEach((o,i)=>{const op=document.createElement('option');op.value=i;op.textContent=o;el.append(op)});
    el.value=val}
  else if(it.type==='time'){el=document.createElement('input');el.type='time';
    el.value=String(Math.floor(val/60)).padStart(2,'0')+':'+String(val%60).padStart(2,'0')}
  else{el=document.createElement('input');el.type='range';el.min=it.min;el.max=it.max;el.value=val;
    const v=document.createElement('span');v.className='val';v.textContent=val;
    el.oninput=()=>v.textContent=el.value;row.append(el,v);el.dataset.k=it.key;return row}
  el.dataset.k=it.key;row.append(el);return row;
}

function read(el,it){
  if(it.type==='bool')return el.checked?1:0;
  if(it.type==='color')return num(el.value);
  if(it.type==='time'){const[h,m]=el.value.split(':').map(Number);return h*60+m}
  return parseInt(el.value,10);
}

async function build(){
  schema=await j('/api/schema');
  const cfg=await j('/api/config');
  const sec=await j('/api/secrets');
  const f=$('#form');f.innerHTML='';
  const cats={anzeige:'Anzeige',nacht:'Nachtmodus',aus:'Abschaltzeit'};
  for(const c in cats){
    const items=schema.settings.filter(i=>i.category===c);
    if(!items.length)continue;
    const h=document.createElement('h2');h.textContent=cats[c];f.append(h);
    items.forEach(it=>f.append(field(it,cfg[it.key])));
  }
  const h=document.createElement('h2');h.textContent='Zugangsdaten';f.append(h);
  schema.secrets.forEach(it=>{
    const row=document.createElement('div');row.className='row';
    const lab=document.createElement('label');lab.textContent=it.label;
    const el=document.createElement('input');
    el.type=it.masked?'password':'text';el.value=sec[it.key]||'';el.dataset.s=it.key;
    row.append(lab,el);f.append(row);
  });
  const row=document.createElement('div');row.className='row';
  const b=document.createElement('button');b.className='danger';b.textContent='Werksreset';
  b.onclick=factory;row.append(b);f.append(row);
}

async function save(){
  const cfg={},sec={};
  schema.settings.forEach(it=>{
    const el=document.querySelector(`[data-k="${it.key}"]`);if(el)cfg[it.key]=read(el,it)});
  document.querySelectorAll('[data-s]').forEach(el=>sec[el.dataset.s]=el.value);
  const a=await j('/api/config',{method:'POST',body:JSON.stringify(cfg)});
  const b=await j('/api/secrets',{method:'POST',body:JSON.stringify(sec)});
  $('#msg').textContent=`gespeichert: ${a.applied} Einstellungen, ${b.applied} Zugangsdaten`
    +(a.clamped?`, ${a.clamped} begrenzt`:'');
  status();
}

async function reboot(){await fetch('/api/restart',{method:'POST'});$('#msg').textContent='startet neu...'}
async function factory(){
  if(!confirm('Alle Einstellungen und Zugangsdaten loeschen?'))return;
  await fetch('/api/factory-reset',{method:'POST',body:'{"confirm":true}'});
  $('#msg').textContent='zurueckgesetzt, startet neu...';
}

async function status(){
  try{
    const s=await j('/api/status');
    const col={ok:'#3fb950',warnung:'#d29922',kritisch:'#e05252'}[s.severity]||'#999';
    $('#status').innerHTML=
      `<span class=dot style=background:${col}></span><b>${s.time}</b> &middot; ${s.display}`
      +` &middot; ${s.fault}${s.code?' (Code '+s.code+')':''}<br>`
      +`WLAN ${s.wifi?'verbunden, '+s.rssi+' dBm':'getrennt'}${s.ap?' &middot; AP offen':''}`
      +` &middot; MQTT ${!s.mqtt_enabled?'nicht eingerichtet':(s.mqtt?'verbunden':'getrennt')}<br>`
      +`${s.ip} &middot; ${Math.floor(s.uptime/60)} min &middot; ${(s.heap/1024).toFixed(1)} kB frei`;
  }catch(e){$('#status').textContent='keine Verbindung zur Uhr'}
}

build().then(status);
setInterval(status,5000);
</script>
)HTML";

}  // namespace

const char* WebApi::indexPage() { return kIndexHtml; }
size_t WebApi::indexPageLength() { return std::strlen(kIndexHtml); }

}  // namespace wordclock
