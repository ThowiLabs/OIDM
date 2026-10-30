#include "integration/native_bridge.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QUrl>

namespace oidm {
namespace {
constexpr qsizetype MaxMessageBytes = 64 * 1024;

QByteArray response(const bool ok, const QString &requestId, const QString &message) {
    QJsonObject object;
    object.insert(QStringLiteral("ok"), ok);
    object.insert(QStringLiteral("requestId"), requestId);
    object.insert(ok ? QStringLiteral("result") : QStringLiteral("error"), message);
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + QByteArrayLiteral("\n");
}
}

QString nativeBridgeName() {
    const auto scope = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation).toUtf8();
    const QByteArray hash = QCryptographicHash::hash(scope, QCryptographicHash::Sha256).toHex().left(18);
    return QStringLiteral("com.thowilabs.oidm.%1").arg(QString::fromLatin1(hash));
}

NativeBridge::NativeBridge(QObject *parent) : QObject(parent), m_server(this) {
    connect(&m_server, &QLocalServer::newConnection, this, &NativeBridge::onConnected);
}

NativeBridge::~NativeBridge() {
    m_server.close();
}

bool NativeBridge::start(QString *error) {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    const QString name = nativeBridgeName();
    if (m_server.listen(name)) return true;

    // Only remove stale socket paths. Never replace an active application server.
    QLocalSocket probe;
    probe.connectToServer(name, QIODevice::ReadWrite);
    if (probe.waitForConnected(150)) {
        if (error) *error = QStringLiteral("OIDM ya tiene una instancia con integración activa.");
        return false;
    }
    QLocalServer::removeServer(name);
    if (m_server.listen(name)) return true;
    if (error) *error = m_server.errorString();
    return false;
}

void NativeBridge::onConnected() {
    while (m_server.hasPendingConnections()) {
        QLocalSocket *socket = m_server.nextPendingConnection();
        if (!socket) continue;
        socket->setParent(this);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
            QByteArray buffer = socket->property("oidm_buffer").toByteArray();
            buffer += socket->readAll();
            if (buffer.size() > MaxMessageBytes) {
                socket->write(response(false, {}, QStringLiteral("Mensaje demasiado grande.")));
                socket->flush();
                socket->disconnectFromServer();
                return;
            }
            const qsizetype end = buffer.indexOf('\n');
            if (end < 0) {
                socket->setProperty("oidm_buffer", buffer);
                return;
            }
            socket->setProperty("oidm_buffer", QByteArray());
            processLine(socket, buffer.left(end));
        });
    }
}

void NativeBridge::processLine(QLocalSocket *socket, const QByteArray &line) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    const QJsonObject data = document.isObject() ? document.object() : QJsonObject{};
    const QString requestId = data.value(QStringLiteral("requestId")).toString();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || data.value(QStringLiteral("type")).toString() != QStringLiteral("enqueue")) {
        socket->write(response(false, requestId, QStringLiteral("Solicitud no válida.")));
        socket->flush();
        socket->disconnectFromServer();
        return;
    }
    const QString url = data.value(QStringLiteral("url")).toString();
    const QString suggestedName = data.value(QStringLiteral("suggestedName")).toString();
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.host().isEmpty() || !parsed.userInfo().isEmpty()
        || (parsed.scheme() != QStringLiteral("http") && parsed.scheme() != QStringLiteral("https"))) {
        socket->write(response(false, requestId, QStringLiteral("Solo se aceptan URLs HTTP/HTTPS sin credenciales.")));
        socket->flush();
        socket->disconnectFromServer();
        return;
    }
    // The desktop application selects the destination. Browser-supplied paths
    // are intentionally unsupported: the extension cannot write arbitrary files.
    emit enqueueRequested(url, suggestedName, requestId, socket);
}

} // namespace oidm
