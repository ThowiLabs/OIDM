#pragma once

#include "core/download.h"

#include <QAbstractTableModel>
#include <QVector>

namespace oidm {

class DownloadTableModel final : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name = 0, Queue, Size, Status, Progress, Remaining, Speed, Source, Date, ColumnCount };
    static constexpr int SortRole = Qt::UserRole + 1;

    explicit DownloadTableModel(QObject *parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void setDownloads(QVector<Download> downloads);
    void upsert(const Download &download);
    void remove(const QString &id);
    [[nodiscard]] const Download *at(int row) const;

private:
    QVector<Download> m_downloads;
};

} // namespace oidm
