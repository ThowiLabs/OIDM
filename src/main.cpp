#include "core/download_repository.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace {
using namespace oidm;

bool runTransferScenario(const QString &root, const QByteArray &payload,
                         const bool ignoreRange, const bool wrongRange,
                         const QString &caseName)
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) return false;

    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, payload, ignoreRange, wrongRange] {
        while (server.hasPendingConnections()) {
            QTcpSocket *socket = server.nextPendingConnection();
            auto *requestBuffer = new QByteArray();
            QObject::connect(socket, &QTcpSocket::readyRead, socket,
                             [socket, requestBuffer, payload, ignoreRange, wrongRange] {
                *requestBuffer += socket->readAll();
                if (!requestBuffer->contains(QByteArrayLiteral("\r\n\r\n"))) return;

                const QRegularExpression rangeExpression(
                    QStringLiteral("(?:^|\\r\\n)Range:\\s*bytes=(\\d+)-(\\d+)?"),
                    QRegularExpression::CaseInsensitiveOption);
                const auto match = rangeExpression.match(QString::fromLatin1(*requestBuffer));
                const bool requestedRange = match.hasMatch();
                const qint64 requestedStart = requestedRange ? match.captured(1).toLongLong() : 0;
                const qint64 requestedEnd = requestedRange && !match.captured(2).isEmpty()
                    ? match.captured(2).toLongLong() : static_cast<qint64>(payload.size() - 1);
                QByteArray body;
                QByteArray response;

                if (requestedRange && !ignoreRange) {
                    const qint64 responseStart = wrongRange ? requestedStart + 1 : requestedStart;
                    const qint64 end = qMin(requestedEnd, static_cast<qint64>(payload.size() - 1));
                    body = end >= responseStart
                        ? payload.mid(static_cast<qsizetype>(responseStart), static_cast<qsizetype>(end - responseStart + 1))
                        : QByteArray{};
                    response = QByteArrayLiteral("HTTP/1.1 206 Partial Content\r\n")
                        + QByteArrayLiteral("Content-Type: application/octet-stream\r\n")
                        + QByteArrayLiteral("Accept-Ranges: bytes\r\n")
                        + QByteArrayLiteral("Content-Range: bytes ") + QByteArray::number(responseStart)
                        + QByteArrayLiteral("-") + QByteArray::number(end)
                        + QByteArrayLiteral("/") + QByteArray::number(payload.size()) + QByteArrayLiteral("\r\n")
                        + QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size())
                        + QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
                } else {
                    body = payload;
                    response = QByteArrayLiteral("HTTP/1.1 200 OK\r\n")
                        + QByteArrayLiteral("Content-Type: application/octet-stream\r\n")
                        + QByteArrayLiteral("Accept-Ranges: bytes\r\n")
                        + QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size())
                        + QByteArrayLiteral("\r\nConnection: close\r\n\r\n");
                }
                response += body;
                socket->write(response);
                socket->disconnectFromHost();
                delete requestBuffer;
            });
        }
    });

    DownloadRepository repository(QDir(root).filePath(caseName + QStringLiteral(".sqlite3")));
    if (!repository.isOpen()) return false;

    Download item;
    item.id = caseName;
    item.url = QUrl(QStringLiteral("http://127.0.0.1:%1/payload.bin").arg(server.serverPort())).toString();
    item.destination = QDir(root).filePath(caseName + QStringLiteral(".bin"));
    item.fileName = caseName + QStringLiteral(".bin");
    item.status = DownloadStatus::Paused;
    item.createdAt = QDateTime::currentDateTimeUtc();
    item.updatedAt = item.createdAt;

    QFile partial(item.destination + QStringLiteral(".oidm-part"));
    if (!partial.open(QIODevice::WriteOnly)) return false;
    const QByteArray prefix = payload.left(7);
    if (partial.write(prefix) != prefix.size()) return false;
    partial.close();
    item.downloadedBytes = prefix.size();
    item.totalBytes = payload.size();

    QString error;
    if (!repository.insert(item, &error)) return false;
    DownloadManager manager(&repository);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&manager, &DownloadManager::downloadFinished, &loop,
                     [&loop, &timeout, caseName](const Download &finished) {
        if (finished.id == caseName) {
            timeout.stop();
            loop.quit();
        }
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (!manager.start(item.id, &error)) return false;
    timeout.start(5000);
    loop.exec();

    QElapsedTimer cleanupTimer;
    cleanupTimer.start();
    while (manager.isActive(item.id) && cleanupTimer.elapsed() < 3000) {
        QEventLoop cleanupLoop;
        QTimer::singleShot(20, &cleanupLoop, &QEventLoop::quit);
        cleanupLoop.exec();
    }

    const QVector<Download> records = repository.all(&error);
    if (records.size() != 1) return false;
    const bool shouldSucceed = !wrongRange;
    if (shouldSucceed) {
        if (records.first().status != DownloadStatus::Completed) return false;
        QFile result(item.destination);
        if (!result.open(QIODevice::ReadOnly)) return false;
        if (result.readAll() != payload) return false;
    } else {
        if (records.first().status != DownloadStatus::Failed) return false;
        if (QFileInfo::exists(item.destination)) return false;
    }
    return !manager.isActive(item.id);
}

int runSelfTest()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 10;
    {
        DownloadRepository repository(temporary.filePath(QStringLiteral("repository.sqlite3")));
        if (!repository.isOpen()) return 11;

        Download item;
        item.id = QStringLiteral("self-test-download");
        item.url = QStringLiteral("https://example.invalid/test.bin");
        item.destination = temporary.filePath(QStringLiteral("test.bin"));
        item.fileName = QStringLiteral("test.bin");
        item.status = DownloadStatus::Queued;
        item.createdAt = QDateTime::currentDateTimeUtc();
        item.updatedAt = item.createdAt;
        QString error;
        if (!repository.insert(item, &error)) return 12;

        const auto records = repository.all(&error);
        if (records.size() != 1 || records.first().id != item.id) return 13;
        item.status = DownloadStatus::Downloading;
        item.updatedAt = QDateTime::currentDateTimeUtc();
        if (!repository.update(item, &error)) return 14;
        if (!repository.recoverInterrupted(&error)) return 15;
        const auto recovered = repository.all(&error);
        if (recovered.size() != 1 || recovered.first().status != DownloadStatus::Paused) return 16;
        if (!repository.remove(item.id, &error)) return 17;
        if (!repository.all(&error).isEmpty()) return 18;
    }

    const QByteArray payload = QByteArrayLiteral("OIDM Qt range-resume integration test payload");
    if (!runTransferScenario(temporary.path(), payload, false, false, QStringLiteral("valid-range"))) return 20;
    if (!runTransferScenario(temporary.path(), payload, true, false, QStringLiteral("ignored-range"))) return 21;
    if (!runTransferScenario(temporary.path(), payload, false, true, QStringLiteral("invalid-range"))) return 22;
    return 0;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ThowiLabs"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("thowilabs.com"));
    QCoreApplication::setApplicationName(QStringLiteral("OIDM"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OIDM_VERSION));

    if (application.arguments().contains(QStringLiteral("--self-test"))) {
        return runSelfTest();
    }

    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(dataDirectory)) return 1;

    oidm::DownloadRepository repository(QDir(dataDirectory).filePath(QStringLiteral("oidm.sqlite3")));
    if (!repository.isOpen()) return 2;
    QString recoveryError;
    if (!repository.recoverInterrupted(&recoveryError)) return 3;

    oidm::MainWindow window(&repository);
    window.show();
    return application.exec();
}
