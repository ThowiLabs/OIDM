#pragma once

#include "core/download.h"

#include <QDialog>

class QLineEdit;

namespace oidm {

class AddDownloadDialog final : public QDialog {
    Q_OBJECT
public:
    explicit AddDownloadDialog(QWidget *parent = nullptr);
    [[nodiscard]] Download download() const;

private slots:
    void browseDestination();
    void validateForm();

private:
    QLineEdit *m_url = nullptr;
    QLineEdit *m_destination = nullptr;
};

} // namespace oidm
