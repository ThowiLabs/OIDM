#pragma once
#include <QDialog>

class QSpinBox;
class QLineEdit;
class QCheckBox;

namespace oidm {

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    [[nodiscard]] static int preferredConnections();
    [[nodiscard]] static QString defaultDownloadDirectory();

private:
    void apply();
    QSpinBox *m_connections{};
    QLineEdit *m_folder{};
};

} // namespace oidm
