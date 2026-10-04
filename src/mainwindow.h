#pragma once

#include <QWidget>
#include <functional>

class Config;
class Engine;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QSettings;
class QToolButton;
class StatusDot;

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    MainWindow(Engine &engine, Config &config, QSettings &settings, std::function<void()> save,
               QWidget *parent = nullptr);

    void openSettings();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void refreshRadio();
    void refreshController();
    void refreshStatus();
    void fitHeight();

    Engine &m_engine;
    Config &m_cfg;
    QSettings &m_settings;
    std::function<void()> m_save;

    StatusDot *m_radioDot;
    QLabel *m_band;
    QLabel *m_tx;
    QLabel *m_radioHeading;
    QLabel *m_radioDetail; // why the radio is not connected; hidden otherwise
    StatusDot *m_ctlDot;
    QLabel *m_ctlHeading;
    QLabel *m_ctlLine; // the antenna of each radio, or why there is none
    StatusDot *m_statusDot;
    QLabel *m_status;
    QCheckBox *m_follow;
    QToolButton *m_logToggle;
    QPlainTextEdit *m_log;
    int m_heightWithoutLog = 0;
};
