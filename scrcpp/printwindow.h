#pragma once
#include <QMainWindow>
#include <QByteArray>
#include <QMutex>
#include <QColor>
#include <QImage>
#include <cstdint>

class QTableWidget;
class QLabel;
class QStatusBar;

class PrintWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit PrintWindow(QWidget* parent = nullptr);
    ~PrintWindow();

    // Singleton (lazy)
    static void         setAsSink(PrintWindow* w);
    static PrintWindow* instance();

public slots:
    // AdamNet PRN → UI
    void appendPrinterBytes(const QByteArray& bytes);

    // UI helpers
    void saveAsPdf();
    void saveAsTxt();
    void chooseFont();
    void clearText();
    void copySelection();
    void copyAllToClipboard();

    // Bitmap (onderste paneel) zetten/schoonmaken
    void setBitmap(const QImage& img);
    void clearBitmap();

protected:
    void closeEvent(QCloseEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;

    // NIEUWE OVERRIDE: Afhandeling van rechtermuisklik (Context Menu)
    void contextMenuEvent(QContextMenuEvent* ev) override;

private:
    // intern
    void applyTextFont(const QFont& f);
    void updateStatus();
    void flushCurrentLine(bool forceEmpty = false);
    void ensureLastRowVisible();
    void updateTopContainerMaxHeight();   // QLabels boven/onder uitlijnen
    void toggleRemoveFirstChar(bool checked);

    // Widgets
    QLabel* m_paperLabel  = nullptr;  // BOVEN: container-label
    QTableWidget* m_table       = nullptr;  // 3 kolommen: hole | text | hole, als child van m_paperLabel
    QLabel* m_bitmapLabel = nullptr;  // ONDER: bitmap-label (fixed height 73)
    QStatusBar* m_status      = nullptr;

    // Statusbar labels
    QLabel* m_statLeft  = nullptr;    // Lines
    QLabel* m_statMid   = nullptr;    // Bytes
    QLabel* m_statRight = nullptr;    // Hint

    // Data
    QString       m_currentLine;
    bool          m_atLineStart = true;
    QFont         m_monoFont;
    std::uint64_t m_totalBytes = 0;
    std::uint64_t m_totalLines = 0;
    QMutex        m_appendMutex;

    // Staat variabele voor de ']' optie
    bool          m_removeFirstChar = false;

    // Look & feel voor 3-koloms papier
    QColor   m_rowLight = QColor(240,245,250);
    QColor   m_rowDark  = QColor(228,234,240);
    int      m_holeDiameter = 6;
    int      m_holePadding  = 1;
    int      m_holeColWidth = 16;
    int      m_textColMin   = 200;

    // Singleton
    static PrintWindow* s_instance;
};
