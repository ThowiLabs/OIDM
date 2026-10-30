#include "core/download_task.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <limits>

namespace oidm {
namespace {

constexpr qint64 kMinimumParallelBytes = 4LL * 1024LL * 1024LL;
constexpr qint64 kTargetSegmentBytes = 1LL * 1024LL * 1024LL;
constexpr int kMaximumSegments = 8;
constexpr qint64 kProgressIntervalMs = 180;
constexpr qint64 kManifestIntervalMs = 1000;
constexpr qint64 kCopyBufferBytes = 256LL * 1024LL;

struct ParsedRange {
    qint64 start{-1};
    qint64 end{-1};
    qint64 total{-1};
};

bool parseContentRange(const QByteArray &header, ParsedRange *range)
{
    static const QRegularExpression pattern(
        QStringLiteral("^bytes\\s+(\\d+)-(\\d+)/(\\d+|\\*)$"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(QString::fromLatin1(header).trimmed());
    if (!match.hasMatch() || range == nullptr) return false;

    bool startOk = false;
    bool endOk = false;
    const qint64 start = match.captured(1).toLongLong(&startOk);
    const qint64 end = match.captured(2).toLongLong(&endOk);
    if (!startOk || !endOk || start < 0 || end < start) return false;

    qint64 total = -1;
    if (match.captured(3) != QStringLiteral("*")) {
        bool totalOk = false;
        total = match.captured(3).toLongLong(&totalOk);
        if (!totalOk || total <= end) return false;
    }

    range->start = start;
    range->end = end;
    range->total = total;
    return true;
}

QString strongEtag(const QString &etag)
{
    const QString value = etag.trimmed();
    return value.startsWith(QStringLiteral("W/"), Qt::CaseInsensitive) ? QString() : value;
}

QString validatorForIfRange(const QString &etag, const QString &lastModified)
{
    const QString strong = strongEtag(etag);
    return strong.isEmpty() ? lastModified.trimmed() : strong;
}

QString responseEtag(QNetworkReply *reply)
{
    return reply == nullptr ? QString() : QString::fromLatin1(reply->rawHeader(QByteArrayLiteral("ETag"))).trimmed();
}

QString responseLastModified(QNetworkReply *reply)
{
    return reply == nullptr ? QString() : QString::fromLatin1(reply->rawHeader(QByteArrayLiteral("Last-Modified"))).trimmed();
}

bool responseValidatorMatches(QNetworkReply *reply, const QString &etag, const QString &lastModified)
{
    const QString receivedEtag = responseEtag(reply);
    const QString receivedLastModified = responseLastModified(reply);
    if (!etag.isEmpty() && !receivedEtag.isEmpty() && receivedEtag != etag) return false;
    if (!lastModified.isEmpty() && !receivedLastModified.isEmpty()
        && receivedLastModified != lastModified) return false;
    return true;
}

QNetworkRequest makeRequest(const QString &url)
{
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OIDM/0.1.0"));
    request.setRawHeader(QByteArrayLiteral("Accept-Encoding"), QByteArrayLiteral("identity"));
    request.setTransferTimeout(30000);
    request.setMaximumRedirectsAllowed(10);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

bool isActiveReply(const QPointer<QNetworkReply> &reply)
{
    return !reply.isNull() && reply->isRunning();
}

} // namespace

DownloadTask::DownloadTask(Download download, QObject *parent)
    : QObject(parent), m_download(std::move(download))
{
}

QString DownloadTask::partialPath() const
{
    return m_download.destination + QStringLiteral(".oidm-part");
}

QString DownloadTask::segmentDirectory() const
{
    return m_download.destination + QStringLiteral(".oidm-parts");
}

QString DownloadTask::manifestPath() const
{
    return QDir(segmentDirectory()).filePath(QStringLiteral("manifest.json"));
}

void DownloadTask::start()
{
    if (m_terminal || m_network != nullptr) return;

    const QUrl url(m_download.url);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))) {
        fail(tr("Solo se admiten direcciones HTTP y HTTPS válidas en esta versión."));
        return;
    }
    if (!url.userName().isEmpty() || !url.password().isEmpty()) {
        fail(tr("No se admiten credenciales incrustadas en la dirección URL."));
        return;
    }

    const QFileInfo destinationInfo(m_download.destination);
    if (!QDir().mkpath(destinationInfo.absolutePath())) {
        fail(tr("No se pudo crear la carpeta de destino."));
        return;
    }
    if (QFileInfo::exists(m_download.destination) || QFileInfo(m_download.destination).isSymLink()) {
        fail(tr("El archivo de destino ya existe. Elige otro nombre para evitar sobrescribirlo."));
        return;
    }
    if (QFileInfo(segmentDirectory()).isSymLink() || QFileInfo(manifestPath()).isSymLink()
        || QFileInfo(partialPath()).isSymLink()) {
        fail(tr("OIDM no utilizará archivos temporales que sean enlaces simbólicos."));
        return;
    }

    loadExistingManifest();
    if (m_terminal) return;

