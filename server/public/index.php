<?php
// ============================================================================
//  public/index.php  -  管理画面 (UIシェルのみ)。ESP32 ダッシュボード相当。
// ----------------------------------------------------------------------------
//  遷移: ログイン → 端末選択 → ダッシュボード(☰: ホーム/グラフ/データ)。
//  グラフ: 温度/湿度/気圧/雷 を1グラフに重ね描き、チェックで表示切替(ESP32 同様、
//          気圧は右軸 y2)。ダークモード(◐)対応(localStorage 保存, ESP32 同様)。
//  API: 人間UI=api/*.py (session/login/devices/latest/series/csv) / 端末=webapi/*.py。
// ============================================================================
header('Content-Type: text/html; charset=utf-8');
?><!doctype html>
<html lang="ja"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WetherLoggerBox サーバ</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.1/dist/chart.umd.min.js"></script>
<style>
:root{--bg:#fff;--fg:#111;--card:#f6faf7;--line:#5fb98c;--acc:#2f9e6a;--mut:#5a6b62;--ng:#c0392b}
:root[data-theme=dark]{--bg:#0b1020;--fg:#eef2f8;--card:#131a2e;--line:#26406a;--acc:#5fd08a;--mut:#8fa3bf;--ng:#ff6b6b}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);
 font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:15px}
header{position:sticky;top:0;display:flex;align-items:center;gap:10px;padding:10px 12px;
 background:var(--card);border-bottom:2px solid var(--line);z-index:10}
header h1{font-size:16px;margin:0;flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.icon{background:var(--card);color:var(--fg);border:1px solid var(--line);cursor:pointer;padding:6px 10px;font-size:18px;border-radius:8px}
input,button,select{font:inherit;padding:7px 10px;border:1px solid var(--line);border-radius:8px;color:var(--fg);background:var(--bg)}
button{background:var(--acc);color:#062;border:0;cursor:pointer}
:root[data-theme=dark] button{color:#04240f}
button.ghost{background:var(--card);color:var(--acc);border:1px solid var(--line)}
main{padding:14px;max-width:860px;margin:0 auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin:10px 0}
label{color:var(--mut);font-size:13px}
.err{color:var(--ng);font-size:14px;margin:6px 0}
.login{max-width:320px;margin:8vh auto}.login input{width:100%;margin:6px 0}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.big{font-size:34px;font-weight:700}.mut{color:var(--mut);font-size:13px}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:8px 0}
.gauge{height:14px;border-radius:7px;background:#8883;overflow:hidden}.gauge>i{display:block;height:100%;background:var(--acc)}
.lv0{color:#5fd08a}.lv1{color:#c9d05f}.lv2{color:#e0b04f}.lv3{color:#e07a4f}.lv4{color:#ff5b5b}
.chartwrap{position:relative;height:320px}
.chk{display:inline-flex;align-items:center;gap:5px;border:1px solid var(--line);border-radius:20px;padding:4px 10px;cursor:pointer;font-size:13px}
.chk input{width:auto;margin:0}.sw{width:12px;height:12px;border-radius:3px;display:inline-block}
table{width:100%;border-collapse:collapse}td,th{border:1px solid var(--line);padding:6px 8px;text-align:left;font-size:14px}
tr.dev{cursor:pointer}tr.dev:hover{background:#5fb98c22}
nav{position:fixed;top:0;left:-270px;width:260px;height:100%;background:var(--card);
 border-right:2px solid var(--line);transition:left .2s;z-index:30;padding-top:56px}
nav.open{left:0}nav a{display:block;padding:14px 18px;color:var(--fg);text-decoration:none;
 border-bottom:1px solid var(--line);cursor:pointer}nav a.on{background:var(--acc);color:#062}
.mask{position:fixed;inset:0;background:#0006;z-index:20;display:none}.mask.open{display:block}
section.pg{display:none}section.pg.on{display:block}
@media(max-width:520px){.grid{grid-template-columns:1fr}}
[hidden]{display:none!important}
</style></head><body>

<!-- ログイン -->
<div id="vLogin" hidden>
  <main><div class="card login">
    <div class="row"><h1 style="margin:0;flex:1">WetherLoggerBox サーバ</h1><button class="icon tbtn" title="theme">◐</button></div>
    <div id="loginErr" class="err"></div>
    <label>メールアドレス / ユーザー名</label>
    <input id="fLogin" type="text" autocomplete="username" autofocus>
    <label>パスワード</label>
    <input id="fPass" type="password" autocomplete="current-password">
    <div class="row"><button id="btnLogin" style="flex:1">ログイン</button></div>
    <p class="mut">初期ユーザー: admin / wether。ログイン後に変更してください。</p>
  </div></main>
</div>

<!-- 端末選択 -->
<div id="vList" hidden>
  <header><h1>端末選択</h1><span id="who1" class="mut"></span>
    <button class="icon tbtn" title="theme">◐</button>
    <button class="ghost" id="btnLogout1">ログアウト</button></header>
  <main><div class="card">
    <h2 style="margin-top:0">登録端末</h2>
    <table>
      <thead><tr><th>MAC</th><th>端末名</th><th>最終データ</th><th>温度</th><th>湿度</th><th>気圧</th><th>雷</th></tr></thead>
      <tbody id="devBody"><tr><td colspan="7" class="mut">読み込み中...</td></tr></tbody>
    </table>
    <p class="mut">行をクリックするとダッシュボードを表示します。</p>
  </div></main>
</div>

<!-- ダッシュボード -->
<div id="vDash" hidden>
  <header>
    <button class="icon" id="mbtn">☰</button>
    <h1 id="dashTitle">Dashboard</h1>
    <span id="who2" class="mut"></span>
    <button class="icon tbtn" title="theme">◐</button>
  </header>
  <nav id="nav">
    <a data-pg="home" class="on">ホーム</a>
    <a data-pg="chart">グラフ</a>
    <a data-pg="data">データ</a>
    <a id="navList">← 端末選択</a>
    <a id="navLogout">ログアウト</a>
  </nav>
  <div class="mask" id="mask"></div>
  <main>
    <!-- ホーム -->
    <section class="pg on" id="pgHome">
      <div class="card" style="padding:8px"><span class="mut">最終データ</span> <b id="clock">--</b>
        <span id="upd" class="mut" style="margin-left:auto"></span></div>
      <div class="grid">
        <div class="card"><div class="mut">温度</div><div class="big"><span id="t">--</span>°C</div><div class="mut" id="tsub"></div></div>
        <div class="card"><div class="mut">湿度</div><div class="big"><span id="h">--</span>%</div></div>
      </div>
      <div class="card"><div class="mut">気圧</div><div class="big"><span id="p">--</span> hPa</div></div>
      <div class="card">
        <div class="row"><b>雷 危険度</b><span id="lv" class="lv0" style="margin-left:auto"></span></div>
        <div class="gauge"><i id="gg" style="width:0%"></i></div>
        <div class="row"><span class="big"><span id="dg">0</span>%</span></div>
        <div class="mut">累計 L/D: <span id="ldn">--/--</span></div>
      </div>
      <div class="card" id="moreTiles" hidden><div class="mut">その他チャンネル</div>
        <div id="tiles" class="grid" style="margin-top:6px"></div></div>
    </section>
    <!-- グラフ (1グラフ重ね描き + 表示選択) -->
    <section class="pg" id="pgChart">
      <div class="card">
        <div class="row">
          <label class="chk"><input type="checkbox" id="ck_temp" checked><span class="sw" style="background:#e07a4f"></span>温度</label>
          <label class="chk"><input type="checkbox" id="ck_humidity" checked><span class="sw" style="background:#5fb0d0"></span>湿度</label>
          <label class="chk"><input type="checkbox" id="ck_pressure" checked><span class="sw" style="background:#9f7fe0"></span>気圧</label>
          <label class="chk"><input type="checkbox" id="ck_lightning" checked><span class="sw" style="background:#ff5b5b"></span>雷</label>
          <span style="margin-left:auto"></span>
          <select id="selKeyTemp" title="温度チャンネル"></select>
          <select id="selRange">
            <option value="1">1時間</option><option value="24" selected>24時間</option>
            <option value="168">7日</option><option value="720">30日</option><option value="2160">90日</option>
          </select>
        </div>
        <div class="chartwrap"><canvas id="chart"></canvas></div>
        <div class="mut" id="chNote">左軸=温度/湿度/雷、右軸=気圧。凡例クリックでも表示切替できます。</div>
      </div>
    </section>
    <!-- データ (CSV) -->
    <section class="pg" id="pgData">
      <div class="card">
        <h2 style="margin-top:0">データ ダウンロード</h2>
        <div class="row"><a id="csvLink" href="#"><button>CSV ダウンロード (サーバ)</button></a>
          <span class="mut">サーバDB全期間 (daytime_utc,metric,sensor_key,value)</span></div>
        <table style="margin-top:8px">
          <tr><td>MAC</td><td id="dMac">--</td></tr>
          <tr><td>件数 (温/湿/気/雷)</td><td id="dCnt">--</td></tr>
          <tr><td>最終データ</td><td id="dLast">--</td></tr>
        </table>
      </div>
    </section>
  </main>
</div>

<script>
const $=s=>document.querySelector(s), $$=s=>[...document.querySelectorAll(s)];
async function getJSON(u){const r=await fetch(u,{credentials:'same-origin'});return {status:r.status,body:await r.json().catch(()=>({}))};}
async function postJSON(u,o){const r=await fetch(u,{method:'POST',credentials:'same-origin',
  headers:{'Content-Type':'application/json'},body:JSON.stringify(o||{})});return {status:r.status,body:await r.json().catch(()=>({}))};}
const esc=s=>String(s==null?'':s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const fmtT=s=>{if(!s)return '--';const d=new Date(s.replace(' ','T')+'Z');return isNaN(d)?s:d.toLocaleString('ja-JP');};
const LV=['安全','注意','警戒','危険','厳重警戒'];

// ---- テーマ (ダークモード, ESP32 同様 localStorage 保存) ----
const root=document.documentElement;
try{if(localStorage.wlbTheme)root.dataset.theme=localStorage.wlbTheme;}catch(e){}
function toggleTheme(){const d=root.dataset.theme==='dark'?'light':'dark';root.dataset.theme=d;
  try{localStorage.wlbTheme=d}catch(e){} if(chart)drawChart();}
$$('.tbtn').forEach(b=>b.onclick=toggleTheme);
function cssVar(n){return getComputedStyle(root).getPropertyValue(n).trim();}

let ME=null, DEVS=[], MAC=null, NAME=null, LATEST=null, chart=null, pollTimer=null;

function show(view){for(const v of ['vLogin','vList','vDash'])$('#'+v).hidden=(v!==view);
  if(ME){const s=ME.name+' (role '+ME.role+')';$('#who1').textContent=s;$('#who2').textContent=s;}
  if(view!=='vDash'){clearInterval(pollTimer);pollTimer=null;}}
function showLogin(){show('vLogin');$('#fLogin').focus();}

// ☰ メニュー
const nav=$('#nav'),mask=$('#mask');
const openM=v=>{nav.classList.toggle('open',v);mask.classList.toggle('open',v);};
$('#mbtn').onclick=()=>openM(!nav.classList.contains('open'));mask.onclick=()=>openM(false);
$$('#nav a[data-pg]').forEach(a=>a.onclick=()=>{
  const pg=a.dataset.pg;
  $$('#nav a[data-pg]').forEach(x=>x.classList.toggle('on',x===a));
  ['Home','Chart','Data'].forEach(k=>$('#pg'+k).classList.toggle('on',k.toLowerCase()===pg));
  openM(false); if(pg==='chart')drawChart();
});
$('#navList').onclick=gotoList;
$('#navLogout').onclick=()=>doLogout();
$('#btnLogout1').onclick=()=>doLogout();

// 端末選択
async function gotoList(){openM(false);show('vList');
  const r=await getJSON('api/devices.py');
  if(r.status===401){showLogin();return;}
  DEVS=(r.body&&r.body.devices)||[];const tb=$('#devBody');
  if(!DEVS.length){tb.innerHTML='<tr><td colspan="7" class="mut">端末はまだありません</td></tr>';return;}
  tb.innerHTML='';
  DEVS.forEach(d=>{const tr=document.createElement('tr');tr.className='dev';
    tr.innerHTML='<td>'+esc(d.mac)+'</td><td>'+esc(d.name)+'</td><td>'+fmtT(d.last)
     +'</td><td>'+(d.counts.temp|0)+'</td><td>'+(d.counts.humidity|0)+'</td><td>'+(d.counts.pressure|0)+'</td><td>'+(d.counts.lightning|0)+'</td>';
    tr.onclick=()=>openDash(d.mac,d.name);tb.appendChild(tr);});
}

// ダッシュボード
function curDev(){return DEVS.find(d=>d.mac===MAC)||{};}
async function openDash(mac,name){MAC=mac;NAME=name||mac;show('vDash');
  $('#dashTitle').textContent=NAME;
  $$('#nav a[data-pg]').forEach(x=>x.classList.toggle('on',x.dataset.pg==='home'));
  $('#pgHome').classList.add('on');$('#pgChart').classList.remove('on');$('#pgData').classList.remove('on');
  const d=curDev();
  $('#dMac').textContent=MAC;$('#csvLink').href='api/csv.py?mac='+encodeURIComponent(MAC);
  $('#dCnt').textContent=d.counts?((d.counts.temp|0)+'/'+(d.counts.humidity|0)+'/'+(d.counts.pressure|0)+'/'+(d.counts.lightning|0)):'--';
  $('#dLast').textContent=fmtT(d.last);
  await loadLatest();
  clearInterval(pollTimer);pollTimer=setInterval(loadLatest,30000);
}
async function loadLatest(){
  if(!MAC)return;
  const r=await getJSON('api/latest.py?mac='+encodeURIComponent(MAC));
  if(r.status===401){showLogin();return;}
  LATEST=(r.body&&r.body.latest)||{};
  const temps=LATEST.temp||[];
  const primary=temps.find(x=>x.key==='air'||x.key==='main')||temps[0];
  $('#t').textContent=primary&&primary.value!=null?Number(primary.value).toFixed(2):'--';
  $('#tsub').textContent=primary?('ch: '+esc(primary.key)):'';
  const hum=(LATEST.humidity||[])[0],pre=(LATEST.pressure||[])[0];
  $('#h').textContent=hum&&hum.value!=null?Number(hum.value).toFixed(2):'--';
  $('#p').textContent=pre&&pre.value!=null?Number(pre.value).toFixed(2):'--';
  const lns=LATEST.lightning||[];
  const dgo=lns.find(x=>x.key==='danger');const dg=dgo&&dgo.value!=null?Math.round(dgo.value):0;
  const li=dg<5?0:dg<25?1:dg<50?2:dg<75?3:4;
  $('#dg').textContent=dg;$('#gg').style.width=dg+'%';
  const lv=$('#lv');lv.textContent=LV[li];lv.className='lv'+li;
  const L=lns.find(x=>x.key==='L'),D=lns.find(x=>x.key==='D');
  $('#ldn').textContent=(L&&L.value!=null?L.value:'--')+'/'+(D&&D.value!=null?D.value:'--');
  let upd=null;['temp','humidity','pressure','lightning'].forEach(m=>(LATEST[m]||[]).forEach(x=>{if(x.daytime&&(!upd||x.daytime>upd))upd=x.daytime;}));
  $('#clock').textContent=upd?fmtT(upd):'--';$('#upd').textContent='取得 '+new Date().toLocaleTimeString('ja-JP');
  const extra=[];
  temps.forEach(x=>{if(x!==primary)extra.push(['温度('+x.key+')',x.value,'°C']);});
  (LATEST.humidity||[]).slice(1).forEach(x=>extra.push(['湿度('+x.key+')',x.value,'%']));
  (LATEST.pressure||[]).slice(1).forEach(x=>extra.push(['気圧('+x.key+')',x.value,'hPa']));
  const mt=$('#moreTiles');
  if(extra.length){mt.hidden=false;$('#tiles').innerHTML=extra.map(e=>'<div class=card><div class=mut>'+esc(e[0])+'</div><div class=big>'+(e[1]==null?'--':Number(e[1]).toFixed(2))+' <span class=mut>'+e[2]+'</span></div></div>').join('');}
  else mt.hidden=true;
  // 温度チャンネル選択肢
  const sk=$('#selKeyTemp');const keys=temps.map(x=>x.key);const uniq=[...new Set(keys.length?keys:['air'])];const cur=sk.value;
  sk.innerHTML=uniq.map(k=>'<option value="'+esc(k)+'">温度: '+esc(k)+'</option>').join('');
  if(uniq.includes(cur))sk.value=cur;
}

// ---- グラフ: 温度/湿度/気圧/雷 を1グラフに重ね描き (気圧=右軸y2) ----
const SERIES=[
  {m:'temp',     label:'温度 °C',   color:'#e07a4f', axis:'y',  ck:'ck_temp'},
  {m:'humidity', label:'湿度 %',    color:'#5fb0d0', axis:'y',  ck:'ck_humidity'},
  {m:'pressure', label:'気圧 hPa',  color:'#9f7fe0', axis:'y2', ck:'ck_pressure'},
  {m:'lightning',label:'雷 danger', color:'#ff5b5b', axis:'y',  ck:'ck_lightning'},
];
function metricKey(m){
  if(m==='temp')     return $('#selKeyTemp').value || ((LATEST&&LATEST.temp&&LATEST.temp[0]&&LATEST.temp[0].key)||'air');
  if(m==='lightning')return 'danger';
  return 'main';
}
async function drawChart(){
  if(!MAC)return;
  const hours=$('#selRange').value;
  const res=await Promise.all(SERIES.map(s=>getJSON('api/series.py?mac='+encodeURIComponent(MAC)+'&metric='+s.m+'&key='+encodeURIComponent(metricKey(s.m))+'&hours='+hours)));
  if(res.some(r=>r.status===401)){showLogin();return;}
  // タイムスタンプの和集合をラベルに (端末は同時刻でPUSHするので基本整列)
  const tset=new Set();
  res.forEach(r=>((r.body&&r.body.points)||[]).forEach(p=>tset.add(p.t)));
  const labels=[...tset].sort();
  const idx={};labels.forEach((t,i)=>idx[t]=i);
  const fg=cssVar('--fg'),line=cssVar('--line');
  const datasets=SERIES.map((s,si)=>{
    const arr=new Array(labels.length).fill(null);
    ((res[si].body&&res[si].body.points)||[]).forEach(p=>{arr[idx[p.t]]=p.v;});
    return {label:s.label,data:arr,borderColor:s.color,backgroundColor:s.color+'22',
      yAxisID:s.axis,pointRadius:0,tension:.2,spanGaps:true,hidden:!$('#'+s.ck).checked};
  });
  const disp=labels.map(fmtT);
  const total=labels.length;
  $('#chNote').textContent=(total?('点数: '+total+' / '):'')+'左軸=温度/湿度/雷、右軸=気圧。凡例クリックでも表示切替。時刻はローカル(DBはUTC)。';
  const opts={animation:false,responsive:true,maintainAspectRatio:false,
    plugins:{legend:{labels:{color:fg}}},
    scales:{
      x:{ticks:{color:fg,maxTicksLimit:8},grid:{color:line+'44'}},
      y:{position:'left',ticks:{color:fg},grid:{color:line+'44'}},
      y2:{position:'right',ticks:{color:fg},grid:{drawOnChartArea:false}}
    }};
  if(chart){chart.data.labels=disp;chart.data.datasets=datasets;chart.options=opts;chart.update();}
  else chart=new Chart($('#chart'),{type:'line',data:{labels:disp,datasets},options:opts});
}
SERIES.forEach(s=>$('#'+s.ck).addEventListener('change',()=>{
  if(!chart){drawChart();return;}
  const di=SERIES.indexOf(s);chart.data.datasets[di].hidden=!$('#'+s.ck).checked;chart.update();
}));
$('#selRange').onchange=drawChart;$('#selKeyTemp').onchange=drawChart;

// 認証
$('#btnLogin').onclick=async()=>{$('#loginErr').textContent='';
  const r=await postJSON('api/login.py',{login:$('#fLogin').value,pass:$('#fPass').value});
  if(r.status===200&&r.body.ok){ME=r.body.user;$('#fPass').value='';gotoList();}
  else $('#loginErr').textContent='メールアドレス/ユーザー名またはパスワードが違います。';};
$('#fPass').addEventListener('keydown',e=>{if(e.key==='Enter')$('#btnLogin').click();});
async function doLogout(){await postJSON('api/session.py',{});ME=null;openM(false);showLogin();}

// 起動
getJSON('api/session.py').then(r=>{
  if(r.body&&r.body.authed){ME=r.body.user;gotoList();}else showLogin();
}).catch(showLogin);
</script>
</body></html>
