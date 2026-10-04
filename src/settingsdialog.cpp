#include "settingsdialog.h"

#include "bands.h"
#include "desktopintegration.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const Config &config, QWidget *parent)
    : QDialog(parent)
    , m_cfg(config)
{
    setWindowTitle(tr("Settings"));

    auto *leftPane = new QWidget;
    leftPane->setFixedWidth(320);
    auto *left = new QVBoxLayout(leftPane);
    left->setContentsMargins(0, 0, 0, 0);
    left->addWidget(buildRadio());
    left->addWidget(buildController());
    left->addWidget(buildAntennas());
    left->addStretch();

    auto *columns = new QHBoxLayout;
    columns->addWidget(leftPane);
    columns->addWidget(buildRoutes(), 1);

    m_conflicts = new QLabel;
    m_conflicts->setStyleSheet(QStringLiteral("color:#d63b2f;"));
    m_conflicts->setWordWrap(true);

    m_menuEntry = new QCheckBox(tr("Show in the application menu, with the window icon"));
    m_menuEntry->setToolTip(tr("Adds a desktop entry and icons to your home folder; turning it off removes them"));
    m_menuEntry->setChecked(m_cfg.desktopEntry);
    m_menuEntry->setVisible(DesktopIntegration::supported());

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(columns);
    root->addWidget(m_conflicts);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_menuEntry);
    bottom->addStretch();
    bottom->addWidget(m_buttons);
    root->addLayout(bottom);

    connect(&m_discovery, &Discovery::progress, this, [this](int done, int total) {
        m_searchStatus->setText(tr("Searching… %1 of %2").arg(done).arg(total));
    });
    connect(&m_discovery, &Discovery::hostFound, this, [this](const QString &host) {
        m_found->addItem(host);
        m_found->setVisible(m_found->count() > 1); // one answer goes straight into Address
    });
    connect(&m_discovery, &Discovery::finished, this, [this](const QStringList &hosts) {
        m_searchButton->setEnabled(true);
        if (hosts.isEmpty()) {
            m_searchStatus->setText(tr("No Easy Controller found"));
        } else if (hosts.size() == 1) {
            m_ctlHost->setText(hosts.first());
            m_searchStatus->setText(tr("Found at %1").arg(hosts.first()));
        } else {
            m_searchStatus->setText(tr("%1 found, double-click one to use it").arg(hosts.size()));
        }
    });

    refreshCombos();
    validate();
}

QWidget *SettingsDialog::buildRadio()
{
    auto *box = new QGroupBox(tr("Radio (rigctld)"));
    auto *form = new QFormLayout(box);
    m_rigHost = new QLineEdit(m_cfg.rigHost);
    m_rigPort = new QSpinBox;
    m_rigPort->setRange(1, 65535);
    m_rigPort->setValue(m_cfg.rigPort);
    m_poll = new QSpinBox;
    m_poll->setRange(1, 60);
    m_poll->setSuffix(tr(" s"));
    m_poll->setValue(qMax(1, (m_cfg.pollMs + 500) / 1000));
    form->addRow(tr("Host"), m_rigHost);
    form->addRow(tr("Port"), m_rigPort);
    form->addRow(tr("Poll every"), m_poll);
    return box;
}

QWidget *SettingsDialog::buildController()
{
    auto *box = new QGroupBox(tr("Easy Controller 6-2"));
    auto *form = new QFormLayout(box);
    m_ctlHost = new QLineEdit(m_cfg.controllerHost);
    m_ctlHost->setPlaceholderText(tr("found by search"));
    m_ctlPort = new QSpinBox;
    m_ctlPort->setRange(1, 65535);
    m_ctlPort->setValue(m_cfg.controllerPort);
    m_searchButton = new QPushButton(tr("Search network"));
    m_searchButton->setToolTip(tr("Look for the controller on the local network and use the address it answers on"));
    m_searchStatus = new QLabel;
    m_searchStatus->setForegroundRole(QPalette::PlaceholderText);
    m_found = new QListWidget;
    m_found->setMaximumHeight(70);
    m_found->hide();

    auto *searchRow = new QHBoxLayout;
    searchRow->addWidget(m_searchButton);
    searchRow->addWidget(m_searchStatus, 1);

    form->addRow(tr("Address"), m_ctlHost);
    form->addRow(tr("Port"), m_ctlPort);
    form->addRow(searchRow);
    form->addRow(m_found);

    connect(m_searchButton, &QPushButton::clicked, this, &SettingsDialog::search);
    connect(m_found, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) { m_ctlHost->setText(item->text()); });
    return box;
}

