#include "integration/native_bridge.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSettings>
#include <QDebug>

#include <array>
#include <cstdio>
#include <cstdint>
#include <vector>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace {
constexpr std::uint32_t MaximumFrameBytes = 1024 * 1024;

bool readFully(void *target, const std::size_t bytes) {
    auto *out = static_cast<unsigned char *>(target);
    std::size_t read = 0;
    while (read < bytes) {
        const std::size_t received = std::fread(out + read, 1, bytes - read, stdin);
        if (received == 0) return false;
        read += received;
    }
    return true;
}

bool writeResponse(const QJsonObject &object) {
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (json.size() > MaximumFrameBytes) return false;
    const std::uint32_t size = static_cast<std::uint32_t>(json.size());
    const std::array<unsigned char, 4> length{
        static_cast<unsigned char>(size & 0xFFU),
        static_cast<unsigned char>((size >> 8U) & 0xFFU),
        static_cast<unsigned char>((size >> 16U) & 0xFFU),
        static_cast<unsigned char>((size >> 24U) & 0xFFU)
    };
    return std::fwrite(length.data(), 1, length.size(), stdout) == length.size()
        && std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout) == static_cast<std::size_t>(json.size())
        && std::fflush(stdout) == 0;
}

QJsonObject failure(const QString &reason) {
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), reason}};
}

constexpr auto HostName = "com.thowilabs.oidm";

bool validExtensionId(const QString &id) {
    static const QRegularExpression pattern(QStringLiteral("^[a-p]{32}$"));
    return pattern.match(id).hasMatch();
}

QString hostManifestPath() {
#ifdef Q_OS_WIN
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/com.thowilabs.oidm.json");
#else
    return {};
#endif
}

#ifdef Q_OS_WIN
QStringList nativeHostRegistryKeys() {
    return {
        QStringLiteral("Software\\Google\\Chrome\\NativeMessagingHosts\\com.thowilabs.oidm"),
        QStringLiteral("Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.thowilabs.oidm")
    };
}

// Registry values must be read via Win32 (not Qt's ini-style registry fallback).
QString readRegistryManifestPath(const QString &keyPath, QString *error) {
    HKEY key = nullptr;
    const std::wstring path = keyPath.toStdWString();
    const LONG opened = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (opened != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("Falta registro HKCU\\%1 (error %2)").arg(keyPath).arg(opened);
        return {};
    }
    DWORD type = 0;
    DWORD bytes = 0;
    LONG result = RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &bytes);
    if (result != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > 32768) {
        RegCloseKey(key);
        if (error) *error = QStringLiteral("Registro de host inválido: %1").arg(keyPath);
        return {};
    }
    std::vector<wchar_t> buffer(static_cast<std::size_t>(bytes / sizeof(wchar_t)) + 1, 0);
    result = RegQueryValueExW(key, nullptr, nullptr, &type,
        reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ) {
        if (error) *error = QStringLiteral("No se pudo leer el registro de host: %1").arg(keyPath);
        return {};
    }
    return QDir::fromNativeSeparators(QString::fromWCharArray(buffer.data()));
}

