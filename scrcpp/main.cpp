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

        //qDebug() << "Succesvol geladen familie:" << family;
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

    LogWidget::installQtHandler();

    MainWindow w;
    w.show();

    return a.exec();
}
