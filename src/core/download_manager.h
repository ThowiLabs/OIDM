#pragma once

#include "core/download.h"

#include <QHash>
#include <QObject>

namespace oidm {

class DownloadRepository;
class DownloadTask;

// Qt's nonblocking network I/O runs on the application's event loop. The
// transfer engine does not create or join a thread for each download.
class DownloadManager final : public QObject {
    Q_OBJECT
public:
    explicit DownloadManager(DownloadRepository *repository, QObject *parent = nullptr);
    ~DownloadManager() override;

    [[nodiscard]] bool start(const QString &id, QString *error = nullptr);
    [[nodiscard]] bool pause(const QString &id, QString *error = nullptr);
    [[nodiscard]] bool cancel(const QString &id, QString *error = nullptr);
    [[nodiscard]] bool remove(const QString &id, QString *error = nullptr);
    [[nodiscard]] bool isActive(const QString &id) const;
    void startQueued();
    void pauseAll();

signals:
    void downloadUpdated(const oidm::Download &download);
    void downloadFinished(const oidm::Download &download);

private slots:
    void onTaskUpdated(const oidm::Download &download);
    void onTaskFinished(const oidm::Download &download);

private:
    [[nodiscard]] bool loadDownload(const QString &id, Download *download, QString *error) const;
    void setInactiveStatus(Download &item, DownloadStatus status);

    DownloadRepository *m_repository{nullptr};
    QHash<QString, DownloadTask *> m_tasks;
    bool m_shuttingDown{false};
    static constexpr int MaxConcurrentDownloads = 3;
};

} // namespace oidm