    m_download.status = DownloadStatus::Inspecting;
    m_download.downloadedBytes = 0;
    m_download.totalBytes = -1;
    m_download.speedBytesPerSecond = 0.0;
    m_download.contentType.clear();
    m_download.error.clear();
    m_download.updatedAt = QDateTime::currentDateTimeUtc();
    emit updated(m_download);

    m_network = new QNetworkAccessManager(this);
    m_speedTimer.start();
    m_progressTimer.start();
    startRangeProbe();
}

void DownloadTask::loadExistingManifest()
{
    m_savedManifest = QJsonObject();
    const QFileInfo info(manifestPath());
    if (!info.exists()) return;
    if (info.isSymLink()) {
        fail(tr("El manifiesto temporal es un enlace simbólico y no se utilizará por seguridad."));
        return;
    }

    QFile file(manifestPath());
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        m_savedManifest = document.object();
    }
}

void DownloadTask::startRangeProbe()
{
    if (m_terminal || m_network == nullptr) return;
    m_probeValidated = false;
    m_fallbackRequested = false;
    m_probeData.clear();
    m_totalBytes = -1;
    m_etag.clear();
    m_lastModified.clear();
    m_contentType.clear();

    QNetworkRequest request = makeRequest(m_download.url);
    request.setRawHeader(QByteArrayLiteral("Range"), QByteArrayLiteral("bytes=0-0"));
    m_probeReply = m_network->get(request);
    connect(m_probeReply, &QNetworkReply::metaDataChanged,
            this, &DownloadTask::onProbeMetadataChanged);
    connect(m_probeReply, &QIODevice::readyRead,
            this, &DownloadTask::onProbeReadyRead);
    connect(m_probeReply, &QNetworkReply::finished,
            this, &DownloadTask::onProbeFinished);
}

void DownloadTask::onProbeMetadataChanged()
{
    if (m_terminal || m_probeReply.isNull() || m_probeValidated || m_fallbackRequested) return;
    if (m_pauseRequested || m_cancelRequested) {
        m_probeReply->abort();
        return;
    }

    QNetworkReply *reply = m_probeReply.data();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_etag = responseEtag(reply);
    m_lastModified = responseLastModified(reply);
    m_contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();

    if (statusCode == 200) {
        const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
        m_totalBytes = length.isValid() ? length.toLongLong() : -1;
        m_fallbackRequested = true;
        reply->abort();
        return;
    }
    if (statusCode != 206) {
        if (statusCode == 0) return;
        fail(tr("El servidor respondió HTTP %1 al comprobar los rangos.").arg(statusCode));
        reply->abort();
        return;
    }

    ParsedRange range;
    const QByteArray contentRange = reply->rawHeader(QByteArrayLiteral("Content-Range"));
    const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
    const bool validLength = !length.isValid() || length.toLongLong() == 1;
    const bool validRange = parseContentRange(contentRange, &range)
        && range.start == 0 && range.end == 0 && range.total > 0;
    const bool hasStableValidator = !strongEtag(m_etag).isEmpty() || !m_lastModified.isEmpty();
    if (!validRange || !validLength) {
        fail(tr("El servidor devolvió una respuesta de rango inválida durante la comprobación."));
        reply->abort();
        return;
    }
    if (!hasStableValidator) {
        m_fallbackRequested = true;
        reply->abort();
        return;
    }

    m_totalBytes = range.total;
    m_probeValidated = true;
}

void DownloadTask::onProbeReadyRead()
{
    if (m_terminal || m_probeReply.isNull() || !m_probeReply->isOpen() || m_fallbackRequested) return;
    m_probeData.append(m_probeReply->readAll());
    if (m_probeData.size() > 1) {
        m_fallbackRequested = true;
        m_probeReply->abort();
        return;
    }
    if (m_probeValidated && m_probeData.size() == 1) {
        m_probeReply->abort();
    }
}

void DownloadTask::onProbeFinished()
{
    if (m_terminal) return;
    QNetworkReply *reply = m_probeReply.data();
    const QNetworkReply::NetworkError probeError = reply != nullptr ? reply->error() : QNetworkReply::NoError;
    const QString probeErrorText = reply != nullptr ? reply->errorString() : QString();
    if (reply != nullptr) {
        if (reply->isOpen() && reply->bytesAvailable() > 0) m_probeData.append(reply->readAll());
        m_probeReply = nullptr;
        reply->deleteLater();
    }

    if (m_cancelRequested) {
        clearSegmentDirectory();
        QFile::remove(partialPath());
        finish(DownloadStatus::Cancelled);
        return;
    }
    if (m_pauseRequested) {
        finish(DownloadStatus::Paused);
        return;
    }
    if (m_fallbackRequested) {
        m_fallbackRequested = false;
        startSingleStream(false);
        return;
    }
    if (m_probeValidated && m_probeData.size() == 1) {
        m_download.totalBytes = m_totalBytes;
        m_download.contentType = m_contentType;
        prepareSegments();
        return;
    }

    if (probeError != QNetworkReply::NoError
        && probeError != QNetworkReply::OperationCanceledError) {
        fail(probeErrorText);
        return;
    }
    startSingleStream(false);
}

