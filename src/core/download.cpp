#include "core/download.h"

#include <QLocale>

namespace oidm {

QString statusText(const DownloadStatus status) {
    switch (status) {
    case DownloadStatus::Queued: return QStringLiteral("En cola");
    case DownloadStatus::AwaitingConfirmation: return QStringLiteral("Pendiente de confirmar");
    case DownloadStatus::Inspecting: return QStringLiteral("Comprobando");
    case DownloadStatus::Downloading: return QStringLiteral("Descargando");
    case DownloadStatus::Paused: return QStringLiteral("Pausada");
    case DownloadStatus::Completed: return QStringLiteral("Completada");
    case DownloadStatus::Failed: return QStringLiteral("Error");
    case DownloadStatus::Cancelled: return QStringLiteral("Cancelada");
    }
    return QStringLiteral("Desconocido");
}

QString statusKey(const DownloadStatus status) {
    switch (status) {
    case DownloadStatus::Queued: return QStringLiteral("queued");
    case DownloadStatus::AwaitingConfirmation: return QStringLiteral("awaiting_confirmation");
    case DownloadStatus::Inspecting: return QStringLiteral("inspecting");
    case DownloadStatus::Downloading: return QStringLiteral("downloading");
    case DownloadStatus::Paused: return QStringLiteral("paused");
    case DownloadStatus::Completed: return QStringLiteral("completed");
    case DownloadStatus::Failed: return QStringLiteral("failed");
    case DownloadStatus::Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}

DownloadStatus statusFromKey(const QString &key) {
    if (key == QStringLiteral("awaiting_confirmation")) return DownloadStatus::AwaitingConfirmation;
    if (key == QStringLiteral("inspecting")) return DownloadStatus::Inspecting;
    if (key == QStringLiteral("downloading")) return DownloadStatus::Downloading;
    if (key == QStringLiteral("paused")) return DownloadStatus::Paused;
    if (key == QStringLiteral("completed")) return DownloadStatus::Completed;
    if (key == QStringLiteral("failed")) return DownloadStatus::Failed;
    if (key == QStringLiteral("cancelled")) return DownloadStatus::Cancelled;
    return DownloadStatus::Queued;
}

QString formatBytes(const qint64 bytes) {
    if (bytes < 0) return QStringLiteral("—");
    static const QStringList units{
        QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"),
        QStringLiteral("GB"), QStringLiteral("TB")
    };
    double value = static_cast<double>(bytes);
    qsizetype unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) {
        value /= 1024.0;
        ++unit;
    }
    const int decimals = unit == 0 ? 0 : 2;
    return QLocale().toString(value, 'f', decimals) + QLatin1Char(' ') + units.at(unit);
}

QString formatSpeed(const double bytesPerSecond) {
    if (bytesPerSecond <= 0.0) return QStringLiteral("—");
    return formatBytes(static_cast<qint64>(bytesPerSecond)) + QStringLiteral("/s");
}

} // namespace oidm
