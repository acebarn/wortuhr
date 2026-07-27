#pragma once

#include <string>
#include <vector>

#include "wordclock/Animator.h"
#include "wordclock/Frame.h"
#include "wordclock/WordLayout.h"

namespace sim {

// Galerie aller Animationen, nebeneinander und live.
//
// Nur im Simulator: auf der Frontplatte kann ohnehin immer nur eine Animation
// laufen, eine Uebersicht gaebe es dort nicht zu sehen.
//
// Wichtig -- jede Kachel hat ihren EIGENEN Animator. Die Galerie fasst die
// laufende Uhr also nicht an; man kann sie offen lassen, waehrend die Uhr
// normal weiterlaeuft.
class Gallery {
public:
    Gallery() {
        for (uint8_t i = 0; i < wordclock::kAnimPresetCount; ++i) {
            animators_.emplace_back();
            animators_.back().start(wordclock::kAnimPresets[i].kind,
                                    wordclock::kAnimPresets[i].params, 0);
        }
    }

    std::string framesJson(uint32_t nowMs) {
        std::string out = "[";
        char buf[24];
        for (uint8_t i = 0; i < animators_.size(); ++i) {
            wordclock::Frame f;
            f.clear();
            animators_[i].render(f, nowMs);

            if (i) out += ',';
            out += "{\"name\":\"";
            out += wordclock::kAnimPresets[i].name;
            out += "\",\"cells\":[";
            for (uint16_t c = 0; c < wordclock::kLetterCount; ++c) {
                const wordclock::Rgb p = f.cell(c);
                std::snprintf(buf, sizeof(buf), "%s%u", c ? "," : "",
                              (unsigned(p.r) << 16) | (unsigned(p.g) << 8) | p.b);
                out += buf;
            }
            out += "]}";
        }
        out += ']';
        return out;
    }

private:
    std::vector<wordclock::Animator> animators_;
};

inline const char* galleryPage() {
    static const char kHtml[] =
        R"HTML(<!doctype html><html lang=de><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Animationen</title>
<style>
body{margin:0;padding:1.6rem;background:#08080a;color:#777;
     font:13px system-ui,sans-serif}
h1{font-size:1.1rem;color:#ccc;font-weight:500;margin:0 0 .3rem}
p{margin:0 0 1.4rem;color:#666}
a{color:#888}
#g{display:grid;grid-template-columns:repeat(auto-fill,minmax(210px,1fr));gap:1.3rem}
.p{background:#141416;border-radius:11px;padding:1rem;box-shadow:0 8px 26px #000a}
.t{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:.7rem}
.t b{color:#bbb;font-weight:500;font-size:.9rem}
.t button{font:inherit;font-size:.78rem;padding:.2rem .55rem;border-radius:.3rem;
  border:1px solid #333;background:transparent;color:#888;cursor:pointer}
.t button:hover{color:#ffb43c;border-color:#ffb43c}
.g{display:grid;grid-template-columns:repeat(11,1fr);gap:3px;
   font:600 11px/1 ui-monospace,monospace;text-align:center}
.g i{font-style:normal;transition:color .09s linear,text-shadow .09s linear}
</style>
<h1>Animationen</h1>
<p>Alle Presets gleichzeitig, live gerechnet. Die Uhr laeuft daneben normal weiter &mdash;
  mit <em>auf die Uhr</em> spielst du eine davon auf dem Panel ab.
  &nbsp;<a href="/panel">Uhr ansehen</a> &middot; <a href="/">Einstellungen</a></p>
<div id=g></div>
<script>
const GRID="ESKISTAFUNFZEHNZWANZIGDREIVIERTELVORDIRSNACHHALBAELFUNFEINSXAMZWEIDREIAUJVIERSECHSNLACHTSIEBENZWOLFZEHNEUNKUHR";
let cells=null;

function build(names){
  const g=document.getElementById('g');g.innerHTML='';cells=[];
  names.forEach(n=>{
    const p=document.createElement('div');p.className='p';
    const t=document.createElement('div');t.className='t';
    const b=document.createElement('b');b.textContent=n;
    const btn=document.createElement('button');btn.textContent='auf die Uhr';
    btn.onclick=()=>fetch('/api/animation',{method:'POST',
      body:JSON.stringify({name:n,seconds:20})});
    t.append(b,btn);
    const grid=document.createElement('div');grid.className='g';
    const row=[];
    for(let i=0;i<110;i++){const e=document.createElement('i');e.textContent=GRID[i];
      grid.append(e);row.push(e)}
    cells.push(row);p.append(t,grid);g.append(p);
  });
}

// Dieselbe Streuung wie in der Uhrenansicht -- ohne den Schein wirkt es wie
// eine Tabelle, nicht wie eine Frontplatte.
function paint(v,el){
  const r=v>>16&255,g=v>>8&255,b=v&255,lum=Math.max(r,g,b);
  if(lum<6){el.style.color='#ffffff0a';el.style.textShadow='none';return}
  const a=(lum/255).toFixed(2);
  el.style.color=`rgb(${r},${g},${b})`;
  el.style.textShadow=`0 0 ${3+lum/34}px rgba(${r},${g},${b},${a}),`
                     +`0 0 ${8+lum/13}px rgba(${r},${g},${b},${(a*0.4).toFixed(2)})`;
}

async function tick(){
  try{
    const d=await(await fetch('/api/gallery')).json();
    if(!cells)build(d.map(x=>x.name));
    d.forEach((x,i)=>x.cells.forEach((v,c)=>paint(v,cells[i][c])));
  }catch(e){}
}
tick();setInterval(tick,110);
</script>
)HTML";
    return kHtml;
}

}  // namespace sim