int DownloadTask::chooseSegmentCount(const qint64 totalBytes) const
{
    if (totalBytes < kMinimumParallelBytes) return 1;
    const qint64 count = (totalBytes + kTargetSegmentBytes - 1) / kTargetSegmentBytes;
    return static_cast<int>(std::clamp<qint64>(count, 2, kMaximumSegments));
}

bool DownloadTask::manifestMatchesProbe() const
{
    if (m_savedManifest.isEmpty()) return false;
    if (m_savedManifest.value(QStringLiteral("version")).toInt() != 1) return false;
    if (m_savedManifest.value(QStringLiteral("url")).toString() != m_download.url) return false;
    if (m_savedManifest.value(QStringLiteral("totalBytes")).toVariant().toLongLong() != m_totalBytes) return false;

    const QString savedEtag = m_savedManifest.value(QStringLiteral("etag")).toString();
    const QString savedLastModified = m_savedManifest.value(QStringLiteral("lastModified")).toString();
    if (!strongEtag(m_etag).isEmpty()) {
        if (savedEtag != m_etag) return false;
    } else if (!m_lastModified.isEmpty()) {
        if (savedLastModified != m_lastModified) return false;
    } else {
        return false;
    }
    if (!savedLastModified.isEmpty() && !m_lastModified.isEmpty()
        && savedLastModified != m_lastModified) return false;
    return true;
}

void DownloadTask::prepareSegments()
{
    if (m_terminal) return;
    if (m_totalBytes <= 0 || chooseSegmentCount(m_totalBytes) <= 1
        || (strongEtag(m_etag).isEmpty() && m_lastModified.isEmpty())) {
        startSingleStream(true);
        return;
    }

    const int segmentCount = chooseSegmentCount(m_totalBytes);
    bool reuseManifest = m_savedManifest.value(QStringLiteral("mode")).toString() == QStringLiteral("segments")
        && manifestMatchesProbe();
    QDir().mkpath(segmentDirectory());
    m_segments.clear();

    if (reuseManifest) {
        const QJsonArray savedSegments = m_savedManifest.value(QStringLiteral("segments")).toArray();
        if (savedSegments.size() != segmentCount) {
            reuseManifest = false;
        } else {
            qint64 expectedStart = 0;
            for (int index = 0; index < segmentCount; ++index) {
                const QJsonObject saved = savedSegments.at(index).toObject();
                const qint64 start = saved.value(QStringLiteral("start")).toVariant().toLongLong();
                const qint64 end = saved.value(QStringLiteral("end")).toVariant().toLongLong();
                const qint64 expectedLength = end - start + 1;
                const QString path = QDir(segmentDirectory()).filePath(
                    QStringLiteral("segment-%1.part").arg(index, 4, 10, QLatin1Char('0')));
                const QFileInfo partInfo(path);
                const qint64 actualSize = partInfo.exists() ? partInfo.size() : 0;
                if (saved.value(QStringLiteral("index")).toInt(-1) != index
                    || start != expectedStart || end < start || end >= m_totalBytes
                    || actualSize > expectedLength || partInfo.isSymLink()) {
                    reuseManifest = false;
                    break;
                }
                Segment segment;
                segment.index = index;
                segment.start = start;
                segment.end = end;
                segment.downloaded = actualSize;
                segment.path = path;
                segment.complete = actualSize == expectedLength;
                m_segments.push_back(std::move(segment));
                expectedStart = end + 1;
            }
            if (expectedStart != m_totalBytes) reuseManifest = false;
        }
    }

    if (!reuseManifest) {
        m_segments.clear();
        clearSegmentDirectory();
        if (!QDir().mkpath(segmentDirectory())) {
            fail(tr("No se pudo crear la carpeta temporal de segmentos."));
            return;
        }

        const qint64 baseLength = m_totalBytes / segmentCount;
        const qint64 remainder = m_totalBytes % segmentCount;
        qint64 nextStart = 0;
        for (int index = 0; index < segmentCount; ++index) {
            const qint64 length = baseLength + (index < remainder ? 1 : 0);
            Segment segment;
            segment.index = index;
            segment.start = nextStart;
            segment.end = nextStart + length - 1;
            segment.path = QDir(segmentDirectory()).filePath(
                QStringLiteral("segment-%1.part").arg(index, 4, 10, QLatin1Char('0')));
            m_segments.push_back(std::move(segment));
            nextStart += length;
        }
    }

    m_segmented = true;
    m_download.totalBytes = m_totalBytes;
    m_download.status = DownloadStatus::Downloading;
    m_download.error.clear();
    updateProgress(true);
    saveManifest();

    bool allComplete = !m_segments.empty();
    for (const Segment &segment : m_segments) allComplete = allComplete && segment.complete;
    if (allComplete) {
        finalizeSegments();
        return;
    }

    for (int index = 0; index < static_cast<int>(m_segments.size()); ++index) {
        if (m_terminal) return;
        if (!m_segments[static_cast<size_t>(index)].complete) startSegment(index);
    }
}

