# OIDM Browser Integration — preview

The Chromium MV3 extension adds **Download with OIDM** to the context menu of links, images, audio and video. It sends only the user-selected HTTP(S) URL and a suggested filename to `oidm-native-host` through the browser's Native Messaging protocol. The host forwards the request over an OS-user-restricted local socket to the Qt desktop application, which chooses a unique filename under the user's Downloads folder.

## Chrome / Chromium / Microsoft Edge

1. Build and install both `oidm` and `oidm-native-host` (the two executables must be in the same folder for automatic application launch).
2. Open the browser's extensions page, enable Developer mode and load the unpacked directory `extension/chromium`.
3. Copy the extension ID shown by the browser. An unpacked Chromium extension ID consists of 32 lowercase letters in the range `a`–`p`.
4. Run `oidm-native-host --register-chromium YOUR_EXTENSION_ID` once as the current desktop user. This creates the browser-specific Native Messaging registration files in Linux, or user-level registry entries and a host manifest on Windows.
5. In the browser, right-click an HTTP(S) download link and choose **Descargar con OIDM**.

Do not run this registration command with sudo. The extension is not installed automatically in the browser, and the native messaging manifest is deliberately restricted to the extension ID provided by the user.

## Current limitations

- Context-menu downloads work through the native host; this is **not automatic interception** of all downloads yet.
- Automatic media detection, video-download panels, HLS/DASH adaptation, page-link collection, and Firefox-specific registration are future work.
- Browser restrictions and DRM-protected content are not bypassed.
- Chrome notifications can be disabled; OIDM still stores accepted downloads.
