#include "core/download_repository.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace oidm {
namespace {

void setError(QString *target, const QString &message) {
    if (target) *target = message;
}

Download fromQuery(const QSqlQuery &query) {
    Download item;
    item.id = query.value(0).toString();
    item.url = query.value(1).toString();
    item.destination = query.value(2).toString();
    item.fileName = query.value(3).toString();
    item.status = statusFromKey(query.value(4).toString());
    item.downloadedBytes = query.value(5).toLongLong();
    item.totalBytes = query.value(6).isNull() ? -1 : query.value(6).toLongLong();
    item.contentType = query.value(7).toString();
    item.error = query.value(8).toString();
    item.createdAt = query.value(9).toDateTime();
    item.updatedAt = query.value(10).toDateTime();
    item.referrer = query.value(11).toString();
    return item;
}

} // namespace

DownloadRepository::DownloadRepository(const QString &databasePath)
    : m_connectionName(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    const QFileInfo info(databasePath);
    if (!QDir().mkpath(info.absolutePath())) {
        m_lastError = QStringLiteral("No se pudo crear la carpeta de datos de OIDM.");
        return;
    }

    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(databasePath);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!m_database.open()) {
        m_lastError = m_database.lastError().text();
        return;
    }

    QSqlQuery query(m_database);
    const QStringList statements{
        QStringLiteral("PRAGMA journal_mode=WAL"),
        QStringLiteral("PRAGMA foreign_keys=ON"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS downloads ("
            "id TEXT PRIMARY KEY NOT NULL, url TEXT NOT NULL, destination TEXT NOT NULL, "
            "file_name TEXT NOT NULL, status TEXT NOT NULL, downloaded_bytes INTEGER NOT NULL DEFAULT 0, "
            "total_bytes INTEGER, content_type TEXT, error TEXT, created_at TEXT NOT NULL, updated_at TEXT NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_downloads_created ON downloads(created_at DESC)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_downloads_status ON downloads(status)")
    };
    for (const QString &statement : statements) {
        if (!query.exec(statement)) {
            m_lastError = query.lastError().text();
            m_database.close();
            return;
        }
    }
    // Optional referrer was introduced in 0.1.1. Existing download histories
    // must remain valid on upgrades (SQLite does not support ADD COLUMN IF NOT EXISTS).
    QSqlQuery columns(m_database);
    if (!columns.exec(QStringLiteral("PRAGMA table_info(downloads)"))) {
        m_lastError = columns.lastError().text();
        m_database.close();
        return;
    }
    bool hasReferrer = false;
    while (columns.next()) {
        if (columns.value(1).toString() == QStringLiteral("referrer")) {
            hasReferrer = true;
            break;
        }
    }
    if (!hasReferrer && !query.exec(QStringLiteral("ALTER TABLE downloads ADD COLUMN referrer TEXT NOT NULL DEFAULT ''"))) {
        m_lastError = query.lastError().text();
        m_database.close();
        return;
    }
}

DownloadRepository::~DownloadRepository() {
    if (m_database.isValid()) {
        m_database.close();
        m_database = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool DownloadRepository::isOpen() const { return m_database.isOpen(); }
QString DownloadRepository::lastError() const { return m_lastError; }

QVector<Download> DownloadRepository::all(QString *error) const {
    QVector<Download> items;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
        "SELECT id,url,destination,file_name,status,downloaded_bytes,total_bytes,content_type,error,created_at,updated_at,referrer "
        "FROM downloads ORDER BY created_at DESC"))) {
        setError(error, query.lastError().text());
        return items;
    }
    while (query.next()) items.push_back(fromQuery(query));
    return items;
}

bool DownloadRepository::insert(const Download &item, QString *error) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO downloads(id,url,destination,file_name,status,downloaded_bytes,total_bytes,content_type,error,created_at,updated_at,referrer) "
        "VALUES(:id,:url,:destination,:file_name,:status,:downloaded,:total,:type,:error,:created,:updated,:referrer)"));
    query.bindValue(QStringLiteral(":id"), item.id);
    query.bindValue(QStringLiteral(":url"), item.url);
    query.bindValue(QStringLiteral(":referrer"), item.referrer.isEmpty() ? QStringLiteral("") : item.referrer);
    query.bindValue(QStringLiteral(":destination"), item.destination);
    query.bindValue(QStringLiteral(":file_name"), item.fileName);
    query.bindValue(QStringLiteral(":status"), statusKey(item.status));
    query.bindValue(QStringLiteral(":downloaded"), item.downloadedBytes);
    if (item.totalBytes >= 0) query.bindValue(QStringLiteral(":total"), item.totalBytes);
    else query.bindValue(QStringLiteral(":total"), QVariant(QMetaType::fromType<qlonglong>()));
    query.bindValue(QStringLiteral(":type"), item.contentType);
    query.bindValue(QStringLiteral(":error"), item.error);
    query.bindValue(QStringLiteral(":created"), item.createdAt.toUTC().toString(Qt::ISODateWithMs));
    query.bindValue(QStringLiteral(":updated"), item.updatedAt.toUTC().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool DownloadRepository::update(const Download &item, QString *error) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE downloads SET status=:status,downloaded_bytes=:downloaded,total_bytes=:total,content_type=:type,error=:error,updated_at=:updated WHERE id=:id"));
    query.bindValue(QStringLiteral(":status"), statusKey(item.status));
    query.bindValue(QStringLiteral(":downloaded"), item.downloadedBytes);
    if (item.totalBytes >= 0) query.bindValue(QStringLiteral(":total"), item.totalBytes);
    else query.bindValue(QStringLiteral(":total"), QVariant(QMetaType::fromType<qlonglong>()));
    query.bindValue(QStringLiteral(":type"), item.contentType);
    query.bindValue(QStringLiteral(":error"), item.error);
    query.bindValue(QStringLiteral(":updated"), item.updatedAt.toUTC().toString(Qt::ISODateWithMs));
    query.bindValue(QStringLiteral(":id"), item.id);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool DownloadRepository::remove(const QString &id, QString *error) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM downloads WHERE id=:id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool DownloadRepository::recoverInterrupted(QString *error) {
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
        "UPDATE downloads SET status='paused',error='La aplicación se cerró durante la descarga.',updated_at=datetime('now') "
        "WHERE status IN ('downloading','inspecting')"))) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

} // namespace oidm
