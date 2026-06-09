#include "debugterminalwidget.h"
#include "commandprocessor.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QColor>
#include <QDebug>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSize>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

// ------------------------------------------------------------
// ADAMP terminal colors
// ------------------------------------------------------------

static QTextCharFormat adampPromptFormat()
{
    QTextCharFormat fmt;
    fmt.setForeground(QColor("#CFA700")); // geel / goud
    fmt.setFontWeight(QFont::Bold);
    return fmt;
}

static QTextCharFormat adampInputFormat()
{
    QTextCharFormat fmt;
    fmt.setForeground(QColor("#00FF66")); // groen
    fmt.setFontWeight(QFont::Normal);
    return fmt;
}

static QTextCharFormat adampOutputFormat()
{
    QTextCharFormat fmt;
    fmt.setForeground(QColor("#DDDDDD")); // lichtgrijs / wit
    fmt.setFontWeight(QFont::Normal);
    return fmt;
}

static QTextCharFormat adampErrorFormat()
{
    QTextCharFormat fmt;
    fmt.setForeground(QColor("#DF3535")); // rood
    fmt.setFontWeight(QFont::Bold);
    return fmt;
}

// ------------------------------------------------------------
// Constructor
// ------------------------------------------------------------

DebugTerminalWidget::DebugTerminalWidget(CommandProcessor* processor, QWidget* parent)
    : QWidget(parent),
      m_processor(processor)
{
    // Dit widget is nu een GUI-container, niet meer rechtstreeks de editor.
    // Zo kunnen we intern tabs toevoegen zonder DebuggerWindow aan te passen.
    setObjectName("DebugTerminalWidget");
    setFocusPolicy(Qt::StrongFocus);

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QFont tabFont("Roboto", 10);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setObjectName("DebugTerminalTabWidget");
    m_tabWidget->setFont(tabFont);
    m_tabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QWidget* monitorTab = new QWidget(m_tabWidget);
    QVBoxLayout* monitorLayout = new QVBoxLayout(monitorTab);
    monitorLayout->setContentsMargins(4, 4, 4, 4);
    monitorLayout->setSpacing(4);

    m_console = new QPlainTextEdit(monitorTab);
    m_console->setObjectName("ADAMPMonitorConsole");
    m_console->setUndoRedoEnabled(false);
    m_console->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_console->setTabChangesFocus(false);
    m_console->installEventFilter(this);

    // Focus netjes naar de echte terminal sturen.
    setFocusProxy(m_console);

    // --------------------------------------------------------
    // Context menu: Copy / Select All / Clear
    // --------------------------------------------------------

    m_console->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_console, &QPlainTextEdit::customContextMenuRequested,
            this, [this](const QPoint& pos)
            {
                QMenu menu(m_console);

                QAction* copyAction = menu.addAction(tr("Copy to Clipboard"));
                copyAction->setEnabled(m_console->textCursor().hasSelection());

                QAction* selectAllAction = menu.addAction(tr("Select All"));

                menu.addSeparator();

                QAction* clearAction = menu.addAction(tr("Clear Terminal"));

                QAction* chosen = menu.exec(m_console->mapToGlobal(pos));

                if (chosen == copyAction)
                {
                    m_console->copy();
                }
                else if (chosen == selectAllAction)
                {
                    m_console->selectAll();
                }
                else if (chosen == clearAction)
                {
                    clearTerminal();
                }
            });

    // --------------------------------------------------------
    // Font uit resources laden
    // --------------------------------------------------------

    int fontId = QFontDatabase::addApplicationFont(":/fonts/fonts/luculent.ttf");

    QString family;
    if (fontId != -1)
    {
        family = QFontDatabase::applicationFontFamilies(fontId).value(0);
    }
    else
    {
        qWarning() << "[ADAMP Monitor] Could not load luculent.ttf, fallback to Consolas";
        family = "Consolas";
    }

    QFont terminalFont(family, 11);
    terminalFont.setStyleHint(QFont::Monospace);
    m_console->setFont(terminalFont);

    // Retro block cursor: Qt cursor breder maken.
    int blockWidth = QFontMetrics(terminalFont).horizontalAdvance(QLatin1Char('M'));
    if (blockWidth < 2)
        blockWidth = 8;

    m_console->setCursorWidth(blockWidth);

    // Zelfde donkere debugger-look, met jouw monitor-kleur als achtergrond.
    m_console->setStyleSheet(
        "QPlainTextEdit {"
        " background-color:#1D1D1D;"
        " color:#00FF66;"
        " border:1px solid #303030;"
        " selection-background-color:#4DA6FF;"
        " selection-color:#000000;"
        "}"
        );

    monitorLayout->addWidget(m_console, 1);

    // LED-status op MONITOR tab: zelfde idee als DebuggerWindow.
    // Grijs = stop/default, groen = run, geel/grijs knipperen = paused/stop.
    m_tabWidget->setIconSize(QSize(14, 14));
    m_monitorTabIndex = m_tabWidget->addTab(monitorTab, tr("MONITOR"));
    setMonitorLedGray();

    // --------------------------------------------------------
    // SCRIPTING tab
    // --------------------------------------------------------
    // Belangrijk: hier bewust GEEN stylesheet op m_tabWidget zetten.
    // Zo blijft de tab-look exact die van DebuggerWindow / parent stylesheet.

    QWidget* scriptingTab = new QWidget(m_tabWidget);
    QVBoxLayout* scriptingLayout = new QVBoxLayout(scriptingTab);
    scriptingLayout->setContentsMargins(4, 4, 4, 4);
    scriptingLayout->setSpacing(4);

    m_tabWidget->addTab(scriptingTab, tr("SCRIPTING"));

    rootLayout->addWidget(m_tabWidget, 1);

    printBanner();
    printPrompt();
}


