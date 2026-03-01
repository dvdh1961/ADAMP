#include "joypadwindow.h"
#include <QTabWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QSettings>
#include <QComboBox>
#include <QIcon>
#include <QPixmap>
#include <QDebug>
#include <QTimer>

static QString vkPretty(int vk) {
    if (vk == 0) return "none";
    if (vk >= 0x10000) {
        return QString("Joy Btn %1").arg(vk - 0x10000);
    }

    if (vk >= Qt::Key_A && vk <= Qt::Key_Z) return QString(QChar(vk)).toUpper();
    switch (vk) {
    case Qt::Key_Up: return "Up";
    case Qt::Key_Down: return "Down";
    case Qt::Key_Left: return "Left";
    case Qt::Key_Right: return "Right";
    case Qt::Key_Space: return "Space";
    case Qt::Key_Return: return "Enter";
    case Qt::Key_Control: return "Ctrl";
    case Qt::Key_Shift: return "Shift";
    default: return QString("0x%1").arg(vk,0,16);
    }
}

JoypadWindow::JoypadWindow(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Keypad mapper");
    setModal(true);

    setFixedSize(680, 460);

    buildUi();
    fillFromSettings();
    installEventFilter(this);
}

void JoypadWindow::buildUi()
{
    auto* v = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    v->addWidget(m_tabs);

    buildPlayerPage(m_p1, "Player 1");
    buildPlayerPage(m_p2, "Player 2");

    m_tabs->addTab(m_p1.page, "Player 1");
    m_tabs->addTab(m_p2.page, "Player 2");

    QIcon okIcon(":/images/images/OK.png");
    QIcon cancelIcon(":/images/images/CANCEL.png");
    QPixmap okPixmap(":/images/images/OK.png");
    QPixmap cancelPixmap(":/images/images/CANCEL.png");

    if (okIcon.isNull()) { qWarning() << "JoypadWindow: Could not load OK.png"; }

    QString buttonStyle =
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }";

    QPushButton* okButton = new QPushButton(this);
    okButton->setIcon(okIcon);
    okButton->setIconSize(okPixmap.size());
    okButton->setFixedSize(okPixmap.size());
    okButton->setFlat(true);
    okButton->setStyleSheet(buttonStyle);

    QPushButton* cancelButton = new QPushButton(this);
    cancelButton->setIcon(cancelIcon);
    cancelButton->setIconSize(cancelPixmap.size());
    cancelButton->setFixedSize(cancelPixmap.size());
    cancelButton->setFlat(true);
    cancelButton->setStyleSheet(buttonStyle);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);

    okButton->setCursor(Qt::PointingHandCursor);
    cancelButton->setCursor(Qt::PointingHandCursor);

    connect(okButton, &QPushButton::clicked, this, &JoypadWindow::onAccept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    v->addLayout(buttonLayout);
}

