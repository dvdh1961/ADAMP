#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QColor>
#include "logwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

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
        QMenuBar::item:selected {            /* hover */
            background: #3a3a3a;
            color: #ffffff;
        }
        QMenuBar::item:pressed,              /* open/ingedrukt */
        QMenuBar::item:open {
            background: #4c5a66;             /* accent */
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
            padding: 6px 24px;               /* ruimte voor icon/checkbox */
            border-radius: 3px;
        }
        QMenu::item:selected {               /* hover in menu */
            background: #4c5a66;             /* zelfde accent als menubalk */
            color: #ffffff;
        }
        QMenu::item:disabled {
            color: #777;
        }
        QMenu::item:selected:disabled {
            background: #3a3a3a;
        }

        /* Checkbox/checked items in menu */
        QMenu::indicator:checked {
            background: #6b7c8a;
            border: 1px solid #8da0ae;
        }
    )");

    LogWidget::installQtHandler();

    MainWindow w;
    w.show();

    return a.exec();
}