void DownloadTask::startSegment(const int index)
{
    if (m_terminal || m_pauseRequested || m_cancelRequested || m_switchingMode
        || m_network == nullptr || index < 0 || index >= static_cast<int>(m_segments.size())) return;

    Segment &segment = m_segments[static_cast<size_t>(index)];
    if (segment.complete || !segment.reply.isNull()) return;
    if (QFileInfo(segment.path).isSymLink()) {
        fail(tr("Un archivo temporal de segmento es un enlace simbólico."));
        return;
    }

    segment.requestStart = segment.start + segment.downloaded;
    if (segment.requestStart > segment.end) {
        fail(tr("El estado de un segmento contiene un desplazamiento inválido."));
        return;
    }

    segment.file = std::make_unique<QFile>(segment.path);
    const QIODevice::OpenMode mode = QIODevice::WriteOnly
        | (segment.downloaded > 0 ? QIODevice::Append : QIODevice::Truncate);
    if (!segment.file->open(mode)) {
        fail(tr("No se pudo abrir un archivo temporal de segmento: %1").arg(segment.file->errorString()));
        return;
    }

    QNetworkRequest request = makeRequest(m_download.url);
    const QByteArray range = QByteArrayLiteral("bytes=") + QByteArray::number(segment.requestStart)
        + QByteArrayLiteral("-") + QByteArray::number(segment.end);
    request.setRawHeader(QByteArrayLiteral("Range"), range);
    const QString validator = validatorForIfRange(m_etag, m_lastModified);
    if (!validator.isEmpty()) request.setRawHeader(QByteArrayLiteral("If-Range"), validator.toLatin1());

    segment.reply = m_network->get(request);
    QNetworkReply *reply = segment.reply.data();
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, index, reply] {
        if (!m_terminal && !m_switchingMode) (void)validateSegmentHeaders(index, reply);
    });
    connect(reply, &QIODevice::readyRead, this, [this, index, reply] {
        readSegmentData(index, reply);
    });
    connect(reply, &QNetworkReply::finished, this, [this, index, reply] {
        onSegmentFinished(index, reply);
    });
}

bool DownloadTask::validateSegmentHeaders(const int index, QNetworkReply *reply)
{
    if (m_terminal || m_switchingMode || reply == nullptr
        || index < 0 || index >= static_cast<int>(m_segments.size())) return false;

    Segment &segment = m_segments[static_cast<size_t>(index)];
    if (segment.headersValidated) return true;

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode == 200) {
        switchToSingleStream();
        return false;
    }
    if (statusCode != 206) {
        if (statusCode == 0 && reply->error() != QNetworkReply::NoError) return false;
        fail(tr("El servidor respondió HTTP %1 al solicitar el segmento %2.").arg(statusCode).arg(index + 1));
        return false;
    }

    ParsedRange range;
    if (!parseContentRange(reply->rawHeader(QByteArrayLiteral("Content-Range")), &range)
        || range.start != segment.requestStart || range.end != segment.end
        || range.total != m_totalBytes) {
        fail(tr("El servidor devolvió un Content-Range incompatible para el segmento %1.").arg(index + 1));
        return false;
    }

    const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
    const qint64 expectedLength = segment.end - segment.requestStart + 1;
    if (length.isValid() && length.toLongLong() != expectedLength) {
        fail(tr("La longitud anunciada del segmento %1 no coincide con su rango.").arg(index + 1));
        return false;
    }
    if (!responseValidatorMatches(reply, m_etag, m_lastModified)) {
        fail(tr("La identidad del recurso cambió durante la descarga segmentada."));
        return false;
    }

    segment.headersValidated = true;
    return true;
}

void DownloadTask::readSegmentData(const int index, QNetworkReply *reply)
{
    if (m_terminal || m_switchingMode || reply == nullptr
        || index < 0 || index >= static_cast<int>(m_segments.size())) return;
    if (!validateSegmentHeaders(index, reply)) return;

    Segment &segment = m_segments[static_cast<size_t>(index)];
    if (!segment.file || !segment.file->isOpen()) return;
    const QByteArray data = reply->readAll();
    if (data.isEmpty()) return;

    const qint64 written = segment.file->write(data);
    if (written != data.size()) {
        fail(tr("No se pudo escribir el segmento %1: %2").arg(index + 1).arg(segment.file->errorString()));
        return;
    }
    segment.downloaded += written;
    if (segment.downloaded > segment.end - segment.start + 1) {
        fail(tr("El segmento %1 recibió más bytes de los permitidos.").arg(index + 1));
        return;
    }
    updateProgress();
}

