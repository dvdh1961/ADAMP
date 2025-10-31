#ifndef CARTRIDGEINFODIALOG_H
#define CARTRIDGEINFODIALOG_H

#include <QDialog>
#include <QScopedPointer> // Gebruik QScopedPointer voor PImpl

// Forward declarations van UI-componenten
class QTextEdit;
class QScrollArea;
class QWidget;
class QRadioButton;
class QButtonGroup;

#include "emu.h"

class CartridgeInfoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CartridgeInfoDialog(QWidget *parent = nullptr);
    ~CartridgeInfoDialog();

public slots:
    // Publieke slot om de data te verversen wanneer het venster wordt getoond
    void refreshData();

private slots:
    // Interne slot voor de radiobuttons
    void updateEmptyValueAndRefresh();

private:
    // Helperfuncties die het werk doen
    void setupUi();
    void showProfile();
    void showBanks();

    // Functie om één bank-widget te maken (voor de footprint)
    QWidget* createBankWidget(int bankIndex, const BYTE* data);

    // UI-elementen
    QTextEdit *m_memoEdit;
    QScrollArea *m_scrollArea;
    QWidget *m_bankContainer; // Widget *in* de scroll area
    QRadioButton *m_emptyFF;
    QRadioButton *m_empty00;
    QButtonGroup *m_emptyGroup;

    // Huidige waarde voor 'leeg' geheugen
    int m_valEmpty;
};

#endif // CARTRIDGEINFODIALOG_H
