#ifndef CARTRIDGEINFODIALOG_H
#define CARTRIDGEINFODIALOG_H

#include <QDialog>
#include <QScopedPointer>

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
    void refreshData();

private slots:
    void updateEmptyValueAndRefresh();

private:
    void setupUi();
    void showProfile();
    void showBanks();

    QWidget* createBankWidget(int bankIndex, const BYTE* data);

    QTextEdit *m_memoEdit;
    QScrollArea *m_scrollArea;
    QWidget *m_bankContainer;
    QRadioButton *m_emptyFF;
    QRadioButton *m_empty00;
    QButtonGroup *m_emptyGroup;

    int m_valEmpty;
};

#endif
