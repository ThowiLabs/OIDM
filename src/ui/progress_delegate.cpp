#include "ui/progress_delegate.h"

#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionProgressBar>

namespace oidm {

ProgressDelegate::ProgressDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void ProgressDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    const QVariant value = index.data(Qt::DisplayRole);
    if (!value.isValid() || value.isNull()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionProgressBar progress;
    progress.rect = option.rect.adjusted(7, 9, -7, -9);
    progress.minimum = 0;
    progress.maximum = 100;
    progress.progress = qBound(0, qRound(value.toDouble()), 100);
    progress.text = QStringLiteral("%1%").arg(progress.progress);
    progress.textVisible = true;
    progress.textAlignment = Qt::AlignCenter;
    progress.state = QStyle::State_Enabled;
    QApplication::style()->drawControl(QStyle::CE_ProgressBar, &progress, painter);
}

} // namespace oidm
