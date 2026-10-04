#pragma once

#include "config.h"
#include "discovery.h"

#include <QDialog>
#include <QList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(const Config &config, QWidget *parent = nullptr);

    Config config() const;

    // For tests and the window: start a network search as if the button was pressed.
    void search();
    Discovery &discovery() { return m_discovery; }

private:
    QWidget *buildRadio();
    QWidget *buildController();
    QWidget *buildAntennas();
    QWidget *buildRoutes();
    void fillCombo(QComboBox *combo, int selected) const;
    void refreshCombos();
    void validate();

    Config m_cfg;
    Discovery m_discovery;

    QLineEdit *m_rigHost = nullptr;
    QSpinBox *m_rigPort = nullptr;
    QSpinBox *m_poll = nullptr;
    QLineEdit *m_ctlHost = nullptr;
    QSpinBox *m_ctlPort = nullptr;
    QPushButton *m_searchButton = nullptr;
    QLabel *m_searchStatus = nullptr;
    QListWidget *m_found = nullptr;
    QList<QLineEdit *> m_names;
    QTableWidget *m_table = nullptr;
    QList<QComboBox *> m_comboA;
    QList<QComboBox *> m_comboB;
    QLabel *m_conflicts = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QCheckBox *m_menuEntry = nullptr;
};
