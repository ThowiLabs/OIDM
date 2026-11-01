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
#include <QTimer>
#include <QSettings>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QDateTimeEdit>
#include <QScopeGuard>
#include <QTreeWidget>
#include <QMessageBox>
#include "ui/download_progress_dialog.h"
#include "ui/download_offer_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/scheduler_dialog.h"
#include "ui/browser_setup_dialog.h"
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
    BrowserSetupDialog setup(&window);
    if (!setup.findChild<QPushButton *>(QStringLiteral("prepareBrowserIntegration"))
        || !setup.findChild<QPushButton *>(QStringLiteral("setupExtensionFolder"))) return 28;
    QString id;
    QString error;
    const bool accepted = window.enqueueFromBrowser(
        QStringLiteral("http://127.0.0.1:9/oidm-smoke-test.bin"),
        QStringLiteral("oidm-smoke-test.bin"), QString(), 12345, true, &id, &error);
    if (!accepted || id.isEmpty()) return 22;
    auto *table = window.findChild<QTableView *>(QStringLiteral("downloadsTable"));
    auto *categories = window.findChild<QTreeWidget *>(QStringLiteral("categories"));
    if (!table || !categories || !table->model() || table->model()->rowCount() != 1) return 23;
    if (!table->currentIndex().isValid() || table->currentIndex().data(Qt::UserRole).toString() != id) return 24;
    if (categories->currentItem() != categories->topLevelItem(0)) return 25;
    if (window.findChildren<DownloadOfferDialog *>().size() != 0) return 26;
    // Pending browser-owned downloads are never started by the normal queue
    // or by an accidental click on the Start action before Chrome releases.
    QMetaObject::invokeMethod(&window, "startQueue", Qt::DirectConnection);
    QMetaObject::invokeMethod(&window, "startSelected", Qt::DirectConnection);
    QCoreApplication::processEvents();
    if (repository.all().first().status != DownloadStatus::AwaitingConfirmation
        || !window.findChildren<DownloadOfferDialog *>().isEmpty()) return 47;
    {
        // A fired schedule must NEVER start Chrome-reserved downloads.
        const QStringList keys{QStringLiteral("scheduler/enabled"), QStringLiteral("scheduler/start"),
            QStringLiteral("scheduler/hasStop"), QStringLiteral("scheduler/stop"),
            QStringLiteral("scheduler/startFired"), QStringLiteral("scheduler/stopFired")};
        QSettings config;
        QHash<QString, QVariant> originals;
        for (const QString &key : keys) {
            if (config.contains(key)) originals.insert(key, config.value(key));
        }
        const auto restore = qScopeGuard([keys, originals] {
            QSettings saved;
            for (const QString &key : keys) {
                if (originals.contains(key)) saved.setValue(key, originals.value(key));
                else saved.remove(key);
            }
        });
        config.setValue(QStringLiteral("scheduler/enabled"), true);
        config.setValue(QStringLiteral("scheduler/start"), QDateTime::currentDateTime().addSecs(-45));
        config.setValue(QStringLiteral("scheduler/hasStop"), true);
        config.setValue(QStringLiteral("scheduler/stop"), QDateTime::currentDateTime().addSecs(120));
        config.remove(QStringLiteral("scheduler/startFired"));
        config.remove(QStringLiteral("scheduler/stopFired"));
        QMetaObject::invokeMethod(&window, "checkSchedule", Qt::DirectConnection);
        if (QSettings().value(QStringLiteral("scheduler/startFired")).toString().isEmpty()
            || repository.all().first().status != DownloadStatus::AwaitingConfirmation) return 54;
    }
    if (table->model()->columnCount() != DownloadTableModel::ColumnCount
        || table->model()->columnCount() != 9) return 48;
    // Table is a real sortable model, rather than a static decorative list.
    if (table->model()->data(table->model()->index(0, DownloadTableModel::Source),
                              DownloadTableModel::SortRole).toString() != QStringLiteral("127.0.0.1")) return 49;
    if (!window.browserTransferReleased(id)) return 30;
    if (window.findChildren<DownloadOfferDialog *>().size() != 1) return 31;
    if (!window.browserTransferReleased(id)) return 32; // idempotent show, no new download
    if (window.findChildren<DownloadOfferDialog *>().size() != 1) return 33;
    auto *offer = window.findChild<DownloadOfferDialog *>(QStringLiteral("downloadOfferDialog"));
    if (!offer || !offer->findChild<QPushButton *>(QStringLiteral("downloadOfferStart"))
        || !offer->findChild<QPushButton *>(QStringLiteral("downloadOfferCancel"))) return 34;
    QCoreApplication::processEvents();
    if (offer->height() > 310) return 44; // compact confirmation, no blank vertical reserve
    SettingsDialog settings(&window);
    auto *concurrency = settings.findChild<QSpinBox *>(QStringLiteral("settingsConcurrentDownloads"));
    auto *themes = settings.findChild<QComboBox *>(QStringLiteral("settingsTheme"));
    if (!concurrency || concurrency->minimum() != 1 || concurrency->maximum() != 16
        || !themes || themes->findData(QStringLiteral("classic")) < 0) return 52;
    SchedulerDialog scheduler(&window);
    if (!scheduler.findChild<QCheckBox *>(QStringLiteral("scheduleEnabled"))
        || !scheduler.findChild<QDateTimeEdit *>(QStringLiteral("scheduleStart"))
        || !window.findChild<QAction *>(QStringLiteral("schedulerAction"))) return 53;
    auto *connections = settings.findChild<QSpinBox *>(QStringLiteral("settingsMaxConnections"));
    if (!connections || connections->value() < 1 || connections->value() > 32) return 35;
    if (SettingsDialog::preferredConnections() < 1 || SettingsDialog::preferredConnections() > 32) return 29;
    const auto records = repository.all();
    if (records.size() != 1 || records.constFirst().id != id || records.constFirst().status != DownloadStatus::AwaitingConfirmation) return 27;
    // Browser is fully canceled before user sees the offer. Cancel removes
    // the prepared record; no network DownloadTask is ever started.
    offer->findChild<QPushButton *>(QStringLiteral("downloadOfferCancel"))->click();
    if (repository.all().size() != 0 || table->model()->rowCount() != 0) return 36;

    // The browser has already released this second transfer. Only clicking
    // "Iniciar descarga" may turn the persisted record from Queued to Inspecting.
    QString startedId;
    if (!window.enqueueFromBrowser(
            QStringLiteral("http://127.0.0.1:9/oidm-start-test.bin"),
            QStringLiteral("oidm-start-test.bin"), QString(), 321, true, &startedId, &error)) return 37;
    if (!window.browserTransferReleased(startedId)) return 38;
    const auto beforeStart = repository.all();
    if (beforeStart.size() != 1 || beforeStart.first().status != DownloadStatus::AwaitingConfirmation) return 39;
    DownloadOfferDialog *secondOffer = nullptr;
    for (DownloadOfferDialog *candidate : window.findChildren<DownloadOfferDialog *>()) {
        if (candidate->downloadId() == startedId) { secondOffer = candidate; break; }
    }
    if (!secondOffer) return 40;
    auto *startButton = secondOffer->findChild<QPushButton *>(QStringLiteral("downloadOfferStart"));
    if (!startButton) return 41;
    startButton->click();
    const auto afterStart = repository.all();
    if (afterStart.size() != 1 || afterStart.first().id != startedId
        || afterStart.first().status != DownloadStatus::Inspecting) return 42;
    if (window.findChildren<DownloadProgressDialog *>().isEmpty()) return 43;

    // Verify that completion collapses an already opened progress window to
    // a concise status and destination, not an empty 505px dialog.
    Download sample = afterStart.first();
    sample.status = DownloadStatus::Downloading;
    sample.connectionPercents = {10, 25, 60, 75, 80, 95};
    DownloadProgressDialog completed(sample, &window);
    completed.show();
    QCoreApplication::processEvents();
    const int activeHeight = completed.height();
    sample.status = DownloadStatus::Completed;
    sample.downloadedBytes = 321;
    completed.updateDownload(sample);
    QCoreApplication::processEvents();
    if (completed.height() > 270 || completed.height() >= activeHeight) return 45;
    if (!completed.findChild<QLabel *>(QStringLiteral("downloadCompleteSummary"))->isVisible()) return 46;
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
                  qint64 totalBytes, bool browserOwned, const QString &requestId, QObject *replyTarget) {
            auto *socket = qobject_cast<QLocalSocket *>(replyTarget);
            if (!socket) return;
            QString id;
            QString error;
            const bool ok = window.enqueueFromBrowser(url, suggestedName, referrer, totalBytes, browserOwned, &id, &error);
            QJsonObject response;
            response.insert(QStringLiteral("ok"), ok);
            response.insert(QStringLiteral("requestId"), requestId);
            response.insert(ok ? QStringLiteral("result") : QStringLiteral("error"), ok ? id : error);
            socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
            socket->flush();
            socket->disconnectFromServer();
        });
    const auto replyToBrowser = [](QObject *target, bool ok, const QString &requestId, const QString &message) {
        auto *socket = qobject_cast<QLocalSocket *>(target);
        if (!socket) return;
        QJsonObject response{{QStringLiteral("ok"), ok}, {QStringLiteral("requestId"), requestId}};
        response.insert(ok ? QStringLiteral("result") : QStringLiteral("error"), message);
        socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n');
        socket->flush();
        socket->disconnectFromServer();
    };
    QObject::connect(&bridge, &oidm::NativeBridge::browserTransferReleased, &window,
        [&window, replyToBrowser](const QString &id, const QString &requestId, QObject *target) {
            const bool ok = window.browserTransferReleased(id);
            replyToBrowser(target, ok, requestId,
                ok ? QStringLiteral("Descarga lista para confirmar") : QStringLiteral("Transferencia no encontrada"));
        });
    QObject::connect(&bridge, &oidm::NativeBridge::browserTransferAbandoned, &window,
        [&window, replyToBrowser](const QString &id, const QString &requestId, QObject *target) {
            const bool ok = window.browserTransferAbandoned(id);
            replyToBrowser(target, ok, requestId,
                ok ? QStringLiteral("Descarga descartada") : QStringLiteral("No se puede descartar la transferencia"));
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
    QSettings settings;
    const bool explicitSetup = application.arguments().contains(QStringLiteral("--setup-browser"));
    if (explicitSetup || !settings.value(QStringLiteral("browser/integrationOffered"), false).toBool()) {
        settings.setValue(QStringLiteral("browser/integrationOffered"), true);
        QTimer::singleShot(0, &window, &oidm::MainWindow::openBrowserSetup);
    }
    return application.exec();
}
