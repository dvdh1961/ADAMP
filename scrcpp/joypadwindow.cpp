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

// kleine util
static QString vkPretty(int vk) {
    if (vk == 0) return "none";
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

    // vaste afmetingen – niet resizable
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

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &JoypadWindow::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &JoypadWindow::reject);
    v->addWidget(bb);
}

void JoypadWindow::buildPlayerPage(PlayerUI& ui, const QString& title)
{
    ui.page = new QWidget(this);
    auto* vbox = new QVBoxLayout(ui.page);

    // Bovenste rij: controller type
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

    // --- Nieuw: horizontale split met links de PNG, rechts je mapping-grid ---
    auto* split = new QHBoxLayout();
    vbox->addLayout(split, 1);

    // Links: image
    auto* imgBox = new QVBoxLayout();
    auto* imgLbl = new QLabel(ui.page);
    imgLbl->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    QPixmap px(":/images/images/joypad.png");
    if (!px.isNull()) {
        imgLbl->setPixmap(px);  // géén scaling
        imgLbl->setFixedSize(px.size()); // houdt originele resolutie
    } else {
        imgLbl->setText("(joypad.png niet gevonden)");
    }

    imgBox->addWidget(imgLbl);
    imgBox->addStretch(1);
    split->addLayout(imgBox, 0); // smalle kolom links

    // Rechts: je bestaande grid met twee kolommen (links/ rechts)
    auto* rightBox = new QVBoxLayout();
    ui.grid = new QGridLayout();
    rightBox->addLayout(ui.grid);
    split->addLayout(rightBox, 1);

    // --- Mappings opbouwen ---
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

    // Linker set met extra lege rijen tussen RIGHT–TRIG R en TRIG L–BUT #
    int row = 0;
    for (int i = 0; i < leftLabels.size(); ++i) {
        makeRow(row, leftLabels[i], 0);
        if (leftLabels[i] == "RIGHT" || leftLabels[i] == "TRIG L") {
            ++row; // lege regel voor ademruimte
        }
        ++row;
    }

    // Rechterzijde compact: BUT 0..9
    for (int i = 0; i < rightLabels.size(); ++i) {
        makeRow(i, rightLabels[i], 5);
    }
}

void JoypadWindow::fillFromSettings()
{
    QSettings s;

    auto clampType = [](int v){ return (v < 0 || v > 3) ? 0 : v; };

    // P1
    m_p1.typeCombo->setCurrentIndex(
        clampType(s.value("input/p1/type", 0).toInt())
        );
    for (int i = 0; i <= 17; ++i) {
        int vk = s.value(QString("input/p1/%1").arg(i), 0).toInt();
        m_p1.keys[i] = vk;
        m_p1.edits[i]->setText(vkPretty(vk));
        m_p1.captureBtns[i]->setProperty("player", 0);
        m_p1.clearBtns[i]->setProperty("player", 0);
    }

    // P2
    m_p2.typeCombo->setCurrentIndex(
        clampType(s.value("input/p2/type", 0).toInt())
        );
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
    auto* b=qobject_cast<QPushButton*>(sender());
    if(!b)return;
    m_captureIndex=b->property("idx").toInt();
    m_capturePlayer=b->property("player").toInt();
    m_capturing=true;
    auto& P=(m_capturePlayer==0)?m_p1:m_p2;
    P.edits[m_captureIndex]->setText("…press key…");
    activateWindow(); setFocus();
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