// ------------------------------------------------------------
// MONITOR tab LED status
// ------------------------------------------------------------

void DebugTerminalWidget::setMonitorLedGray()
{
    if (!m_tabWidget || m_monitorTabIndex < 0)
        return;

    m_tabWidget->setTabIcon(m_monitorTabIndex, QIcon(":/images/images/LED_GRAY.png"));
}

void DebugTerminalWidget::setMonitorLedGreen()
{
    if (!m_tabWidget || m_monitorTabIndex < 0)
        return;

    m_tabWidget->setTabIcon(m_monitorTabIndex, QIcon(":/images/images/LED_GREEN.png"));
}

void DebugTerminalWidget::setMonitorLedYellow()
{
    if (!m_tabWidget || m_monitorTabIndex < 0)
        return;

    m_tabWidget->setTabIcon(m_monitorTabIndex, QIcon(":/images/images/LED_YELLOW.png"));
}

void DebugTerminalWidget::stopMonitorLedBlink()
{
    if (m_monitorLedBlinkTimer)
        m_monitorLedBlinkTimer->stop();

    m_monitorLedBlinkState = false;
}

void DebugTerminalWidget::startMonitorLedBlink()
{
    if (!m_monitorLedBlinkTimer)
    {
        m_monitorLedBlinkTimer = new QTimer(this);
        m_monitorLedBlinkTimer->setInterval(400);

        connect(m_monitorLedBlinkTimer, &QTimer::timeout,
                this, [this]()
                {
                    m_monitorLedBlinkState = !m_monitorLedBlinkState;

                    if (m_monitorLedBlinkState)
                        setMonitorLedYellow();
                    else
                        setMonitorLedGray();
                });
    }

    setMonitorLedYellow();
    m_monitorLedBlinkTimer->start();
}

void DebugTerminalWidget::setEmulatorPaused(bool paused)
{
    if (paused)
    {
        // Zelfde gedrag als DebuggerWindow: paused/stop knippert geel/grijs.
        startMonitorLedBlink();
    }
    else
    {
        // Running = groen.
        stopMonitorLedBlink();
        setMonitorLedGreen();
    }
}

// ------------------------------------------------------------
// Banner / clear
// ------------------------------------------------------------

void DebugTerminalWidget::printBanner()
{
    if (!m_console) return;

    QTextCursor c = m_console->textCursor();
    c.movePosition(QTextCursor::End);
    c.setCharFormat(adampOutputFormat());

    c.insertText("ADAMP Emulator Debug Monitor\n");
    c.insertText("Type help for commands.\n");
    c.insertText("\n");

    m_console->setTextCursor(c);
    m_console->setCurrentCharFormat(adampInputFormat());
}

void DebugTerminalWidget::clearTerminal()
{
    if (!m_console) return;

    m_console->clear();
    printBanner();
    printPrompt();
}

