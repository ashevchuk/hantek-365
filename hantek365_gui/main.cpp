#include <QApplication>
#include <QPalette>
#include <QColor>
#include "mainwindow.h"

static void applyDarkTheme(QApplication &app)
{
    app.setStyle("Fusion");

    QPalette p;
    p.setColor(QPalette::Window,          QColor(0x2b, 0x2b, 0x2b));
    p.setColor(QPalette::WindowText,      QColor(0xcc, 0xcc, 0xcc));
    p.setColor(QPalette::Base,            QColor(0x1e, 0x1e, 0x1e));
    p.setColor(QPalette::AlternateBase,   QColor(0x2b, 0x2b, 0x2b));
    p.setColor(QPalette::ToolTipBase,     QColor(0x2b, 0x2b, 0x2b));
    p.setColor(QPalette::ToolTipText,     QColor(0xcc, 0xcc, 0xcc));
    p.setColor(QPalette::Text,            QColor(0xcc, 0xcc, 0xcc));
    p.setColor(QPalette::Button,          QColor(0x3c, 0x3c, 0x3c));
    p.setColor(QPalette::ButtonText,      QColor(0xcc, 0xcc, 0xcc));
    p.setColor(QPalette::BrightText,      Qt::red);
    p.setColor(QPalette::Link,            QColor(0x2a, 0x82, 0xda));
    p.setColor(QPalette::Highlight,       QColor(0x2a, 0x82, 0xda));
    p.setColor(QPalette::HighlightedText, Qt::black);
    app.setPalette(p);

    app.setStyleSheet(R"(
        QMainWindow, QWidget       { background-color: #2b2b2b; }
        QFrame[frameShape="1"]     { border: 1px solid #404040; border-radius: 3px;
                                     background-color: #333333; }
        QPushButton {
            background-color: #4a4a4a;
            color: #cccccc;
            border: 1px solid #5a5a5a;
            border-radius: 3px;
            padding: 3px 10px;
            min-height: 22px;
        }
        QPushButton:hover    { background-color: #5a5a5a; }
        QPushButton:pressed  { background-color: #3a3a3a; }
        QPushButton:checked  {
            background-color: #1c5c1c;
            border-color: #3a9a3a;
            color: #aaffaa;
        }
        QPushButton:disabled { background-color: #333; color: #555; }
        QComboBox {
            background-color: #3c3c3c;
            color: #cccccc;
            border: 1px solid #5a5a5a;
            border-radius: 3px;
            padding: 2px 8px;
            min-height: 22px;
        }
        QComboBox::drop-down      { border: none; width: 20px; }
        QComboBox QAbstractItemView {
            background-color: #3c3c3c;
            color: #cccccc;
            selection-background-color: #2a82da;
        }
        QLabel      { color: #cccccc; }
        QStatusBar  { background-color: #222; color: #888; font-size: 11px; }
        QStatusBar::item { border: none; }
        QChartView  { background: transparent; border: none; }
    )");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Hantek 365 DMM");
    app.setApplicationVersion("1.0");

    applyDarkTheme(app);

    MainWindow w;
    w.show();

    return app.exec();
}