void JoypadWindow::buildPlayerPage(PlayerUI& ui, const QString& title)
{
    ui.page = new QWidget(this);
    auto* vbox = new QVBoxLayout(ui.page);

    // Clear (X) icon for the mapping grid
    const QIcon clearIcon(":/images/images/CAN.png");
    const QPixmap clearPixmap(":/images/images/CAN.png");
    if (clearIcon.isNull()) {
        qWarning() << "JoypadWindow: Could not load CAN.png";
    }
    const QString iconButtonStyle =
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: "
        "2px; padding-left: 2px; }";
    const QSize clearIconSize = clearPixmap.isNull() ? QSize(16, 16) : clearPixmap.size();
    const QSize clearButtonSize = clearIconSize + QSize(8, 8); // some padding around the icon

    // auto* topRow = new QHBoxLayout();
    // topRow->addWidget(new QLabel("Controller type:", ui.page));
    // ui.typeCombo = new QComboBox(ui.page);
    // ui.typeCombo->addItems(QStringList{
    //     "Coleco standard controller",
    //     "Coleco steering wheel",
    //     "Coleco roller controller",
    //     "Coleco super action controller"
    // });
    // topRow->addWidget(ui.typeCombo, 1);
    // vbox->addLayout(topRow);

    auto* split = new QHBoxLayout();
    vbox->addLayout(split, 1);

    auto* imgBox = new QVBoxLayout();
    auto* imgLbl = new QLabel(ui.page);
    imgLbl->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    QPixmap px(":/images/images/joypad.png");
    if (!px.isNull()) {
        imgLbl->setPixmap(px);
        imgLbl->setFixedSize(px.size());
    } else {
        imgLbl->setText("(joypad.png missing)");
    }

    imgBox->addWidget(imgLbl);
    imgBox->addStretch(1);
    split->addLayout(imgBox, 0);

    auto* rightBox = new QVBoxLayout();
    ui.grid = new QGridLayout();
    ui.grid->setHorizontalSpacing(0);

    rightBox->addLayout(ui.grid);
    split->addLayout(rightBox, 1);

    QStringList leftLabels = {
        "UP","DOWN","LEFT","RIGHT",
        "TRIG R","TRIG L","BUT #","BUT *"
    };
    QStringList rightLabels;
    for (int i = 0; i <= 9; ++i)
        rightLabels << QString("BUT %1").arg(i);

    ui.edits.clear(); ui.captureBtns.clear(); ui.clearBtns.clear();

    auto makeRow = [&](int row, const QString& label, int colBase){
        auto* name = new QLabel(label + ":", ui.page);
        auto* edit = new QLineEdit(ui.page); edit->setReadOnly(true);
        auto* cap  = new QPushButton(ui.page);
        cap->setIcon(QIcon(":/images/images/OK1.png"));
        cap->setText("");
        cap->setToolTip("Capture");
        cap->setIconSize(clearIconSize);
        cap->setFixedSize(clearButtonSize);
        cap->setFlat(true);
        cap->setStyleSheet(iconButtonStyle);
        auto* clr  = new QPushButton(ui.page);

        // Replace "Clear" text button with an X icon from resources
        clr->setIcon(clearIcon);
        clr->setText("");
        clr->setToolTip("Clear");
        clr->setIconSize(clearIconSize);
        clr->setFixedSize(clearButtonSize);
        clr->setFlat(true);
        clr->setStyleSheet(iconButtonStyle);

        cap->setCursor(Qt::PointingHandCursor);
        clr->setCursor(Qt::PointingHandCursor);

        cap->setFocusPolicy(Qt::NoFocus);
        clr->setFocusPolicy(Qt::NoFocus);
        edit->setFocusPolicy(Qt::NoFocus);

        int idx = ui.edits.size();
        edit->setProperty("idx", idx);
        cap->setProperty("idx", idx);
        clr->setProperty("idx", idx);

        ui.grid->addWidget(name, row, colBase + 0);
        ui.grid->addWidget(edit, row, colBase + 1);
        ui.grid->addWidget(cap,  row, colBase + 2);
        ui.grid->addWidget(clr,  row, colBase + 3);

        cap->setStyleSheet(iconButtonStyle + " QPushButton { margin-top: -6px; }");
        clr->setStyleSheet(iconButtonStyle + " QPushButton { margin-top: -6px; }");

        ui.edits << edit; ui.captureBtns << cap; ui.clearBtns << clr;
        connect(cap, &QPushButton::clicked, this, &JoypadWindow::onCaptureClicked);
        connect(clr, &QPushButton::clicked, this, &JoypadWindow::onClearClicked);
    };

    int row = 0;
    for (int i = 0; i < leftLabels.size(); ++i) {
        makeRow(row, leftLabels[i], 0);
        if (leftLabels[i] == "RIGHT" || leftLabels[i] == "TRIG L") {
            ++row;
        }
        ++row;
    }
    for (int i = 0; i < rightLabels.size(); ++i) {
        makeRow(i, rightLabels[i], 5);
    }
}

