#pragma once

#include "core/download.h"

#include <QDialog>
#include <QVector>

class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTabWidget;

namespace oidm {

// Modeless native transfer details, updated from the actual DownloadTask progress.
// Unlike IDM screenshots, we never draw fictitious segment activity.
class DownloadProgressDialog final : public QDialog {
    Q_OBJECT
public:
    explicit DownloadProgressDialog(const Download &download, QWidget *parent = nullptr);
    [[nodiscard]] QString downloadId() const { return m_id; }
    void updateDownload(const Download &download);

signals:
    void pauseRequested(const QString &id);
    void resumeRequested(const QString &id);
    void cancelRequested(const QString &id);

private:
    QString m_id;
    QLabel *m_urlLabel{};
    QLabel *m_statusLabel{};
    QLabel *m_sizeLabel{};
    QLabel *m_downloadedLabel{};
    QLabel *m_speedLabel{};
    QLabel *m_timeLabel{};
    QLabel *m_resumeLabel{};
    QLabel *m_connectionsLabel{};
    QLabel *m_completionSummary{};
    QTabWidget *m_tabs{};
    QProgressBar *m_progress{};
    QTableWidget *m_connections{};
    QPushButton *m_pauseButton{};
    QPushButton *m_resumeButton{};
    QPushButton *m_cancelButton{};
    bool m_compactCompleted{false};
};

} // namespace oidm
