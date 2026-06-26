#include <ArduinoJson.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>          // GANTI: Library WiFi ESP8266
#include <ESPAsyncTCP.h>          // GANTI: Library TCP ESP8266
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <ArduinoJson.h>

// --- KONFIGURASI PIN (Disesuaikan untuk NodeMCU/Wemos D1) ---
#define BTN_A 3   // Pin rx (GPIO3)
#define LED_A 14  // Pin D5 (GPIO14)
#define LED_B 12  // Pin D6 (GPIO12)
#define LED_C 13  // Pin D7 (GPIO13) -> PWM OK
#define LED_D 15  // Pin D8 (GPIO15) -> PWM OK

// --- VARIABEL KUSTOM DISPLAY & LED ---
int ledc_max_br = 255;      // Max brightness LED C (0-255)
int ledc_blink_spd = 1500;  // Speed blink LED C (ms)
int oled_br_normal = 255;   // OLED normal brightness
int oled_br_dim = 20;       // OLED dimming brightness
int oled_time_norm = 5;     // Sisa detik sebelum redup (sebelum dimming)

bool btnReady = false;


// --- OBJEK & VARIABEL GLOBAL ---
Adafruit_SSD1306 display(128, 64, &Wire, -1);
AsyncWebServer server(80);

unsigned long Counter1 = 0, Time01 = 1000, Time02 = 1000, Time03 = 1000;
unsigned long sleepDuration = 101, processDuration = 0, startProcessMillis = 0;
String userNote = "Sistem Aktif.", configPass = "4444", themeColor = "#ff007f", mode_w = "ST";
int try_mode = 10;
String stSSID = "us", stPASS = "asdfghjkl", apSSID = "ESP8266_NeonPink", apPASS = "asdfghjkl", apStaticIP = "192.168.4.4";
String apGateway = "192.168.4.4", apSubnet = "255.255.255.0";

int step = 0, carouselStep = 0;
unsigned long prevMillis = 0, lastActivity = 0, lastCarouselMillis = 0, lastDebounce = 0;
bool isSleep = false, lastBtnState = HIGH, isProcessing = false, btnWasPressed = false;
long remainingSec = 0;

// --- FUNGSI LOG MONITOR (KEMBALI KE FORMAT ASLI) ---
void updateSerialLog(String trigger) {
  // Variabel statis untuk menyimpan kondisi terakhir
  static unsigned long lastCounter = 0;
  static unsigned long lastT1 = 0, lastT2 = 0, lastT3 = 0;
  static String lastMode = "";
  static bool lastSleepState = false;
  static String lastTrigger = "";

  // Periksa apakah ada perubahan pada data utama atau pemicu
  bool isChanged = (Counter1 != lastCounter) || 
                   (Time01 != lastT1 || Time02 != lastT2 || Time03 != lastT3) ||
                   (mode_w != lastMode) || 
                   (isSleep != lastSleepState) ||
                   (trigger != lastTrigger);

  // Jika tidak ada yang berubah, keluar dari fungsi (Jangan print apa pun)
  if (!isChanged) return;

  // Jika ada perubahan, perbarui snapshot data terakhir
  lastCounter = Counter1;
  lastT1 = Time01; lastT2 = Time02; lastT3 = Time03;
  lastMode = mode_w;
  lastSleepState = isSleep;
  lastTrigger = trigger;

  if (!Serial) Serial.begin(115200); 
  // Cetak Log ke Serial Monitor
  Serial.println("\n>>>> STORED CONFIGURATION DATA <<<<");
  Serial.printf("%-20s : %s\n", "Triggered by", trigger.c_str());
  Serial.printf("%-20s : %s\n", "Active Mode", mode_w.c_str());
  Serial.printf("%-20s : %lu\n", "Counter", Counter1);
  Serial.printf("%-20s : %lu ms\n", "Last Process Dur", processDuration);
  Serial.printf("%-20s : %lu | %lu | %lu ms\n", "Timer T1-T2-T3", Time01, Time02, Time03);
  Serial.printf("%-20s : %lu s\n", "Sleep Duration", sleepDuration);
  Serial.printf("%-20s : %s\n", "Config Password", configPass.c_str());
  Serial.printf("%-20s : %s / %s\n", "Station SSID/PASS", stSSID.c_str(), stPASS.c_str());
  Serial.printf("%-20s : %s\n", "Station IP", WiFi.localIP().toString().c_str());
  Serial.printf("%-20s : %s / %s\n", "AP SSID / PASS", apSSID.c_str(), apPASS.c_str());
  Serial.printf("%-20s : %s\n", "AP Static IP", apStaticIP.c_str());
  Serial.printf("%-20s : %d | %d\n", "LED Max Bright|Speed", ledc_max_br, ledc_blink_spd);
  Serial.printf("%-20s : %d | %d\n", "OLED Bright N|D", oled_br_normal, oled_br_dim);
  Serial.printf("%-20s : %d s\n", "OLED Time Normal", oled_time_norm);
  Serial.printf("%-20s : %s\n", "User Note", userNote.c_str());
  Serial.println("------------------------------------------");
  Serial.flush(); // Pastikan data terkirim semua
}



