#include "core/download_task.h"
#include "core/download_manager.h"
#include "core/download_repository.h"

#include <QFile>
#include <QHostAddress>
#include <QHash>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QQueue>
#include <QPointer>
#include <QElapsedTimer>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace oidm;

namespace {

class LocalHttpServer final : public QObject {
    Q_OBJECT
public:
    enum class Behavior {
        FullResponse,
        RangeResponse,
        IgnoreRange,
        WrongRange,
        TruncatedResponse
    };

    explicit LocalHttpServer(QByteArray body, Behavior behavior, QObject *parent = nullptr)
        : QObject(parent), m_body(std::move(body)), m_behavior(behavior)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &LocalHttpServer::acceptConnections);
    }

    [[nodiscard]] bool listen()
    {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] QString url() const
    {
        return QStringLiteral("http://127.0.0.1:%1/file.bin").arg(m_server.serverPort());
    }

    [[nodiscard]] QByteArray lastRequest() const { return m_lastRequest; }
    [[nodiscard]] QList<QByteArray> requests() const { return m_allRequests; }
    [[nodiscard]] int maxConcurrentConnections() const { return m_maxConcurrentConnections; }
    [[nodiscard]] const QByteArray &body() const { return m_body; }

private slots:
    void acceptConnections()
    {
        while (m_server.hasPendingConnections()) {
            QTcpSocket *socket = m_server.nextPendingConnection();
            if (socket == nullptr) continue;
            ++m_activeConnections;
            m_maxConcurrentConnections = qMax(m_maxConcurrentConnections, m_activeConnections);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                if (m_handled.contains(socket)) return;
                m_requests[socket].append(socket->readAll());
                if (!m_requests.value(socket).contains(QByteArrayLiteral("\r\n\r\n"))) return;
                m_handled.insert(socket);
                m_lastRequest = m_requests.take(socket);
                m_allRequests.push_back(m_lastRequest);
                respond(socket, m_lastRequest);
            });
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                m_handled.remove(socket);
                m_requests.remove(socket);
                m_activeConnections = qMax(0, m_activeConnections - 1);
                socket->deleteLater();
            });
        }
    }

private:
    [[nodiscard]] static QPair<qint64, qint64> requestedRange(const QByteArray &request)
    {
        const QRegularExpression pattern(QStringLiteral("(?im)^range:\\s*bytes=(\\d+)-(\\d+)?"));
        const auto match = pattern.match(QString::fromLatin1(request));
        if (!match.hasMatch()) return {-1, -1};
        bool startOk = false;
        bool endOk = true;
        const qint64 start = match.captured(1).toLongLong(&startOk);
        const qint64 end = match.captured(2).isEmpty() ? -1 : match.captured(2).toLongLong(&endOk);
        return startOk && endOk ? qMakePair(start, end) : qMakePair(qint64{-1}, qint64{-1});
    }

    [[nodiscard]] QByteArray response(int status, const QByteArray &reason,
                                      const QByteArray &headers, const QByteArray &body) const
    {
        QByteArray result = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
        result += "Connection: close\r\n";
        result += "Content-Type: application/octet-stream\r\n";
        result += "ETag: \"oidm-test-v1\"\r\n";
        result += headers;
        result += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
        result += body;
        return result;
    }

    void respond(QTcpSocket *socket, const QByteArray &request)
    {
        const auto [offset, requestedEnd] = requestedRange(request);
        QByteArray output;

        switch (m_behavior) {
        case Behavior::FullResponse:
        case Behavior::IgnoreRange:
            output = response(200, "OK", "Accept-Ranges: bytes\r\n", m_body);
            break;
        case Behavior::TruncatedResponse: {
            QByteArray partial = m_body.left(qMax(1, m_body.size() / 2));
            QByteArray headers = "Accept-Ranges: bytes\r\nContent-Length: "
                + QByteArray::number(m_body.size()) + "\r\n";
            output = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/octet-stream\r\n"
                + headers + "\r\n" + partial;
            break;
        }
        case Behavior::RangeResponse:
        case Behavior::WrongRange: {
            if (offset < 0 || offset >= m_body.size()) {
                output = response(416, "Range Not Satisfiable",
                                  "Content-Range: bytes */" + QByteArray::number(m_body.size()) + "\r\n", {});
                break;
            }
            const qint64 start = m_behavior == Behavior::WrongRange ? offset + 1 : offset;
            const qint64 end = requestedEnd >= 0 ? qMin(requestedEnd, static_cast<qint64>(m_body.size() - 1))
                                                   : static_cast<qint64>(m_body.size() - 1);
            const QByteArray partial = end >= start
                ? m_body.mid(static_cast<qsizetype>(start), static_cast<qsizetype>(end - start + 1))
                : QByteArray{};
            const QByteArray headers = "Accept-Ranges: bytes\r\nContent-Range: bytes "
                + QByteArray::number(start) + '-' + QByteArray::number(end) + '/'
                + QByteArray::number(m_body.size()) + "\r\n";
            output = response(206, "Partial Content", headers, partial);
            break;
        }
        }

        QTimer::singleShot(20, socket, [socket, output] {
            if (socket->state() == QAbstractSocket::ConnectedState) {
                socket->write(output);
                socket->disconnectFromHost();
            }
        });
    }

    QTcpServer m_server;
    QByteArray m_body;
    Behavior m_behavior;
    QHash<QTcpSocket *, QByteArray> m_requests;
    QSet<QTcpSocket *> m_handled;
    QByteArray m_lastRequest;
    QList<QByteArray> m_allRequests;
    int m_activeConnections{0};
    int m_maxConcurrentConnections{0};
};


