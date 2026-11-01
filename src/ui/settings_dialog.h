#pragma once
#include <QDialog>

class QSpinBox;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QSpinBox;

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
    QSpinBox *m_concurrent{};
    QComboBox *m_theme{};
    QLineEdit *m_folder{};
};

} // namespace oidm
