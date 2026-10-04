#include "mainwindow.h"

#include "bands.h"
#include "desktopintegration.h"
#include "engine.h"
#include "logbus.h"
#include "settingsdialog.h"
#include "statusdot.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QLabel *heading(const QString &text)
{
    auto *l = new QLabel(text);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

QLabel *detail()
{
    auto *l = new QLabel;
    l->setForegroundRole(QPalette::PlaceholderText);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

StatusDot::Tone toneFor(EasyController::Link link)
{
    switch (link) {
    case EasyController::Link::Connected:
        return StatusDot::Tone::Good;
    case EasyController::Link::Connecting:
        return StatusDot::Tone::Busy;
    case EasyController::Link::Unreachable:
        return StatusDot::Tone::Bad;
    case EasyController::Link::NotConfigured:
        break;
    }
    return StatusDot::Tone::Off;
}

}

MainWindow::MainWindow(Engine &engine, Config &config, QSettings &settings, std::function<void()> save,
                       QWidget *parent)
    : QWidget(parent)
    , m_engine(engine)
    , m_cfg(config)
    , m_settings(settings)
    , m_save(std::move(save))
{
    setWindowTitle(QGuiApplication::applicationDisplayName());

    m_radioDot = new StatusDot;
    m_band = new QLabel;
    QFont bandFont = m_band->font();
    bandFont.setPointSizeF(bandFont.pointSizeF() * 1.6);
    bandFont.setBold(true);
    m_band->setFont(bandFont);
    m_tx = new QLabel(QStringLiteral("TX"));
    m_tx->setStyleSheet(QStringLiteral(
        "background:#d63b2f; color:white; border-radius:3px; padding:1px 6px; font-weight:bold;"));
    m_radioDetail = detail();

    m_ctlDot = new StatusDot;
    m_ctlLine = new QLabel;
    m_ctlLine->setTextFormat(Qt::RichText);
    m_radioHeading = heading(tr("Radio"));
    m_ctlHeading = heading(tr("Controller"));

    m_statusDot = new StatusDot;
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *radioLine = new QHBoxLayout;
    radioLine->setSpacing(12);
    radioLine->addWidget(m_band);
    radioLine->addWidget(m_tx);
    radioLine->addStretch();

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(2, 1);
    grid->addWidget(m_radioDot, 0, 0, Qt::AlignCenter);
    grid->addWidget(m_radioHeading, 0, 1);
    grid->addLayout(radioLine, 0, 2);
    grid->addWidget(m_radioDetail, 1, 2);
    grid->setRowMinimumHeight(2, 6);
    grid->addWidget(m_ctlDot, 3, 0, Qt::AlignCenter);
    grid->addWidget(m_ctlHeading, 3, 1);
    grid->addWidget(m_ctlLine, 3, 2);
    grid->setRowMinimumHeight(4, 6);
    grid->addWidget(m_statusDot, 5, 0, Qt::AlignTop | Qt::AlignHCenter);
    grid->addWidget(heading(tr("Switch")), 5, 1, Qt::AlignTop);
    grid->addWidget(m_status, 5, 2);

    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);

    m_follow = new QCheckBox(tr("Follow band"));
    m_follow->setChecked(m_cfg.follow);
    m_follow->setToolTip(tr("Switch the antennas whenever the radio changes band"));
    auto *applyButton = new QPushButton(tr("Apply now"));
    applyButton->setToolTip(tr("Put the antennas of the current band from the table on the controller"));
    auto *settingsButton = new QPushButton(tr("Settings…"));
    m_logToggle = new QToolButton;
    m_logToggle->setText(tr("Log"));
    m_logToggle->setCheckable(true);
    m_logToggle->setAutoRaise(true);
    m_logToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_logToggle->setArrowType(Qt::RightArrow);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_follow);
    buttons->addWidget(m_logToggle);
    buttons->addStretch();
    buttons->addWidget(applyButton);
    buttons->addWidget(settingsButton);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setMinimumHeight(160);
    m_log->hide();

    auto *root = new QVBoxLayout(this);
    root->addLayout(grid);
    root->addWidget(line);
    root->addLayout(buttons);
    root->addWidget(m_log, 1);
    root->addStretch(0);

    connect(m_follow, &QCheckBox::toggled, &m_engine, &Engine::setFollow);
    connect(applyButton, &QPushButton::clicked, &m_engine, &Engine::applyNow);
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(m_logToggle, &QToolButton::toggled, this, [this](bool on) {
        m_logToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        if (on) {
            m_heightWithoutLog = height();
            m_log->show();
            return;
        }
        m_log->hide();
        // The layout lowers the window's minimum height only when it runs again; shrink after that.
        QTimer::singleShot(0, this, [this] {
            layout()->activate();
            resize(width(), qMax(m_heightWithoutLog, minimumSizeHint().height()));
        });
    });
    connect(&LogBus::instance(), &LogBus::line, m_log, &QPlainTextEdit::appendPlainText);

    RigClient &rig = m_engine.rig();
    connect(&rig, &RigClient::onlineChanged, this, &MainWindow::refreshRadio);
    connect(&rig, &RigClient::frequencyChanged, this, &MainWindow::refreshRadio);
    connect(&rig, &RigClient::pttChanged, this, &MainWindow::refreshRadio);
    connect(&m_engine, &Engine::bandChanged, this, &MainWindow::refreshRadio);
    EasyController &ctl = m_engine.controller();
    connect(&ctl, &EasyController::linkChanged, this, &MainWindow::refreshStatus);
    connect(&ctl, &EasyController::stateChanged, this, &MainWindow::refreshController);
    connect(&ctl, &EasyController::pttChanged, this, &MainWindow::refreshController);
    connect(&m_engine, &Engine::statusChanged, this, &MainWindow::refreshStatus);
    connect(&m_engine, &Engine::comparisonChanged, this, &MainWindow::refreshStatus);
    connect(&m_engine.discovery(), &Discovery::progress, this, &MainWindow::refreshController);
    connect(&m_engine, &Engine::configChanged, this, &MainWindow::refreshController);

    refreshRadio();
    refreshController();
    refreshStatus();

    setMinimumWidth(480);
    restoreGeometry(m_settings.value(QStringLiteral("window/geometry")).toByteArray());
}

