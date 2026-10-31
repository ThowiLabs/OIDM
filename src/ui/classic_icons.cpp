#include "ui/classic_icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

namespace oidm {

QIcon classicIcon(const QString &kind, const QColor &accent)
{
    QPixmap image(80, 80);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(QStringLiteral("#526882")), 1.8));
    p.setBrush(QColor(QStringLiteral("#26354a")));
    p.drawRoundedRect(QRectF(5, 5, 70, 70), 12, 12);
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    if (kind == QStringLiteral("add")) {
        p.drawRoundedRect(QRectF(19, 36, 42, 25), 4, 4);
        p.setBrush(Qt::white);
        p.drawRoundedRect(QRectF(36, 14, 8, 32), 3, 3);
        p.drawPolygon(QPolygonF{{26,37},{54,37},{40,51}});
    } else if (kind == QStringLiteral("start")) {
        p.drawPolygon(QPolygonF{{27,20},{58,40},{27,60}});
    } else if (kind == QStringLiteral("pause")) {
        p.drawRoundedRect(QRectF(24,22,10,37),3,3);
        p.drawRoundedRect(QRectF(45,22,10,37),3,3);
    } else if (kind == QStringLiteral("stop")) {
        p.drawRoundedRect(QRectF(24,24,32,32),5,5);
    } else if (kind == QStringLiteral("stop-all")) {
        p.drawRoundedRect(QRectF(17,25,21,30),4,4);
        p.drawRoundedRect(QRectF(43,25,21,30),4,4);
    } else if (kind == QStringLiteral("remove")) {
        p.drawRoundedRect(QRectF(24,28,32,32),4,4);
        p.drawRoundedRect(QRectF(20,19,40,6),2,2);
        p.setPen(QPen(QColor(QStringLiteral("#26354a")),2));
        p.drawLine(35,33,35,53);
        p.drawLine(45,33,45,53);
    } else if (kind == QStringLiteral("clear")) {
        p.drawRoundedRect(QRectF(19,27,42,30),4,4);
        p.setPen(QPen(QColor(QStringLiteral("#26354a")),5,Qt::SolidLine,Qt::RoundCap));
        p.drawLine(29,37,51,47);
        p.drawLine(51,37,29,47);
    } else if (kind == QStringLiteral("queue")) {
        p.drawRoundedRect(QRectF(23,20,36,8),3,3);
        p.drawRoundedRect(QRectF(23,35,36,8),3,3);
        p.drawRoundedRect(QRectF(23,50,36,8),3,3);
    } else if (kind == QStringLiteral("folder")) {
        QPainterPath path;
        path.moveTo(15,30); path.lineTo(27,30);path.lineTo(34,36);
        path.lineTo(64,36);path.lineTo(59,59);path.lineTo(15,59);path.closeSubpath();
        p.drawPath(path);
    } else if (kind == QStringLiteral("options")) {
        p.drawEllipse(QPointF(40,40),22,22);
        p.setBrush(QColor(QStringLiteral("#26354a")));
        p.drawEllipse(QPointF(40,40),10,10);
    }
    p.end();
    return QIcon(image);
}

} // namespace oidm
