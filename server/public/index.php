<?php
// ============================================================================
//  public/index.php  -  管理画面 (UIシェルのみ) v1.0
// ----------------------------------------------------------------------------
//  ログイン(+ゲスト) → ☰メニュー(権限別):
//    ホーム/チャート/データ(CSV) … 選択端末のダッシュボード
//    端末一覧(名称のみ) / 端末登録・変更 / ユーザー登録(admin/メール後)
//    アクセスログ(admin: IP集計⇄時系列⇄明細, IPクリックでWHOIS) / BAN管理(admin)
//  ゲスト(role=-1,パス無し): guest_public端末のみ閲覧。ログアウト/CSV/管理は不可。
//  API: /api/*.py (人間) / /webapi/*.py (端末)。ダーク(◐)対応。
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
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font-family:system-ui,-apple-system,"Segoe UI",sans-serif;font-size:15px}
header{position:sticky;top:0;display:flex;align-items:center;gap:10px;padding:10px 12px;background:var(--card);border-bottom:2px solid var(--line);z-index:10}
header h1{font-size:16px;margin:0;flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.icon{background:var(--card);color:var(--fg);border:1px solid var(--line);cursor:pointer;padding:6px 10px;font-size:18px;border-radius:8px}
input,button,select{font:inherit;padding:7px 10px;border:1px solid var(--line);border-radius:8px;color:var(--fg);background:var(--bg)}
button{background:var(--acc);color:#063;border:0;cursor:pointer}:root[data-theme=dark] button{color:#04240f}
button.ghost{background:var(--card);color:var(--acc);border:1px solid var(--line)}
button.sm{padding:3px 8px;font-size:13px}
main{padding:14px;max-width:900px;margin:0 auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin:10px 0}
label{color:var(--mut);font-size:13px}
.err{color:var(--ng);font-size:14px;margin:6px 0}.ok{color:var(--acc)}
.login{max-width:340px;margin:8vh auto}.login input{width:100%;margin:6px 0}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}.big{font-size:34px;font-weight:700}.mut{color:var(--mut);font-size:13px}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin:8px 0}
.gauge{height:14px;border-radius:7px;background:#8883;overflow:hidden}.gauge>i{display:block;height:100%;background:var(--acc)}
.lv0{color:#5fd08a}.lv1{color:#c9d05f}.lv2{color:#e0b04f}.lv3{color:#e07a4f}.lv4{color:#ff5b5b}
.st_ok{color:#5fd08a}.st_stale{color:#e0b04f}.st_err{color:#ff5b5b}
.chartwrap{position:relative;height:320px}
.chk{display:inline-flex;align-items:center;gap:5px;border:1px solid var(--line);border-radius:20px;padding:4px 10px;cursor:pointer;font-size:13px}
.chk input{width:auto;margin:0}.sw{width:12px;height:12px;border-radius:3px;display:inline-block}
table{width:100%;border-collapse:collapse}td,th{border:1px solid var(--line);padding:6px 8px;text-align:left;font-size:13px}
tr.click{cursor:pointer}tr.click:hover{background:#5fb98c22}a.iplink{color:var(--acc);cursor:pointer;text-decoration:underline}
nav{position:fixed;top:0;left:-280px;width:270px;height:100%;background:var(--card);border-right:2px solid var(--line);transition:left .2s;z-index:30;padding-top:56px;overflow:auto}
nav.open{left:0}nav a{display:block;padding:13px 18px;color:var(--fg);text-decoration:none;border-bottom:1px solid var(--line);cursor:pointer}nav a.on{background:var(--acc);color:#063}
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
    <label>メールアドレス / ユーザー名</label><input id="fLogin" type="text" autocomplete="username" autofocus>
    <label>パスワード</label><input id="fPass" type="password" autocomplete="current-password">
    <div class="row"><button id="btnLogin" style="flex:1">ログイン</button></div>
    <div class="row"><button id="btnGuest" class="ghost" style="flex:1">ゲストとして閲覧</button></div>
    <p class="mut">初期ユーザー: admin / wether。ゲストは公開端末のみ閲覧できます。</p>
    <div class="row"><a id="toAct" class="iplink">アカウントを有効化する</a></div>
    <div id="actBox" hidden>
      <hr>
      <div id="actMsg" class="err"></div>
      <label>メールアドレス</label><input id="aEmail" type="text">
      <label>認証コード(メール記載)</label><input id="aCode" type="text">
      <label>新しいパスワード(8文字以上)</label><input id="aPass" type="password">
      <div class="row"><button id="btnAct" style="flex:1">有効化</button></div>
    </div>
  </div></main>
</div>

<!-- アプリ -->
<div id="vApp" hidden>
  <header>
    <button class="icon" id="mbtn">☰</button>
    <h1 id="dashTitle">WetherLoggerBox</h1>
    <span id="who" class="mut"></span>
    <button class="icon tbtn" title="theme">◐</button>
  </header>
  <nav id="nav"></nav>
  <div class="mask" id="mask"></div>
  <main>
    <!-- ホーム -->
    <section class="pg on" id="pgHome">
      <div class="card" style="padding:8px"><span class="mut">最終更新</span> <b id="clock">--</b>
        <span id="devStatus" style="margin-left:auto;font-weight:700"></span></div>
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
      <div class="card" id="moreTiles" hidden><div class="mut">その他チャンネル</div><div id="tiles" class="grid" style="margin-top:6px"></div></div>
    </section>
    <!-- チャート -->
    <section class="pg" id="pgChart">
      <div class="card">
        <div class="row">
          <label class="chk"><input type="checkbox" id="ck_temp" checked><span class="sw" style="background:#e07a4f"></span>温度</label>
          <label class="chk"><input type="checkbox" id="ck_humidity" checked><span class="sw" style="background:#5fb0d0"></span>湿度</label>
          <label class="chk"><input type="checkbox" id="ck_pressure" checked><span class="sw" style="background:#9f7fe0"></span>気圧</label>
          <label class="chk"><input type="checkbox" id="ck_lightning" checked><span class="sw" style="background:#ff5b5b"></span>雷</label>
          <span style="margin-left:auto"></span>
          <select id="selKeyTemp" title="温度チャンネル"></select>
          <select id="selRange"><option value="1">1時間</option><option value="24" selected>24時間</option><option value="168">7日</option><option value="720">30日</option><option value="2160">90日</option></select>
        </div>
        <div class="chartwrap"><canvas id="chart"></canvas></div>
        <div class="mut" id="chNote">左軸=温度/湿度/雷、右軸=気圧。凡例クリックでも切替。</div>
      </div>
    </section>
    <!-- データ(CSV) -->
    <section class="pg" id="pgData">
      <div class="card"><h2 style="margin-top:0">データ ダウンロード</h2>
        <div class="row"><a id="csvLink" href="#"><button>CSV ダウンロード (サーバ)</button></a>
          <span class="mut">サーバDB全期間</span></div>
      </div>
    </section>
    <!-- 端末一覧 (名称のみ) -->
    <section class="pg" id="pgList">
      <div class="card"><h2 style="margin-top:0">端末一覧</h2>
        <table><thead><tr><th>端末名</th><th>最終データ</th></tr></thead><tbody id="listBody"></tbody></table>
        <p class="mut">クリックでダッシュボード表示。</p>
      </div>
    </section>
    <!-- 端末登録・変更 -->
    <section class="pg" id="pgDevedit">
      <div class="card">
        <div class="row"><button id="deTabNew">新規登録</button><button id="deTabEdit" class="ghost">変更</button></div>
        <!-- 新規登録 -->
        <div id="deNew">
          <div class="row"><label style="width:90px">MAC</label><input id="rgMac" placeholder="aa:bb:cc:dd:ee:ff" style="flex:1"></div>
          <div class="row"><label style="width:90px">端末名</label><input id="rgName" placeholder="端末名(任意)" style="flex:1"></div>
          <div class="row"><label class="chk"><input type="checkbox" id="rgGp">公開フラグ(ゲスト公開)</label></div>
          <div class="row"><button id="rgAdd">登録してキー送信</button><span id="rgMsg" class="mut"></span></div>
          <p class="mut">オーナー(あなた)のメールへ per-device キーを送付。端末「サーバ連携」の認証コード欄に入力→[接続]で有効化。</p>
        </div>
        <!-- 変更 -->
        <div id="deEdit" hidden>
          <div class="row"><label style="width:90px">検索</label><input id="deSearch" placeholder="端末名/MACで絞り込み(空=全件)" style="flex:1"></div>
          <table><thead><tr><th>MAC</th><th>端末名</th><th>公開</th><th></th></tr></thead><tbody id="deBody"></tbody></table>
          <span id="deMsg" class="mut"></span>
        </div>
      </div>
    </section>
    <!-- ユーザー登録(admin) -->
    <section class="pg" id="pgUsers">
      <div class="card"><h2 style="margin-top:0">ユーザー登録</h2>
        <div class="row"><label style="width:110px">ユーザー名</label><input id="uName" style="flex:1"></div>
        <div class="row"><label style="width:110px">メール</label><input id="uEmail" style="flex:1"></div>
        <div class="row"><label style="width:110px">ロール</label>
          <select id="uRole"><option value="0">参照のみ</option><option value="1">登録のみ</option><option value="2">編集削除可</option><option value="3">アドミン</option></select>
          <label style="width:80px">親ユーザ</label><select id="uParent"><option value="">なし</option></select></div>
        <div><label>利用可能端末(複数選択)</label><div id="uDevs" class="row" style="max-height:140px;overflow:auto"></div></div>
        <div class="row"><button id="uCreate">作成して認証コード送付</button><span id="uMsg" class="mut"></span></div>
      </div>
      <div class="card"><h2 style="margin-top:0">ユーザー一覧</h2>
        <table><thead><tr><th>ID</th><th>ユーザー名</th><th>メール</th><th>role</th><th>有効</th></tr></thead><tbody id="uList"></tbody></table>
      </div>
    </section>
    <!-- パスワード変更 -->
    <section class="pg" id="pgPasswd">
      <div class="card"><h2 style="margin-top:0">パスワード変更</h2>
        <div class="row"><label style="width:140px">現在のパスワード</label><input id="pwOld" type="password" style="flex:1"></div>
        <div class="row"><label style="width:140px">新しいパスワード</label><input id="pwNew" type="password" style="flex:1"></div>
        <div class="row"><button id="pwSave">変更</button><span id="pwMsg" class="mut"></span></div>
        <p class="mut">8文字以上。変更後も現在のセッションは有効です。</p>
      </div>
    </section>
    <!-- アクセスログ(admin) -->
    <section class="pg" id="pgLog">
      <div class="card">
        <div class="row"><b>アクセスログ</b><span style="margin-left:auto"></span>
          <button class="sm logv on" data-v="byip">IP集計</button>
          <button class="sm logv" data-v="byhour">時系列</button>
          <button class="sm logv" data-v="recent">明細</button>
        </div>
        <div id="logWrap"></div>
        <div class="mut" id="whoisOut"></div>
      </div>
    </section>
    <!-- BAN管理(admin) -->
    <section class="pg" id="pgBan">
      <div class="card"><h2 style="margin-top:0">禁止IP</h2>
        <div class="row"><input id="banIp" placeholder="IPアドレス" style="flex:1"><input id="banIpR" placeholder="理由(任意)" style="flex:1"><button id="banIpAdd">追加</button></div>
        <table><tbody id="banIpBody"></tbody></table>
      </div>
      <div class="card"><h2 style="margin-top:0">禁止端末(MAC)</h2>
        <div class="row"><input id="banMac" placeholder="MACアドレス" style="flex:1"><input id="banMacR" placeholder="理由(任意)" style="flex:1"><button id="banMacAdd">追加</button></div>
        <table><tbody id="banMacBody"></tbody></table>
      </div>
    </section>
  </main>
</div>

<script>
const $=s=>document.querySelector(s),$$=s=>[...document.querySelectorAll(s)];
async function getJSON(u){const r=await fetch(u,{credentials:'same-origin'});return {status:r.status,body:await r.json().catch(()=>({}))};}
async function postJSON(u,o){const r=await fetch(u,{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json'},body:JSON.stringify(o||{})});return {status:r.status,body:await r.json().catch(()=>({}))};}
const esc=s=>String(s==null?'':s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const fmtT=s=>{if(!s)return '--';const d=new Date(String(s).replace(' ','T')+'Z');return isNaN(d)?s:d.toLocaleString('ja-JP');};
const LV=['安全','注意','警戒','危険','厳重警戒'];
const root=document.documentElement;
try{if(localStorage.wlbTheme)root.dataset.theme=localStorage.wlbTheme;}catch(e){}
function toggleTheme(){const d=root.dataset.theme==='dark'?'light':'dark';root.dataset.theme=d;try{localStorage.wlbTheme=d}catch(e){}if(chart)drawChart();}
$$('.tbtn').forEach(b=>b.onclick=toggleTheme);
const cssVar=n=>getComputedStyle(root).getPropertyValue(n).trim();

let ME=null,DEVS=[],MAC=null,NAME=null,LATEST=null,chart=null,pollTimer=null;
const isGuest=()=>ME&&ME.role===-1, isAdmin=()=>ME&&ME.role>=3;
const UNIT={temp:'°C',humidity:'%',pressure:'hPa',lightning:''},MLABEL={temp:'温度',humidity:'湿度',pressure:'気圧',lightning:'雷'};

// ---- ナビ(権限別) ----
function buildNav(){
  const items=[['home','ホーム'],['chart','チャート']];
  if(!isGuest())items.push(['data','データ']);
  items.push(['list','端末一覧']);
  if(ME&&ME.role>=1)items.push(['devedit','端末登録・変更']);
  if(isAdmin()){items.push(['users','ユーザー登録']);items.push(['log','アクセスログ']);items.push(['ban','BAN管理']);}
  if(!isGuest())items.push(['passwd','パスワード変更']);
  const nav=$('#nav');nav.innerHTML='';
  items.forEach(([k,label])=>{const a=document.createElement('a');a.dataset.pg=k;a.textContent=label;a.onclick=()=>goPage(k);nav.appendChild(a);});
  if(!isGuest()){const a=document.createElement('a');a.textContent='ログアウト';a.onclick=doLogout;nav.appendChild(a);}
}
const PAGES=['home','chart','data','list','devedit','users','passwd','log','ban'];
function goPage(k){
  $$('#nav a[data-pg]').forEach(x=>x.classList.toggle('on',x.dataset.pg===k));
  PAGES.forEach(p=>{const el=$('#pg'+p.charAt(0).toUpperCase()+p.slice(1));if(el)el.classList.toggle('on',p===k);});
  openM(false);
  if(k==='chart')drawChart();
  else if(k==='list')loadList();
  else if(k==='devedit')loadDevEdit();
  else if(k==='users')loadUsers();
  else if(k==='log')loadLog(curLogView);
  else if(k==='ban')loadBans();
}
const nav=$('#nav'),mask=$('#mask');
const openM=v=>{nav.classList.toggle('open',v);mask.classList.toggle('open',v);};
$('#mbtn').onclick=()=>openM(!nav.classList.contains('open'));mask.onclick=()=>openM(false);

// ---- 起動/認証 ----
function showLogin(){$('#vApp').hidden=true;$('#vLogin').hidden=false;clearInterval(pollTimer);pollTimer=null;$('#fLogin').focus();}
async function enter(){$('#vLogin').hidden=true;$('#vApp').hidden=false;
  $('#who').textContent=ME.name+(isGuest()?' (guest)':' (role '+ME.role+')');
  buildNav();
  const r=await getJSON('api/devices.py');
  if(r.status===401){showLogin();return;}
  DEVS=(r.body&&r.body.devices)||[];
  if(DEVS.length){MAC=DEVS[0].mac;NAME=DEVS[0].name||MAC;}
  goPage('home');
  if(MAC){await openDash(MAC,NAME);}
  else{$('#clock').textContent='閲覧可能な端末がありません';}
}
$('#btnLogin').onclick=async()=>{$('#loginErr').textContent='';
  const r=await postJSON('api/login.py',{login:$('#fLogin').value,pass:$('#fPass').value});
  if(r.status===200&&r.body.ok){ME=r.body.user;$('#fPass').value='';enter();}
  else $('#loginErr').textContent='メールアドレス/ユーザー名またはパスワードが違います。';};
$('#fPass').addEventListener('keydown',e=>{if(e.key==='Enter')$('#btnLogin').click();});
$('#btnGuest').onclick=async()=>{const r=await postJSON('api/guest.py',{});if(r.status===200&&r.body.ok){try{sessionStorage.wlbGuest='1'}catch(e){}ME=r.body.user;enter();}else $('#loginErr').textContent='ゲスト閲覧を開始できませんでした。';};
async function doLogout(){await postJSON('api/session.py',{});try{sessionStorage.removeItem('wlbGuest')}catch(e){}ME=null;openM(false);showLogin();}

// ---- ダッシュボード ----
function curDev(){return DEVS.find(d=>d.mac===MAC)||{};}
async function openDash(mac,name){MAC=mac;NAME=name||mac;$('#dashTitle').textContent=NAME;
  const d=curDev();$('#csvLink').href='api/csv.py?mac='+encodeURIComponent(MAC);
  await loadLatest();clearInterval(pollTimer);pollTimer=setInterval(loadLatest,30000);
  goPage('home');
}
async function loadLatest(){if(!MAC)return;
  const r=await getJSON('api/latest.py?mac='+encodeURIComponent(MAC));
  if(r.status===401){showLogin();return;}
  LATEST=(r.body&&r.body.latest)||{};
  const temps=LATEST.temp||[],primary=temps.find(x=>x.key==='air'||x.key==='main')||temps[0];
  $('#t').textContent=primary&&primary.value!=null?Number(primary.value).toFixed(2):'--';
  $('#tsub').textContent=primary?('ch: '+esc(primary.key)):'';
  const hum=(LATEST.humidity||[])[0],pre=(LATEST.pressure||[])[0];
  $('#h').textContent=hum&&hum.value!=null?Number(hum.value).toFixed(2):'--';
  $('#p').textContent=pre&&pre.value!=null?Number(pre.value).toFixed(2):'--';
  const lns=LATEST.lightning||[],dgo=lns.find(x=>x.key==='danger'),dg=dgo&&dgo.value!=null?Math.round(dgo.value):0;
  const li=dg<5?0:dg<25?1:dg<50?2:dg<75?3:4;$('#dg').textContent=dg;$('#gg').style.width=dg+'%';
  const lv=$('#lv');lv.textContent=LV[li];lv.className='lv'+li;
  const L=lns.find(x=>x.key==='L'),D=lns.find(x=>x.key==='D');
  $('#ldn').textContent=(L&&L.value!=null?L.value:'--')+'/'+(D&&D.value!=null?D.value:'--');
  // 最終更新 + センサ異常判定 (stale>10分 / 欠測)
  let upd=null;['temp','humidity','pressure','lightning'].forEach(m=>(LATEST[m]||[]).forEach(x=>{if(x.daytime&&(!upd||x.daytime>upd))upd=x.daytime;}));
  $('#clock').textContent=upd?fmtT(upd):'--';
  const st=$('#devStatus');
  if(!upd){st.textContent='データなし';st.className='st_err';}
  else{const ageMin=(Date.now()-new Date(String(upd).replace(' ','T')+'Z').getTime())/60000;
    const missing=(primary&&primary.value==null)||(hum&&hum.value==null)||(pre&&pre.value==null);
    if(ageMin>10){st.textContent='状態: 更新途絶('+Math.round(ageMin)+'分)';st.className='st_stale';}
    else if(missing){st.textContent='状態: センサ異常(欠測)';st.className='st_err';}
    else{st.textContent='状態: 正常';st.className='st_ok';}}
  const extra=[];temps.forEach(x=>{if(x!==primary)extra.push(['温度('+x.key+')',x.value,'°C']);});
  (LATEST.humidity||[]).slice(1).forEach(x=>extra.push(['湿度('+x.key+')',x.value,'%']));
  (LATEST.pressure||[]).slice(1).forEach(x=>extra.push(['気圧('+x.key+')',x.value,'hPa']));
  const mt=$('#moreTiles');
  if(extra.length){mt.hidden=false;$('#tiles').innerHTML=extra.map(e=>'<div class=card><div class=mut>'+esc(e[0])+'</div><div class=big>'+(e[1]==null?'--':Number(e[1]).toFixed(2))+' <span class=mut>'+e[2]+'</span></div></div>').join('');}else mt.hidden=true;
  const sk=$('#selKeyTemp'),keys=temps.map(x=>x.key),uniq=[...new Set(keys.length?keys:['air'])],cur=sk.value;
  sk.innerHTML=uniq.map(k=>'<option value="'+esc(k)+'">温度: '+esc(k)+'</option>').join('');if(uniq.includes(cur))sk.value=cur;
}

// ---- チャート(重ね描き) ----
const SERIES=[{m:'temp',label:'温度 °C',color:'#e07a4f',axis:'y',ck:'ck_temp'},{m:'humidity',label:'湿度 %',color:'#5fb0d0',axis:'y',ck:'ck_humidity'},{m:'pressure',label:'気圧 hPa',color:'#9f7fe0',axis:'y2',ck:'ck_pressure'},{m:'lightning',label:'雷 danger',color:'#ff5b5b',axis:'y',ck:'ck_lightning'}];
function metricKey(m){if(m==='temp')return $('#selKeyTemp').value||((LATEST&&LATEST.temp&&LATEST.temp[0]&&LATEST.temp[0].key)||'air');if(m==='lightning')return 'danger';return 'main';}
async function drawChart(){if(!MAC)return;
  const hours=$('#selRange').value;
  const res=await Promise.all(SERIES.map(s=>getJSON('api/series.py?mac='+encodeURIComponent(MAC)+'&metric='+s.m+'&key='+encodeURIComponent(metricKey(s.m))+'&hours='+hours)));
  if(res.some(r=>r.status===401)){showLogin();return;}
  const tset=new Set();res.forEach(r=>((r.body&&r.body.points)||[]).forEach(p=>tset.add(p.t)));
  const labels=[...tset].sort(),idx={};labels.forEach((t,i)=>idx[t]=i);
  const fg=cssVar('--fg'),line=cssVar('--line');
  const datasets=SERIES.map((s,si)=>{const arr=new Array(labels.length).fill(null);((res[si].body&&res[si].body.points)||[]).forEach(p=>{arr[idx[p.t]]=p.v;});return {label:s.label,data:arr,borderColor:s.color,backgroundColor:s.color+'22',yAxisID:s.axis,pointRadius:0,tension:.2,spanGaps:true,hidden:!$('#'+s.ck).checked};});
  $('#chNote').textContent=(labels.length?('点数: '+labels.length+' / '):'')+'左軸=温度/湿度/雷、右軸=気圧。時刻はローカル(DBはUTC)。';
  const opts={animation:false,responsive:true,maintainAspectRatio:false,plugins:{legend:{labels:{color:fg}}},scales:{x:{ticks:{color:fg,maxTicksLimit:8},grid:{color:line+'44'}},y:{position:'left',ticks:{color:fg},grid:{color:line+'44'}},y2:{position:'right',ticks:{color:fg},grid:{drawOnChartArea:false}}}};
  if(chart){chart.data.labels=labels.map(fmtT);chart.data.datasets=datasets;chart.options=opts;chart.update();}
  else chart=new Chart($('#chart'),{type:'line',data:{labels:labels.map(fmtT),datasets},options:opts});
}
SERIES.forEach(s=>$('#'+s.ck).addEventListener('change',()=>{if(!chart){drawChart();return;}chart.data.datasets[SERIES.indexOf(s)].hidden=!$('#'+s.ck).checked;chart.update();}));
$('#selRange').onchange=drawChart;$('#selKeyTemp').onchange=drawChart;

// ---- 端末一覧(名称のみ) ----
async function loadList(){const r=await getJSON('api/devices.py');if(r.status===401){showLogin();return;}
  DEVS=(r.body&&r.body.devices)||[];const tb=$('#listBody');
  if(!DEVS.length){tb.innerHTML='<tr><td colspan=2 class=mut>端末がありません</td></tr>';return;}
  tb.innerHTML='';DEVS.forEach(d=>{const tr=document.createElement('tr');tr.className='click';
    tr.innerHTML='<td>'+esc(d.name||'(無名)')+'</td><td>'+fmtT(d.last)+'</td>';tr.onclick=()=>openDash(d.mac,d.name);tb.appendChild(tr);});
}
// ---- 端末登録・変更 ----
let DEVEDIT=[];
function deSetMode(edit){$('#deNew').hidden=edit;$('#deEdit').hidden=!edit;
  $('#deTabNew').classList.toggle('ghost',edit);$('#deTabEdit').classList.toggle('ghost',!edit);
  if(edit)renderEdit();}
$('#deTabNew').onclick=()=>deSetMode(false);
$('#deTabEdit').onclick=()=>deSetMode(true);
$('#deSearch').addEventListener('input',renderEdit);
async function loadDevEdit(){deSetMode(false);                 // 既定=新規登録
  const r=await getJSON('api/mydevices.py');
  DEVEDIT=(r.status===200&&r.body&&r.body.devices)?r.body.devices:[];
}
function renderEdit(){const q=($('#deSearch').value||'').toLowerCase();
  const rows=DEVEDIT.filter(d=>!q||((d.name||'').toLowerCase().includes(q)||String(d.mac).toLowerCase().includes(q)));
  const tb=$('#deBody');
  if(!rows.length){tb.innerHTML='<tr><td colspan=4 class=mut>'+(DEVEDIT.length?'該当なし':'編集可能な端末がありません')+'</td></tr>';return;}
  tb.innerHTML='';
  rows.forEach(d=>{const tr=document.createElement('tr');
    tr.innerHTML='<td>'+esc(d.mac)+'</td>'
      +'<td><input value="'+esc(d.name||'')+'" class=denm style=width:100%></td>'
      +'<td style=text-align:center><input type=checkbox class=dgp '+(d.guest_public?'checked':'')+'></td>'
      +'<td style=white-space:nowrap><button class="sm dsv">保存</button> <button class="sm ddel">削除</button></td>';
    tr.querySelector('.dsv').onclick=async()=>{const nm=tr.querySelector('.denm').value,gp=tr.querySelector('.dgp').checked?1:0;
      const rr=await postJSON('api/device_save.py',{mac:d.mac,name:nm,guest_public:gp});
      $('#deMsg').textContent=(rr.body&&rr.body.ok)?('保存しました: '+d.mac):'保存失敗';
      const t=DEVEDIT.find(x=>x.mac===d.mac);if(t){t.name=nm;t.guest_public=gp;}};
    tr.querySelector('.ddel').onclick=async()=>{if(!confirm(d.mac+' を削除しますか？(論理削除)'))return;
      const rr=await postJSON('api/device_delete.py',{mac:d.mac});
      if(rr.body&&rr.body.ok){$('#deMsg').textContent='削除しました: '+d.mac;DEVEDIT=DEVEDIT.filter(x=>x.mac!==d.mac);renderEdit();}
      else $('#deMsg').textContent='削除失敗';};
    tb.appendChild(tr);});
}
// ---- アクセスログ(admin) ----
let curLogView='byip';
$$('.logv').forEach(b=>b.onclick=()=>{curLogView=b.dataset.v;$$('.logv').forEach(x=>x.classList.toggle('on',x===b));loadLog(curLogView);});
async function loadLog(view){const r=await getJSON('api/accesslog.py?view='+view);const w=$('#logWrap');$('#whoisOut').textContent='';
  if(r.status===403){w.innerHTML='<div class=mut>権限がありません</div>';return;}
  const rows=(r.body&&r.body.rows)||[];
  if(view==='byip'){
    w.innerHTML='<table><thead><tr><th>IP</th><th>回数</th><th>失敗</th><th>blocked</th><th>最終</th></tr></thead><tbody>'+
      rows.map(x=>'<tr><td><a class=iplink data-ip="'+esc(x.ip)+'">'+esc(x.ip)+'</a></td><td>'+x.cnt+'</td><td>'+(x.ng||0)+'</td><td>'+(x.blocked||0)+'</td><td>'+fmtT(x.last)+'</td></tr>').join('')+'</tbody></table>';
    $$('#logWrap .iplink').forEach(a=>a.onclick=()=>whois(a.dataset.ip));
  }else if(view==='byhour'){
    w.innerHTML='<table><thead><tr><th>時間帯(UTC)</th><th>件数</th><th>ユニークIP</th><th>失敗</th></tr></thead><tbody>'+
      rows.map(x=>'<tr><td>'+esc(x.slot)+'</td><td>'+x.cnt+'</td><td>'+x.ips+'</td><td>'+(x.ng||0)+'</td></tr>').join('')+'</tbody></table>';
  }else{
    w.innerHTML='<table><thead><tr><th>時刻</th><th>IP</th><th>event</th><th>OS/ブラウザ</th><th>path</th></tr></thead><tbody>'+
      rows.map(x=>'<tr><td>'+fmtT(x.created_at)+'</td><td><a class=iplink data-ip="'+esc(x.ip)+'">'+esc(x.ip)+'</a></td><td>'+esc(x.event)+'</td><td>'+esc(x.os||'')+'/'+esc(x.browser||'')+'</td><td>'+esc(x.path||'')+'</td></tr>').join('')+'</tbody></table>';
    $$('#logWrap .iplink').forEach(a=>a.onclick=()=>whois(a.dataset.ip));
  }
}
async function whois(ip){$('#whoisOut').textContent='WHOIS取得中: '+ip+' ...';
  const r=await getJSON('api/whois.py?ip='+encodeURIComponent(ip));const b=r.body||{};
  $('#whoisOut').innerHTML='<b>'+esc(ip)+'</b> — 国: '+esc(b.country||'?')+' / ISP: '+esc(b.isp||'?')+' / 組織: '+esc(b.org||'?')+' / AS: '+esc(b.asn||'?')
    +' <button class="sm" onclick="banIpQuick(\''+esc(ip)+'\')">このIPを禁止</button>';
}
window.banIpQuick=async(ip)=>{await postJSON('api/bans.py',{type:'ip',value:ip,reason:'from accesslog'});alert('禁止IPに追加: '+ip);if($('#pgBan').classList.contains('on'))loadBans();};
// ---- BAN管理(admin) ----
async function loadBans(){const r=await getJSON('api/bans.py');const b=r.body||{};
  $('#banIpBody').innerHTML=((b.ip)||[]).map(x=>'<tr><td>'+esc(x.ip)+'</td><td>'+esc(x.reason||'')+'</td><td><button class="sm" data-ip="'+esc(x.ip)+'">解除</button></td></tr>').join('')||'<tr><td class=mut colspan=3>なし</td></tr>';
  $('#banMacBody').innerHTML=((b.device)||[]).map(x=>'<tr><td>'+esc(x.mac)+'</td><td>'+esc(x.reason||'')+'</td><td><button class="sm" data-mac="'+esc(x.mac)+'">解除</button></td></tr>').join('')||'<tr><td class=mut colspan=3>なし</td></tr>';
  $$('#banIpBody button[data-ip]').forEach(x=>x.onclick=async()=>{await postJSON('api/bans.py',{type:'ip',value:x.dataset.ip,action:'delete'});loadBans();});
  $$('#banMacBody button[data-mac]').forEach(x=>x.onclick=async()=>{await postJSON('api/bans.py',{type:'device',value:x.dataset.mac,action:'delete'});loadBans();});
}
$('#banIpAdd').onclick=async()=>{const v=$('#banIp').value.trim();if(!v)return;await postJSON('api/bans.py',{type:'ip',value:v,reason:$('#banIpR').value});$('#banIp').value='';$('#banIpR').value='';loadBans();};
$('#banMacAdd').onclick=async()=>{const v=$('#banMac').value.trim();if(!v)return;await postJSON('api/bans.py',{type:'device',value:v,reason:$('#banMacR').value});$('#banMac').value='';$('#banMacR').value='';loadBans();};

// ---- アカウント有効化(ログイン画面) ----
$('#toAct').onclick=()=>{$('#actBox').hidden=!$('#actBox').hidden;};
$('#btnAct').onclick=async()=>{$('#actMsg').textContent='';
  const r=await postJSON('api/user_activate.py',{email:$('#aEmail').value,code:$('#aCode').value,password:$('#aPass').value});
  if(r.status===200&&r.body.ok){$('#actMsg').className='ok';$('#actMsg').textContent='有効化しました。ログインしてください。';$('#actBox').hidden=true;}
  else{$('#actMsg').className='err';$('#actMsg').textContent='失敗: '+((r.body&&r.body.error)||'')+'(コード/期限/パスワード長を確認)';}};

// ---- ユーザー登録(admin) ----
async function loadUsers(){const r=await getJSON('api/users.py');if(r.status!==200){$('#uList').innerHTML='<tr><td colspan=5 class=mut>権限無し</td></tr>';return;}
  const us=(r.body&&r.body.users)||[],ds=(r.body&&r.body.devices)||[];
  $('#uParent').innerHTML='<option value="">なし</option>'+us.map(u=>'<option value="'+u.user_id+'">'+esc(u.username)+'</option>').join('');
  $('#uDevs').innerHTML=ds.map(d=>'<label class=chk><input type=checkbox class=udev value="'+esc(d.mac)+'">'+esc(d.device_name||d.mac)+'</label>').join('')||'<span class=mut>端末なし</span>';
  $('#uList').innerHTML=us.map(u=>'<tr><td>'+u.user_id+'</td><td>'+esc(u.username)+'</td><td>'+esc(u.email)+'</td><td>'+u.role+'</td><td>'+(u.activated?'✓':'未')+'</td></tr>').join('');
}
$('#uCreate').onclick=async()=>{$('#uMsg').textContent='送信中...';
  const macs=$$('#uDevs .udev:checked').map(x=>x.value);
  const r=await postJSON('api/users.py',{username:$('#uName').value,email:$('#uEmail').value,role:$('#uRole').value,parent_user_id:$('#uParent').value,macs});
  if(r.status===200&&r.body.ok){$('#uMsg').textContent=r.body.mailed?'作成・メール送付しました':('作成(メール不可)。認証コード: '+r.body.code);$('#uName').value='';$('#uEmail').value='';loadUsers();}
  else{$('#uMsg').textContent='失敗: '+((r.body&&r.body.error)||'');}};

// ---- 端末 新規登録(キーメール) ----
$('#rgAdd').onclick=async()=>{$('#rgMsg').textContent='送信中...';
  const r=await postJSON('api/device_register.py',{mac:$('#rgMac').value,name:$('#rgName').value,guest_public:$('#rgGp').checked?1:0});
  if(r.status===200&&r.body.ok){$('#rgMsg').textContent=r.body.mailed?'登録・キーをメール送付':('登録(メール不可)。キー: '+r.body.key);$('#rgMac').value='';$('#rgName').value='';loadDevEdit();}
  else{$('#rgMsg').textContent='失敗: '+((r.body&&r.body.error)||'');}};

// ---- パスワード変更 ----
$('#pwSave').onclick=async()=>{$('#pwMsg').textContent='';
  const r=await postJSON('api/passwd.py',{old:$('#pwOld').value,new:$('#pwNew').value});
  if(r.status===200&&r.body.ok){$('#pwMsg').textContent='変更しました';$('#pwOld').value='';$('#pwNew').value='';}
  else{$('#pwMsg').textContent='失敗: '+((r.body&&r.body.error)||'')+'(現パス/8文字以上)';}};

// 起動
getJSON('api/session.py').then(r=>{
  if(r.body&&r.body.authed){
    if(r.body.user&&r.body.user.role===-1){          // ゲストはタブ単位。新規タブは再入力へ
      let f=false;try{f=sessionStorage.wlbGuest==='1'}catch(e){}
      if(!f){postJSON('api/session.py',{}).catch(()=>{});showLogin();return;}  // Cookie破棄→ログイン画面
    }
    ME=r.body.user;enter();
  }else showLogin();
}).catch(showLogin);
</script>
</body></html>
