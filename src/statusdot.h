#pragma once

#include <QColor>
#include <QWidget>

// A small filled circle used as a state indicator.
class StatusDot : public QWidget
{
    Q_OBJECT
public:
    enum class Tone { Off, Good, Busy, Bad };

    explicit StatusDot(QWidget *parent = nullptr);
    void setTone(Tone tone);
    Tone tone() const { return m_tone; }
    QSize sizeHint() const override { return {14, 14}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Tone m_tone = Tone::Off;
};