void DownloadTask::onSegmentFinished(const int index, QNetworkReply *reply)
{
    if (reply != nullptr) reply->readAll();
    if (index < 0 || index >= static_cast<int>(m_segments.size())) {
        if (reply != nullptr) reply->deleteLater();
        return;
    }

    Segment &segment = m_segments[static_cast<size_t>(index)];
    segment.reply = nullptr;
    if (m_terminal) {
        if (reply != nullptr) reply->deleteLater();
        return;
    }

    if (m_switchingMode) {
        if (reply != nullptr) reply->deleteLater();
        const bool pending = std::any_of(m_segments.begin(), m_segments.end(),
            [](const Segment &item) { return isActiveReply(item.reply); });
        if (!pending && m_fallbackRequested) {
            m_segments.clear();
            clearSegmentDirectory();
            m_segmented = false;
            m_switchingMode = false;
            m_fallbackRequested = false;
            startSingleStream(false);
        }
        return;
    }

    if (!m_pauseRequested && !m_cancelRequested && reply != nullptr) {
        if (!segment.headersValidated && !validateSegmentHeaders(index, reply)) {
            if (reply != nullptr) reply->deleteLater();
            return;
        }
        if (m_switchingMode || m_terminal) {
            if (reply != nullptr) reply->deleteLater();
            return;
        }
        readSegmentData(index, reply);
    }

    if (segment.file && segment.file->isOpen()) {
        if (!segment.file->flush() && !m_pauseRequested && !m_cancelRequested) {
            const QString error = segment.file->errorString();
            if (reply != nullptr) reply->deleteLater();
            fail(tr("No se pudo guardar el segmento %1: %2").arg(index + 1).arg(error));
            return;
        }
        segment.file->close();
    }
    if (reply != nullptr) reply->deleteLater();

    const bool pending = std::any_of(m_segments.begin(), m_segments.end(),
        [](const Segment &item) { return isActiveReply(item.reply); });

    if (m_cancelRequested) {
        if (!pending) {
            clearSegmentDirectory();
            QFile::remove(partialPath());
            m_download.downloadedBytes = 0;
            finish(DownloadStatus::Cancelled);
        }
        return;
    }
    if (m_pauseRequested) {
        if (reply != nullptr && segment.headersValidated && reply->isOpen()) {
            readSegmentData(index, reply);
            if (m_terminal) return;
        }
        (void)saveManifest();
        if (!pending) {
            updateProgress(true);
            finish(DownloadStatus::Paused);
        }
        return;
    }

    if (reply != nullptr && reply->error() != QNetworkReply::NoError) {
        fail(reply->errorString());
        return;
    }

    const qint64 expectedLength = segment.end - segment.start + 1;
    if (segment.downloaded != expectedLength) {
        fail(tr("El segmento %1 quedó incompleto (%2 de %3 bytes).")
                 .arg(index + 1).arg(segment.downloaded).arg(expectedLength));
        return;
    }

    segment.complete = true;
    updateProgress(true);
    saveManifest();

    const bool allComplete = std::all_of(m_segments.begin(), m_segments.end(),
        [](const Segment &item) { return item.complete; });
    if (allComplete) finalizeSegments();
}

void DownloadTask::switchToSingleStream()
{
    if (m_terminal || m_switchingMode) return;
    m_switchingMode = true;
    m_fallbackRequested = true;
    stopSegmentRequests();

    const bool pending = std::any_of(m_segments.begin(), m_segments.end(),
        [](const Segment &item) { return isActiveReply(item.reply); });
    if (!pending) {
        m_segments.clear();
        clearSegmentDirectory();
        m_segmented = false;
        m_switchingMode = false;
        m_fallbackRequested = false;
        startSingleStream(false);
    }
}

void DownloadTask::stopSegmentRequests()
{
    for (Segment &segment : m_segments) {
        if (segment.file && segment.file->isOpen()) {
            segment.file->flush();
            segment.file->close();
        }
        if (segment.reply && segment.reply->isRunning()) segment.reply->abort();
    }
}

void DownloadTask::startSingleStream(const bool allowResume)
{
    if (m_terminal || m_network == nullptr) return;
    m_segmented = false;
    m_singleHeadersValidated = false;
    m_singleResponseBytes = 0;
    m_resumeOffset = 0;

    const QFileInfo partialInfo(partialPath());
    if (partialInfo.isSymLink()) {
        fail(tr("El archivo temporal de descarga es un enlace simbólico."));
        return;
    }

    const bool canResume = allowResume
        && m_savedManifest.value(QStringLiteral("mode")).toString() == QStringLiteral("single")
        && manifestMatchesProbe() && partialInfo.exists() && m_totalBytes > 0
        && partialInfo.size() <= m_totalBytes;
    if (canResume) {
        m_resumeOffset = partialInfo.size();
    } else {
        if (partialInfo.exists() && !QFile::remove(partialPath())) {
            fail(tr("No se pudo reiniciar el archivo temporal de descarga."));
            return;
        }
        clearSegmentDirectory();
    }

    m_download.totalBytes = m_totalBytes;
    m_download.downloadedBytes = m_resumeOffset;
    m_download.contentType = m_contentType;
    if (m_resumeOffset > 0 && m_resumeOffset == m_totalBytes) {
        finalizeSingleStream();
        return;
    }

    QNetworkRequest request = makeRequest(m_download.url);
    if (m_resumeOffset > 0) {
        request.setRawHeader(QByteArrayLiteral("Range"),
                             QByteArrayLiteral("bytes=") + QByteArray::number(m_resumeOffset) + QByteArrayLiteral("-"));
        const QString validator = validatorForIfRange(m_etag, m_lastModified);
        if (!validator.isEmpty()) request.setRawHeader(QByteArrayLiteral("If-Range"), validator.toLatin1());
    }

    m_singleReply = m_network->get(request);
    connect(m_singleReply, &QNetworkReply::metaDataChanged,
            this, &DownloadTask::onSingleMetadataChanged);
    connect(m_singleReply, &QIODevice::readyRead,
            this, &DownloadTask::onSingleReadyRead);
    connect(m_singleReply, &QNetworkReply::finished,
            this, &DownloadTask::onSingleFinished);
}

