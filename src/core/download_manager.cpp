#include "core/download_manager.h"

#include "core/download_repository.h"
#include "core/download_task.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace oidm {
namespace {
void setError(QString *target, const QString &message) {
    if (target) *target = message;
}

void cleanupTemporaryArtifacts(const Download &item)
{
    const QString partial = item.destination + QStringLiteral(".oidm-part");
    const QString directory = item.destination + QStringLiteral(".oidm-parts");
    (void)QFile::remove(partial);
    const QFileInfo info(directory);
    if (info.isSymLink()) {
        (void)QFile::remove(directory);
    } else if (info.exists() && info.isDir()) {
        (void)QDir(directory).removeRecursively();
    }
}
}

DownloadManager::DownloadManager(DownloadRepository *repository, QObject *parent)
    : QObject(parent), m_repository(repository) {
    qRegisterMetaType<oidm::Download>("oidm::Download");
}

DownloadManager::~DownloadManager() {
    m_shuttingDown = true;
    pauseAll();
    // QObject parent ownership safely destroys any deferred-delete tasks.
}

bool DownloadManager::loadDownload(const QString &id, Download *download, QString *error) const {
    if (m_repository == nullptr || !m_repository->isOpen()) {
        setError(error, tr("La base de datos no está disponible."));
        return false;
    }
    const auto items = m_repository->all(error);
    for (const auto &item : items) {
        if (item.id == id) {
            if (download) *download = item;
            return true;
        }
    }
    setError(error, tr("No se encontró la descarga."));
    return false;
}

void DownloadManager::setInactiveStatus(Download &item, DownloadStatus status) {
    item.status = status;
    item.speedBytesPerSecond = 0;
    item.error.clear();
    item.updatedAt = QDateTime::currentDateTimeUtc();
    if (m_repository) {
        QString error;
        if (!m_repository->update(item, &error)) qWarning() << "OIDM state persistence error:" << error;
    }
    emit downloadUpdated(item);
    emit downloadFinished(item);
}

bool DownloadManager::start(const QString &id, QString *error) {
    if (m_shuttingDown) {
        setError(error, tr("La aplicación se está cerrando."));
        return false;
    }
    if (m_tasks.contains(id)) {
        setError(error, tr("La descarga ya está activa."));
        return false;
    }

    Download download;
    if (!loadDownload(id, &download, error)) return false;
    if (download.status == DownloadStatus::Completed || download.status == DownloadStatus::Cancelled) {
        setError(error, tr("La descarga ya terminó o fue cancelada."));
        return false;
    }
    if (m_tasks.size() >= MaxConcurrentDownloads) {
        if (download.status != DownloadStatus::Queued) {
            download.status = DownloadStatus::Queued;
            download.error.clear();
            download.updatedAt = QDateTime::currentDateTimeUtc();
            if (m_repository == nullptr || !m_repository->update(download, error)) return false;
            emit downloadUpdated(download);
        }
        return true;
    }

    auto *task = new DownloadTask(download, this);
    m_tasks.insert(id, task);
    connect(task, &DownloadTask::updated, this, &DownloadManager::onTaskUpdated);
    connect(task, &DownloadTask::finished, this, &DownloadManager::onTaskFinished);
    task->start();
    return true;
}

bool DownloadManager::pause(const QString &id, QString *error) {
    if (auto *task = m_tasks.value(id, nullptr)) {
        task->pause();
        return true;
    }
    Download item;
    if (!loadDownload(id, &item, error)) return false;
    if (item.status == DownloadStatus::Queued || item.status == DownloadStatus::Failed) {
        setInactiveStatus(item, DownloadStatus::Paused);
        return true;
    }
    setError(error, tr("La descarga no está activa."));
    return false;
}

bool DownloadManager::cancel(const QString &id, QString *error) {
    if (auto *task = m_tasks.value(id, nullptr)) {
        task->cancel();
        return true;
    }
    Download item;
    if (!loadDownload(id, &item, error)) return false;
    if (item.status == DownloadStatus::Completed || item.status == DownloadStatus::Cancelled) {
        setError(error, tr("La descarga ya terminó."));
        return false;
    }
    cleanupTemporaryArtifacts(item);
    item.downloadedBytes = 0;
    setInactiveStatus(item, DownloadStatus::Cancelled);
    return true;
}

bool DownloadManager::remove(const QString &id, QString *error) {
    if (isActive(id)) {
        setError(error, tr("Primero pausa o cancela la descarga."));
        return false;
    }
    Download item;
    if (!loadDownload(id, &item, error)) return false;
    if (!m_repository || !m_repository->remove(id, error)) return false;
    cleanupTemporaryArtifacts(item);
    return true;
}

bool DownloadManager::isActive(const QString &id) const {
    return m_tasks.contains(id);
}

void DownloadManager::startQueued() {
    if (m_shuttingDown || m_repository == nullptr) return;
    QString error;
    const auto downloads = m_repository->all(&error);
    if (!error.isEmpty()) {
        qWarning() << "OIDM: could not inspect queue:" << error;
        return;
    }
    // Queue is returned in newest-first order. Start oldest pending item first.
    for (auto it = downloads.crbegin(); it != downloads.crend() && m_tasks.size() < MaxConcurrentDownloads; ++it) {
        if (it->status == DownloadStatus::Queued && !m_tasks.contains(it->id)) {
            QString startError;
            if (!start(it->id, &startError)) qWarning() << "OIDM: cannot start queued item:" << startError;
        }
    }
}

void DownloadManager::pauseAll() {
    // snapshot avoids modifying the QHash as DownloadTask::finished fires.
    const auto tasks = m_tasks.values();
    for (auto *task : tasks) {
        if (task) task->pause();
    }
}

void DownloadManager::onTaskUpdated(const Download &download) {
    if (m_repository) {
        QString error;
        if (!m_repository->update(download, &error)) qWarning() << "OIDM: update failed:" << error;
    }
    emit downloadUpdated(download);
}

void DownloadManager::onTaskFinished(const Download &download) {
    if (auto *task = m_tasks.take(download.id)) task->deleteLater();
    if (m_repository) {
        QString error;
        if (!m_repository->update(download, &error)) qWarning() << "OIDM: final update failed:" << error;
    }
    emit downloadUpdated(download);
    emit downloadFinished(download);
    if (!m_shuttingDown) QTimer::singleShot(0, this, &DownloadManager::startQueued);
}

} // namespace oidm
