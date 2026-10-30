#pragma once

#include <QStyledItemDelegate>

namespace oidm {

class ProgressDelegate final : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit ProgressDelegate(QObject *parent = nullptr);
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};

} // namespace oidm
