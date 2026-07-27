#pragma once

#include <cstdio>
#include <string>

#include "wordclock/Frame.h"
#include "wordclock/WordLayout.h"

namespace sim {

// Wortuhr-Ansicht im Browser.
//
// Nur im Simulator -- auf dem Geraet gaebe es nichts zu zeigen und der Platz
// waere verschwendet.
//
// Bildet nach, was die Frontplatte tut: schwarzes Acryl, ausgeschnittene
// Buchstaben, Milchglas davor. Unbeleuchtete Buchstaben bleiben schwach
// sichtbar wie im Gegenlicht; leuchtende bekommen einen weichen Schein, weil
// das Licht hinter der Streuscheibe in die Nachbarzellen auslaeuft.

inline std::string frameJson(const wordclock::Frame& f) {
    std::string out = "{\"cells\":[";
    char buf[24];
    for (uint16_t c = 0; c < wordclock::kLetterCount; ++c) {
        const wordclock::Rgb p = f.cell(c);
        std::snprintf(buf, sizeof(buf), "%s%u", c ? "," : "",
                      (unsigned(p.r) << 16) | (unsigned(p.g) << 8) | p.b);
        out += buf;
    }
    out += "],\"dots\":[";
    for (uint8_t d = 0; d < wordclock::kDotCount; ++d) {
        const wordclock::Rgb p = f.dot(d);
        std::snprintf(buf, sizeof(buf), "%s%u", d ? "," : "",
                      (unsigned(p.r) << 16) | (unsigned(p.g) << 8) | p.b);
        out += buf;
    }
    out += "]}";
    return out;
}

inline const char* panelPage() {
    static const char kHtml[] =
        R"HTML(<!doctype html><html lang=de><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Wortuhr</title>
<style>
body{margin:0;min-height:100vh;display:grid;place-items:center;gap:1.2rem;
     background:#0a0a0c;font:14px/1.4 system-ui,sans-serif;color:#666;padding:2rem}
#plate{position:relative;background:#141416;border-radius:14px;
       padding:min(4.4vmin,34px);box-shadow:0 20px 60px #000c,inset 0 1px 0 #ffffff10}
#grid{display:grid;grid-template-columns:repeat(11,1fr);gap:min(1.5vmin,11px)}
.c{width:min(4.2vmin,32px);height:min(4.2vmin,32px);display:grid;place-items:center;
   font:600 min(2.4vmin,18px)/1 ui-monospace,monospace;color:#ffffff0a;
   transition:color .09s linear,text-shadow .09s linear}
.d{position:absolute;width:min(1.5vmin,11px);height:min(1.5vmin,11px);border-radius:50%;
   background:#ffffff08;transition:background .09s linear,box-shadow .09s linear}
#d0{top:min(1.5vmin,12px);right:min(1.5vmin,12px)}
#d1{top:min(1.5vmin,12px);left:min(1.5vmin,12px)}
#d2{bottom:min(1.5vmin,12px);left:min(1.5vmin,12px)}
#d3{bottom:min(1.5vmin,12px);right:min(1.5vmin,12px)}
#info{font-variant-numeric:tabular-nums;text-align:center}
#info b{color:#bbb;font-weight:500}
a{color:#777}
</style>
<div id=plate>
  <div id=grid></div>
  <div class=d id=d0></div><div class=d id=d1></div>
  <div class=d id=d2></div><div class=d id=d3></div>
</div>
<div id=info>&hellip;</div>
<div><a href="/">Einstellungen</a></div>
<script>
const GRID="ESKISTAFUNFZEHNZWANZIGDREIVIERTELVORDIRSNACHHALBAELFUNFEINSXAMZWEIDREIAUJVIERSECHSNLACHTSIEBENZWOLFZEHNEUNKUHR";
const g=document.getElementById('grid'),cells=[];
for(let i=0;i<110;i++){const d=document.createElement('div');d.className='c';d.textContent=GRID[i];
  g.append(d);cells.push(d)}
const dots=[0,1,2,3].map(i=>document.getElementById('d'+i));

// Die Frontplatte streut das Licht: ein leuchtender Buchstabe traegt weiter als
// seine eigene Zelle. Ohne diesen Schein wirkt die Ansicht wie eine Tabelle,
// nicht wie eine Uhr.
function paint(v,el,isDot){
  const r=v>>16&255,g_=v>>8&255,b=v&255,lum=Math.max(r,g_,b);
  if(lum<6){el.style.color='#ffffff0a';el.style.textShadow='none';
    if(isDot){el.style.background='#ffffff08';el.style.boxShadow='none'}return}
  const c=`rgb(${r},${g_},${b})`,a=(lum/255).toFixed(2);
  if(isDot){el.style.background=c;
    el.style.boxShadow=`0 0 ${4+lum/22}px rgba(${r},${g_},${b},${a})`}
  else{el.style.color=c;
    el.style.textShadow=`0 0 ${3+lum/34}px rgba(${r},${g_},${b},${a}),`
                       +`0 0 ${9+lum/12}px rgba(${r},${g_},${b},${(a*0.45).toFixed(2)})`}
}

async function tick(){
  try{
    const f=await(await fetch('/api/frame')).json();
    f.cells.forEach((v,i)=>paint(v,cells[i],false));
    f.dots.forEach((v,i)=>paint(v,dots[i],true));
    const s=await(await fetch('/api/status')).json();
    document.getElementById('info').innerHTML=
      `<b>${s.time}</b> &middot; ${s.display} &middot; ${s.fault}`
      +(s.code?` (Code ${s.code})`:'');
  }catch(e){document.getElementById('info').textContent='Simulator laeuft nicht'}
}
tick();setInterval(tick,100);
</script>
)HTML";
    return kHtml;
}

}  // namespace sim