void JoypadWindow::fillFromSettings()
{
    QSettings s;
    auto clampType = [](int v){ return (v < 0 || v > 3) ? 0 : v; };

    //m_p1.typeCombo->setCurrentIndex(clampType(s.value("input/p1/type", 0).toInt()));
    for (int i = 0; i <= 17; ++i) {
        int vk = s.value(QString("input/p1/%1").arg(i), 0).toInt();
        m_p1.keys[i] = vk;
        m_p1.edits[i]->setText(vkPretty(vk));
        m_p1.captureBtns[i]->setProperty("player", 0);
        m_p1.clearBtns[i]->setProperty("player", 0);
    }

    //m_p2.typeCombo->setCurrentIndex(clampType(s.value("input/p2/type", 0).toInt()));
    for (int i = 0; i <= 17; ++i) {
        int vk = s.value(QString("input/p2/%1").arg(i), 0).toInt();
        m_p2.keys[i] = vk;
        m_p2.edits[i]->setText(vkPretty(vk));
        m_p2.captureBtns[i]->setProperty("player", 1);
        m_p2.clearBtns[i]->setProperty("player", 1);
    }
}

void JoypadWindow::pushToSettings()
{
    QSettings s;
    auto save=[&](PlayerUI& ui,const QString& base){
        //s.setValue(base+"/type",ui.typeCombo->currentIndex());
        s.setValue(base+"/type", 0);

        for (int i=0;i<ui.edits.size();++i)
            s.setValue(base+QString("/%1").arg(i),ui.keys[i]);
    };
    save(m_p1,"input/p1");
    save(m_p2,"input/p2");
}

bool JoypadWindow::eventFilter(QObject*, QEvent* ev)
{
    if(!m_capturing) return false;
    if(ev->type()==QEvent::KeyPress){
        auto* ke=static_cast<QKeyEvent*>(ev);
        int vk=ke->key();

        PlayerUI& P=(m_capturePlayer==0)?m_p1:m_p2;
        if(m_captureIndex>=0 && m_captureIndex<P.edits.size()){
            P.keys[m_captureIndex]=vk;
            P.edits[m_captureIndex]->setText(vkPretty(vk));
        }
        m_capturing=false;
        return true;
    }
    return false;
}

void JoypadWindow::onCaptureClicked()
{
    if (m_capturing) return;

    auto* b = qobject_cast<QPushButton*>(sender());
    if (!b) return;

    m_captureIndex = b->property("idx").toInt();
    m_capturePlayer = b->property("player").toInt();

    b->clearFocus();

    auto& P = (m_capturePlayer == 0) ? m_p1 : m_p2;
    if (m_captureIndex >= 0 && m_captureIndex < P.edits.size()) {
        P.edits[m_captureIndex]->setText("...");
    }

    m_capturing = false;

    QTimer::singleShot(250, this, [this, &P]() {
        m_capturing = true;

        if (m_captureIndex >= 0 && m_captureIndex < P.edits.size()) {
            P.edits[m_captureIndex]->setText("Press button...");
        }

        this->activateWindow();
        this->setFocus();
    });
}

void JoypadWindow::onClearClicked()
{
    auto* b=qobject_cast<QPushButton*>(sender());
    if(!b)return;
    int idx=b->property("idx").toInt();
    int pl=b->property("player").toInt();
    auto& P=(pl==0)?m_p1:m_p2;
    P.keys[idx]=0;
    P.edits[idx]->setText("none");
}

void JoypadWindow::onAccept()
{
    pushToSettings();
    accept();
}

void JoypadWindow::loadMappingsFromSettings(int (&p1)[20], int (&p2)[20])
{
    QSettings s;
    for(int i=0;i<20;++i){
        p1[i]=s.value(QString("input/p1/%1").arg(i),0).toInt();
        p2[i]=s.value(QString("input/p2/%1").arg(i),0).toInt();
    }
}
void JoypadWindow::saveMappingsToSettings(const int (&p1)[20], const int (&p2)[20])
{
    QSettings s;
    for(int i=0;i<20;++i){
        s.setValue(QString("input/p1/%1").arg(i),p1[i]);
        s.setValue(QString("input/p2/%1").arg(i),p2[i]);
    }
}

void JoypadWindow::onJoystickButtonDetected(int btnId)
{
    if (!m_capturing) {
        return;
    }

    qDebug() << "Joystick Button detected:" << btnId;

    PlayerUI& P = (m_capturePlayer == 0) ? m_p1 : m_p2;

    if(m_captureIndex >= 0 && m_captureIndex < P.edits.size()){
        int storedValue = 0x10000 + btnId;

        P.keys[m_captureIndex] = storedValue;
        P.edits[m_captureIndex]->setText(vkPretty(storedValue));
    }

    m_capturing = false;
}


