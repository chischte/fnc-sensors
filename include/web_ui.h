#pragma once

const char UPDATE_HTML[] = R"rawliteral(
<!doctype html><html><head><meta charset="utf-8"><title>Firmware Update</title>
<style>
body{font-family:system-ui,sans-serif;max-width:500px;margin:60px auto;padding:20px;background:#f3f6f4}
.card{background:white;border:1px solid #d9e2dc;border-radius:8px;padding:32px;box-shadow:0 2px 8px #173b2412}
h2{color:#2a3630;margin-top:0}
input[type=file]{margin:16px 0;width:100%}
button{background:#2878a8;color:white;border:none;padding:10px 24px;border-radius:6px;cursor:pointer;font-size:1rem}
button:hover{background:#1d5c82}
button:disabled{background:#a0b8c8;cursor:default}
#status{margin-top:16px;color:#607068;font-size:.95rem}
#prog-wrap{display:none;margin-top:16px;background:#e8eff0;border-radius:6px;height:20px;overflow:hidden}
#prog-bar{height:100%;width:0;background:#2878a8;transition:width .2s;border-radius:6px}
#prog-label{text-align:center;font-size:.85rem;color:#2a3630;margin-top:4px}
</style></head>
<body><div class="card"><h2>&#128225; Firmware Update</h2>
<form id="f">
  <label>Firmware-Datei (.ota):</label><br>
  <input type="file" id="bin" accept=".ota" required>
  <br><button id="btn" type="submit">Upload &amp; Flash</button>
</form>
<div id="prog-wrap"><div id="prog-bar"></div></div>
<div id="prog-label"></div>
<div id="status"></div>
</div>
<script>
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  var file=document.getElementById('bin').files[0];
  if(!file)return;
  var btn=document.getElementById('btn');
  var status=document.getElementById('status');
  var wrap=document.getElementById('prog-wrap');
  var bar=document.getElementById('prog-bar');
  var label=document.getElementById('prog-label');
  btn.disabled=true;
  status.textContent='Phase 1/3: Firmware-Datei wird übertragen ...';
  wrap.style.display='block';
  bar.style.width='0%';
  label.textContent='0% — 0 / '+Math.round(file.size/1024)+' KB';
  var xhr=new XMLHttpRequest();
  xhr.open('POST','/update');
  xhr.upload.onprogress=function(ev){
    if(ev.lengthComputable){
      var pct=Math.round(ev.loaded/ev.total*100);
      var kb=Math.round(ev.loaded/1024);
      var tot=Math.round(ev.total/1024);
      bar.style.width=pct+'%';
      label.textContent=pct+'% — '+kb+' / '+tot+' KB';
    }
  };
  xhr.upload.onload=function(){
    bar.style.width='100%';
    status.textContent='Phase 2/3: Firmware wird geprüft und installiert. Bitte warten ...';
  };
  xhr.onload=function(){
    bar.style.width='100%';
    label.textContent='100% — Upload abgeschlossen';
    if(xhr.status>=200&&xhr.status<300){
      status.textContent='Phase 3/3: '+xhr.responseText+' Verbindung wird wiederhergestellt ...';
      setTimeout(function(){window.location.href='/';},10000);
    }else{
      status.textContent='Fehler: '+xhr.responseText;
      btn.disabled=false;
    }
  };
  xhr.onerror=function(){
    status.textContent='Netzwerkfehler beim Upload. Das Gerät wurde nicht aktualisiert.';
    btn.disabled=false;
  };
  var fd=new FormData();
  fd.append('firmware',file);
  xhr.send(fd);
});
</script></body></html>
)rawliteral";

const char INDEX_HTML[] = R"rawliteral(
<!doctype html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Sensors</title>
<style>
body{font-family:system-ui,sans-serif;max-width:1000px;margin:0 auto;padding:20px;background:#f3f6f4;color:#17211d}
h2{margin:0 0 10px;font-size:1rem;color:#2a3630;text-align:center}.values{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}.value,section{background:white;border:1px solid #d9e2dc;border-radius:8px;padding:16px;box-shadow:0 2px 8px #173b2412}.value{text-align:center}.value strong{display:block;font-size:2rem;margin-top:8px}.unit{color:#607068}.delta{display:inline-block;font-size:1rem;color:#7bafd4;margin-left:8px;vertical-align:middle;position:absolute;left:62%;top:50%;transform:translateY(-50%)}.value{position:relative}section{margin-top:16px}canvas{width:100%;height:220px;display:block}@media(max-width:650px){.values{grid-template-columns:1fr}.value strong{font-size:1.7rem}}
#humidity{color:#2878a8}#co2{color:#7b2cbf}#boxtemp{color:#d65a4a}
.legend{display:flex;justify-content:center;gap:20px;font-size:12px;margin-bottom:8px}.legend span{display:inline-flex;align-items:center;gap:6px}.legend i{display:inline-block;width:30px;border-top:2px solid #2878a8}.legend .inbox{border-color:#d65a4a}.legend .ambient{border-top:2px dashed #e89489}.legend .scd{border-top:3px dotted #78a9c4}
#status-footer{text-align:center;margin:22px 0 2px;color:#607068;font:12px ui-monospace,SFMono-Regular,Consolas,monospace;letter-spacing:.06em}
</style></head><body>
<div class="values">
  <div class="value">Inbox humidity (SHT45)<strong id="humidity">--</strong><span class="unit">%RH</span></div>
  <div class="value">CO2<strong id="co2">--</strong><span class="unit">ppm</span></div>
  <div class="value">Inbox temperature (SHT45)
    <div style="position:relative;text-align:center;margin-top:8px;margin-bottom:4px">
      <strong id="boxtemp" style="font-size:2rem">--</strong>
      <span id="tempdelta" style="position:absolute;font-size:1rem;color:#e89489;font-weight:600;left:calc(50% + 2.2rem);top:50%;transform:translateY(-50%)"></span>
    </div>
    <span class="unit">&deg;C</span>
  </div>
</div>
<section><h2>Humidity</h2><div class="legend" aria-label="Feuchte-Sensoren"><span><i></i>SHT45</span><span><i class="scd"></i>SCD41</span></div><canvas id="chart-humidity" width="900" height="220"></canvas></section>
<section><h2>CO2</h2><canvas id="chart-co2" width="900" height="220"></canvas></section>
<section><h2>Temperature</h2><div class="legend"><span><i class="inbox"></i>Inbox (SHT45)</span><span><i class="ambient"></i>Ambient (PT100)</span></div><canvas id="chart-temperature" width="900" height="220"></canvas></section>
<footer id="status-footer">Last Reading: -- &nbsp; | &nbsp; -- data points</footer>
<script>
const series=[
  {id:'chart-humidity',key:'humidity_corrected',color:'#2878a8',min:85,max:100,unit:'%RH',step:5,overlay:'scd_humidity',overlayColor:'#78a9c4',overlayDash:[1,4]},
  {id:'chart-co2',key:'co2',color:'#7b2cbf',min:0,max:10000,unit:'ppm'},
  {id:'chart-temperature',key:'boxtemp',color:'#d65a4a',min:20,max:35,unit:'°C',step:5,overlay:'outertemp',overlayColor:'#e89489'}
];

function drawSeries(canvasId, history, cfg){
  const c=document.getElementById(canvasId),ctx=c.getContext('2d'),w=c.width,h=c.height;
  const left=78,right=12,top=12,bottom=30;
  const pw=w-left-right,ph=h-top-bottom;
  ctx.clearRect(0,0,w,h);
  if(!history.length){return;}

  const ticks=cfg.step
    ? Array.from({length:Math.floor((cfg.max-cfg.min)/cfg.step)+1},(_,i)=>cfg.max-(i*cfg.step))
    : Array.from({length:5},(_,i)=>cfg.max-((cfg.max-cfg.min)*i/4));

  ctx.strokeStyle='#d9e2dc';
  ctx.fillStyle='#607068';
  ctx.font='12px system-ui,sans-serif';
  ctx.textAlign='right';
  ticks.forEach((value)=>{
    const y=top+((cfg.max-value)*ph/(cfg.max-cfg.min));
    ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(left+pw,y);ctx.stroke();
    ctx.fillText(value.toFixed(cfg.key==='co2'?0:1),left-6,y+4);
  });

  ctx.save();
  ctx.translate(20, top + ph / 2);
  ctx.rotate(-Math.PI / 2);
  ctx.textAlign='center';
  ctx.fillStyle='#607068';
  ctx.fillText(cfg.unit, 0, 0);
  ctx.restore();

  ctx.beginPath();
  ctx.moveTo(left,top);
  ctx.lineTo(left,top+ph);
  ctx.lineTo(left+pw,top+ph);
  ctx.strokeStyle='#93a39a';
  ctx.stroke();

  function drawLine(key, color, dash=[]){
    const values=history.map(v=>v[key]==null?NaN:Number(v[key]));
    ctx.strokeStyle=color;
    ctx.lineWidth=dash.length?1.5:2;
    ctx.setLineDash(dash);
    ctx.lineCap=dash.length?'round':'butt';
    ctx.beginPath();
    const firstTime=Number(history[0].uptime_ms||0),lastTime=Number(history[history.length-1].uptime_ms||0);
    let connected=false;
    values.forEach((v,i)=>{
      if(!Number.isFinite(v)){connected=false;return;}
      const clamped=Math.max(cfg.min,Math.min(cfg.max,v));
      const sampleTime=Number(history[i].uptime_ms||0);
      const px=left+(lastTime>firstTime?(sampleTime-firstTime)*pw/(lastTime-firstTime):i*pw/Math.max(1,values.length-1));
      const py=top+(cfg.max-clamped)*ph/(cfg.max-cfg.min);
      connected?ctx.lineTo(px,py):ctx.moveTo(px,py);
      connected=true;
    });
    ctx.stroke();
    ctx.setLineDash([]);
  }

  drawLine(cfg.key, cfg.color);
  if(cfg.overlay){drawLine(cfg.overlay, cfg.overlayColor, cfg.overlayDash||[5,4]);}


  (cfg.extraOverlays||[]).forEach(s=>drawLine(s.key,s.color,s.dash));

  ctx.fillStyle='#607068';
  ctx.textAlign='center';
  const secs=30*60;
  for(let i=0;i<=4;i++){
    const x=left+i*pw/4;
    const t=(secs-(secs*i/4))/60;
    ctx.fillText(i===4?'now':'-'+t.toFixed(0)+'m',x,h-8);
  }
}

function draw(history){series.forEach(s=>drawSeries(s.id,history,s));}
function updateFooter(sequence){
  const now=new Date();
  const weekdays=['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'];
  const months=['January','February','March','April','May','June','July','August','September','October','November','December'];
  const pad=n=>String(n).padStart(2,'0');
  const stamp=weekdays[now.getDay()]+', '+pad(now.getDate())+' '+months[now.getMonth()]+' '+now.getFullYear()+' '+pad(now.getHours())+':'+pad(now.getMinutes())+':'+pad(now.getSeconds());
  document.querySelector('#status-footer').textContent='Last Reading:  '+stamp+'   |   '+String(sequence||0)+' data points';
}
async function update(){
  try{
    let r=await fetch('/api/measurement'),payload=await r.json(),d=payload.measurement||payload;
    document.querySelector('#co2').textContent=d.co2===null?'Fehler':d.co2;
    document.querySelector('#boxtemp').textContent=d.boxtemp===null?'Fehler':Number(d.boxtemp).toFixed(1);
    document.querySelector('#humidity').textContent=d.humidity_corrected===null?'Fehler':Number(d.humidity_corrected).toFixed(1);
    document.querySelector('#tempdelta').textContent='';
    if(d.outertemp!==null&&d.boxtemp!==null){
      const delta=d.boxtemp-d.outertemp;
      const sign=delta>=0?'+':'';
      document.querySelector('#tempdelta').textContent=sign+delta.toFixed(1);
    }
    draw(payload.history||[]);
    updateFooter(d.sequence);
  }catch(e){console.log(e)}
}
update();setInterval(update,5000);
</script></body></html>
)rawliteral";
