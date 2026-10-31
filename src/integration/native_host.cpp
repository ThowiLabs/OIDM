#include "integration/native_bridge.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
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

int registerChromiumHost(const QString &extensionId) {
    const QRegularExpression idPattern(QStringLiteral("^[a-p]{32}$"));
    if (!idPattern.match(extensionId).hasMatch()) {
        qCritical("Invalid extension ID; expected 32 lowercase a-p characters.");
        return 2;
    }

    QJsonObject manifest{
        {QStringLiteral("name"), QStringLiteral("com.thowilabs.oidm")},
        {QStringLiteral("description"), QStringLiteral("OIDM native download bridge")},
        {QStringLiteral("path"), QCoreApplication::applicationFilePath()},
        {QStringLiteral("type"), QStringLiteral("stdio")},
        {QStringLiteral("allowed_origins"), QJsonArray{QStringLiteral("chrome-extension://%1/").arg(extensionId)}}
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
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/com.thowilabs.oidm.json");
    if (!saveManifest(path)) {
        qCritical("Could not write the native host manifest.");
        return 3;
    }
    const QStringList registryPaths{
        QStringLiteral("Software\\Google\\Chrome\\NativeMessagingHosts\\com.thowilabs.oidm"),
        QStringLiteral("Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.thowilabs.oidm")
    };
    const std::wstring widePath = QDir::toNativeSeparators(path).toStdWString();
    int succeeded = 0;
    for (const QString &registryPath : registryPaths) {
        HKEY key = nullptr;
        const std::wstring wideRegistryPath = registryPath.toStdWString();
        const LONG created = RegCreateKeyExW(HKEY_CURRENT_USER, wideRegistryPath.c_str(), 0,
            nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (created != ERROR_SUCCESS) continue;
        const LONG written = RegSetValueExW(key, nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE *>(widePath.c_str()),
            static_cast<DWORD>((widePath.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        if (written == ERROR_SUCCESS) ++succeeded;
    }
    if (succeeded == 0) {
        qCritical("Could not register native messaging for Chrome or Edge.");
        return 4;
    }
    qInfo("OIDM Native Messaging registered for %d Chromium browsers.", succeeded);
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

QJsonObject forwardToApp(const QJsonObject &message) {
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("enqueue") && type != QStringLiteral("ping")) {
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
