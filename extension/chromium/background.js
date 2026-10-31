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

function nativeErrorDetail(error) {
  const detail = String(error || "Host sin respuesta");
  if (/native messaging host|native host|no host specified|host.*not found/i.test(detail)) {
    return "Chrome/Edge no encuentra el host de OIDM. Abre OIDM > Ayuda > Reparar integración Chrome / Edge, " +
      "pega el ID actual de chrome://extensions y recarga la extensión. Detalle: " + detail;
  }
  return detail;
}

function sendNative(payload) {
  return new Promise(resolve => {
    try {
      chrome.runtime.sendNativeMessage(HOST, {
        ...payload,
        requestId: `${Date.now()}-${Math.random().toString(36).slice(2)}`
      }, response => {
        const error = chrome.runtime.lastError?.message;
        resolve(error ? { ok: false, error: nativeErrorDetail(error) } : response || { ok: false, error: "Host sin respuesta" });
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

// One click pauses/resumes all *new* browser captures. Existing OIDM transfers
// continue, and Chrome never stops new transfers while capture is paused.
chrome.action.onClicked.addListener(() => {
  void (async () => {
    const state = await sendNative({ type: "capture-state" });
    if (state.ok !== true) {
      showStatus(false, state.error || "El host de OIDM no responde; Chrome mantiene sus descargas.");
      return;
    }
    const nextPaused = state.paused !== true;
    const result = await sendNative({ type: "set-capture", paused: nextPaused });
    if (result.ok !== true) {
      showStatus(false, result.error || "No se pudo cambiar el estado de capturas.");
      return;
    }
    await chrome.storage.local.set({ capturePaused: nextPaused });
    showCaptureBadge(nextPaused);
    showStatus(true, nextPaused
      ? "Capturas pausadas. Chrome/Edge descargará normalmente. Haz clic para reanudar OIDM."
      : "Capturas activadas. OIDM interceptará nuevas descargas. Haz clic para pausar.");
    showCaptureBadge(nextPaused);
  })();
});

function showCaptureBadge(paused) {
  chrome.action.setBadgeBackgroundColor({ color: paused ? "#b7791f" : "#18845d" });
  chrome.action.setBadgeText({ text: paused ? "OFF" : "ON" });
  chrome.action.setTitle({ title: paused
    ? "OIDM pausado — Chrome descarga normalmente. Clic para activar."
    : "OIDM activo — Clic para pausar capturas." });
}

chrome.runtime.onStartup.addListener(() => {
  void sendNative({ type: "capture-state" }).then(state => {
    if (state.ok === true) showCaptureBadge(state.paused === true);
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
  let preparedId = "";
  try {
    // Pause immediately when Chrome notifies a new download. Native Messaging
    // and settings reads are asynchronous and must not race the browser.
    try {
      await chrome.downloads.pause(item.id);
      paused = true;
    } catch {
      return; // Already completed or not pausable: Chrome remains in control.
    }
    const capture = await sendNative({ type: "capture-state" });
    if (capture.ok !== true || capture.paused === true) {
      await resumeBrowserDownload(item.id);
      paused = false;
      if (capture.ok === true) showCaptureBadge(true);
      return;
    }
    const preferences = await chrome.storage.local.get({ automaticInterception: true });
    if (preferences.automaticInterception === false) {
      await resumeBrowserDownload(item.id);
      paused = false;
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
      suggestedName: filenameFromDownload(current),
      totalBytes: Number.isSafeInteger(current.totalBytes) && current.totalBytes > 0 ? current.totalBytes : -1,
      browserOwned: true
    });
    if (response.ok !== true) {
      await resumeBrowserDownload(item.id);
      paused = false;
      showStatus(false, response.error || "OIDM no disponible; Chrome continúa la descarga.");
      return;
    }

    // OIDM has only prepared a Queued record, and MUST NOT start the transfer
    // until Chrome has successfully cancelled its own download.
    preparedId = String(response.result || "");
    if (!preparedId) throw new Error("OIDM respondió sin ID de transferencia");
    await chrome.downloads.cancel(item.id);
    paused = false; // Browser no longer owns the transfer.
    await chrome.downloads.erase({ id: item.id }).catch(() => {});
    const released = await sendNative({ type: "released", downloadId: preparedId });
    if (released.ok !== true) {
      showStatus(false, "Chrome detuvo su descarga, pero OIDM no mostró la confirmación: " +
        (released.error || "Sin respuesta") + ". Revisa la cola de OIDM.");
      return;
    }
    showStatus(true, "Chrome canceló su transferencia. Confirma Iniciar en OIDM.");
  } catch (error) {
    if (preparedId && paused) {
      // Chrome is still the owner: discard the unstarted OIDM record BEFORE
      // resuming. This avoids two simultaneously active downloads on failures.
      await sendNative({ type: "abandon", downloadId: preparedId });
    }
    if (paused) await resumeBrowserDownload(item.id);
    showStatus(false, `El navegador mantiene la descarga: ${String(error)}`);
  } finally {
    intercepting.delete(item.id);
  }
}

async function resumeBrowserDownload(id) {
  try { await chrome.downloads.resume(id); } catch { /* May have completed or been cancelled. */ }
}
