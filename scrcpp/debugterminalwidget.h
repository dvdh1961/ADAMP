#pragma once

#include <QWidget>
#include <QStringList>

class CommandProcessor;
class QEvent;
class QKeyEvent;
class QPlainTextEdit;
class QTabWidget;
class QTimer;

class DebugTerminalWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DebugTerminalWidget(CommandProcessor* processor, QWidget* parent = nullptr);

public slots:
    void printOutput(const QString& text);
    void printError(const QString& text);
    void clearTerminal();
    void setEmulatorPaused(bool paused);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void printBanner();
    void printPrompt();
    void moveCursorToEnd();
    bool cursorBeforePrompt() const;
    QString currentCommand() const;
    void replaceCurrentCommand(const QString& command);
    bool handleTerminalKeyPress(QKeyEvent* event);

    QTimer* m_cursorBlinkTimer = nullptr;
    bool m_cursorVisible = true;
    void updateBlockCursor();

    void setMonitorLedGray();
    void setMonitorLedGreen();
    void setMonitorLedYellow();
    void stopMonitorLedBlink();
    void startMonitorLedBlink();

private:
    CommandProcessor* m_processor = nullptr;

    QTabWidget* m_tabWidget = nullptr;
    QPlainTextEdit* m_console = nullptr;

    QTimer* m_monitorLedBlinkTimer = nullptr;
    bool m_monitorLedBlinkState = false;
    int m_monitorTabIndex = -1;

    QString m_prompt = "ADAMP> ";
    int m_promptPosition = 0;
    QStringList m_history;
    int m_historyIndex = 0;
};
