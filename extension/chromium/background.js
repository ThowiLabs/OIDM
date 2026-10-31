// OIDM Chromium/Edge extension (MV3). Only the URL, destination filename and
// optional public referrer are sent. Cookies and authorization never leave Chrome.
const HOST = "com.thowilabs.oidm";
const MENU_ID = "oidm-download-url";

function validUrl(value) {
  try {
    const parsed = new URL(value);
    return (parsed.protocol === "https:" || parsed.protocol === "http:") &&
      !parsed.username && !parsed.password;
  } catch {
    return false;
  }
}

function suggestedName(value) {
  try {
    const parsed = new URL(value);
    return decodeURIComponent(parsed.pathname.split("/").pop() || "").slice(0, 160);
  } catch {
    return "";
  }
}

function filenameFromDownload(item) {
  const name = item?.filename?.split(/[\\/]/).pop() || "";
  return name.slice(0, 160) || suggestedName(item?.finalUrl || item?.url || "");
}

function createMenu() {
  chrome.contextMenus.removeAll(() => {
    chrome.contextMenus.create({
      id: MENU_ID,
      title: "Descargar con OIDM",
      contexts: ["link", "image", "audio", "video"]
    });
  });
}

chrome.runtime.onInstalled.addListener(createMenu);
chrome.runtime.onStartup.addListener(createMenu);

function showStatus(ok, message) {
  chrome.action.setBadgeBackgroundColor({ color: ok ? "#18845d" : "#b3262e" });
  chrome.action.setBadgeText({ text: ok ? "OK" : "!" });
  chrome.notifications.create({
    type: "basic",
    iconUrl: "icons/oidm-128.png",
    title: ok ? "OIDM conectado" : "OIDM: revisar conexión",
    message: String(message).slice(0, 240)
  }, () => { void chrome.runtime.lastError; });
}

function sendNative(payload) {
  return new Promise(resolve => {
    try {
      chrome.runtime.sendNativeMessage(HOST, {
        ...payload,
        requestId: `${Date.now()}-${Math.random().toString(36).slice(2)}`
      }, response => {
        const error = chrome.runtime.lastError?.message;
        resolve(error ? { ok: false, error } : response || { ok: false, error: "Host sin respuesta" });
      });
    } catch (error) {
      resolve({ ok: false, error: String(error) });
    }
  });
}

chrome.contextMenus.onClicked.addListener((info) => {
  if (info.menuItemId !== MENU_ID) return;
  const url = info.linkUrl || info.srcUrl;
  if (!validUrl(url)) {
    showStatus(false, "Solo se permiten direcciones HTTP y HTTPS.");
    return;
  }
  void sendNative({
    type: "enqueue",
    url,
    referrer: validUrl(info.pageUrl) ? info.pageUrl : "",
    suggestedName: suggestedName(url)
  }).then(response => {
    showStatus(response.ok === true,
      response.ok ? "Descarga añadida a OIDM." : response.error || "OIDM no aceptó el enlace.");
  });
});

// Click the OIDM icon in Chrome/Edge to test the native bridge without downloading.
chrome.action.onClicked.addListener(() => {
  void sendNative({ type: "ping" }).then(response => {
    showStatus(response.ok === true,
      response.ok ? "Extensión y programa conectados correctamente." :
        (response.error || "El programa OIDM o su host nativo no están registrados."));
  });
});

// Browser downloads are intercepted only AFTER Chrome has started a real HTTP(S)
// transfer. This catches MediaFire's actual generated CDN download URL, not its
// HTML share page. If OIDM is absent or rejects the URL, Chrome resumes safely.
const intercepting = new Set();

function eligibleDownload(item) {
  return item && Number.isInteger(item.id) && item.state === "in_progress" &&
    !item.byExtensionId && !item.byExtensionName &&
    validUrl(item.finalUrl || item.url) &&
    !["application/x-chrome-extension", "application/x-xpinstall"].includes(item.mime);
}

chrome.downloads.onCreated.addListener(item => {
  void interceptDownload(item);
});

async function interceptDownload(item) {
  if (!eligibleDownload(item) || intercepting.has(item.id)) return;
  intercepting.add(item.id);
  let paused = false;
  try {
    const preferences = await chrome.storage.local.get({ automaticInterception: true });
    if (preferences.automaticInterception === false) return;

    // Do not create a second download if Chrome has already finished a small file.
    try {
      await chrome.downloads.pause(item.id);
      paused = true;
    } catch {
      return;
    }

    const matches = await chrome.downloads.search({ id: item.id });
    const current = matches?.[0] || item;
    if (current.state !== "in_progress") {
      await resumeBrowserDownload(item.id);
      paused = false;
      return;
    }
    const url = current.finalUrl || current.url || item.finalUrl || item.url;
    if (!validUrl(url)) {
      await resumeBrowserDownload(item.id);
      paused = false;
      return;
    }
    const response = await sendNative({
      type: "enqueue",
      url,
      referrer: validUrl(current.referrer) ? current.referrer : "",
      suggestedName: filenameFromDownload(current)
    });
    if (response.ok !== true) {
      await resumeBrowserDownload(item.id);
      paused = false;
      showStatus(false, response.error || "OIDM no disponible; Chrome continúa la descarga.");
      return;
    }

    // Transfer ownership only when OIDM ACKs successful queue insertion.
    await chrome.downloads.cancel(item.id);
    paused = false;
    await chrome.downloads.erase({ id: item.id }).catch(() => {});
    showStatus(true, "Descarga enviada automáticamente a OIDM.");
  } catch (error) {
    if (paused) await resumeBrowserDownload(item.id);
    showStatus(false, `Se mantuvo la descarga en Chrome: ${String(error)}`);
  } finally {
    intercepting.delete(item.id);
  }
}

async function resumeBrowserDownload(id) {
  try { await chrome.downloads.resume(id); } catch { /* May have completed or been cancelled. */ }
}
