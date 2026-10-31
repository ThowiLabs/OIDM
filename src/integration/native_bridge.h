#pragma once

#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QString>

namespace oidm {

// Browser native-messaging host and Qt desktop application use this local
// channel. The OS grants access to the current user only.
QString nativeBridgeName();

class NativeBridge final : public QObject {
    Q_OBJECT
public:
    explicit NativeBridge(QObject *parent = nullptr);
    ~NativeBridge() override;

    [[nodiscard]] bool start(QString *error = nullptr);

signals:
    void activationRequested();
    void enqueueRequested(const QString &url, const QString &suggestedName,
                          const QString &referrer, qint64 totalBytes, bool browserOwned,
                          const QString &requestId, QObject *replyTarget);
    void browserTransferReleased(const QString &id, const QString &requestId, QObject *replyTarget);
    void browserTransferAbandoned(const QString &id, const QString &requestId, QObject *replyTarget);

private:
    void onConnected();
    void processLine(class QLocalSocket *socket, const QByteArray &line);
    QLocalServer m_server;
};

} // namespace oidm
