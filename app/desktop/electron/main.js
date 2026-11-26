// Loop Studio desktop shell: the whole DAW is one web page (www/index.html) shown in a Chromium window.
// Unlike a browser tab, MIDI keyboards work without any permission prompt and the page keeps running in the background.
const { app, BrowserWindow, session, Menu, shell, ipcMain } = require('electron');
const path = require('path');
const fs = require('fs');
const net = require('net');
const { spawn } = require('child_process');

app.commandLine.appendSwitch('autoplay-policy', 'no-user-gesture-required');

const ALLOWED = ['midi', 'midiSysex', 'media', 'fullscreen', 'clipboard-sanitized-write', 'clipboard-read'];

function createWindow() {
  const win = new BrowserWindow({
    width: 1480,
    height: 920,
    minWidth: 960,
    minHeight: 600,
    backgroundColor: '#121316',
    title: 'Loop Studio',
    autoHideMenuBar: true,
    webPreferences: { contextIsolation: true, nodeIntegration: false, backgroundThrottling: false, preload: path.join(__dirname, 'preload.js') }
  });
  win.loadFile(path.join(__dirname, '..', 'www', 'index.html'));
  win.webContents.setWindowOpenHandler(({ url }) => {
    if (/^https?:/.test(url)) shell.openExternal(url);
    return { action: 'deny' };
  });
}

/* ---- native audio engine (LoopEngine) ----
   Started on demand by the page; talks over a local TCP socket, one JSON message per line. */
const ENGINE_PORT = 47821;
let engineProc = null, engineSock = null, engineBuf = '', engineErr = '';
function engineExe() {
  const name = process.platform === 'win32' ? 'LoopEngine.exe' : 'LoopEngine';
  const cands = [path.join(process.resourcesPath || '', 'engine', name), path.join(__dirname, '..', 'engine-bin', name)];
  return cands.find(p => fs.existsSync(p)) || null;
}
function toPage(m) { BrowserWindow.getAllWindows().forEach(w => { if (!w.isDestroyed()) w.webContents.send('engine:msg', m); }); }
function connectEngine(tries) {
  return new Promise(resolve => {
    const sock = net.connect(ENGINE_PORT, '127.0.0.1');
    sock.setNoDelay(true);
    sock.once('connect', () => {
      engineSock = sock; engineBuf = '';
      sock.on('data', d => {
        engineBuf += d.toString('utf8');
        let i; while ((i = engineBuf.indexOf('\n')) >= 0) { const line = engineBuf.slice(0, i); engineBuf = engineBuf.slice(i + 1); try { toPage(JSON.parse(line)); } catch (e) { /* ignore a broken line */ } }
      });
      sock.on('close', () => { if (engineSock === sock) engineSock = null; toPage({ t: 'closed' }); });
      sock.on('error', () => {});
      resolve(true);
    });
    sock.once('error', () => { sock.destroy(); if (tries > 0) setTimeout(() => connectEngine(tries - 1).then(resolve), 150); else resolve(false); });
  });
}
ipcMain.handle('engine:start', async () => {
  if (engineSock) return { ok: true };
  if (!engineProc) {
    // an engine that is already running (started by hand) is used as it is
    if (await connectEngine(0)) return { ok: true, external: true };
    const exe = engineExe();
    if (!exe) return { ok: false, error: 'LoopEngine is not part of this build' };
    try { if (process.platform !== 'win32') fs.chmodSync(exe, 0o755); } catch (e) { /* ignore */ }
    engineErr = '';
    engineProc = spawn(exe, ['--port', String(ENGINE_PORT)], { stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true });
    engineProc.stderr.on('data', d => { engineErr += d.toString(); if (engineErr.length > 4000) engineErr = engineErr.slice(-4000); });
    engineProc.on('exit', code => { engineProc = null; toPage({ t: 'exit', code, err: engineErr }); });
    engineProc.on('error', e => { engineErr += String(e); engineProc = null; });
  }
  const ok = await connectEngine(40);
  return ok ? { ok: true } : { ok: false, error: 'Could not connect to LoopEngine. ' + engineErr.slice(-300) };
});
ipcMain.on('engine:send', (_e, obj) => { if (engineSock && !engineSock.destroyed) engineSock.write(JSON.stringify(obj) + '\n'); });
ipcMain.handle('engine:status', () => ({ connected: !!engineSock, running: !!engineProc, exe: engineExe() }));
app.on('before-quit', () => { try { if (engineSock) engineSock.destroy(); } catch (e) { /* ignore */ } try { if (engineProc) engineProc.kill(); } catch (e) { /* ignore */ } });

app.whenReady().then(() => {
  const ses = session.defaultSession;
  ses.setPermissionRequestHandler((wc, permission, cb) => cb(ALLOWED.includes(permission)));
  ses.setPermissionCheckHandler((wc, permission) => ALLOWED.includes(permission));
  if (process.platform === 'darwin') {
    Menu.setApplicationMenu(Menu.buildFromTemplate([
      { role: 'appMenu' }, { role: 'editMenu' }, { role: 'viewMenu' }, { role: 'windowMenu' }
    ]));
  } else {
    Menu.setApplicationMenu(null);
  }
  createWindow();
  app.on('activate', () => { if (BrowserWindow.getAllWindows().length === 0) createWindow(); });
});

app.on('window-all-closed', () => app.quit());
