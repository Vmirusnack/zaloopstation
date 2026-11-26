// Gives the page a small bridge to the native audio engine (LoopEngine). The page sees only window.loopEngine.
const { contextBridge, ipcRenderer } = require('electron');
const listeners = new Set();
ipcRenderer.on('engine:msg', (_e, m) => listeners.forEach(f => { try { f(m); } catch (e) { console.warn(e); } }));
contextBridge.exposeInMainWorld('loopEngine', {
  available: true,
  start: () => ipcRenderer.invoke('engine:start'),        // resolves {ok, error?}
  send: (obj) => ipcRenderer.send('engine:send', obj),
  onMessage: (f) => { listeners.add(f); return () => listeners.delete(f); },
  status: () => ipcRenderer.invoke('engine:status')
});
