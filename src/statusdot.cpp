#include "statusdot.h"

#include <QPainter>

StatusDot::StatusDot(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(sizeHint());
}

void StatusDot::setTone(Tone tone)
{
    if (tone == m_tone)
        return;
    m_tone = tone;
    update();
}

void StatusDot::paintEvent(QPaintEvent *)
{
    QColor c;
    switch (m_tone) {
    case Tone::Off:
        c = palette().color(QPalette::Mid);
        break;
    case Tone::Good:
        c = QColor(0x2e, 0xa0, 0x4f);
        break;
    case Tone::Busy:
        c = QColor(0xe0, 0x9a, 0x1b);
        break;
    case Tone::Bad:
        c = QColor(0xd6, 0x3b, 0x2f);
        break;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(c.darker(130));
    p.setBrush(c);
    p.drawEllipse(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
}