void MainWindow::openSettings()
{
    const QString hostBefore = m_cfg.controllerHost;
    SettingsDialog dialog(m_cfg, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    Config updated = dialog.config();
    if (updated.controllerHost == hostBefore) // keep an address the engine found meanwhile
        updated.controllerHost = m_cfg.controllerHost;
    m_cfg = updated;
    m_save();
    DesktopIntegration::apply(m_cfg.desktopEntry);
    m_engine.reconfigure();
    m_follow->setChecked(m_cfg.follow);
    refreshRadio();
    refreshController();
    refreshStatus();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    QWidget::closeEvent(event);
}

void MainWindow::refreshRadio()
{
    const RigClient &rig = m_engine.rig();
    const bool online = rig.isOnline();
    m_radioDot->setTone(online ? StatusDot::Tone::Good : StatusDot::Tone::Bad);
    m_radioHeading->setToolTip(QStringLiteral("rigctld %1:%2").arg(rig.host()).arg(rig.port()));
    if (online) {
        const QString band = Bands::forFrequency(rig.frequency());
        m_band->setText(band.isEmpty() ? tr("out of band") : band);
    } else {
        m_band->setText(QStringLiteral("—"));
        m_radioDetail->setText(rig.lastError().isEmpty() ? tr("Connecting to rigctld")
                                                         : tr("Not connected: %1").arg(rig.lastError()));
    }
    m_radioDetail->setVisible(!online);
    m_tx->setVisible(online && rig.ptt());
    fitHeight();
}

void MainWindow::refreshController()
{
    const EasyController &ctl = m_engine.controller();
    const bool searching = m_engine.isSearching();
    m_ctlDot->setTone(searching ? StatusDot::Tone::Busy : toneFor(ctl.link()));
    m_ctlHeading->setToolTip(ctl.host().isEmpty() ? tr("No address yet")
                                                  : QStringLiteral("%1:%2").arg(ctl.host()).arg(ctl.port()));

    QString line;
    switch (ctl.link()) {
    case EasyController::Link::Connected: {
        const ControllerState s = ctl.state();
        auto bank = [this, &ctl](const QString &name, int port, int bankNr) {
            QString text = QStringLiteral("%1: <b>%2</b>").arg(name, port ? m_cfg.shortLabel(port).toHtmlEscaped() : tr("none"));
            if (ctl.ptt(bankNr))
                text += QStringLiteral(" <span style='color:#d63b2f'><b>TX</b></span>");
            return text;
        };
        line = bank(QStringLiteral("TRX A"), s.a, 1) + QStringLiteral("&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;")
               + bank(QStringLiteral("TRX B"), s.b, 2);
        break;
    }
    case EasyController::Link::Connecting:
        line = tr("Connecting");
        break;
    case EasyController::Link::Unreachable:
        line = tr("Not reachable");
        break;
    case EasyController::Link::NotConfigured:
        line = tr("Not found yet: Search network in Settings, or change band");
        break;
    }
    if (searching)
        line = tr("Searching the local network");
    m_ctlLine->setText(line);
}

void MainWindow::refreshStatus()
{
    const Engine::Status status = m_engine.status();
    const QStringList differences = m_engine.differences();
    const bool connected = m_engine.controller().link() == EasyController::Link::Connected;
    StatusDot::Tone tone = StatusDot::Tone::Off;
    QString text = m_engine.statusText();

    if (status == Engine::Status::Busy) {
        tone = StatusDot::Tone::Busy;
    } else if (status == Engine::Status::Failed) {
        tone = StatusDot::Tone::Bad;
    } else if (!differences.isEmpty()) { // settled, but not what the table says for this band
        tone = StatusDot::Tone::Busy;
        text = differences.join(QLatin1Char('\n'));
    } else if (connected && status == Engine::Status::Ok) {
        tone = StatusDot::Tone::Good;
        text = tr("As in the table");
    } // Idle, or the last result while the controller is away: grey

    m_statusDot->setTone(tone);
    m_status->setText(text.isEmpty() ? tr("Waiting for a band change") : text);
    refreshController();
    fitHeight();
}

void MainWindow::fitHeight()
{
    // Wrapped labels need more height than a top-level window asks for by itself.
    QTimer::singleShot(0, this, [this] {
        layout()->activate();
        const int need = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width())
                                                       : layout()->totalMinimumSize().height();
        if (need > height() || (!m_log->isVisible() && need != height()))
            resize(width(), need);
    });
}