void DownloadTask::onSingleMetadataChanged()
{
    if (m_terminal || m_singleReply.isNull() || m_singleHeadersValidated) return;
    QNetworkReply *reply = m_singleReply.data();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    bool append = false;

    if (statusCode == 206) {
        ParsedRange range;
        if (!parseContentRange(reply->rawHeader(QByteArrayLiteral("Content-Range")), &range)
            || range.start != m_resumeOffset || range.end < range.start || range.total <= range.end
            || range.end != range.total - 1) {
            fail(tr("El servidor devolvió un Content-Range incompatible al reanudar."));
            return;
        }
        if (m_totalBytes > 0 && range.total != m_totalBytes) {
            fail(tr("El tamaño del recurso cambió durante la reanudación."));
            return;
        }
        if (!responseValidatorMatches(reply, m_etag, m_lastModified)) {
            fail(tr("La identidad del recurso cambió durante la reanudación."));
            return;
        }
        const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
        if (length.isValid() && length.toLongLong() != range.end - range.start + 1) {
            fail(tr("Content-Length no coincide con Content-Range."));
            return;
        }
        m_totalBytes = range.total;
        append = m_resumeOffset > 0;
    } else if (statusCode == 200) {
        if (m_resumeOffset > 0) {
            m_resumeOffset = 0;
            m_singleResponseBytes = 0;
        }
        const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
        m_totalBytes = length.isValid() ? length.toLongLong() : -1;
        const QString receivedEtag = responseEtag(reply);
        const QString receivedLastModified = responseLastModified(reply);
        if (!receivedEtag.isEmpty()) m_etag = receivedEtag;
        if (!receivedLastModified.isEmpty()) m_lastModified = receivedLastModified;
    } else {
        if (statusCode == 0 && reply->error() != QNetworkReply::NoError) return;
        fail(tr("El servidor respondió HTTP %1 al iniciar la descarga.").arg(statusCode));
        return;
    }

    m_contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    m_singleFile = std::make_unique<QFile>(partialPath());
    const QIODevice::OpenMode mode = QIODevice::WriteOnly
        | (append ? QIODevice::Append : QIODevice::Truncate);
    if (!m_singleFile->open(mode)) {
        fail(tr("No se pudo abrir el archivo temporal: %1").arg(m_singleFile->errorString()));
        return;
    }

    m_singleHeadersValidated = true;
    m_download.status = DownloadStatus::Downloading;
    m_download.totalBytes = m_totalBytes;
    m_download.contentType = m_contentType;
    m_download.error.clear();
    updateProgress(true);
    saveManifest();
}

void DownloadTask::onSingleReadyRead()
{
    if (m_terminal || m_singleReply.isNull()) return;
    if (!m_singleHeadersValidated) onSingleMetadataChanged();
    if (m_terminal || !m_singleHeadersValidated || !m_singleFile || !m_singleFile->isOpen()) return;

    const QByteArray data = m_singleReply->readAll();
    if (data.isEmpty()) return;
    const qint64 written = m_singleFile->write(data);
    if (written != data.size()) {
        fail(tr("No se pudo escribir el archivo temporal: %1").arg(m_singleFile->errorString()));
        return;
    }
    m_singleResponseBytes += written;
    updateProgress();
}

