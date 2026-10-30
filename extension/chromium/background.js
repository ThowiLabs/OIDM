// OIDM Chromium extension (Manifest V3).
// Browser JavaScript is the only non-C++ part of OIDM. It never receives or
// stores cookies, authentication headers or page content.
const HOST = "com.thowilabs.oidm";
const MENU_ID = "oidm-download-url";

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

function isValidUrl(value) {
  try {
    const url = new URL(value);
    return ["http:", "https:"].includes(url.protocol) &&
      !url.username && !url.password;
  } catch {
    return false;
  }
}

function suggestedName(value) {
  try {
    const url = new URL(value);
    const component = decodeURIComponent(url.pathname.split("/").pop() || "");
    return component.slice(0, 160);
  } catch {
    return "";
  }
}

function showStatus(ok, message) {
  chrome.action.setBadgeBackgroundColor({ color: ok ? "#18845d" : "#b3262e" });
  chrome.action.setBadgeText({ text: ok ? "OK" : "!" });
  chrome.notifications.create({
    type: "basic",
    iconUrl: "icons/oidm-128.png",
    title: ok ? "OIDM: descarga añadida" : "OIDM: no se pudo añadir",
    message
  }, () => {
    if (chrome.runtime.lastError) {
      // Notifications may be disabled by browser policy; badge still updates.
    }
  });
}

chrome.contextMenus.onClicked.addListener((info) => {
  if (info.menuItemId !== MENU_ID) return;
  const url = info.linkUrl || info.srcUrl;
  if (!isValidUrl(url)) {
    showStatus(false, "Solo se permiten enlaces HTTP/HTTPS.");
    return;
  }
  chrome.runtime.sendNativeMessage(HOST, {
    type: "enqueue",
    requestId: `${Date.now()}-${Math.random().toString(36).slice(2)}`,
    url,
    suggestedName: suggestedName(url)
  }, (response) => {
    if (chrome.runtime.lastError) {
      showStatus(false, chrome.runtime.lastError.message || "Host nativo no disponible.");
      return;
    }
    if (!response || response.ok !== true) {
      showStatus(false, response?.error || "OIDM rechazó la descarga.");
      return;
    }
    showStatus(true, "La descarga se agregó a la biblioteca de OIDM.");
  });
});
