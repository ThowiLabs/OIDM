'use strict';

// No remote scripts, dynamic HTML or unsafe markup: extension CSP compatible.
const capture = document.getElementById('capture');
const automatic = document.getElementById('automatic');
const indicator = document.getElementById('indicator');
const connectionLabel = document.getElementById('connection-label');
const description = document.getElementById('connection-description');
const retry = document.getElementById('retry');
const recent = document.getElementById('recent');

function request(message) {
  return new Promise(resolve => {
    chrome.runtime.sendMessage(message, response => {
      const error = chrome.runtime.lastError?.message;
      resolve(error ? { ok: false, error } : response || { ok: false, error: 'Sin respuesta de la extensión' });
    });
  });
}

function showHistory(items) {
  recent.replaceChildren();
  if (!Array.isArray(items) || !items.length) {
    const empty = document.createElement('li');
    empty.className = 'empty';
    empty.textContent = 'Sin transferencias recientes';
    recent.appendChild(empty);
    return;
  }
  for (const item of items.slice(0, 6)) {
    const row = document.createElement('li');
    const name = document.createElement('strong');
    name.textContent = String(item.name || 'Archivo');
    const subtitle = document.createElement('small');
    const date = Number.isFinite(item.at) ? new Date(item.at).toLocaleTimeString() : '';
    subtitle.textContent = `${String(item.host || '')} · ${date}`;
    row.append(name, subtitle);
    recent.appendChild(row);
  }
}

async function refresh() {
  retry.disabled = true;
  const state = await request({ type: 'oidm.get-status' });
  indicator.className = 'indicator ' + (state.ok ? 'online' : 'offline');
  connectionLabel.textContent = state.ok ? (state.paused ? 'OIDM conectado · pausado' : 'OIDM conectado') : 'OIDM desconectado';
  description.textContent = state.ok
    ? 'Integración nativa operativa. Las descargas se confirmarán en OIDM.'
    : (state.error || 'Abre OIDM y comprueba la integración desde Ayuda.');
  capture.checked = state.ok && !state.paused;
  capture.disabled = !state.ok;
  automatic.checked = state.automatic !== false;
  automatic.disabled = false;
  showHistory(state.recentTransfers);
  retry.disabled = false;
}

capture.addEventListener('change', async () => {
  capture.disabled = true;
  const result = await request({ type: 'oidm.set-capture', paused: !capture.checked });
  if (!result.ok) description.textContent = result.error || 'No se pudo cambiar la captura';
  await refresh();
});
automatic.addEventListener('change', async () => {
  automatic.disabled = true;
  const result = await request({ type: 'oidm.set-automatic', enabled: automatic.checked });
  if (!result.ok) description.textContent = result.error || 'No se pudo guardar la preferencia';
  await refresh();
});
retry.addEventListener('click', refresh);
void refresh();
