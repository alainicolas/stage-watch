// Connexion WebSocket avec reconnexion automatique
function connectWS(role, onMsg, onStatus) {
  let ws, timer;
  const api = { send(o) { if (ws && ws.readyState === 1) { ws.send(JSON.stringify(o)); return true } return false } };
  function open() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    ws = new WebSocket(`${proto}://${location.host}/ws?role=${role}`);
    ws.onopen = () => { onStatus && onStatus(true); api.onopen && api.onopen() };
    ws.onmessage = e => { try { onMsg(JSON.parse(e.data)) } catch (_) {} };
    ws.onclose = () => { onStatus && onStatus(false); clearTimeout(timer); timer = setTimeout(open, 1000) };
    ws.onerror = () => ws.close();
  }
  open();
  return api;
}
function esc(s) { return String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])) }
function hhmm(ms) { return new Date(ms).toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit', second: '2-digit' }) }
