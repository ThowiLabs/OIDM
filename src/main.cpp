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
#include <QTableView>
#include <QTreeWidget>
#include <QMessageBox>
#include "ui/download_progress_dialog.h"
#include <QProcess>
#include <QFileInfo>
#include "integration/browser_extension_id.h"
#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {
using namespace oidm;

int runUiSelfTest()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 20;
    DownloadRepository repository(temporary.filePath(QStringLiteral("ui.sqlite3")));
    if (!repository.isOpen()) return 21;
    MainWindow window(&repository);
    QString id;
    QString error;
    const bool accepted = window.enqueueFromBrowser(
        QStringLiteral("http://127.0.0.1:9/oidm-smoke-test.bin"),
        QStringLiteral("oidm-smoke-test.bin"), QString(), &id, &error);
    if (!accepted || id.isEmpty()) return 22;
    auto *table = window.findChild<QTableView *>(QStringLiteral("downloadsTable"));
    auto *categories = window.findChild<QTreeWidget *>(QStringLiteral("categories"));
    if (!table || !categories || !table->model() || table->model()->rowCount() != 1) return 23;
    if (!table->currentIndex().isValid() || table->currentIndex().data(Qt::UserRole).toString() != id) return 24;
    if (categories->currentItem() != categories->topLevelItem(0)) return 25;
    if (window.findChildren<DownloadProgressDialog *>().size() != 1) return 26;
    const auto records = repository.all();
    if (records.size() != 1 || records.constFirst().id != id) return 27;
    return 0;
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
    if (application.arguments().contains(QStringLiteral("--ui-self-test"))) {
        return runUiSelfTest();
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
    QObject::connect(&bridge, &oidm::NativeBridge::activationRequested, &window, [&window] {
        if (window.isMinimized()) window.showNormal();
        if (!window.isVisible()) window.show();
        window.raise();
        window.activateWindow();
    });
    QString bridgeError;
    if (!bridge.start(&bridgeError)) {
        QLocalSocket first;
        first.connectToServer(oidm::nativeBridgeName());
        if (first.waitForConnected(350)) {
            const QJsonObject request{{QStringLiteral("type"), QStringLiteral("activate")},
                                      {QStringLiteral("requestId"), QStringLiteral("second-instance")}};
            first.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
            first.waitForBytesWritten(500);
            first.waitForReadyRead(650);
            QMessageBox::information(nullptr, QObject::tr("OIDM ya está abierto"),
                QObject::tr("Ya existe otra instancia de OIDM en ejecución. Se intentó mostrar la primera ventana.\n\n"
                    "Para evitar que las descargas se envíen a una instancia antigua, "
                    "cierra todas las ventanas de OIDM y vuelve a abrir esta versión."));
            return 0;
        }
        QMessageBox::critical(nullptr, QObject::tr("Integración no disponible"),
            QObject::tr("No se pudo iniciar el receptor del navegador: %1").arg(bridgeError));
        return 4;
    }
    window.show();
    return application.exec();
}