void DownloadTask::onSingleFinished()
{
    if (m_terminal) return;
    QNetworkReply *reply = m_singleReply.data();
    if (reply != nullptr && !m_pauseRequested && !m_cancelRequested && !m_singleHeadersValidated) {
        onSingleMetadataChanged();
    }
    if (!m_terminal && reply != nullptr && !m_pauseRequested && !m_cancelRequested
        && m_singleHeadersValidated && m_singleFile && m_singleFile->isOpen()) {
        const QByteArray remaining = reply->readAll();
        if (!remaining.isEmpty()) {
            const qint64 written = m_singleFile->write(remaining);
            if (written != remaining.size()) {
                fail(tr("No se pudo escribir el último bloque: %1").arg(m_singleFile->errorString()));
            } else {
                m_singleResponseBytes += written;
            }
        }
    }

    m_singleReply = nullptr;
    if (reply != nullptr) reply->deleteLater();
    if (m_terminal) return;

    if (m_singleFile && m_singleFile->isOpen()) {
        if (!m_singleFile->flush() && !m_pauseRequested && !m_cancelRequested) {
            fail(tr("No se pudo vaciar el archivo temporal al disco: %1").arg(m_singleFile->errorString()));
            return;
        }
        m_singleFile->close();
    }

    if (m_cancelRequested) {
        QFile::remove(partialPath());
        clearSegmentDirectory();
        m_download.downloadedBytes = 0;
        finish(DownloadStatus::Cancelled);
        return;
    }
    if (m_pauseRequested) {
        updateProgress(true);
        saveManifest();
        finish(DownloadStatus::Paused);
        return;
    }
    if (reply != nullptr && reply->error() != QNetworkReply::NoError) {
        fail(reply->errorString());
        return;
    }

    updateProgress(true);
    if (m_totalBytes >= 0 && m_download.downloadedBytes != m_totalBytes) {
        fail(tr("Descarga incompleta: se recibieron %1 de %2 bytes.")
                 .arg(m_download.downloadedBytes).arg(m_totalBytes));
        return;
    }
    finalizeSingleStream();
}

void DownloadTask::finalizeSingleStream()
{
    if (m_terminal) return;
    if (QFileInfo::exists(m_download.destination) || QFileInfo(m_download.destination).isSymLink()) {
        fail(tr("El destino apareció durante la descarga; se conservó el archivo temporal."));
        return;
    }
    if (m_singleFile && m_singleFile->isOpen()) {
        if (!m_singleFile->flush()) {
            fail(tr("No se pudo finalizar la escritura del archivo."));
            return;
        }
        m_singleFile->close();
    }
    if (!QFile::rename(partialPath(), m_download.destination)) {
        fail(tr("No se pudo mover el archivo temporal al destino final."));
        return;
    }
    clearSegmentDirectory();
    finish(DownloadStatus::Completed);
}

void DownloadTask::finalizeSegments()
{
    if (m_terminal || !m_segmented) return;
    if (QFileInfo::exists(m_download.destination) || QFileInfo(m_download.destination).isSymLink()) {
        fail(tr("El destino apareció durante la descarga; se conservaron los segmentos temporales."));
        return;
    }

    QSaveFile output(m_download.destination);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        fail(tr("No se pudo crear el archivo final: %1").arg(output.errorString()));
        return;
    }

    qint64 assembledBytes = 0;
    QByteArray buffer;
    buffer.resize(static_cast<qsizetype>(kCopyBufferBytes));
    for (const Segment &segment : m_segments) {
        QFile input(segment.path);
        if (!input.open(QIODevice::ReadOnly)) {
            output.cancelWriting();
            fail(tr("No se pudo leer el segmento %1 para ensamblarlo.").arg(segment.index + 1));
            return;
        }
        const qint64 expected = segment.end - segment.start + 1;
        if (input.size() != expected) {
            output.cancelWriting();
            fail(tr("El segmento %1 no tiene el tamaño esperado.").arg(segment.index + 1));
            return;
        }
        while (!input.atEnd()) {
            const qint64 bytesRead = input.read(buffer.data(), buffer.size());
            if (bytesRead < 0) {
                output.cancelWriting();
                fail(tr("Error al leer el segmento %1.").arg(segment.index + 1));
                return;
            }
            if (bytesRead == 0) break;
            const qint64 bytesWritten = output.write(buffer.constData(), bytesRead);
            if (bytesWritten != bytesRead) {
                output.cancelWriting();
                fail(tr("Error al ensamblar el archivo final: %1").arg(output.errorString()));
                return;
            }
            assembledBytes += bytesWritten;
        }
    }

    if (assembledBytes != m_totalBytes) {
        output.cancelWriting();
        fail(tr("El archivo ensamblado tiene %1 de %2 bytes.").arg(assembledBytes).arg(m_totalBytes));
        return;
    }
    if (!output.commit()) {
        fail(tr("No se pudo publicar el archivo final: %1").arg(output.errorString()));
        return;
    }

    m_download.downloadedBytes = assembledBytes;
    clearSegmentDirectory();
    QFile::remove(partialPath());
    finish(DownloadStatus::Completed);
}

void DownloadTask::updateProgress(const bool force)
{
    qint64 downloaded = 0;
    if (m_segmented) {
        for (const Segment &segment : m_segments) downloaded += segment.downloaded;
    } else {
        downloaded = m_resumeOffset + m_singleResponseBytes;
    }
    m_download.downloadedBytes = downloaded;
    m_download.totalBytes = m_totalBytes;
    m_download.contentType = m_contentType;

    const qint64 elapsed = m_speedTimer.elapsed();
    if (elapsed >= 500) {
        const qint64 delta = qMax<qint64>(0, downloaded - m_speedSampleBytes);
        m_download.speedBytesPerSecond = static_cast<double>(delta) * 1000.0
            / static_cast<double>(elapsed);
        m_speedSampleBytes = downloaded;
        m_speedTimer.restart();
    }

    const qint64 progressElapsed = m_progressTimer.elapsed();
    if (force || progressElapsed >= kProgressIntervalMs) {
        m_download.updatedAt = QDateTime::currentDateTimeUtc();
        emit updated(m_download);
        if (progressElapsed >= kManifestIntervalMs && (m_segmented || !m_singleReply.isNull())) {
            saveManifest();
        }
        m_progressTimer.restart();
    }
}