class HoldingHttpServer final : public QObject {
    Q_OBJECT
public:
    explicit HoldingHttpServer(QByteArray body, QObject *parent = nullptr)
        : QObject(parent), m_body(std::move(body))
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *socket = m_server.nextPendingConnection();
                if (socket == nullptr) continue;
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    if (m_handled.contains(socket)) return;
                    m_requests[socket].append(socket->readAll());
                    if (!m_requests.value(socket).contains(QByteArrayLiteral("\r\n\r\n"))) return;
                    m_handled.insert(socket);
                    const QByteArray request = m_requests.take(socket).toLower();
                    if (request.contains(QByteArrayLiteral("range: bytes=0-0"))) {
                        // The origin ignores Range for the lightweight probe;
                        // only full GET requests are held for concurrency tests.
                        const QByteArray response = QByteArrayLiteral(
                            "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/octet-stream\r\nContent-Length: ")
                            + QByteArray::number(m_body.size())
                            + QByteArrayLiteral("\r\n\r\n") + m_body;
                        socket->write(response);
                        socket->disconnectFromHost();
                        return;
                    }
                    m_pending.enqueue(socket);
                    ++m_requestCount;
                });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                m_handled.remove(socket);
                m_requests.remove(socket);
                socket->deleteLater();
            });
            }
        });
    }

    [[nodiscard]] bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    [[nodiscard]] QString url() const
    {
        return QStringLiteral("http://127.0.0.1:%1/queued.bin").arg(m_server.serverPort());
    }
    [[nodiscard]] int requestCount() const { return m_requestCount; }
    [[nodiscard]] int pendingCount() const { return m_pending.size(); }

    bool releaseNext()
    {
        while (!m_pending.isEmpty()) {
            const QPointer<QTcpSocket> guardedSocket = m_pending.dequeue();
            QTcpSocket *socket = guardedSocket.data();
            if (socket == nullptr || socket->state() == QAbstractSocket::UnconnectedState) continue;
            QByteArray response = "HTTP/1.1 200 OK\r\nConnection: close\r\n"
                "Content-Type: application/octet-stream\r\nContent-Length: "
                + QByteArray::number(m_body.size()) + "\r\n\r\n" + m_body;
            socket->write(response);
            socket->disconnectFromHost();
            return true;
        }
        return false;
    }

private:
    QTcpServer m_server;
    QByteArray m_body;
    QHash<QTcpSocket *, QByteArray> m_requests;
    QSet<QTcpSocket *> m_handled;
    QQueue<QPointer<QTcpSocket>> m_pending;
    int m_requestCount{0};
};

Download makeDownload(const QString &url, const QString &destination)
{
    Download item;
    item.id = QStringLiteral("test-download");
    item.url = url;
    item.destination = destination;
    item.fileName = QFileInfo(destination).fileName();
    item.status = DownloadStatus::Queued;
    item.createdAt = QDateTime::currentDateTimeUtc();
    item.updatedAt = item.createdAt;
    return item;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

} // namespace

class DownloadTaskTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void downloadsFullResponse();
    void segmentedTransfersReassembleLargeFiles();
    void segmentedTransferResumesSavedFragments();
    void resumesFromValidatedRange();
    void restartsWhenServerIgnoresRange();
    void rejectsMismatchedContentRange();
    void preservesPartialFileAfterInterruptedResponse();
    void limitsConcurrentDownloadsAndStartsQueuedItems();
};

void DownloadTaskTests::initTestCase()
{
    qRegisterMetaType<oidm::Download>("oidm::Download");
}

void DownloadTaskTests::downloadsFullResponse()
{
    const QByteArray body("OIDM full download test payload");
    LocalHttpServer server(body, LocalHttpServer::Behavior::FullResponse);
    QVERIFY(server.listen());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    DownloadTask task(makeDownload(server.url(), temporary.filePath(QStringLiteral("full.bin"))));
    QSignalSpy finished(&task, &DownloadTask::finished);
    QVERIFY(finished.isValid());
    task.start();
    QVERIFY(finished.wait(5000));

    const Download result = qvariant_cast<Download>(finished.takeFirst().at(0));
    QCOMPARE(result.status, DownloadStatus::Completed);
    QCOMPARE(result.downloadedBytes, static_cast<qint64>(body.size()));
    QCOMPARE(readFile(result.destination), body);
}

void DownloadTaskTests::segmentedTransfersReassembleLargeFiles()
{
    QByteArray body(5 * 1024 * 1024 + 127, Qt::Uninitialized);
    for (qsizetype index = 0; index < body.size(); ++index) {
        body[index] = static_cast<char>((index * 17 + index / 809) % 251);
    }
    LocalHttpServer server(body, LocalHttpServer::Behavior::RangeResponse);
    QVERIFY(server.listen());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString destination = temporary.filePath(QStringLiteral("parallel.bin"));
    DownloadTask task(makeDownload(server.url(), destination));
    QSignalSpy finished(&task, &DownloadTask::finished);
    task.start();
    QVERIFY2(finished.wait(20000), "Segmented HTTP download did not terminate");
    const Download result = qvariant_cast<Download>(finished.takeFirst().at(0));
    QCOMPARE(result.status, DownloadStatus::Completed);
    QCOMPARE(result.totalBytes, static_cast<qint64>(body.size()));
    QCOMPARE(result.downloadedBytes, static_cast<qint64>(body.size()));
    QCOMPARE(readFile(destination), body);
    int segments = 0;
    for (const QByteArray &request : server.requests()) {
        if (!request.contains(QByteArrayLiteral("Range: bytes=0-0"))
            && request.toLower().contains(QByteArrayLiteral("range: bytes="))) ++segments;
    }
    QVERIFY2(segments >= 4, "The engine did not issue multiple bounded requests");
    QVERIFY(!QFileInfo::exists(destination + QStringLiteral(".oidm-parts")));
}

