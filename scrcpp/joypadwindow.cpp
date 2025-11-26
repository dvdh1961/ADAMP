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

// Helper function to make key codes readable
static QString vkPretty(int vk) {
    if (vk == 0) return "none";
    // Check for our custom internal ID for joystick buttons (simple visualization)
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
    // Removed m_isWaitingForInput init because we use m_capturing now

    setWindowTitle("Keypad mapper");
    setModal(true);

    // Fixed size
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

    // --- Buttons ---
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

    connect(okButton, &QPushButton::clicked, this, &JoypadWindow::onAccept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    v->addLayout(buttonLayout);
}

void JoypadWindow::buildPlayerPage(PlayerUI& ui, const QString& title)
{
    ui.page = new QWidget(this);
    auto* vbox = new QVBoxLayout(ui.page);

    auto* topRow = new QHBoxLayout();
    topRow->addWidget(new QLabel("Controller type:", ui.page));
    ui.typeCombo = new QComboBox(ui.page);
    ui.typeCombo->addItems(QStringList{
        "Coleco standard controller",
        "Coleco steering wheel",
        "Coleco roller controller",
        "Coleco super action controller"
    });
    topRow->addWidget(ui.typeCombo, 1);
    vbox->addLayout(topRow);

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
        auto* cap  = new QPushButton("Capture", ui.page);
        auto* clr  = new QPushButton("Clear", ui.page);

        cap->setFocusPolicy(Qt::NoFocus);
        clr->setFocusPolicy(Qt::NoFocus);
        edit->setFocusPolicy(Qt::NoFocus); // Ook het tekstvak niet focussen

        int idx = ui.edits.size();
        edit->setProperty("idx", idx);
        cap->setProperty("idx", idx);
        clr->setProperty("idx", idx);

        ui.grid->addWidget(name, row, colBase + 0);
        ui.grid->addWidget(edit, row, colBase + 1);
        ui.grid->addWidget(cap,  row, colBase + 2);
        ui.grid->addWidget(clr,  row, colBase + 3);

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

    m_p1.typeCombo->setCurrentIndex(clampType(s.value("input/p1/type", 0).toInt()));
    for (int i = 0; i <= 17; ++i) {
        int vk = s.value(QString("input/p1/%1").arg(i), 0).toInt();
        m_p1.keys[i] = vk;
        m_p1.edits[i]->setText(vkPretty(vk));
        m_p1.captureBtns[i]->setProperty("player", 0);
        m_p1.clearBtns[i]->setProperty("player", 0);
    }

    m_p2.typeCombo->setCurrentIndex(clampType(s.value("input/p2/type", 0).toInt()));
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
        s.setValue(base+"/type",ui.typeCombo->currentIndex());
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

        // Safety check to ensure we are focused on the capture button
        // or just apply to the active capture state
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
    // Als we al bezig zijn, negeer de klik
    if (m_capturing) return;

    auto* b = qobject_cast<QPushButton*>(sender());
    if (!b) return;

    m_captureIndex = b->property("idx").toInt();
    m_capturePlayer = b->property("player").toInt();

    // 1. HAAL DE FOCUS WEG VAN DE KNOP
    // Dit voorkomt dat Qt de focus doorschuift naar de 'Clear' knop ernaast
    b->clearFocus();

    // 2. Update UI
    auto& P = (m_capturePlayer == 0) ? m_p1 : m_p2;
    if (m_captureIndex >= 0 && m_captureIndex < P.edits.size()) {
        P.edits[m_captureIndex]->setText("...");
    }

    // 3. Wacht even voordat we luisteren (tegen dubbelklikken/enter)
    m_capturing = false;

    QTimer::singleShot(250, this, [this, &P]() {
        m_capturing = true;

        if (m_captureIndex >= 0 && m_captureIndex < P.edits.size()) {
            P.edits[m_captureIndex]->setText("Press button...");
        }

        // Zet focus op het venster, zodat toetsaanslagen binnenkomen
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

// THIS IS THE FIXED FUNCTION
void JoypadWindow::onJoystickButtonDetected(int btnId)
{
    // Use the existing m_capturing flag logic
    if (!m_capturing) {
        return;
    }

    qDebug() << "Joystick Button detected:" << btnId;

    // Determine which player we are capturing for
    PlayerUI& P = (m_capturePlayer == 0) ? m_p1 : m_p2;

    if(m_captureIndex >= 0 && m_captureIndex < P.edits.size()){
        // To distinguish Joystick buttons from Keyboard keys (Qt::Key_),
        // we can add a magic number (offset) or negative numbers.
        // Here I add 0x10000 (65536) to indicate it's a Joystick Button ID.
        // You must handle this offset in your InputWidget/Controller logic when reading keys!
        int storedValue = 0x10000 + btnId;

        P.keys[m_captureIndex] = storedValue;
        P.edits[m_captureIndex]->setText(vkPretty(storedValue));
    }

    // Stop capturing
    m_capturing = false;
}


