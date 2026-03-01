#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include "logwindow.h"
#include <QFontDatabase>

extern "C" void z80_set_logger(void (*cb)(const char*));

static void z80ToQtLogger(const char* msg)
{
    LogWidget::log(QString::fromUtf8(msg));
}


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);


    // Laad het font uit de resources
    int fontId = QFontDatabase::addApplicationFont(":/fonts/fonts/luculent.ttf");

    if (fontId != -1) {
        // 2. Haal de exacte 'family name' op uit de database
        QString family = QFontDatabase::applicationFontFamilies(fontId).at(0);

        // 3. Maak een QFont object en stel dit in als de standaard voor de HELE app
        QFont appFont(family,10);
        a.setFont(appFont);

        qDebug() << "Succesvol geladen familie:" << family;
    } else {
        qDebug() << "Font kon niet worden geladen uit resources!";
    }

    // 1) Logger UI eerst
    auto* log = new LogWidget();
    LogWidget::bindTo(log);                 // <-- zet s_instance :contentReference[oaicite:0]{index=0}
    LogWidget::installQtHandler();          // optional: Qt messages ook in log

    // 2) Z80 callback meteen zetten (voordat er ooit z80_printf draait)
    z80_set_logger(z80ToQtLogger);          // <-- anders valt hij terug naar stdout :contentReference[oaicite:1]{index=1}


    // 3) Dan pas je main window / emu core


    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(30, 30, 30));
    darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Highlight, QColor(100, 100, 255));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    a.setPalette(darkPalette);

    a.setStyleSheet("QToolTip { color: #ffffff; background-color: #2a2a2a; border: 1px solid #767676; }");

    qApp->setStyleSheet(R"(
        /* Menubalk */
        QMenuBar {
            background-color: #2b2b2b;
            color: #eaeaea;
        }
        QMenuBar::item {
            background: transparent;
            padding: 6px 10px;
        }
        QMenuBar::item:selected {
            background: #3a3a3a;
            color: #ffffff;
        }
        QMenuBar::item:pressed,
        QMenuBar::item:open {
            background: #4c5a66;
            color: #ffffff;
        }

        /* Pulldown QMenu */
        QMenu {
            background-color: #2b2b2b;
            color: #eaeaea;
            border: 1px solid #555;
            padding: 4px;
        }
        QMenu::separator {
            height: 1px;
            background: #444;
            margin: 6px 8px;
        }
        QMenu::item {
            padding: 6px 24px;
            border-radius: 3px;
        }
        QMenu::item:selected {
            background: #4c5a66;
            color: #ffffff;
        }
        QMenu::item:disabled {
            color: #777;
        }
        QMenu::item:selected:disabled {
            background: #3a3a3a;
        }
        QMenu::indicator {
            width: 16px; height: 16px;
        }
        /* Checkbox/checked items in menu */
        QMenu::indicator:checked {
            background: transparent;
            border: 0px;
            image: url(:/images/images/Check.png);
        }
    )");



    LogWidget::installQtHandler();

    MainWindow w;
    w.show();

    return a.exec();
}
