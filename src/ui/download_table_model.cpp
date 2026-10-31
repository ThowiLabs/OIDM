#include "ui/download_table_model.h"

#include <QLocale>
#include <QColor>

namespace oidm {

DownloadTableModel::DownloadTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int DownloadTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_downloads.size());
}

int DownloadTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DownloadTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_downloads.size()) return {};
    const Download &download = m_downloads.at(index.row());

    if (role == Qt::UserRole) return download.id;
    if (role == Qt::ToolTipRole) return download.url;
    if (role == Qt::TextAlignmentRole && index.column() != Name && index.column() != Status) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case Name: return download.fileName;
        case Size:
            return download.totalBytes >= 0
                ? QStringLiteral("%1 / %2").arg(formatBytes(download.downloadedBytes), formatBytes(download.totalBytes))
                : formatBytes(download.downloadedBytes);
        case Status: return statusText(download.status);
        case Progress: return download.progressPercent();
        case Speed: return formatSpeed(download.speedBytesPerSecond);
        case Date: return download.createdAt.toLocalTime().toString(QStringLiteral("dd/MM/yyyy hh:mm"));
        default: return {};
        }
    }
    if (role == Qt::ForegroundRole && download.status == DownloadStatus::Failed) {
        return QColor(QStringLiteral("#ff8383"));
    }
    return {};
}

QVariant DownloadTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case Name: return tr("Nombre");
    case Size: return tr("Tamaño");
    case Status: return tr("Estado");
    case Progress: return tr("Progreso");
    case Speed: return tr("Velocidad");
    case Date: return tr("Fecha");
    default: return {};
    }
}

void DownloadTableModel::setDownloads(QVector<Download> downloads)
{
    beginResetModel();
    m_downloads = std::move(downloads);
    endResetModel();
}

void DownloadTableModel::upsert(const Download &download)
{
    for (int row = 0; row < m_downloads.size(); ++row) {
        if (m_downloads.at(row).id == download.id) {
            m_downloads[row] = download;
            emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
            return;
        }
    }
    const int row = static_cast<int>(m_downloads.size());
    beginInsertRows({}, row, row);
    m_downloads.push_back(download);
    endInsertRows();
}

void DownloadTableModel::remove(const QString &id)
{
    for (int row = 0; row < m_downloads.size(); ++row) {
        if (m_downloads.at(row).id == id) {
            beginRemoveRows({}, row, row);
            m_downloads.removeAt(row);
            endRemoveRows();
            return;
        }
    }
}

const Download *DownloadTableModel::at(int row) const
{
    return row >= 0 && row < m_downloads.size() ? &m_downloads.at(row) : nullptr;
}

} // namespace oidm