void DownloadTask::saveManifest()
{
    if (m_download.destination.isEmpty() || !QDir().mkpath(segmentDirectory())) return;
    if (QFileInfo(segmentDirectory()).isSymLink() || QFileInfo(manifestPath()).isSymLink()) return;

    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), 1);
    manifest.insert(QStringLiteral("url"), m_download.url);
    manifest.insert(QStringLiteral("totalBytes"), m_totalBytes);
    manifest.insert(QStringLiteral("etag"), m_etag);
    manifest.insert(QStringLiteral("lastModified"), m_lastModified);
    manifest.insert(QStringLiteral("contentType"), m_contentType);
    manifest.insert(QStringLiteral("mode"), m_segmented ? QStringLiteral("segments") : QStringLiteral("single"));
    manifest.insert(QStringLiteral("downloadedBytes"), m_download.downloadedBytes);

    if (m_segmented) {
        QJsonArray segments;
        for (const Segment &segment : m_segments) {
            QJsonObject item;
            item.insert(QStringLiteral("index"), segment.index);
            item.insert(QStringLiteral("start"), segment.start);
            item.insert(QStringLiteral("end"), segment.end);
            item.insert(QStringLiteral("downloaded"), segment.downloaded);
            item.insert(QStringLiteral("complete"), segment.complete);
            segments.append(item);
        }
        manifest.insert(QStringLiteral("segments"), segments);
    }

    const QByteArray encoded = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    QSaveFile file(manifestPath());
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return;
    if (file.write(encoded) != encoded.size() || !file.commit()) return;
    m_savedManifest = manifest;
}

void DownloadTask::clearSegmentDirectory()
{
    const QFileInfo info(segmentDirectory());
    if (info.isSymLink()) {
        QFile::remove(segmentDirectory());
    } else if (info.exists()) {
        QDir directory(segmentDirectory());
        directory.removeRecursively();
    }
    m_savedManifest = QJsonObject();
}

void DownloadTask::pause()
{
    if (m_terminal) return;
    m_pauseRequested = true;
    if (m_probeReply && m_probeReply->isRunning()) m_probeReply->abort();
    if (m_singleReply && m_singleReply->isRunning()) m_singleReply->abort();
    stopSegmentRequests();

    const bool pending = isActiveReply(m_probeReply) || isActiveReply(m_singleReply)
        || std::any_of(m_segments.begin(), m_segments.end(),
            [](const Segment &segment) { return isActiveReply(segment.reply); });
    if (!pending) {
        if (m_singleFile && m_singleFile->isOpen()) {
            m_singleFile->flush();
            m_singleFile->close();
        }
        saveManifest();
        finish(DownloadStatus::Paused);
    }
}

void DownloadTask::cancel()
{
    if (m_terminal) return;
    m_cancelRequested = true;
    if (m_probeReply && m_probeReply->isRunning()) m_probeReply->abort();
    if (m_singleReply && m_singleReply->isRunning()) m_singleReply->abort();
    stopSegmentRequests();

    const bool pending = isActiveReply(m_probeReply) || isActiveReply(m_singleReply)
        || std::any_of(m_segments.begin(), m_segments.end(),
            [](const Segment &segment) { return isActiveReply(segment.reply); });
    if (!pending) {
        if (m_singleFile && m_singleFile->isOpen()) m_singleFile->close();
        clearSegmentDirectory();
        QFile::remove(partialPath());
        m_download.downloadedBytes = 0;
        finish(DownloadStatus::Cancelled);
    }
}

void DownloadTask::fail(const QString &error)
{
    if (m_terminal) return;
    if (m_probeReply && m_probeReply->isRunning()) m_probeReply->abort();
    if (m_singleReply && m_singleReply->isRunning()) m_singleReply->abort();
    stopSegmentRequests();
    if (m_singleFile && m_singleFile->isOpen()) {
        m_singleFile->flush();
        m_singleFile->close();
    }
    updateProgress(true);
    saveManifest();
    finish(DownloadStatus::Failed, error);
}

void DownloadTask::finish(const DownloadStatus status, const QString &error)
{
    if (m_terminal) return;
    m_terminal = true;
    m_download.status = status;
    m_download.error = error;
    m_download.speedBytesPerSecond = 0.0;
    m_download.updatedAt = QDateTime::currentDateTimeUtc();
    emit updated(m_download);
    emit finished(m_download);

    if (m_probeReply) m_probeReply->deleteLater();
    if (m_singleReply) m_singleReply->deleteLater();
    for (Segment &segment : m_segments) {
        if (segment.reply) segment.reply->deleteLater();
    }
}

} // namespace oidm