void DownloadTaskTests::segmentedTransferResumesSavedFragments()
{
    QByteArray body(5 * 1024 * 1024 + 127, Qt::Uninitialized);
    for (qsizetype index = 0; index < body.size(); ++index) {
        body[index] = static_cast<char>((index * 7 + index / 1009) % 251);
    }
    LocalHttpServer server(body, LocalHttpServer::Behavior::RangeResponse);
    QVERIFY(server.listen());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString destination = temporary.filePath(QStringLiteral("recover.bin"));
    const QString partsDirectory = destination + QStringLiteral(".oidm-parts");
    QVERIFY(QDir().mkpath(partsDirectory));
    constexpr int count = 5;
    const qint64 base = body.size() / count;
    const qint64 remainder = body.size() % count;
    auto segmentStart = [&](int index) {
        return static_cast<qint64>(index) * base + qMin<qint64>(index, remainder);
    };
    const qint64 firstBytes = 35031;
    const QString firstPart = partsDirectory + QStringLiteral("/segment_00.part");
    const QString secondPart = partsDirectory + QStringLiteral("/segment_01.part");
    {
        QFile part(firstPart);
        QVERIFY(part.open(QIODevice::WriteOnly));
        QCOMPARE(part.write(body.left(firstBytes)), firstBytes);
    }
    {
        const qint64 begin = segmentStart(1);
        const qint64 length = base + (1 < remainder ? 1 : 0);
        QFile part(secondPart);
        QVERIFY(part.open(QIODevice::WriteOnly));
        QCOMPARE(part.write(body.mid(begin, length)), length);
    }
    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), 1);
    manifest.insert(QStringLiteral("url"), server.url());
    manifest.insert(QStringLiteral("totalBytes"), static_cast<double>(body.size()));
    manifest.insert(QStringLiteral("segmentCount"), count);
    manifest.insert(QStringLiteral("firstByte"), QString::fromLatin1(body.left(1).toHex()));
    manifest.insert(QStringLiteral("etag"), QStringLiteral("\"oidm-test-v1\""));
    manifest.insert(QStringLiteral("lastModified"), QString{});
    manifest.insert(QStringLiteral("contentType"), QStringLiteral("application/octet-stream"));
    QFile record(partsDirectory + QStringLiteral("/manifest.json"));
    QVERIFY(record.open(QIODevice::WriteOnly));
    const QByteArray json = QJsonDocument(manifest).toJson();
    QCOMPARE(record.write(json), static_cast<qint64>(json.size()));
    record.close();

    DownloadTask task(makeDownload(server.url(), destination));
    QSignalSpy finished(&task, &DownloadTask::finished);
    task.start();
    QVERIFY2(finished.wait(20000), "Resumed segmented download did not terminate");
    const Download result = qvariant_cast<Download>(finished.takeFirst().at(0));
    QCOMPARE(result.status, DownloadStatus::Completed);
    QCOMPARE(readFile(destination), body);

    const QByteArray expectedRemainingRequest = QByteArrayLiteral("range: bytes=")
        + QByteArray::number(firstBytes) + QByteArrayLiteral("-");
    const QByteArray forbiddenCompleteRequest = QByteArrayLiteral("range: bytes=")
        + QByteArray::number(segmentStart(1)) + QByteArrayLiteral("-");
    bool sawPartialResume = false;
    bool requestedAlreadyComplete = false;
    for (const QByteArray &request : server.requests()) {
        const QByteArray lower = request.toLower();
        sawPartialResume |= lower.contains(expectedRemainingRequest);
        requestedAlreadyComplete |= lower.contains(forbiddenCompleteRequest);
    }
    QVERIFY2(sawPartialResume, "Engine did not resume the saved partial segment");
    QVERIFY2(!requestedAlreadyComplete, "Engine re-downloaded an already complete segment");
    QVERIFY(!QFileInfo::exists(partsDirectory));
}

void DownloadTaskTests::resumesFromValidatedRange()
{
    const QByteArray prefix("saved-prefix-");
    const QByteArray body = prefix + QByteArray("remaining-data-for-resume");
    LocalHttpServer server(body, LocalHttpServer::Behavior::RangeResponse);
    QVERIFY(server.listen());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString destination = temporary.filePath(QStringLiteral("resume.bin"));
    QFile partial(destination + QStringLiteral(".oidm-part"));
    QVERIFY(partial.open(QIODevice::WriteOnly));
    QCOMPARE(partial.write(prefix), static_cast<qint64>(prefix.size()));
    partial.close();

    DownloadTask task(makeDownload(server.url(), destination));
    QSignalSpy finished(&task, &DownloadTask::finished);
    task.start();
    QVERIFY(finished.wait(5000));

    const Download result = qvariant_cast<Download>(finished.takeFirst().at(0));
    QCOMPARE(result.status, DownloadStatus::Completed);
    bool sawFirstSegmentContinuation = false;
    for (const auto &request : server.requests()) {
        sawFirstSegmentContinuation |= request.toLower().contains("range: bytes=" + QByteArray::number(prefix.size()) + "-");
    }
    QVERIFY2(sawFirstSegmentContinuation, qPrintable(QString::fromLatin1(server.lastRequest())));
    QCOMPARE(readFile(destination), body);
}

void DownloadTaskTests::restartsWhenServerIgnoresRange()
{
    const QByteArray stalePrefix("stale-partial-data");
    const QByteArray body("authoritative-full-response");
    LocalHttpServer server(body, LocalHttpServer::Behavior::IgnoreRange);
    QVERIFY(server.listen());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString destination = temporary.filePath(QStringLiteral("restart.bin"));
    QFile partial(destination + QStringLiteral(".oidm-part"));
    QVERIFY(partial.open(QIODevice::WriteOnly));
    QCOMPARE(partial.write(stalePrefix), static_cast<qint64>(stalePrefix.size()));
    partial.close();

    DownloadTask task(makeDownload(server.url(), destination));
    QSignalSpy finished(&task, &DownloadTask::finished);
    task.start();
    QVERIFY(finished.wait(5000));

    const Download result = qvariant_cast<Download>(finished.takeFirst().at(0));
    QCOMPARE(result.status, DownloadStatus::Completed);
    QCOMPARE(readFile(destination), body);
}

