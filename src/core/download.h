#pragma once

#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace oidm {

enum class DownloadStatus {
    Queued,
    Inspecting,
    Downloading,
    Paused,
    Completed,
    Failed,
    Cancelled
};

struct Download {
    QString id;
    QString url;
    QString destination;
    QString fileName;
    DownloadStatus status{DownloadStatus::Queued};
    qint64 downloadedBytes{0};
    qint64 totalBytes{-1};
    double speedBytesPerSecond{0.0};
    QString contentType;
    QString error;
    QDateTime createdAt;
    QDateTime updatedAt;

    [[nodiscard]] double progressPercent() const noexcept {
        if (totalBytes <= 0) return 0.0;
        const auto percent = static_cast<double>(downloadedBytes) * 100.0
            / static_cast<double>(totalBytes);
        return qBound(0.0, percent, 100.0);
    }
};

[[nodiscard]] QString statusText(DownloadStatus status);
[[nodiscard]] QString statusKey(DownloadStatus status);
[[nodiscard]] DownloadStatus statusFromKey(const QString &key);
[[nodiscard]] QString formatBytes(qint64 bytes);
[[nodiscard]] QString formatSpeed(double bytesPerSecond);

} // namespace oidm

Q_DECLARE_METATYPE(oidm::Download)