// ------------------------------------------------------------
// Prompt / output / error
// ------------------------------------------------------------

void DebugTerminalWidget::printPrompt()
{
    if (!m_console) return;

    moveCursorToEnd();

    QTextCursor cursor = m_console->textCursor();

    cursor.setCharFormat(adampPromptFormat());
    cursor.insertText(m_prompt);

    cursor.setCharFormat(adampInputFormat());

    m_console->setTextCursor(cursor);
    m_console->setCurrentCharFormat(adampInputFormat());

    m_promptPosition = m_console->textCursor().position();

    moveCursorToEnd();
}

void DebugTerminalWidget::printOutput(const QString& text)
{
    if (!m_console) return;

    if (text == "__ADAMP_TERMINAL_CLEAR__")
    {
        clearTerminal();
        return;
    }

    moveCursorToEnd();

    QTextCursor c = m_console->textCursor();
    c.setCharFormat(adampOutputFormat());

    QString t = text;

    if (!t.startsWith('\n'))
        c.insertText("\n");

    c.insertText(t);

    if (!t.endsWith('\n'))
        c.insertText("\n");

    c.setCharFormat(adampInputFormat());

    m_console->setTextCursor(c);
    m_console->setCurrentCharFormat(adampInputFormat());

    m_console->verticalScrollBar()->setValue(m_console->verticalScrollBar()->maximum());
}

void DebugTerminalWidget::printError(const QString& text)
{
    if (!m_console) return;

    moveCursorToEnd();

    QTextCursor c = m_console->textCursor();
    c.setCharFormat(adampErrorFormat());

    QString t = text;

    if (!t.startsWith("ERROR: "))
        t = "ERROR: " + t;

    if (!t.startsWith('\n'))
        c.insertText("\n");

    c.insertText(t);

    if (!t.endsWith('\n'))
        c.insertText("\n");

    c.setCharFormat(adampInputFormat());

    m_console->setTextCursor(c);
    m_console->setCurrentCharFormat(adampInputFormat());

    m_console->verticalScrollBar()->setValue(m_console->verticalScrollBar()->maximum());
}

// ------------------------------------------------------------
// Cursor helpers
// ------------------------------------------------------------

void DebugTerminalWidget::moveCursorToEnd()
{
    if (!m_console) return;

    QTextCursor c = m_console->textCursor();
    c.movePosition(QTextCursor::End);
    m_console->setTextCursor(c);
}

bool DebugTerminalWidget::cursorBeforePrompt() const
{
    if (!m_console) return false;
    return m_console->textCursor().position() < m_promptPosition;
}

QString DebugTerminalWidget::currentCommand() const
{
    if (!m_console) return {};

    const QString all = m_console->toPlainText();

    if (m_promptPosition < 0 || m_promptPosition > all.length())
        return {};

    return all.mid(m_promptPosition).trimmed();
}

void DebugTerminalWidget::replaceCurrentCommand(const QString& command)
{
    if (!m_console) return;

    QTextCursor c = m_console->textCursor();

    c.setPosition(m_promptPosition);
    c.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    c.removeSelectedText();

    c.setCharFormat(adampInputFormat());
    c.insertText(command);

    m_console->setTextCursor(c);
    m_console->setCurrentCharFormat(adampInputFormat());
}

void DebugTerminalWidget::updateBlockCursor()
{
    // Voorlopig niet nodig: de block cursor wordt via setCursorWidth() geregeld.
    // Deze functie blijft staan omdat ze al in de header bestond.
}

// ------------------------------------------------------------
// Event filter
// ------------------------------------------------------------

bool DebugTerminalWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_console && event->type() == QEvent::KeyPress)
    {
        return handleTerminalKeyPress(static_cast<QKeyEvent*>(event));
    }

    // Muisklikken laten we bewust door QPlainTextEdit zelf afhandelen.
    // Zo blijft selecteren van boven naar onder én onder naar boven werken.
    return QWidget::eventFilter(watched, event);
}

// ------------------------------------------------------------
// Keyboard
// ------------------------------------------------------------

