'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.resolve(__dirname, '../extension/chromium/background.js'), 'utf8');

function extensionMock(nativeResponse = { ok: true }) {
  const calls = [];
  const listeners = {};
  const chrome = {
    runtime: {
      onInstalled: { addListener: fn => { listeners.installed = fn; } },
      onStartup: { addListener: fn => { listeners.startup = fn; } },
      onMessage: { addListener: fn => { listeners.message = fn; } },
      sendNativeMessage: (host, request, callback) => {
        calls.push(['native', host, request]);
        const response = typeof nativeResponse === 'function' ? nativeResponse(request) : nativeResponse;
        queueMicrotask(() => callback(response));
      },
      lastError: null
    },
    storage: { local: {
      get: async defaults => { calls.push(['storage', defaults]); return defaults; },
      set: async options => { calls.push(['storageSet', options]); }
    } },
    action: {
      onClicked: { addListener: fn => { listeners.action = fn; } },
      setBadgeBackgroundColor: options => { calls.push(['color', options]); },
      setBadgeText: options => { calls.push(['badge', options]); },
      setTitle: options => { calls.push(['title', options]); }
    },
    contextMenus: {
      onClicked: { addListener: fn => { listeners.context = fn; } },
      removeAll: callback => callback(),
      create: options => { calls.push(['menu', options]); }
    },
    notifications: { create: (options, callback) => {
      calls.push(['notification', options]);
      callback();
    } },
    downloads: {
      onCreated: { addListener: fn => { listeners.download = fn; } },
      pause: async id => { calls.push(['pause', id]); },
      search: async query => { calls.push(['search', query.id]); return [mockItem]; },
      cancel: async id => { calls.push(['cancel', id]); },
      erase: async query => { calls.push(['erase', query.id]); return [query.id]; },
      resume: async id => { calls.push(['resume', id]); }
    }
  };
  let mockItem;
  const context = { chrome, URL, Date, Math, console, Promise, Set };
  vm.runInNewContext(source, context, { filename: 'background.js' });
  return {
    chrome, calls, listeners,
    setItem: value => { mockItem = value; }
  };
}

async function settle() {
  for (let index = 0; index < 6; index++) await new Promise(resolve => setImmediate(resolve));
}