void DownloadTaskTests::rejectsMismatchedContentRange()
{
    const QByteArray prefix("existing-");
    const QByteArray body = prefix + QByteArray("correct-remaining-data");
    LocalHttpServer server(body, LocalHttpServer::Behavior::WrongRange);
    QVERIFY(server.listen());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString destination = temporary.filePath(QStringLiteral("invalid-range.bin"));
    QFile partial(destination + QStringLiteral(".oidm-part"));
    QVERIFY(partial.open(QIODevice::WriteOnly));
    QCOMPARE(partial.write(prefix), static_cast<qint64>(prefix.size()));
    partial.close();

    DownloadTask task(makeDownload(server.url(), destination));
    QSignalSpy finished(&task, &DownloadTask::finished);
    task.start();
    QVERIFY(finished.wait(5000));

    const Download result = qvariant_cast<Download>(finished.takeFirst().at(0));
    QCOMPARE(result.status, DownloadStatus::Failed);
    QVERIFY(!QFileInfo::exists(destination));
    QCOMPARE(readFile(destination + QStringLiteral(".oidm-part")), prefix);
}

void DownloadTaskTests::preservesPartialFileAfterInterruptedResponse()
{
    const QByteArray body(8192, 'x');
    LocalHttpServer server(body, LocalHttpServer::Behavior::TruncatedResponse);
    QVERIFY(server.listen());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString destination = temporary.filePath(QStringLiteral("interrupted.bin"));
    DownloadTask task(makeDownload(server.url(), destination));
    QSignalSpy finished(&task, &DownloadTask::finished);
    task.start();
    QVERIFY(finished.wait(5000));

    const Download result = qvariant_cast<Download>(finished.takeFirst().at(0));
    QCOMPARE(result.status, DownloadStatus::Failed);
    QVERIFY(!QFileInfo::exists(destination));
    const QFileInfo partialInfo(destination + QStringLiteral(".oidm-part"));
    QVERIFY(partialInfo.exists());
    QVERIFY(partialInfo.size() > 0);
    QVERIFY(partialInfo.size() < body.size());
}

void DownloadTaskTests::limitsConcurrentDownloadsAndStartsQueuedItems()
{
    const QByteArray body("queue-test");
    HoldingHttpServer server(body);
    QVERIFY(server.listen());
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    DownloadRepository repository(temporary.filePath(QStringLiteral("queue.sqlite3")));
    QVERIFY2(repository.isOpen(), qPrintable(repository.lastError()));

    QStringList ids;
    for (int index = 0; index < 4; ++index) {
        Download item = makeDownload(server.url(), temporary.filePath(
            QStringLiteral("queued-%1.bin").arg(index)));
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString error;
        QVERIFY2(repository.insert(item, &error), qPrintable(error));
        ids.push_back(item.id);
    }

    DownloadManager manager(&repository);
    QSignalSpy finished(&manager, &DownloadManager::downloadFinished);
    QVERIFY(finished.isValid());
    for (const QString &id : ids) {
        QString error;
        QVERIFY2(manager.start(id, &error), qPrintable(error));
    }

    QTRY_COMPARE(server.requestCount(), 3);
    QTRY_COMPARE(server.pendingCount(), 3);
    QVERIFY(server.releaseNext());
    QTRY_COMPARE(server.requestCount(), 4);

    QElapsedTimer releaseTimer;
    releaseTimer.start();
    while (finished.count() < 4 && releaseTimer.elapsed() < 12000) {
        while (server.releaseNext()) {}
        QTest::qWait(10);
    }
    QCOMPARE(finished.count(), 4);

    const QVector<Download> saved = repository.all();
    QCOMPARE(saved.size(), 4);
    for (const Download &item : saved) {
        QCOMPARE(item.status, DownloadStatus::Completed);
        QCOMPARE(readFile(item.destination), body);
    }
}

QTEST_GUILESS_MAIN(DownloadTaskTests)
#include "download_task_tests.moc"