bool DebugTerminalWidget::handleTerminalKeyPress(QKeyEvent* event)
{
    if (!m_console) return false;

    // Als er selectie is, enkel Copy toelaten.
    // Bij typen niet oude output overschrijven.
    if (m_console->textCursor().hasSelection() && !event->matches(QKeySequence::Copy))
    {
        moveCursorToEnd();
    }
    else if (cursorBeforePrompt())
    {
        moveCursorToEnd();
    }

    // Copy expliciet ondersteunen.
    if (event->matches(QKeySequence::Copy))
    {
        if (m_console->textCursor().hasSelection())
            m_console->copy();

        return true;
    }

    // Paste manueel groen invoegen.
    if (event->matches(QKeySequence::Paste))
    {
        const QString text = QGuiApplication::clipboard()->text();

        if (!text.isEmpty())
        {
            QTextCursor c = m_console->textCursor();

            if (c.position() < m_promptPosition)
                c.setPosition(m_console->document()->characterCount() - 1);

            c.setCharFormat(adampInputFormat());
            c.insertText(text);

            m_console->setTextCursor(c);
            m_console->setCurrentCharFormat(adampInputFormat());
        }

        return true;
    }

    switch (event->key())
    {
    case Qt::Key_Return:
    case Qt::Key_Enter:
    {
        const QString command = currentCommand();

        moveCursorToEnd();

        QTextCursor c = m_console->textCursor();
        c.setCharFormat(adampInputFormat());
        c.insertText("\n");
        m_console->setTextCursor(c);
        m_console->setCurrentCharFormat(adampInputFormat());

        if (!command.isEmpty())
        {
            m_history.append(command);
            m_historyIndex = m_history.size();

            if (m_processor)
            {
                const QString result = m_processor->execute(command);

                if (!result.isEmpty())
                {
                    // Clear is speciaal: geen extra prompt erachter plakken.
                    if (result == "__ADAMP_TERMINAL_CLEAR__")
                    {
                        clearTerminal();
                        return true;
                    }

                    if (result.startsWith("No ") ||
                        result.startsWith("Invalid") ||
                        result.startsWith("Usage:") ||
                        result.startsWith("Unknown command") ||
                        result.contains("failed", Qt::CaseInsensitive))
                    {
                        printError(result);
                    }
                    else
                    {
                        printOutput(result);
                    }
                }
            }
            else
            {
                printError("No command processor connected.");
            }
        }

        printPrompt();
        return true;
    }

    case Qt::Key_Backspace:
    {
        if (m_console->textCursor().position() <= m_promptPosition)
            return true;

        // Laat QPlainTextEdit de effectieve delete doen.
        return false;
    }

    case Qt::Key_Delete:
    {
        if (m_console->textCursor().position() < m_promptPosition)
            return true;

        return false;
    }

    case Qt::Key_Left:
    {
        if (m_console->textCursor().position() <= m_promptPosition)
            return true;

        return false;
    }

    case Qt::Key_Right:
    {
        return false;
    }

    case Qt::Key_Home:
    {
        QTextCursor c = m_console->textCursor();
        c.setPosition(m_promptPosition);
        m_console->setTextCursor(c);
        m_console->setCurrentCharFormat(adampInputFormat());
        return true;
    }

    case Qt::Key_End:
    {
        moveCursorToEnd();
        m_console->setCurrentCharFormat(adampInputFormat());
        return true;
    }

    case Qt::Key_Up:
    {
        if (!m_history.isEmpty() && m_historyIndex > 0)
        {
            --m_historyIndex;
            replaceCurrentCommand(m_history[m_historyIndex]);
        }

        return true;
    }

    case Qt::Key_Down:
    {
        if (!m_history.isEmpty() && m_historyIndex < m_history.size() - 1)
        {
            ++m_historyIndex;
            replaceCurrentCommand(m_history[m_historyIndex]);
        }
        else
        {
            m_historyIndex = m_history.size();
            replaceCurrentCommand("");
        }

        return true;
    }

    default:
        break;
    }

    // Gewone tekst niet door Qt laten typen.
    // We voegen zelf tekst in met groene input-format.
    const QString text = event->text();

    if (!text.isEmpty() && text.at(0).unicode() >= 0x20)
    {
        QTextCursor c = m_console->textCursor();

        if (c.position() < m_promptPosition)
            c.setPosition(m_console->document()->characterCount() - 1);

        c.setCharFormat(adampInputFormat());
        c.insertText(text);

        m_console->setTextCursor(c);
        m_console->setCurrentCharFormat(adampInputFormat());
        return true;
    }

    return false;
}