bool writeRegistryManifestPath(const QString &keyPath, const QString &manifestPath) {
    HKEY key = nullptr;
    const std::wstring wideKey = keyPath.toStdWString();
    const LONG created = RegCreateKeyExW(HKEY_CURRENT_USER, wideKey.c_str(), 0,
        nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (created != ERROR_SUCCESS) {
        qWarning("Could not create NativeMessagingHosts key (Win32=%ld)", created);
        return false;
    }
    const std::wstring widePath = QDir::toNativeSeparators(manifestPath).toStdWString();
    const LONG written = RegSetValueExW(key, nullptr, 0, REG_SZ,
        reinterpret_cast<const BYTE *>(widePath.c_str()),
        static_cast<DWORD>((widePath.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (written != ERROR_SUCCESS) {
        qWarning("Could not write NativeMessagingHosts key (Win32=%ld)", written);
    }
    return written == ERROR_SUCCESS;
}
#endif

QJsonObject readHostManifest(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024) return {};
    const QJsonDocument parsed = QJsonDocument::fromJson(file.readAll());
    return parsed.isObject() ? parsed.object() : QJsonObject{};
}

int diagnoseChromiumHost(const QString &extensionId) {
    if (!validExtensionId(extensionId)) {
        qCritical("Invalid extension ID; expected 32 lowercase a-p characters.");
        return 2;
    }
#ifdef Q_OS_WIN
    const QString manifestPath = hostManifestPath();
    const QJsonObject manifest = readHostManifest(manifestPath);
    const QString expectedOrigin = QStringLiteral("chrome-extension://%1/").arg(extensionId);
    bool ok = true;
    if (manifest.value(QStringLiteral("name")).toString() != QString::fromLatin1(HostName)
        || manifest.value(QStringLiteral("type")).toString() != QStringLiteral("stdio")) {
        qCritical("FAIL: Native Messaging host manifest is missing or invalid.");
        ok = false;
    } else {
        qInfo("OK: Native Messaging manifest exists.");
    }
    const QString manifestExe = manifest.value(QStringLiteral("path")).toString();
    if (!QFileInfo(manifestExe).isFile()) {
        qCritical("FAIL: Native Messaging executable not found: %s", qPrintable(manifestExe));
        ok = false;
    } else {
        qInfo("OK: Host executable exists: %s", qPrintable(manifestExe));
    }
    bool allowed = false;
    for (const auto &origin : manifest.value(QStringLiteral("allowed_origins")).toArray()) {
        if (origin.toString() == expectedOrigin) allowed = true;
    }
    if (!allowed) {
        qCritical("FAIL: Extension ID not allowed in manifest: %s", qPrintable(extensionId));
        ok = false;
    } else {
        qInfo("OK: Extension ID allowed: %s", qPrintable(extensionId));
    }
    for (const QString &registryKey : nativeHostRegistryKeys()) {
        QString error;
        const QString actual = readRegistryManifestPath(registryKey, &error);
        if (actual.compare(manifestPath, Qt::CaseInsensitive) != 0) {
            qCritical("FAIL: %s (%s)", qPrintable(registryKey), qPrintable(error));
            ok = false;
        } else {
            qInfo("OK: %s", qPrintable(registryKey));
        }
    }
    qInfo("Extension ID actual visible in chrome://extensions MUST match: %s", qPrintable(extensionId));
    return ok ? 0 : 5;
#else
    qInfo("Native host registry diagnostics are available in Windows only.");
    return 0;
#endif
}

int registerChromiumHost(const QString &extensionId) {
    if (!validExtensionId(extensionId)) {
        qCritical("Invalid extension ID; expected 32 lowercase a-p characters.");
        return 2;
    }
    const QString applicationPath = QCoreApplication::applicationFilePath();
    QJsonArray origins;
    const QString currentOrigin = QStringLiteral("chrome-extension://%1/").arg(extensionId);
    origins.append(currentOrigin);
#ifdef Q_OS_WIN
    // Preserve other extension IDs the user explicitly registered. Otherwise
    // automatic registration on app startup would undo a manual repair.
    const QJsonObject prior = readHostManifest(hostManifestPath());
    if (prior.value(QStringLiteral("path")).toString().compare(applicationPath, Qt::CaseInsensitive) == 0) {
        for (const QJsonValue &value : prior.value(QStringLiteral("allowed_origins")).toArray()) {
            const QString origin = value.toString();
            if (!origin.startsWith(QStringLiteral("chrome-extension://")) || !origin.endsWith(QLatin1Char('/'))) continue;
            const QString id = origin.mid(19, origin.size() - 20);
            if (validExtensionId(id) && !origins.contains(origin)) origins.append(origin);
        }
    }
#endif
    const QJsonObject manifest{
        {QStringLiteral("name"), QString::fromLatin1(HostName)},
        {QStringLiteral("description"), QStringLiteral("OIDM native download bridge")},
        {QStringLiteral("path"), applicationPath},
        {QStringLiteral("type"), QStringLiteral("stdio")},
        {QStringLiteral("allowed_origins"), origins}
    };
    const QByteArray payload = QJsonDocument(manifest).toJson(QJsonDocument::Indented);

    auto saveManifest = [&](const QString &path) -> bool {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        if (file.write(payload) != payload.size()) { file.cancelWriting(); return false; }
        return file.commit();
    };
#ifdef Q_OS_WIN
    const QString path = hostManifestPath();
    if (!saveManifest(path)) {
        qCritical("Could not write the native host manifest.");
        return 3;
    }
    int succeeded = 0;
    for (const QString &key : nativeHostRegistryKeys()) {
        if (writeRegistryManifestPath(key, path)) ++succeeded;
    }
    if (succeeded != 2) {
        qCritical("Could not register native messaging for Chrome and Edge.");
        return 4;
    }
    qInfo("OIDM Native Messaging registered for Chrome and Edge.");
#else
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QStringList browserFolders{
        QStringLiteral("google-chrome"),
        QStringLiteral("chromium"),
        QStringLiteral("microsoft-edge")
    };
    int succeeded = 0;
    for (const QString &folder : browserFolders) {
        const QString path = QDir(config).filePath(folder
            + QStringLiteral("/NativeMessagingHosts/com.thowilabs.oidm.json"));
        if (saveManifest(path)) ++succeeded;
        else qWarning("Could not register browser native host at %s", qPrintable(path));
    }
    if (succeeded == 0) return 4;
    qInfo("OIDM Native Messaging registered for %d Chromium browsers.", succeeded);
#endif
    return 0;
}

int unregisterChromiumHost() {
#ifdef Q_OS_WIN
    const QString manifestPath = hostManifestPath();
    const QJsonObject manifest = readHostManifest(manifestPath);
    const QString registeredExe = manifest.value(QStringLiteral("path")).toString();
    if (registeredExe.compare(QCoreApplication::applicationFilePath(), Qt::CaseInsensitive) != 0) {
        qWarning("OIDM: host manifest belongs to another installation. Registry preserved.");
        return 0;
    }
    for (const QString &key : nativeHostRegistryKeys()) {
        QString error;
        if (readRegistryManifestPath(key, &error).compare(manifestPath, Qt::CaseInsensitive) != 0) continue;
        const std::wstring registryKey = key.toStdWString();
        const LONG removed = RegDeleteTreeW(HKEY_CURRENT_USER, registryKey.c_str());
        if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND) {
            qWarning("Could not unregister OIDM host (Win32=%ld)", removed);
            return 5;
        }
    }
    if (!QFile::remove(manifestPath)) {
        qWarning("Unable to remove native host manifest: %s", qPrintable(manifestPath));
        return 6;
    }
    return 0;
#else
    qInfo("Linux native host must be deregistered from browser config dirs manually.");
    return 0;
#endif
}

QJsonObject forwardToApp(const QJsonObject &message) {
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("capture-state") || type == QStringLiteral("set-capture")) {
        QSettings settings;
        if (type == QStringLiteral("set-capture")) {
            if (!message.value(QStringLiteral("paused")).isBool()) return failure(QStringLiteral("Estado de pausa inválido."));
            settings.setValue(QStringLiteral("browser/capturePaused"), message.value(QStringLiteral("paused")).toBool());
            settings.sync();
            if (settings.status() != QSettings::NoError) return failure(QStringLiteral("No se pudo guardar el estado."));
        }
        return QJsonObject{{QStringLiteral("ok"), true},
            {QStringLiteral("paused"), settings.value(QStringLiteral("browser/capturePaused"), false).toBool()}};
    }
    if (type != QStringLiteral("enqueue") && type != QStringLiteral("ping")
        && type != QStringLiteral("released") && type != QStringLiteral("abandon")) {
        return failure(QStringLiteral("Operación no admitida."));
    }
    QLocalSocket socket;
    socket.connectToServer(oidm::nativeBridgeName(), QIODevice::ReadWrite);
    if (!socket.waitForConnected(250)) {
        QString program = QCoreApplication::applicationDirPath()
#ifdef Q_OS_WIN
            + QStringLiteral("/OIDM.exe");
#else
            + QStringLiteral("/oidm");
#endif
        if (!QFileInfo::exists(program) || !QProcess::startDetached(program, {})) {
            return failure(QStringLiteral("OIDM no está ejecutándose y no se pudo iniciar."));
        }
        socket.connectToServer(oidm::nativeBridgeName(), QIODevice::ReadWrite);
        if (!socket.waitForConnected(4000)) {
            return failure(QStringLiteral("No se pudo conectar con OIDM."));
        }
    }

    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (payload.size() > 64 * 1024 || socket.write(payload) != payload.size()
        || !socket.waitForBytesWritten(4000) || !socket.waitForReadyRead(5000)) {
        return failure(QStringLiteral("No se recibió respuesta de OIDM."));
    }
    QByteArray buffer = socket.readAll();
    while (!buffer.contains('\n') && buffer.size() < 64 * 1024 && socket.waitForReadyRead(2000)) {
        buffer += socket.readAll();
    }
    if (!buffer.contains('\n')) return failure(QStringLiteral("Respuesta incompleta de OIDM."));
    QJsonParseError parseError;
    const QJsonDocument result = QJsonDocument::fromJson(buffer.left(buffer.indexOf('\n')), &parseError);
    if (parseError.error != QJsonParseError::NoError || !result.isObject()) {
        return failure(QStringLiteral("Respuesta no válida de OIDM."));
    }
    return result.object();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ThowiLabs"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("thowilabs.com"));
    QCoreApplication::setApplicationName(QStringLiteral("OIDM"));
    const QStringList arguments = app.arguments();
    if (arguments.contains(QStringLiteral("--unregister-chromium"))) {
        return unregisterChromiumHost();
    }
    if (arguments.contains(QStringLiteral("--diagnose-chromium"))) {
        const int index = arguments.indexOf(QStringLiteral("--diagnose-chromium"));
        if (index + 1 >= arguments.size()) return 2;
        return diagnoseChromiumHost(arguments.at(index + 1));
    }
    if (arguments.contains(QStringLiteral("--register-chromium"))) {
        const int index = arguments.indexOf(QStringLiteral("--register-chromium"));
        if (index + 1 >= arguments.size()) {
            qCritical("Usage: oidm-native-host --register-chromium <32-letter-extension-id>");
            return 2;
        }
        return registerChromiumHost(arguments.at(index + 1));
    }
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::array<unsigned char, 4> length{};
    while (readFully(length.data(), length.size())) {
        const std::uint32_t size = static_cast<std::uint32_t>(length[0])
            | (static_cast<std::uint32_t>(length[1]) << 8U)
            | (static_cast<std::uint32_t>(length[2]) << 16U)
            | (static_cast<std::uint32_t>(length[3]) << 24U);
        if (size == 0 || size > MaximumFrameBytes) {
            if (!writeResponse(failure(QStringLiteral("Longitud de mensaje inválida.")))) return 1;
            return 1;
        }
        std::vector<unsigned char> bytes(size);
        if (!readFully(bytes.data(), size)) return 1;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(
            QByteArray(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size())),
            &parseError);
        if (!writeResponse(parseError.error == QJsonParseError::NoError && document.isObject()
            ? forwardToApp(document.object()) : failure(QStringLiteral("JSON inválido.")))) return 1;
    }
    return 0;
}