async function main() {
  const mediafireItem = {
    id: 44,
    url: 'https://download123.mediafire.com/tmp/random/archivo.zip',
    finalUrl: 'https://download123.mediafire.com/tmp/random/archivo.zip',
    filename: 'C:\\Users\\tester\\Downloads\\archivo.zip',
    referrer: 'https://www.mediafire.com/file/abcd/archivo/file',
    state: 'in_progress',
    mime: 'application/zip'
  };

  {
    const env = extensionMock(req => req.type === 'capture-state'
      ? { ok: true, paused: false } : { ok: true, result: 'queued' });
    env.setItem(mediafireItem);
    env.listeners.download(mediafireItem);
    await settle();
    assert.deepEqual(env.calls.filter(x => ['pause','cancel','erase'].includes(x[0])).map(x => x[0]), ['pause', 'cancel', 'erase']);
    assert.equal(env.calls[0][0], 'pause', 'Pause Chrome before native or storage checks');
    const native = env.calls.find(x => x[0] === 'native' && x[2].type === 'enqueue');
    assert.equal(native[2].url, mediafireItem.url);
    assert.equal(native[2].suggestedName, 'archivo.zip');
    assert.equal(native[2].referrer, mediafireItem.referrer);
    assert.equal(native[2].type, 'enqueue');
    assert.equal(native[2].browserOwned, true);
    assert.deepEqual(env.calls.filter(x => x[0] === 'native').map(x => x[2].type), ['capture-state', 'enqueue', 'released']);
    assert.equal(env.calls.filter(x => x[0] === 'native')[2][2].downloadId, 'queued');
    console.log('OK: MediaFire CDN download intercepted and queued');
  }
  {
    const env = extensionMock({ ok: false, error: 'Host not registered' });
    env.setItem(mediafireItem);
    env.listeners.download(mediafireItem);
    await settle();
    assert.deepEqual(env.calls.filter(x => ['pause','resume','cancel'].includes(x[0])).map(x => x[0]), ['pause','resume'], 'Host failure resumes Chrome');
    console.log('OK: failed native host leaves Chrome in control');
  }
  {
    const env = extensionMock(req => req.type === 'capture-state'
      ? { ok: true, paused: false } : { ok: true, result: 'reserved-id' });
    env.setItem(mediafireItem);
    env.chrome.downloads.cancel = async id => { env.calls.push(['cancel-failed', id]); throw new Error('Chrome cancel rejected'); };
    env.listeners.download(mediafireItem);
    await settle();
    assert.deepEqual(env.calls.filter(x => x[0] === 'native').map(x => x[2].type), ['capture-state', 'enqueue', 'abandon']);
    assert.deepEqual(env.calls.filter(x => ['pause', 'resume'].includes(x[0])).map(x => x[0]), ['pause', 'resume']);
    assert.ok(!env.calls.some(x => x[0] === 'erase'));
    console.log('OK: cancel failure abandons OIDM record and resumes Chrome safely');
  }
  {
    const env = extensionMock(req => req.type === 'capture-state'
      ? { ok: true, paused: false } : { ok: true, result: 'queued' });
    env.setItem(mediafireItem);
    env.chrome.storage.local.get = async () => ({ automaticInterception: false });
    env.listeners.download(mediafireItem);
    await settle();
    assert.deepEqual(env.calls.filter(x => ['pause','resume'].includes(x[0])).map(x => x[0]), ['pause','resume'], 'Disabled detection resumes Chrome');
    assert.equal(env.calls.filter(x => x[0] === 'native').length, 1);
    console.log('OK: interception disabled, Chrome retains the download');
  }
  {
    const env = extensionMock({ ok: true });
    env.setItem(mediafireItem);
    env.chrome.downloads.pause = async () => { throw new Error('already complete'); };
    env.listeners.download(mediafireItem);
    await settle();
    assert.ok(!env.calls.some(x => x[0] === 'native' && x[2].type === 'enqueue'));
    console.log('OK: non-pausable download remains with Chrome');
  }
  {
    const env = extensionMock({ ok: true });
    const item = { ...mediafireItem, url: 'blob:https://www.mediafire.com/id', finalUrl: '' };
    env.setItem(item);
    env.listeners.download(item);
    await settle();
    assert.ok(!env.calls.some(x => x[0] === 'native'));
    console.log('OK: non-HTTP downloads skipped');
  }
  {
    const env = extensionMock(req => req.type === 'capture-state'
      ? { ok: true, paused: true } : { ok: true });
    env.setItem(mediafireItem);
    env.listeners.download(mediafireItem);
    await settle();
    assert.deepEqual(env.calls.filter(x => ['pause', 'resume', 'cancel', 'erase'].includes(x[0])).map(x => x[0]), ['pause', 'resume']);
    assert.deepEqual(env.calls.filter(x => x[0] === 'native').map(x => x[2].type), ['capture-state']);
    console.log('OK: paused OIDM immediately returns control to Chrome');
  }
  {
    const env = extensionMock(req => req.type === 'capture-state'
      ? { ok: true, paused: false } : { ok: true, paused: true });
    let answer;
    assert.equal(env.listeners.message({ type: 'oidm.set-capture', paused: true }, {}, data => { answer = data; }), true);
    await settle();
    assert.equal(answer.ok, true);
    assert.deepEqual(env.calls.filter(x => x[0] === 'native').map(x => x[2].type),
      ['capture-state', 'set-capture']);
    assert.equal(env.calls.find(x => x[0] === 'native' && x[2].type === 'set-capture')[2].paused, true);
    assert.ok(env.calls.some(x => x[0] === 'badge' && x[1].text === 'OFF'));
    console.log('OK: popup pauses capture and updates badge');
  }
  {
    const env = extensionMock(req => req.type === 'capture-state'
      ? { ok: true, paused: true } : { ok: true, paused: false });
    let answer;
    env.listeners.message({ type: 'oidm.set-capture', paused: false }, {}, data => { answer = data; });
    await settle();
    assert.equal(answer.ok, true);
    assert.equal(env.calls.find(x => x[0] === 'native' && x[2].type === 'set-capture')[2].paused, false);
    assert.ok(env.calls.some(x => x[0] === 'badge' && x[1].text === 'ON'));
    console.log('OK: popup resumes capture and updates badge');
  }
  {
    const env = extensionMock(req => req.type === 'capture-state'
      ? { ok: true, paused: false } : { ok: false, error: 'bad' });
    let status;
    env.listeners.message({ type: 'oidm.get-status' }, {}, response => { status = response; });
    await settle();
    assert.equal(status.ok, true);
    assert.equal(status.paused, false);
    assert.equal(status.automatic, true);
    assert.equal(status.recentTransfers.length, 0);
    let changed;
    env.listeners.message({ type: 'oidm.set-automatic', enabled: false }, {}, response => { changed = response; });
    await settle();
    assert.equal(changed.automatic, false);
    assert.ok(env.calls.some(x => x[0] === 'storageSet' && x[1].automaticInterception === false));
    console.log('OK: popup reads status and saves auto-capture preference');
  }
  {
    const manifest = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../extension/chromium/manifest.json'), 'utf8'));
    const popup = fs.readFileSync(path.resolve(__dirname, '../extension/chromium/popup.html'), 'utf8');
    const script = fs.readFileSync(path.resolve(__dirname, '../extension/chromium/popup.js'), 'utf8');
    assert.equal(manifest.action.default_popup, 'popup.html');
    assert.ok(popup.includes('popup.js') && popup.includes('popup.css'));
    assert.ok(!popup.includes('<script>') && !popup.includes('onclick='), 'No inline JS in Chrome MV3 popup');
    assert.ok(!script.includes('.innerHTML'), 'User-facing data must use textContent');
    console.log('OK: popup resources and Content Security Policy hygiene');
  }

}

main().catch(error => { console.error(error); process.exitCode = 1; });
