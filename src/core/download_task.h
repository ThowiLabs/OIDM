#pragma once

#include "core/download.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QFile>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>

#include <memory>
#include <vector>

namespace oidm {

class DownloadTask final : public QObject {
    Q_OBJECT
public:
    explicit DownloadTask(Download download, QObject *parent = nullptr);

public slots:
    void start();
    void pause();
    void cancel();

signals:
    void updated(const oidm::Download &download);
    void finished(const oidm::Download &download);

private:
    struct Segment {
        int index{0};
        qint64 start{0};
        qint64 end{-1};
        qint64 downloaded{0};
        qint64 requestStart{0};
        QString path;
        std::unique_ptr<QFile> file;
        QPointer<QNetworkReply> reply;
        bool headersValidated{false};
        bool complete{false};
    };

    void loadExistingManifest();
    void startRangeProbe();
    void onProbeMetadataChanged();
    void onProbeReadyRead();
    void onProbeFinished();
    void prepareSegments();
    void startSegment(int index);
    [[nodiscard]] bool validateSegmentHeaders(int index, QNetworkReply *reply);
    void readSegmentData(int index, QNetworkReply *reply);
    void onSegmentFinished(int index, QNetworkReply *reply);
    void switchToSingleStream();
    void stopSegmentRequests();
    void startSingleStream(bool allowResume);
    void onSingleMetadataChanged();
    void onSingleReadyRead();
    void onSingleFinished();
    void finalizeSegments();
    void finalizeSingleStream();
    void updateProgress(bool force = false);
    [[nodiscard]] bool saveManifest();
    void clearSegmentDirectory();
    void fail(const QString &error);
    void finish(DownloadStatus status, const QString &error = {});
    [[nodiscard]] QString partialPath() const;
    [[nodiscard]] QString segmentDirectory() const;
    [[nodiscard]] QString manifestPath() const;
    [[nodiscard]] bool manifestMatchesProbe() const;
    [[nodiscard]] int chooseSegmentCount(qint64 totalBytes) const;

    Download m_download;
    QNetworkAccessManager *m_network{nullptr};
    QPointer<QNetworkReply> m_probeReply;
    QPointer<QNetworkReply> m_singleReply;
    std::unique_ptr<QFile> m_singleFile;
    std::vector<Segment> m_segments;
    QJsonObject m_savedManifest;
    QByteArray m_probeData;
    QString m_etag;
    QString m_effectiveUrl;
    QString m_lastModified;
    QString m_contentType;
    qint64 m_totalBytes{-1};
    qint64 m_resumeOffset{0};
    qint64 m_singleResponseBytes{0};
    qint64 m_speedSampleBytes{0};
    QElapsedTimer m_speedTimer;
    QElapsedTimer m_progressTimer;
    bool m_probeValidated{false};
    bool m_fallbackRequested{false};
    bool m_segmented{false};
    bool m_switchingMode{false};
    bool m_singleHeadersValidated{false};
    bool m_pauseRequested{false};
    bool m_cancelRequested{false};
    bool m_terminal{false};
};

} // namespace oidm