QWidget *SettingsDialog::buildAntennas()
{
    auto *box = new QGroupBox(tr("Antenna names"));
    auto *grid = new QGridLayout(box);
    for (int i = 0; i < Config::PortCount; ++i) {
        auto *edit = new QLineEdit(m_cfg.antennaNames.value(i));
        edit->setPlaceholderText(tr("ANT%1").arg(i + 1));
        m_names << edit;
        const int row = i % 3;
        const int col = (i / 3) * 2;
        grid->addWidget(new QLabel(QString::number(i + 1)), row, col);
        grid->addWidget(edit, row, col + 1);
        connect(edit, &QLineEdit::textChanged, this, &SettingsDialog::refreshCombos);
    }
    return box;
}

QWidget *SettingsDialog::buildRoutes()
{
    auto *box = new QGroupBox(tr("Antennas per band"));
    auto *layout = new QVBoxLayout(box);
    const QStringList bands = Bands::names();
    m_table = new QTableWidget(bands.size(), 2);
    m_table->setHorizontalHeaderLabels({tr("Radio 1 (TRX A)"), tr("Radio 2 (TRX B)")});
    m_table->setVerticalHeaderLabels(bands);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setShowGrid(false);
    for (int row = 0; row < bands.size(); ++row) {
        const BandRoute r = m_cfg.route(bands[row]);
        auto *a = new QComboBox;
        auto *b = new QComboBox;
        fillCombo(a, r.a);
        fillCombo(b, r.b);
        m_table->setCellWidget(row, 0, a);
        m_table->setCellWidget(row, 1, b);
        m_comboA << a;
        m_comboB << b;
        connect(a, &QComboBox::currentIndexChanged, this, &SettingsDialog::validate);
        connect(b, &QComboBox::currentIndexChanged, this, &SettingsDialog::validate);
    }
    m_table->resizeRowsToContents();
    int height = m_table->horizontalHeader()->height() + 2 * m_table->frameWidth();
    for (int row = 0; row < bands.size(); ++row)
        height += m_table->rowHeight(row);
    m_table->setMinimumHeight(height);
    m_table->setMinimumWidth(360);

    auto *hint = new QLabel(tr("“unchanged” leaves that radio where it is; “off” deselects its antenna."));
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_table);
    layout->addWidget(hint);
    return box;
}

void SettingsDialog::fillCombo(QComboBox *combo, int selected) const
{
    combo->addItem(QString(), Config::Keep);
    combo->addItem(QString(), Config::Off);
    for (int p = 1; p <= Config::PortCount; ++p)
        combo->addItem(QString(), p);
    combo->setCurrentIndex(qMax(0, combo->findData(selected)));
}

void SettingsDialog::refreshCombos()
{
    Config names = m_cfg;
    for (int i = 0; i < m_names.size(); ++i)
        names.antennaNames[i] = m_names[i]->text();
    for (QComboBox *combo : m_comboA + m_comboB)
        for (int i = 0; i < combo->count(); ++i)
            combo->setItemText(i, names.portLabel(combo->itemData(i).toInt()));
}

Config SettingsDialog::config() const
{
    Config c = m_cfg;
    c.rigHost = m_rigHost->text().trimmed();
    c.rigPort = quint16(m_rigPort->value());
    c.pollMs = m_poll->value() * 1000;
    c.controllerHost = m_ctlHost->text().trimmed();
    c.controllerPort = quint16(m_ctlPort->value());
    c.desktopEntry = m_menuEntry->isChecked();
    for (int i = 0; i < m_names.size(); ++i)
        c.antennaNames[i] = m_names[i]->text().trimmed();
    const QStringList bands = Bands::names();
    for (int row = 0; row < bands.size(); ++row)
        c.routes[bands[row]] = BandRoute{m_comboA[row]->currentData().toInt(), m_comboB[row]->currentData().toInt()};
    return c;
}

void SettingsDialog::search()
{
    m_found->clear();
    m_found->hide();
    m_searchButton->setEnabled(false);
    m_searchStatus->setText(tr("Searching…"));
    m_discovery.start(quint16(m_ctlPort->value()));
}

void SettingsDialog::validate()
{
    const QStringList conflicts = config().conflicts();
    m_conflicts->setText(conflicts.join(QLatin1Char('\n')));
    m_conflicts->setVisible(!conflicts.isEmpty());
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(conflicts.isEmpty());
}
