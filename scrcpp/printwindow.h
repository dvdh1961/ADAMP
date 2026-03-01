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

    static void         setAsSink(PrintWindow* w);
    static PrintWindow* instance();

public slots:
    void appendPrinterBytes(const QByteArray& bytes);
    void  updatePrinterMode(bool isTypemachine);
    void saveAsPdf();
    void saveAsTxt();
    void chooseFont();
    void clearText();
    void copySelection();
    void copyAllToClipboard();

    void setBitmap(const QImage& img);
    void clearBitmap();

protected:
    void closeEvent(QCloseEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;
    void showEvent(QShowEvent* event) override;

    void contextMenuEvent(QContextMenuEvent* ev) override;

private:
    void applyTextFont(const QFont& f);
    void updateStatus();
    void flushCurrentLine(bool forceEmpty = false);
    void ensureLastRowVisible();
    void updateTopContainerMaxHeight();
    void toggleRemoveFirstChar(bool checked);

    QLabel* m_paperLabel  = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_bitmapLabel = nullptr;
    QStatusBar* m_status  = nullptr;

    QLabel* m_statLeft  = nullptr;
    QLabel* m_statMid   = nullptr;
    QLabel* m_statRight = nullptr;

    QString       m_currentLine;
    bool          m_atLineStart = true;
    QFont         m_monoFont;
    std::uint64_t m_totalBytes = 0;
    std::uint64_t m_totalLines = 0;
    QMutex        m_appendMutex;

    bool          m_removeFirstChar = false;

    QColor   m_rowLight = QColor(240,245,250);
    QColor   m_rowDark  = QColor(228,234,240);
    int      m_holeDiameter = 6;
    int      m_holePadding  = 1;
    int      m_holeColWidth = 16;
    int      m_textColMin   = 200;

    static PrintWindow* s_instance;
};