// --- CSS LENGKAP (PROGMEM) ---
const char CSS_LIB[] PROGMEM = R"rawliteral(
<style>
@import url('https://googleapis.com');
:root { --p: %THEME%; --bg: #050505; --card-bg: rgba(255, 255, 255, 0.03); }
body { background: var(--bg); color: #e0e0e0; font-family: 'Segoe UI', sans-serif; display: flex; flex-direction: column; align-items: center; padding: 20px; margin: 0; }
.main-title { font-family: 'Orbitron'; font-size: 1.6rem; color: var(--p); text-shadow: 0 0 10px var(--p); margin: 20px 0; text-transform: uppercase; letter-spacing: 3px; font-weight: 700; text-align: center; }
.card { background: var(--card-bg); backdrop-filter: blur(10px); border: 1px solid rgba(255, 255, 255, 0.1); padding: 20px; border-radius: 15px; margin: 10px; width: 95%%; max-width: 400px; text-align: center; border-top: 3px solid var(--p); box-shadow: 0 10px 30px rgba(0,0,0,0.5); }
.seven-seg { font-family: 'Orbitron'; font-size: 3.5rem; color: var(--p); text-shadow: 0 0 20px var(--p); background: rgba(0,0,0,0.6); padding: 15px; border-radius: 12px; display: block; margin: 15px 0; }
.status-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; margin-bottom: 15px; }
.st-box { background:rgba(255,255,255,0.03); border: 1px solid #333; border-radius:12px; padding:10px 0; text-align:center; transition: 0.3s; }
.st-label { font-size: 0.7rem; display: block; opacity: 0.5; margin-bottom: 4px; text-transform: uppercase; }
.timer-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-top: 10px; }
.timer-item { border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 12px; padding: 10px; background: rgba(255,255,255,0.02); display: flex; flex-direction: column; gap: 8px; }
.big-input { font-family: 'Orbitron'; font-size: 1.2rem; color: #fff; font-weight: bold; background: #000; border: 1px solid #333; border-radius: 8px; padding: 10px 0; width: 100%%; text-align: center; outline: none; }
.btn-set { padding: 8px 0; background: transparent; border: 1px solid var(--p); border-radius: 8px; cursor: pointer; font-weight: 700; color: var(--p); width: 100%%; text-transform: uppercase; font-size: 0.8rem; }
input, textarea { padding: 12px; background: #000; color: #fff; border: 1px solid #333; border-radius: 8px; width: 100%%; box-sizing: border-box; margin-top: 6px; outline: none; }
button { padding: 12px; background: transparent; border: 1px solid var(--p); border-radius: 8px; cursor: pointer; font-weight: 700; color: var(--p); width: 100%%; margin-top: 12px; text-transform: uppercase; transition: 0.3s; }
button:hover { background: var(--p); color: #000; box-shadow: 0 0 20px var(--p); }
.pg-cont { flex: 1; height: 8px; background: #222; border-radius: 4px; overflow: hidden; border: 1px solid rgba(255,255,255,0.1); }
#pg { width: 0%%; height: 100%%; background: var(--p); box-shadow: 0 0 10px var(--p); transition: 0.5s; }
.sub-glow { font-family: 'Orbitron'; font-size: 0.8rem; color: var(--p); text-shadow: 0 0 5px var(--p); text-transform: uppercase; margin-bottom: 10px; display: block; }
.sect-label { color: var(--p); font-size: 0.7rem; font-weight: bold; display: block; margin-top: 15px; text-align: left; text-transform: uppercase; opacity: 0.8; }
.pass-row { display: flex; align-items: center; gap: 8px; margin-top: 5px; }
.eye-btn { width: 65px; height: 41px; padding: 0; font-size: 10px; margin-top: 0; background: rgba(255,255,255,0.05); border-color: #444; color: #888; border-style: solid; border-width: 1px; border-radius: 8px; cursor: pointer; }
 .config-section {
    margin-top: 40px;
    padding: 20px;
    border-top: 1px solid rgba(255,255,255,0.05);
    width: 100%;
    display: flex;
    justify-content: center;
 }
 .btn-config {
    background: rgba(255, 255, 255, 0.03);
    border: 1px solid #444;
    color: #888;
    padding: 12px 25px;
    border-radius: 50px;
    font-size: 0.8rem;
    letter-spacing: 2px;
    transition: 0.4s;
    cursor: pointer;
    text-transform: uppercase;
    font-family: 'Orbitron';
 }
 .btn-config:hover {
    color: var(--p);
    border-color: var(--p);
    box-shadow: 0 0 20px rgba(var(--p), 0.2);
    background: rgba(var(--p), 0.05);
    transform: translateY(-2px);
 }
.timer-item:active {
    transform: scale(0.98);
    background: rgba(255,255,255,0.05);
}

</style>
)rawliteral";

// --- DASHBOARD UTAMA (PROGMEM) ---
// --- REORGANIZED MAIN DASHBOARD (ENGLISH) --- 
const char INDEX_HTML[] PROGMEM = R"rawliteral( 
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"> 
%CSS_LIB% 
<style> 
 /* UX Improvements for Dashboard */ 
 .system-status { font-size: 0.65rem; color: #888; letter-spacing: 2px; margin-bottom: 5px; display: block; } 
 .main-data-label { font-size: 0.7rem; opacity: 0.6; text-transform: uppercase; margin-top: -10px; margin-bottom: 10px; display: block; } 
 .action-btn { height: 55px; font-size: 0.9rem; margin-top: 15px; display: flex; align-items: center; justify-content: center; gap: 10px; } 
 .timer-val { font-size: 1.1rem !important; } 
 .note-box { border-left: 3px solid var(--p); text-align: left !important; padding: 12px 15px !important; background: rgba(255,255,255,0.01); } 
 .footer-link { width:100%%; max-width: 400px; background: transparent; border: 1px solid #222; color: #555; margin-top: 30px; font-size: 0.75rem; transition: 0.3s; } 
 .footer-link:hover { color: var(--p); border-color: var(--p); } 
 .config-item { transition: all 0.3s ease; cursor: pointer; } 
 .config-item:hover { border-color: var(--p) !important; box-shadow: 0 0 15px var(--p); background: rgba(255, 255, 255, 0.05) !important; transform: translateY(-2px); } 
</style> 
</head><body> 
 <div class="main-title">MYCTRL V4. XX</div> 
 
 <!-- MAIN MONITOR CARD --> 
 <div class="card"> 
 <span class="system-status">SYSTEM OPERATING IN %MODE% MODE</span> 
 <div id="c1" class="seven-seg">%COUNTER%</div> 
 <span class="main-data-label">Total Cycles Completed</span>

  <!-- PROGRESS BAR AREA --> 
 <div style="display: flex; align-items: center; gap: 15px; margin: 15px 0; background: rgba(255,255,255,0.02); padding: 12px; border-radius: 12px; border: 1px solid rgba(255,255,255,0.05);"> 
   <div class="pg-cont"><div id="pg"></div></div> 
   <div style="text-align: right; min-width: 100px;"> 
     <span class="st-label" style="font-size: 0.6rem; color: var(--p);">Elapsed Time</span> 
     <span style="font-family:'Orbitron'; color:var(--p); font-size:1.1rem; font-weight: bold;"><span id="elapsed_txt">%DUR%</span><small style="font-size:0.6rem; margin-left:2px;">ms</small></span> 
   </div> 
 </div> 

 <!-- INDICATORS GRID --> 
 <div class="status-grid"> 
 <div class="st-box" id="p0"><span class="st-label">Relay A</span><b id="l0">OFF</b></div> 
 <div class="st-box" id="p1"><span class="st-label">Relay B</span><b id="l1">OFF</b></div> 
 <div class="st-box" id="p2"><span class="st-label">System LED</span><b id="l2">OFF</b></div> 
 <div class="st-box" id="pb"><span class="st-label">BUTTON</span><b id="bt">RELEASED</b></div> 
 </div> 
 <!-- PRIMARY ACTIONS --> 
 <button id="btnW" class="action-btn" onclick="fetch('/wakeup')">WAKE UP OLED (%REM%s)</button> 
 <button class="action-btn" style="background: transparent; border-color: rgba(255,255,255,0.1); color: #888; font-size: 0.75rem; height: 40px;" onclick="if(confirm('Reset all counters?')) fetch('/reset').then(()=>location.reload())"> RESET CYCLE COUNTER </button> 
 </div> 
 
 <!-- TIMER SETTINGS CARD --> 
 <div class="card"> 
 <span class="sub-glow" style="font-size:0.7rem; margin-bottom: 15px;">SEQUENCE TIMING (ms)</span> 
 <div class="timer-grid"> 
 <form onsubmit="sendT(event,this)" action="/sT1" class="timer-item"> 
 <span class="st-label" style="color:var(--p)">Phase 1</span> 
 <input type="number" inputmode="numeric" min="0" max="999999" class="big-input timer-val" name="v" value="%T1%"> 
 <button type="submit" class="btn-set">SET</button> 
</form> 
<form onsubmit="sendT(event,this)" action="/sT2" class="timer-item"> 
 <span class="st-label" style="color:var(--p)">Phase 2</span> 
 <input type="number" inputmode="numeric" min="0" max="999999" class="big-input timer-val" name="v" value="%T2%"> 
 <button type="submit" class="btn-set">SET</button> 
</form> 
<form onsubmit="sendT(event,this)" action="/sT3" class="timer-item"> 
 <span class="st-label" style="color:var(--p)">Phase 3</span> 
 <input type="number" inputmode="numeric" min="0" max="999999" class="big-input timer-val" name="v" value="%T3%"> 
 <button type="submit" class="btn-set">SET</button> 
</form> 
 </div> 
 </div> 
 <!-- LOGS / NOTES CARD --> 
 <div class="card note-box"> 
 <span class="st-label" style="margin-bottom: 5px;">System Note:</span> 
 <p style="font-size:0.9rem; color: #eee; margin: 0; line-height: 1.4;">%NOTE%</p> 
 </div> 
<!-- SYSTEM CONFIGURATION CARD --> 
<div class="card" style="margin-top: 25px;"> 
 <div class="timer-grid" style="grid-template-columns: 1fr;"> 
 <div class="timer-item config-item" onclick="let p=prompt('Enter Password:'); if(p) location.href='/net?p='+p"> 
 <span class="st-label">Administrator</span> 
 <span style="font-family:'Orbitron'; color:var(--p); font-size:0.85rem; letter-spacing: 2px;"> SYSTEM CONFIGURATION </span> 
 </div> 
 </div> 
</div> 
</div> 

<script> 
 let localRem = 0; 
 let isProcessingWeb = false;
 let baseElapsed = 0;
 let lastSyncTime = 0;

 function upInd(id, pid, isActive, isBtn=false, customOn='ON'){ 
   let el = document.getElementById(id); let p = document.getElementById(pid); 
   if(!el || !p) return; 
   if(isBtn) { el.innerText = isActive ? 'PRESSED' : 'RELEASED'; } 
   else { el.innerText = isActive ? customOn : 'OFF'; } 
   el.style.color = isActive ? 'var(--p)' : (isBtn ? '#666' : '#333'); 
   p.style.borderColor = isActive ? 'var(--p)' : (isBtn ? '#444' : '#222'); 
   p.style.boxShadow = isActive ? '0 0 15px var(--p)' : 'none'; 
 } 

 // 1. FUNGSI SYNC DATA DARI SERVER (Setiap 2 Detik) 
 setInterval(() => { 
   fetch('/st').then(r => r.json()).then(d => { 
     document.getElementById('c1').innerText = d.c1; 
     
     isProcessingWeb = d.isp;
     if(isProcessingWeb) {
       baseElapsed = d.now - d.str;
       lastSyncTime = performance.now();
     } else {
       // Jika sistem idle, tampilkan durasi final dari proses terakhir
       document.getElementById('elapsed_txt').innerText = d.dur; 
     }
     
     upInd('l0', 'p0', d.la, false, 'ACTIVE'); 
     upInd('l1', 'p1', d.lb, false, 'ACTIVE'); 
     upInd('l2', 'p2', d.lc == 1, false, 'ON'); 
     upInd('bt', 'pb', d.bt, true); 
     
     localRem = d.rem; 
     document.getElementById('btnW').innerText = d.slp ? 'WAKE UP OLED' : 'AUTO-SLEEP IN: ' + localRem + 's'; 
     
     let p = d.st == 1 ? 33 : (d.st == 2 ? 66 : (d.st == 3 ? 100 : 0)); 
     document.getElementById('pg').style.width = p + '%'; 
   }).catch(err => console.error("Sync Error")); 
 }, 2000); 

 // 2. FUNGSI HITUNG MUNDUR LOKAL (Setiap 1 Detik Pas) 
 setInterval(() => { 
   let btn = document.getElementById('btnW'); 
   if (localRem > 0 && !btn.innerText.includes('WAKE UP')) { 
     localRem--; 
     btn.innerText = 'AUTO-SLEEP IN: ' + localRem + 's'; 
   } 
 }, 1000); 

 // 3. FUNGSI STOPWATCH REAL-TIME (Setiap 50ms)
 setInterval(() => {
   if(isProcessingWeb) {
     let currentNow = performance.now();
     let timePassedSinceSync = currentNow - lastSyncTime;
     document.getElementById('elapsed_txt').innerText = Math.round(baseElapsed + timePassedSinceSync);
   }
 }, 50);

 function sendT(e, f) { 
   e.preventDefault(); 
   fetch(f.action + '?v=' + f.v.value) 
   .then(response => { 
     if(response.ok) { alert('Timing Updated Successfully'); location.reload(); } 
     else { alert('Input tidak valid! Hanya angka murni.'); } 
   }) 
   .catch(err => alert('Koneksi terputus!')); 
 } 
</script> 
</body></html> 
)rawliteral";




// --- HALAMAN KONFIGURASI (PROGMEM) ---
// --- FULLY REORGANIZED CONFIG PAGE (ENGLISH) --- 
const char CONFIG_HTML[] PROGMEM = R"rawliteral( 
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1"> 
%CSS_LIB% 
<style>
  /* Minor UI Enhancements */
  .group-box { border: 1px solid rgba(255,255,255,0.1); padding: 18px; border-radius: 12px; margin-bottom: 25px; text-align: left; background: rgba(255,255,255,0.01); }
  .group-title { font-family: 'Orbitron'; color: var(--p); font-size: 0.8rem; letter-spacing: 1px; border-bottom: 1px solid rgba(255,255,255,0.1); padding-bottom: 8px; margin-bottom: 15px; display: block; text-transform: uppercase; }
  label { font-size: 0.8rem; color: #bbb; display: block; margin-top: 12px; margin-bottom: 4px; }
  .hint { font-size: 0.7rem; color: #666; font-style: italic; margin-bottom: 5px; display: block; }
  input[type="color"] { padding: 5px; height: 45px; border: 1px solid #333; }
</style>
</head><body> 
 <div class="main-title">MYCTRL CONFIG</div> 
 <div class="card"><form onsubmit="doSv(event)"> 
 
 <!-- GROUP 1: DISPLAY & LIGHTS -->
 <div class="group-box">
    <span class="group-title">Display & LED Setup</span>
    
    <label>LED Indicator Intensity (0-255)</label>
    <input name="lmx" type="number" min="0" max="255" value="%LMX%" onchange="checkRange(this)">
    
    <label>LED Breathing Pulse (ms)</label>
    <span class="hint">*Higher = Slower pulse</span>
    <input name="lsp" type="number" value="%LSP%"> 

    <hr style="border:0; border-top:1px solid rgba(255,255,255,0.05); margin: 20px 0 10px 0;">

    <label>OLED Active Brightness (0-255)</label>
    <input name="obn" type="number" min="0" max="255" value="%OBN%" onchange="checkRange(this)"> 
    
    <label>OLED Dimmed Brightness (0-255)</label>
    <input name="obd" type="number" min="0" max="255" value="%OBD%" onchange="checkRange(this)"> 
    
    <label>Delay Before Dimming (Seconds)</label>
    <input name="otn" type="number" value="%OTN%"> 
    
    <label>Delay Before Sleep (Seconds)</label>
    <input name="sd" type="number" value="%SD%"> 
 </div>

 <!-- GROUP 2: NETWORK CONFIGURATION -->
 <div class="group-box">
    <span class="group-title">Network Connectivity</span>
    
    <small style="color:var(--p); text-transform:uppercase; font-size:0.7rem; font-weight:bold;">STATION MODE</small>
    <label>ST SSID</label>
    <input name="sts" value="%STSSID%" placeholder="Enter WiFi Name"> 
    <label>ST PASS</label>
    <div class="pass-row"><input id="stp" name="stp" type="password" value="%STPASS%"><button type="button" class="eye-btn" onclick="tog('stp')">SHOW</button></div> 
    <label>Change to mode (Seconds)</label>
    <input name="tm" type="number" value="%TM%"> 
    
    <div style="margin-top: 25px; border-top: 1px solid rgba(255,255,255,0.05); padding-top: 10px;"></div>
    <small style="color:var(--p); text-transform:uppercase; font-size:0.7rem; font-weight:bold;">Device Hotspot (AP Mode)</small>
    <label>AP SSID</label>
    <input name="s" value="%APSSID%"> 
    <label>AP PASS</label>
    <div class="pass-row"><input id="app" name="app" type="password" value="%APP%"><button type="button" class="eye-btn" onclick="tog('app')">SHOW</button></div> 
    <label>Static Gateway IP</label>
    <input name="sip" value="%APIP%"> 
 </div>

 <!-- GROUP 3: SYSTEM & APP INTERFACE -->
 <div class="group-box">
    <span class="group-title">System Customization</span>
    
    <label>Access Password</label>
    <span class="hint">*Password to enter this config page</span>
    <div class="pass-row"><input id="cp" name="cp" type="password" value="%CP%"><button type="button" class="eye-btn" onclick="tog('cp')">SHOW</button></div> 
    
    <label>UI Accent Color</label>
    <input type="color" name="tc" value="%THEME%" oninput="document.documentElement.style.setProperty('--p',this.value)"> 
    
    <label>System Note / Label</label>
    <textarea name="nt" style="height:80px; font-size:0.85rem;">%NOTE_RAW%</textarea> 
 </div>

 <!-- DANGER ZONE -->
 <div style="background: rgba(255,68,68,0.05); border: 1px solid #ff4444; padding: 15px; border-radius: 12px;">
    <span style="color:#ff4444; font-family:'Orbitron'; font-size:0.75rem; display:block; margin-bottom:10px;">DANGER ZONE</span>
    <label style="color:#ff8888; margin-top:0;">Type RESET to factory wipe</label>
    <input name="fr" placeholder="Type RESET to confirm"> 
 </div>

 <button type="submit" style="background:var(--p); color:#000; margin-top:25px; font-size:1rem; height:50px;">APPLY & REBOOT</button> 
 <button type="button" onclick="location.href='/'" style="background:transparent; color:#888; border:none; margin-top:10px; font-size:0.8rem;">DISCARD CHANGES</button> 
 </form></div> 

 <script> 
 function tog(i){ const x=document.getElementById(i); x.type=x.type==='password'?'text':'password'; event.target.innerText=x.type==='password'?'SHOW':'HIDE'; } 
 function checkRange(el) { 
    let val = parseInt(el.value); 
    if (val < 0 || val > 255) { 
        alert("Value out of range! Please use 0 to 255."); 
        el.value = val < 0 ? 0 : 255;
    } 
 } 
 function doSv(e){ 
    e.preventDefault(); 
    if(confirm('Apply changes and reboot device?')){
        const f = e.target; 
        let q = new URLSearchParams(new FormData(f)).toString(); 
        fetch('/saveAll?' + q).then(() => { 
            alert('Saving... System will be back online in 5 seconds.');
            setTimeout(() => { location.href = '/'; }, 5000); 
        }); 
    }
 } 
 </script> 
</body></html> 
)rawliteral";



// --- FUNGSI PENDUKUNG ---
String processor(const String& var) {
    if (var == "CSS_LIB") return String(CSS_LIB);
    if (var == "THEME")   return themeColor;
    if (var == "MODE")    return mode_w;
    if (var == "COUNTER") return String(Counter1);
    if (var == "DUR")     return String(processDuration);
    if (var == "REM")     return String(remainingSec);
    if (var == "NOTE")    
    {
        String webNote = userNote; 
        webNote.replace("\n", "<br>"); 
        return webNote;
    }
    if (var == "NOTE_RAW") return userNote; 
    if (var == "STSSID")  return stSSID;
    if (var == "STPASS")  return stPASS;
    if (var == "CP")      return configPass;
    if (var == "TM")      return String(try_mode);
    if (var == "SD")      return String(sleepDuration);
    if (var == "APSSID")  return apSSID;
    if (var == "APP")     return apPASS;
    if (var == "APIP")    return apStaticIP;
    if (var == "T1")      return String(Time01);
    if (var == "T2")      return String(Time02);
    if (var == "T3")      return String(Time03);

    if (var == "LMX") return String(ledc_max_br); if (var == "LSP") return String(ledc_blink_spd);
    if (var == "OBN") return String(oled_br_normal);
    if (var == "OBD") return String(oled_br_dim);
    if (var == "OTN") return String(oled_time_norm);
    return String();
}

void saveData() {
  File f = LittleFS.open("/config.dat", "w");
  if(f) {
    String safeNote = userNote;
    safeNote.replace("\n", "|");
    
    // Gabungkan format string (%...) dan daftar variabel dengan teliti
    f.printf("%lu\n%lu\n%lu\n%lu\n%lu\n%s\n%s\n%s\n%s\n%d\n%s\n%s\n%s\n%s\n%s\n%lu\n%d\n%d\n%d\n%d\n%d\n", 
      Counter1, Time01, Time02, Time03, sleepDuration, 
      apSSID.c_str(), apPASS.c_str(), safeNote.c_str(), 
      apStaticIP.c_str(), try_mode, stSSID.c_str(), stPASS.c_str(), 
      apGateway.c_str(), configPass.c_str(), themeColor.c_str(), processDuration,
      ledc_max_br, ledc_blink_spd, oled_br_normal, oled_br_dim, oled_time_norm); 
      
    f.close();
  }
}


void loadData() {
    if (LittleFS.exists("/config.dat")) {
        File f = LittleFS.open("/config.dat", "r");
        Counter1 = f.readStringUntil('\n').toInt();
        Time01 = f.readStringUntil('\n').toInt();
        Time02 = f.readStringUntil('\n').toInt();
        Time03 = f.readStringUntil('\n').toInt();
        sleepDuration = f.readStringUntil('\n').toInt();
        apSSID = f.readStringUntil('\n'); apSSID.trim();
        apPASS = f.readStringUntil('\n'); apPASS.trim();

        userNote = f.readStringUntil('\n'); 
        userNote.trim(); 
        userNote.replace("|", "\n");

        apStaticIP = f.readStringUntil('\n'); apStaticIP.trim();
        try_mode = f.readStringUntil('\n').toInt();
        stSSID = f.readStringUntil('\n'); stSSID.trim();
        stPASS = f.readStringUntil('\n'); stPASS.trim();
        apGateway = f.readStringUntil('\n'); apGateway.trim();
        configPass = f.readStringUntil('\n'); configPass.trim();
        if(f.available()) { themeColor = f.readStringUntil('\n'); themeColor.trim(); }
        if(f.available()) { processDuration = f.readStringUntil('\n').toInt(); }
        if(f.available()) ledc_max_br = f.readStringUntil('\n').toInt();
        if(f.available()) ledc_blink_spd = f.readStringUntil('\n').toInt();
        if(f.available()) oled_br_normal = f.readStringUntil('\n').toInt();
        if(f.available()) oled_br_dim = f.readStringUntil('\n').toInt();
        if(f.available()) oled_time_norm = f.readStringUntil('\n').toInt();
        f.close();
    }
}

void wakeUpSystem() {
    lastActivity = millis();
    if (isSleep) { 
        isSleep = false; 
        display.ssd1306_command(SSD1306_SETCONTRAST);
        display.ssd1306_command(oled_br_normal); // Set ke terang normal
        analogWrite(LED_C, 0);   // Matikan detak PWM
        digitalWrite(LED_C, LOW); // Pastikan LED mati total
        updateSerialLog("WAKE_UP_TRIGGER");  // Log saat bangun
    }

}

void updateOLED() {
  static unsigned long lastOLEDUpdate = 0;
  if (millis() - lastOLEDUpdate < 100) return;
  lastOLEDUpdate = millis();
  
  if (isSleep) return;

  display.clearDisplay();

  // --- AREA KUNING (HEADER) ---
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 2);

  if (millis() - lastCarouselMillis > 2000) {
    carouselStep = (carouselStep + 1) % 3;
    lastCarouselMillis = millis();
  }

  if (carouselStep == 0) display.print("MODE: " + mode_w);
  else if (carouselStep == 1) display.print("SSID: " + (mode_w == "ST" ? stSSID : apSSID));
  else display.print("IP  : " + (mode_w == "ST" ? WiFi.localIP().toString() : WiFi.softAPIP().toString()));

  // --- AREA BIRU ---
  display.setTextColor(SSD1306_WHITE);

  // 1. Baris Cycle & Sisa Waktu (Y: 18)
  display.setCursor(0, 18);
  display.print("CYCLE: "); display.print(Counter1);

  String remStr = String(remainingSec) + "s";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(remStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(128 - w, 18); 
  display.print(remStr);

  // --- GARIS PEMBATAS BARU (Y: 28) ---
  display.drawLine(0, 28, 128, 28, SSD1306_WHITE);

  // 2. Status Bar (Y: 32 - Sedikit diturunkan agar tidak menempel garis)
  String btnState = (digitalRead(BTN_A) == LOW) ? "P" : "R";
  display.setCursor(0, 32); 
  display.printf("I:%s | A:%d B:%d C:%d", 
                 btnState.c_str(), 
                 !digitalRead(LED_A), 
                 !digitalRead(LED_B), 
                 digitalRead(LED_C));

  // 3. Kotak Timer T1, T2, T3 (Tetap di paling bawah Y: 43)
  for(int i = 0; i < 3; i++) {
    int x = i * 43; 
    display.drawRect(x, 43, 41, 20, SSD1306_WHITE); 
    display.setCursor(x + 13, 45); display.printf("T%d", i + 1);
    display.setCursor(x + 4, 53);  display.printf("%4lu", (i == 0) ? Time01 : (i == 1) ? Time02 : Time03);
  }

  display.display();
}





void displayBooting(String status) {
  display.clearDisplay();
  
  // Header Putih
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(18, 3);
  display.print("SYSTEM BOOTING");

  // Status Koneksi
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 25);
  display.print("Connection Mode:");
  
  display.setCursor(0, 38);
  display.setTextSize(1); // Gunakan size 1 atau 2 sesuai selera
  display.print(">> " + status);
  
  // Animasi Loading sederhana (opsional)
  display.drawRect(0, 54, 128, 6, SSD1306_WHITE);
  static int loadBar = 0;
  loadBar = (loadBar + 20) % 120;
  display.fillRect(4, 56, loadBar, 2, SSD1306_WHITE);
  
  display.display();
}


void setup() {
  // 1. Inisialisasi Serial Monitor (Tetap Aktif)
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n[SYSTEM] Starting Setup..."));

  // 2. Inisialisasi Hardware Pin
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(LED_A, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(LED_C, OUTPUT);
  pinMode(LED_D, OUTPUT);
  
  // Pastikan Relay/LED mati di awal (HIGH untuk Active Low)
  digitalWrite(LED_A, HIGH);
  digitalWrite(LED_B, HIGH);
  digitalWrite(LED_C, LOW);
  digitalWrite(LED_D, LOW);

   // Cek kondisi awal saat booting
  if (digitalRead(BTN_A) == LOW) {
    btnReady = false; // Jika ditekan saat nyala, tandai sebagai BELUM SIAP
    Serial.println(F("[WARN] Button pressed on boot. Ignoring until released."));
  } else {
    btnReady = true;  // Jika tidak ditekan, langsung SIAP
  }


  // 3. Inisialisasi Layar OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("[ERROR] OLED Failed")); 
  }
  display.clearDisplay();
  display.display();

  // 4. Inisialisasi Memori & Muat Data
  if (LittleFS.begin()) {
    loadData();
    Serial.println(F("[SYSTEM] Data Loaded from LittleFS."));
  }

  // 5. Proses Koneksi WiFi
  displayBooting("TRY CONNECT ST");
  WiFi.mode(WIFI_STA); 
  WiFi.begin(stSSID.c_str(), stPASS.c_str());
  
  unsigned long startAtt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAtt) < (try_mode * 1000)) {
    delay(100); 
    yield(); 
  }

  if (WiFi.status() != WL_CONNECTED) {
    mode_w = "AP"; 
    WiFi.mode(WIFI_AP);
    IPAddress _ip, _gw, _sn(255,255,255,0);
    _ip.fromString(apStaticIP); 
    _gw.fromString(apStaticIP);
    WiFi.softAPConfig(_ip, _gw, _sn);
    WiFi.softAP(apSSID.c_str(), apPASS.c_str());
    displayBooting("ST FAIL! START AP");
  } else { 
    mode_w = "ST"; 
    displayBooting("ST CONNECTED!");
  }
  delay(1000);

  // 6. Daftarkan Server Routes (Halaman Web)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ 
    r->send_P(200, "text/html", INDEX_HTML, processor); 
  });
  
  server.on("/net", HTTP_GET, [](AsyncWebServerRequest *r){
    String p = r->hasParam("p") ? r->getParam("p")->value() : "";
    if(p != configPass) { r->send(403, "text/plain", "DENIED"); return; }
    r->send_P(200, "text/html", CONFIG_HTML, processor);
  });
  
  server.on("/st", HTTP_GET, [](AsyncWebServerRequest *r){
    JsonDocument doc;
    doc["c1"] = Counter1; doc["dur"] = processDuration;
    doc["la"] = !digitalRead(LED_A); doc["lb"] = !digitalRead(LED_B); 
    doc["lc"] = isSleep ? 1 : 0; doc["bt"] = !digitalRead(BTN_A); 
    doc["st"] = step; doc["rem"] = remainingSec; doc["slp"] = isSleep;
    doc["isp"] = isProcessing; doc["now"] = millis(); doc["str"] = startProcessMillis;
    String js; serializeJson(doc, js); r->send(200, "application/json", js);
  });

  server.on("/saveAll", HTTP_GET, [](AsyncWebServerRequest *r){
    if(r->hasParam("fr") && r->getParam("fr")->value() == "RESET") { LittleFS.remove("/config.dat"); r->send(200, "text/plain", "Resetting..."); delay(500); ESP.restart(); }
    if(r->hasParam("nt")) userNote =  r->getParam("nt")->value(); 
    if(r->hasParam("cp")) configPass = r->getParam("cp")->value();
    if(r->hasParam("sd")) sleepDuration = r->getParam("sd")->value().toInt();
    if(r->hasParam("sts")) stSSID = r->getParam("sts")->value();
    if(r->hasParam("stp")) stPASS = r->getParam("stp")->value();
    if(r->hasParam("s")) apSSID = r->getParam("s")->value();
    if(r->hasParam("app")) apPASS = r->getParam("app")->value();
    if(r->hasParam("sip")) apStaticIP = r->getParam("sip")->value();
    if(r->hasParam("tc")) themeColor = r->getParam("tc")->value();
    if(r->hasParam("lmx")) ledc_max_br = constrain(r->getParam("lmx")->value().toInt(), 0, 255);
    if(r->hasParam("lsp")) ledc_blink_spd = r->getParam("lsp")->value().toInt();
    if(r->hasParam("obn")) oled_br_normal = constrain(r->getParam("obn")->value().toInt(), 0, 255);
    if(r->hasParam("obd")) oled_br_dim = constrain(r->getParam("obd")->value().toInt(), 0, 255);
    if(r->hasParam("otn")) oled_time_norm = r->getParam("otn")->value().toInt();
    
    saveData();
    delay(1000);
    ESP.restart();
  });

   // === GANTI KODE ROUTE KEMARIN DENGAN KODE PROTEKSI ANGKA INI ===

 server.on("/sT1", HTTP_GET, [](AsyncWebServerRequest *r){
   if(r->hasParam("v")) {
     String val = r->getParam("v")->value();
     // Validasi: pastikan panjangnya > 0 dan karakter pertama adalah angka
     if(val.length() > 0 && isDigit(val[0])) { 
       Time01 = val.toInt(); 
       saveData(); 
       r->send(200, "text/plain", "OK");
     } else {
       r->send(400, "text/plain", "ERROR: Harus Angka murni!");
     }
   } else { r->send(400); }
 });

 server.on("/sT2", HTTP_GET, [](AsyncWebServerRequest *r){
   if(r->hasParam("v")) {
     String val = r->getParam("v")->value();
     if(val.length() > 0 && isDigit(val[0])) { 
       Time02 = val.toInt(); 
       saveData(); 
       r->send(200, "text/plain", "OK");
     } else {
       r->send(400, "text/plain", "ERROR: Harus Angka murni!");
     }
   } else { r->send(400); }
 });

 server.on("/sT3", HTTP_GET, [](AsyncWebServerRequest *r){
   if(r->hasParam("v")) {
     String val = r->getParam("v")->value();
     if(val.length() > 0 && isDigit(val[0])) { 
       Time03 = val.toInt(); 
       saveData(); 
       r->send(200, "text/plain", "OK");
     } else {
       r->send(400, "text/plain", "ERROR: Harus Angka murni!");
     }
   } else { r->send(400); }
 });



  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *r){ Counter1=0; saveData(); r->send(200); });
  server.on("/wakeup", HTTP_GET, [](AsyncWebServerRequest *r){ wakeUpSystem(); r->send(200); });
  

  // 7. Jalankan Server
  server.begin();
  lastActivity = millis();
  updateSerialLog("SYSTEM_BOOT_COMPLETED");
}

void loop() {
  unsigned long now = millis();
  static unsigned long lastTickMillis = 0;

  // --- 1. LOGIKA HITUNG MUNDUR (PREVENT FREEZE) ---
  // Menghitung selisih waktu dari aktivitas terakhir dalam detik
  // --- 1. LOGIKA HITUNG MUNDUR YANG PRESISI (TICKING 1 DETIK) --- 
 if (isSleep) {
   remainingSec = 0;
 } 
 else if (isProcessing) {
   remainingSec = sleepDuration; // Kunci nilai saat proses otomasi berjalan
   lastActivity = now;           // Jaga agar tidak tidur
 } 
 else {
   // Jalankan pengurangan setiap kali waktu berjalan kelipatan 1000ms (1 Detik)
   if (now - lastTickMillis >= 1000) {
     lastTickMillis = now; // Perbarui penanda waktu
     
     long elapsedSec = (now - lastActivity) / 1000;
     long diff = (long)sleepDuration - elapsedSec;
     
     if (diff > 0) {
       remainingSec = diff;
     } else {
       remainingSec = 0;
     }
   }
 }
  // --- 2. LOGIKA DIMMING (BERDASARKAN OLED TIME NORMAL) ---
  // Jika waktu tersisa kurang dari atau sama dengan oled_time_norm, layar meredup
  if (!isSleep && !isProcessing) {
    if (remainingSec <= (long)oled_time_norm && remainingSec > 0) {
      display.ssd1306_command(SSD1306_SETCONTRAST);
      display.ssd1306_command(oled_br_dim); 
    } else {
      display.ssd1306_command(SSD1306_SETCONTRAST);
      display.ssd1306_command(oled_br_normal);
    }
  }

  // --- 3. LOGIKA MASUK SLEEP MODE ---
  if (remainingSec <= 0 && !isSleep && !isProcessing) {
    isSleep = true;
    display.clearDisplay();
    display.display();
    // Pastikan kontras kembali normal untuk saat bangun nanti
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(oled_br_normal);
    updateSerialLog("INACTIVITY_SLEEP");
  }

  // --- 4. EFEK BREATHING LED C (HANYA SAAT SLEEP) ---
  if (isSleep) {
    float pulse = sin(now / (float)ledc_blink_spd * PI);
    int brightness = (pulse * (ledc_max_br / 2)) + (ledc_max_br / 2);
    analogWrite(LED_C, brightness);
  }

  // --- 5. LOGIKA TOMBOL (WAKEUP & PROCESS) ---
  int rd = digitalRead(BTN_A);
  
  // Proteksi jika tombol ditekan saat boot (Abaikan sampai dilepas)
  if (rd == HIGH && !btnReady) {
    btnReady = true; 
  }

  if (btnReady) {
    if (rd != lastBtnState) {
      lastDebounce = now;
    }

    if ((now - lastDebounce) > 50) { // Debounce 50ms
      if (rd == LOW && !btnWasPressed) {
        btnWasPressed = true;
        
        // Hentikan LED C & Bangunkan Sistem
        analogWrite(LED_C, 0);
        digitalWrite(LED_C, LOW);
        
        bool wasSleep = isSleep;
        wakeUpSystem(); // Reset lastActivity & isSleep

        if (!isProcessing) {
          isProcessing = true;
          step = 1;
          prevMillis = now;
          startProcessMillis = now;
          delay(2);
          updateSerialLog(wasSleep ? "WAKE_AND_START" : "BUTTON_START");
        }
      } 
      else if (rd == HIGH) {
        btnWasPressed = false;
      }
    }
    lastBtnState = rd;
  }

    // --- 6. LOGIKA OTOMASI PROSES (PARALEL & SEKUENSIAL) ---
  if (isProcessing) {
    unsigned long cur = millis();
    lastActivity = cur; // Jaga agar tidak tidur saat proses berjalan
    
    // --- KONTROL RELAY A (Berjalan Paralel Sejak Tombol Ditekan) ---
    if (step == 1 && (cur - startProcessMillis >= Time01)) {
      digitalWrite(LED_A, LOW); // Relay A ON setelah Time01 selesai
    }
    
    // --- KONTROL RELAY B (Berjalan Paralel Sejak Tombol Ditekan) ---
    if (step == 1 && (cur - startProcessMillis >= Time02)) {
      digitalWrite(LED_B, LOW); // Relay B ON setelah Time02 selesai
    }
    
    // --- CEK APAKAH KEDUA TIMING SUDAH TERPENUHI UNTUK MASUK KE T3 ---
    if (step == 1 && (cur - startProcessMillis >= Time01) && (cur - startProcessMillis >= Time02)) {
      step = 3;                 // Lompat ke Step 3 (Fase Tunggu T3)
      prevMillis = cur;         // Penanda waktu awal untuk Fase T3
    }
    
    // --- JEDA AKHIR SEBELUM SEMUA MATI (Step 3) ---
    else if (step == 3 && (cur - prevMillis >= Time03)) {
      digitalWrite(LED_A, HIGH); // Relay A OFF
      digitalWrite(LED_B, HIGH); // Relay B OFF
      
      processDuration = millis() - startProcessMillis;
      Counter1++;

       if (Counter1 >= 1000) { Counter1 = 0;  }
      isProcessing = false;
      
      saveData(); // Simpan counter terbaru
      delay(5);
      updateSerialLog("PROCESS_DONE");
    }                        
  }


  // --- 7. UPDATE TAMPILAN OLED ---
  if (!isSleep) {
    updateOLED();
  }
}




