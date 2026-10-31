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
      sendNativeMessage: (host, request, callback) => {
        calls.push(['native', host, request]);
        queueMicrotask(() => callback(nativeResponse));
      },
      lastError: null
    },
    storage: { local: { get: async defaults => defaults } },
    action: {
      onClicked: { addListener: fn => { listeners.action = fn; } },
      setBadgeBackgroundColor: options => { calls.push(['color', options]); },
      setBadgeText: options => { calls.push(['badge', options]); }
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
    const env = extensionMock({ ok: true, result: 'queued' });
    env.setItem(mediafireItem);
    env.listeners.download(mediafireItem);
    await settle();
    assert.deepEqual(env.calls.filter(x => ['pause','cancel','erase'].includes(x[0])).map(x => x[0]), ['pause', 'cancel', 'erase']);
    const native = env.calls.find(x => x[0] === 'native');
    assert.equal(native[2].url, mediafireItem.url);
    assert.equal(native[2].suggestedName, 'archivo.zip');
    assert.equal(native[2].referrer, mediafireItem.referrer);
    assert.equal(native[2].type, 'enqueue');
    console.log('OK: MediaFire CDN download intercepted and queued');
  }
  {
    const env = extensionMock({ ok: false, error: 'Host not registered' });
    env.setItem(mediafireItem);
    env.listeners.download(mediafireItem);
    await settle();
    assert.deepEqual(env.calls.filter(x => ['pause','resume','cancel'].includes(x[0])).map(x => x[0]), ['pause', 'resume']);
    console.log('OK: failed native host resumes Chrome download');
  }
  {
    const env = extensionMock({ ok: true });
    env.setItem(mediafireItem);
    env.chrome.downloads.pause = async () => { throw new Error('already complete'); };
    env.listeners.download(mediafireItem);
    await settle();
    assert.ok(!env.calls.some(x => x[0] === 'native'));
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
    const env = extensionMock({ ok: true, result: 'OIDM conectado' });
    env.listeners.action();
    await settle();
    assert.equal(env.calls.find(x => x[0] === 'native')[2].type, 'ping');
    assert.ok(env.calls.some(x => x[0] === 'badge' && x[1].text === 'OK'));
    console.log('OK: icon tests native connection');
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
