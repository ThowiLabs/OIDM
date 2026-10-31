#include "core/download_repository.h"
#include "ui/main_window.h"
#include "integration/native_bridge.h"

#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QFileInfo>
#include "integration/browser_extension_id.h"
#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {
using namespace oidm;

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

    // Detailed HTTP transport tests live in oidm_download_tests (QtTest).
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

    // Register the bundled, fixed-ID Chromium extension on every launch.
    // Native Messaging registration is per-user and does not require UAC.
    const QString nativeHost = QDir(QCoreApplication::applicationDirPath()).filePath(
#ifdef Q_OS_WIN
        QStringLiteral("oidm-native-host.exe")
#else
        QStringLiteral("oidm-native-host")
#endif
    );
    if (QFileInfo(nativeHost).isExecutable()) {
        QProcess registration;
        registration.setProgram(nativeHost);
        registration.setArguments({QStringLiteral("--register-chromium"),
                                   QStringLiteral(OIDM_CHROMIUM_EXTENSION_ID)});
#ifdef Q_OS_WIN
        registration.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif
        registration.start();
        if (!registration.waitForFinished(3500) || registration.exitCode() != 0) {
            qWarning("OIDM could not register native browser integration.");
        }
    }

    oidm::MainWindow window(&repository);
    oidm::NativeBridge bridge(&window);
    QObject::connect(&bridge, &oidm::NativeBridge::enqueueRequested, &window,
        [&window](const QString &url, const QString &suggestedName, const QString &referrer,
                  const QString &requestId, QObject *replyTarget) {
            auto *socket = qobject_cast<QLocalSocket *>(replyTarget);
            if (!socket) return;
            QString id;
            QString error;
            const bool ok = window.enqueueFromBrowser(url, suggestedName, referrer, &id, &error);
            QJsonObject response;
            response.insert(QStringLiteral("ok"), ok);
            response.insert(QStringLiteral("requestId"), requestId);
            response.insert(ok ? QStringLiteral("result") : QStringLiteral("error"), ok ? id : error);
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
            socket->flush();
            socket->disconnectFromServer();
        });
    QString bridgeError;
    if (!bridge.start(&bridgeError)) {
        qWarning("OIDM Native Messaging bridge unavailable: %s", qPrintable(bridgeError));
    }
    window.show();
    return application.exec();
}
