#pragma once

#include "core/download.h"
#include <QDialog>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;

namespace oidm {

// A non-blocking pre-download confirmation. Never starts a transfer by itself.
class DownloadOfferDialog final : public QDialog {
    Q_OBJECT
public:
    explicit DownloadOfferDialog(const Download &download, QWidget *parent = nullptr);
    [[nodiscard]] QString destination() const;
    [[nodiscard]] QString downloadId() const { return m_id; }

signals:
    void startRequested(const QString &destination);

private:
    void probeMetadata(const Download &download);
    QString m_id;
    QLabel *m_size{};
    QLabel *m_type{};
    QLineEdit *m_destination{};
    QNetworkAccessManager *m_network{};
};

} // namespace oidm
