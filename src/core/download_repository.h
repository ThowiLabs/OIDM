#pragma once

#include "core/download.h"

#include <QSqlDatabase>
#include <QString>
#include <QVector>

namespace oidm {

class DownloadRepository final {
public:
    explicit DownloadRepository(const QString &databasePath);
    ~DownloadRepository();

    DownloadRepository(const DownloadRepository &) = delete;
    DownloadRepository &operator=(const DownloadRepository &) = delete;

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QVector<Download> all(QString *error = nullptr) const;
    [[nodiscard]] bool insert(const Download &download, QString *error = nullptr);
    [[nodiscard]] bool update(const Download &download, QString *error = nullptr);
    [[nodiscard]] bool remove(const QString &id, QString *error = nullptr);
    [[nodiscard]] bool recoverInterrupted(QString *error = nullptr);

private:
    QSqlDatabase m_database;
    QString m_connectionName;
    QString m_lastError;
};

} // namespace oidm
