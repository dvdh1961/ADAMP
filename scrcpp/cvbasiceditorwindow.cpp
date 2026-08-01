#include "cvbasiceditorwindow.h"

#include <QAction>
#include <QPixmap>
#include <QRadioButton>
#include <QIcon>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QApplication>
#include <QByteArray>
#include <QCloseEvent>
#include <QCheckBox>
#include <functional>
#include <algorithm>
#include <limits>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSlider>
#include <QScrollBar>
#include <QSpinBox>
#include <QMouseEvent>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QGroupBox>
#include <QComboBox>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QDebug>
#include <QFileInfo>
#include <QEvent>
#include <QFrame>
#include <QFont>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGridLayout>
#include <QFontDatabase>
#include <QFontComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QLinearGradient>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QSplitter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QSyntaxHighlighter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTabBar>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextOption>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QWheelEvent>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>
#include <utility>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// ============================================================================
// Code editor met line numbers
// ============================================================================
class CodeEditor;

class LineNumberArea final : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor* editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    CodeEditor* m_codeEditor = nullptr;
};

class CodeEditor final : public QPlainTextEdit
{
public:
    explicit CodeEditor(QWidget* parent = nullptr)
        : QPlainTextEdit(parent)
    {
        m_lineNumberArea = new LineNumberArea(this);

        connect(this, &QPlainTextEdit::blockCountChanged,
                this, &CodeEditor::updateLineNumberAreaWidth);
        connect(this, &QPlainTextEdit::updateRequest,
                this, &CodeEditor::updateLineNumberArea);
        connect(this, &QPlainTextEdit::cursorPositionChanged,
                this, &CodeEditor::highlightCurrentLine);

        updateLineNumberAreaWidth(0);
        highlightCurrentLine();
    }

    QWidget* lineNumberAreaWidget() const { return m_lineNumberArea; }

    void setLineNumberAreaVisible(bool visible)
    {
        if (m_lineNumberArea)
            m_lineNumberArea->setVisible(visible);
        relayoutLineNumberArea();
    }

    void relayoutLineNumberArea()
    {
        const int areaWidth = (m_lineNumberArea && m_lineNumberArea->isVisible())
            ? lineNumberAreaWidth()
            : 0;

        // Keep viewport margin and line-number geometry synchronized.
        // Without this, the number gutter can visually overlap the code text
        // until a scroll event forces Qt to repair the layout.
        setViewportMargins(areaWidth, 0, 0, 0);

        if (m_lineNumberArea) {
            const QRect cr = contentsRect();
            m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), areaWidth, cr.height()));
            m_lineNumberArea->raise();
            m_lineNumberArea->update();
        }

        viewport()->update();
        update();
    }

    void forceLineNumberRefresh()
    {
        // Do the same internal update that a tiny scroll would trigger, but
        // without leaving the editor scrolled. This fixes the first project tab
        // where the gutter sometimes stays unpainted until the user scrolls.
        relayoutLineNumberArea();

        QScrollBar* sb = verticalScrollBar();
        if (sb && sb->maximum() > sb->minimum()) {
            const int oldValue = sb->value();
            const int nudgedValue = (oldValue < sb->maximum()) ? oldValue + 1 : oldValue - 1;
            sb->setValue(nudgedValue);
            sb->setValue(oldValue);
        }

        updateLineNumberArea(viewport()->rect(), 0);
        relayoutLineNumberArea();
    }

    int lineNumberAreaWidth() const
    {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }

        const int space = 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
        return qMax(58, space);
    }

    void lineNumberAreaPaintEvent(QPaintEvent* event)
    {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), QColor("#2C2C2C"));

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        QFont numberFont = font();
        numberFont.setBold(true);
        painter.setFont(numberFont);

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const QString number = QString::number(blockNumber + 1);

                painter.setPen(QColor("#66D9EF"));
                painter.drawText(0, top, m_lineNumberArea->width() - 10, fontMetrics().height(),
                                 Qt::AlignRight, number);

            }

            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QPlainTextEdit::showEvent(event);

        // The first tab can be created while the QTabWidget is still building
        // its internal stack. At that moment Qt sometimes skips the gutter
        // geometry until the first scroll. Force it once the editor is visible.
        relayoutLineNumberArea();
        QTimer::singleShot(0, this, [this]() { forceLineNumberRefresh(); });
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QPlainTextEdit::resizeEvent(event);
        relayoutLineNumberArea();
    }

private:
    void updateLineNumberAreaWidth(int)
    {
        relayoutLineNumberArea();
    }

    void updateLineNumberArea(const QRect& rect, int dy)
    {
        if (dy)
            m_lineNumberArea->scroll(0, dy);
        else
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

        if (rect.contains(viewport()->rect()))
            updateLineNumberAreaWidth(0);
    }

    void highlightCurrentLine()
    {
        QList<QTextEdit::ExtraSelection> extraSelections;

        if (!isReadOnly()) {
            QTextEdit::ExtraSelection selection;
            QColor lineColor("#3D4D5D");
            selection.format.setBackground(lineColor);
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            selection.cursor = textCursor();
            selection.cursor.clearSelection();
            extraSelections.append(selection);
        }

        setExtraSelections(extraSelections);
    }

private:
    QWidget* m_lineNumberArea = nullptr;
};

LineNumberArea::LineNumberArea(CodeEditor* editor)
    : QWidget(editor)
    , m_codeEditor(editor)
{
}

QSize LineNumberArea::sizeHint() const
{
    return QSize(m_codeEditor ? m_codeEditor->lineNumberAreaWidth() : 58, 0);
}

void LineNumberArea::paintEvent(QPaintEvent* event)
{
    if (m_codeEditor)
        m_codeEditor->lineNumberAreaPaintEvent(event);
}

// ============================================================================
// CVBasic syntax highlighter
// ============================================================================
class CvBasicHighlighter final : public QSyntaxHighlighter
{
public:
    explicit CvBasicHighlighter(QTextDocument* parent)
        : QSyntaxHighlighter(parent)
    {
        QTextCharFormat keywordFmt;
        keywordFmt.setForeground(QColor("#FFFF00"));
        keywordFmt.setFontWeight(QFont::Bold);

        QTextCharFormat numberFmt;
        numberFmt.setForeground(QColor("#9CDCFE"));

        QTextCharFormat stringFmt;
        stringFmt.setForeground(QColor("#57FF8A"));

        QTextCharFormat commentFmt;
        commentFmt.setForeground(QColor("#B9D7FF"));

        QTextCharFormat labelFmt;
        labelFmt.setForeground(QColor("#FFFFFF"));
        labelFmt.setFontWeight(QFont::Bold);

        QTextCharFormat directiveFmt;
        directiveFmt.setForeground(QColor("#50E3C2"));
        directiveFmt.setFontWeight(QFont::Bold);

        const QStringList keywords = {
            "ABS", "AND", "ASC", "AT", "BITMAP", "CALL", "CLS", "COLOR", "CONST",
            "DATA", "DEFINE", "DIM", "DO", "ELSE", "END", "FOR", "GOSUB", "GOTO",
            "IF", "IN", "INPUT", "LEFT$", "LEN", "LET", "LOOP", "MID$", "MODE",
            "NEXT", "NOT", "ON", "OR", "PEEK", "POKE", "PRINT", "READ", "REM",
            "RESTORE", "RETURN", "RIGHT$", "RND", "SCREEN", "SGN", "SOUND", "SPRITE",
            "STEP", "STR$", "THEN", "TO", "USR", "VAL", "VPEEK", "VPOKE", "WEND",
            "WHILE", "XOR", "BANK", "ASM", "PLAY", "WAIT", "SCROLL"
        };

        for (const QString& keyword : keywords) {
            HighlightRule rule;
            rule.pattern = QRegularExpression(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(keyword)),
                                              QRegularExpression::CaseInsensitiveOption);
            rule.format = keywordFmt;
            m_rules.append(rule);
        }

        addRule(QStringLiteral("\\b[0-9]+\\b"), numberFmt);
        addRule(QStringLiteral("\\$[0-9A-Fa-f]+\\b"), numberFmt);
        addRule(QStringLiteral("%[01]+\\b"), numberFmt);
        addRule(QStringLiteral("\"[^\"\\r\\n]*\""), stringFmt);
        addRule(QStringLiteral("^\\s*[A-Za-z_][A-Za-z0-9_]*:"), labelFmt);
        addRule(QStringLiteral("^\\s*#[A-Za-z_][A-Za-z0-9_]*.*$"), directiveFmt);

        m_commentFormat = commentFmt;
    }

protected:
    void highlightBlock(const QString& text) override
    {
        for (const HighlightRule& rule : std::as_const(m_rules)) {
            QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }

        int remIndex = -1;
        const QRegularExpression remRegex(QStringLiteral("\\bREM\\b"), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch remMatch = remRegex.match(text);
        if (remMatch.hasMatch())
            remIndex = remMatch.capturedStart();

        const int apostropheIndex = text.indexOf('\'');

        int commentIndex = -1;
        if (remIndex >= 0 && apostropheIndex >= 0)
            commentIndex = qMin(remIndex, apostropheIndex);
        else if (remIndex >= 0)
            commentIndex = remIndex;
        else if (apostropheIndex >= 0)
            commentIndex = apostropheIndex;

        if (commentIndex >= 0)
            setFormat(commentIndex, text.length() - commentIndex, m_commentFormat);
    }

private:
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    void addRule(const QString& pattern, const QTextCharFormat& format)
    {
        HighlightRule rule;
        rule.pattern = QRegularExpression(pattern);
        rule.format = format;
        m_rules.append(rule);
    }

private:
    QVector<HighlightRule> m_rules;
    QTextCharFormat m_commentFormat;
};

namespace {

#if defined(Q_OS_WIN)
bool lockKeyActive(int virtualKey)
{
    return (GetKeyState(virtualKey) & 0x0001) != 0;
}
#endif

#if defined(Q_OS_LINUX)
bool linuxLockKeyActive(const QString& keyName, bool fallbackValue)
{
    // Many Linux desktops expose keyboard-lock LEDs here:
    // /sys/class/leds/inputX::capslock/brightness
    // /sys/class/leds/inputX::numlock/brightness
    // /sys/class/leds/inputX::scrolllock/brightness
    //
    // This works without X11/Wayland-specific libraries.
    QDir ledsDir("/sys/class/leds");
    if (!ledsDir.exists())
        return fallbackValue;

    const QStringList entries = ledsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    bool found = false;

    for (const QString& entry : entries) {
        const QString low = entry.toLower();

        if (!low.contains(keyName.toLower()))
            continue;

        QFile f(ledsDir.filePath(entry + "/brightness"));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        found = true;
        const QByteArray value = f.readAll().trimmed();

        if (value.toInt() > 0)
            return true;
    }

    // If LED entries exist but are all 0, the lock is off.
    // If nothing was found, use fallback toggles.
    return found ? false : fallbackValue;
}
#endif

} // namespace



// ============================================================================
// Integrated CVBasic paint editor (native Qt port of the first CV Paint Studio step)
// ============================================================================

class CvBasicPaintCanvas final : public QWidget
{
private:
    struct RowCodec
    {
        quint8 pattern = 0;
        int fg = 15;
        int bg = 0;
    };

public:
    enum class ToolMode
    {
        Pen,
        Eraser,
        Fill,
        Pipette,
        Select,
        Line,
        Rect,
        FilledRect,
        Ellipse,
        FilledEllipse
    };

    explicit CvBasicPaintCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(520, 404);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        m_pixels.fill(0, 256 * 192);
        setZoomScale(2);
    }

    void setToolMode(ToolMode mode)
    {
        m_toolMode = mode;
        switch (m_toolMode) {
        case ToolMode::Pen:
        case ToolMode::Eraser:
            setCursor(Qt::CrossCursor);
            break;
        case ToolMode::Fill:
            setCursor(Qt::PointingHandCursor);
            break;
        case ToolMode::Pipette:
            setCursor(Qt::WhatsThisCursor);
            break;
        case ToolMode::Select:
        case ToolMode::Line:
        case ToolMode::Rect:
        case ToolMode::FilledRect:
        case ToolMode::Ellipse:
        case ToolMode::FilledEllipse:
            setCursor(Qt::CrossCursor);
            break;
        }
    }

    void setColorIndex(int color)
    {
        m_colorIndex = qBound(0, color, 15);
        update();
    }

    int colorIndex() const
    {
        return m_colorIndex;
    }

    void setBackgroundColorIndex(int color)
    {
        m_backgroundColorIndex = qBound(0, color, 15);
        updateStatus(QStringLiteral("Background color %1").arg(m_backgroundColorIndex));
    }

    int backgroundColorIndex() const
    {
        return m_backgroundColorIndex;
    }

    void setBrushSize(int size)
    {
        m_brushSize = qBound(1, size, 8);
        updateStatus(QStringLiteral("Brush size %1").arg(m_brushSize));
    }

    int brushSize() const
    {
        return m_brushSize;
    }

    void setTransparentPaste(bool enabled)
    {
        m_transparentPaste = enabled;
        updateStatus(enabled ? QStringLiteral("Transparent paste ON") : QStringLiteral("Transparent paste OFF"));
    }

    bool transparentPaste() const
    {
        return m_transparentPaste;
    }

    void replaceColor(int fromColor, int toColor)
    {
        fromColor = qBound(0, fromColor, 15);
        toColor = qBound(0, toColor, 15);
        if (fromColor == toColor)
            return;

        pushUndoSnapshot();

        int changed = 0;
        for (quint8& px : m_pixels) {
            if (px == fromColor) {
                px = static_cast<quint8>(toColor);
                ++changed;
            }
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Replaced color %1 with %2 (%3 pixels)").arg(fromColor).arg(toColor).arg(changed));
    }

    bool replaceColorInSelection(int fromColor, int toColor)
    {
        const QRect sel = selectionRect();
        if (!sel.isValid()) {
            updateStatus(QStringLiteral("No selection for color replace"));
            return false;
        }

        fromColor = qBound(0, fromColor, 15);
        toColor = qBound(0, toColor, 15);
        if (fromColor == toColor)
            return false;

        pushUndoSnapshot();

        int changed = 0;
        for (int y = sel.top(); y <= sel.bottom(); ++y) {
            for (int x = sel.left(); x <= sel.right(); ++x) {
                const int index = y * 256 + x;
                if (m_pixels[index] == fromColor) {
                    m_pixels[index] = static_cast<quint8>(toColor);
                    ++changed;
                }
            }
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Selection: replaced color %1 with %2 (%3 pixels)").arg(fromColor).arg(toColor).arg(changed));
        return true;
    }

    bool swapColors(int colorA, int colorB)
    {
        colorA = qBound(0, colorA, 15);
        colorB = qBound(0, colorB, 15);
        if (colorA == colorB)
            return false;

        pushUndoSnapshot();

        int changed = 0;
        for (quint8& px : m_pixels) {
            if (px == colorA) {
                px = static_cast<quint8>(colorB);
                ++changed;
            } else if (px == colorB) {
                px = static_cast<quint8>(colorA);
                ++changed;
            }
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Swapped colors %1/%2 on canvas (%3 pixels)").arg(colorA).arg(colorB).arg(changed));
        return true;
    }

    bool swapColorsInSelection(int colorA, int colorB)
    {
        const QRect sel = selectionRect();
        if (!sel.isValid()) {
            updateStatus(QStringLiteral("No selection for color swap"));
            return false;
        }

        colorA = qBound(0, colorA, 15);
        colorB = qBound(0, colorB, 15);
        if (colorA == colorB)
            return false;

        pushUndoSnapshot();

        int changed = 0;
        for (int y = sel.top(); y <= sel.bottom(); ++y) {
            for (int x = sel.left(); x <= sel.right(); ++x) {
                const int index = y * 256 + x;
                if (m_pixels[index] == colorA) {
                    m_pixels[index] = static_cast<quint8>(colorB);
                    ++changed;
                } else if (m_pixels[index] == colorB) {
                    m_pixels[index] = static_cast<quint8>(colorA);
                    ++changed;
                }
            }
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Selection: swapped colors %1/%2 (%3 pixels)").arg(colorA).arg(colorB).arg(changed));
        return true;
    }

    void clearColor(int color)
    {
        replaceColor(color, 0);
    }

    bool clearColorInSelection(int color)
    {
        return replaceColorInSelection(color, m_backgroundColorIndex);
    }

    bool fillSelectionWithColor(int color)
    {
        const QRect sel = selectionRect();
        if (!sel.isValid()) {
            updateStatus(QStringLiteral("No selection to fill"));
            return false;
        }

        color = qBound(0, color, 15);
        pushUndoSnapshot();

        for (int y = sel.top(); y <= sel.bottom(); ++y) {
            for (int x = sel.left(); x <= sel.right(); ++x)
                setPixel(x, y, color);
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Filled selection %1x%2 with color %3").arg(sel.width()).arg(sel.height()).arg(color));
        return true;
    }

    bool outlineSelectionWithColor(int color)
    {
        const QRect sel = selectionRect();
        if (!sel.isValid()) {
            updateStatus(QStringLiteral("No selection to outline"));
            return false;
        }

        color = qBound(0, color, 15);
        pushUndoSnapshot();

        for (int x = sel.left(); x <= sel.right(); ++x) {
            setPixel(x, sel.top(), color);
            setPixel(x, sel.bottom(), color);
        }

        for (int y = sel.top(); y <= sel.bottom(); ++y) {
            setPixel(sel.left(), y, color);
            setPixel(sel.right(), y, color);
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Outlined selection %1x%2 with color %3").arg(sel.width()).arg(sel.height()).arg(color));
        return true;
    }

    bool armTextPlacement(const QString& text,
                          int pointSize,
                          const QString& fontFamily = QStringLiteral("Arial"),
                          int letterSpacing = 0,
                          const QVector<int>& gradientColors = QVector<int>())
    {
        const QString cleanText = text.trimmed();
        if (cleanText.isEmpty()) {
            updateStatus(QStringLiteral("No text to place"));
            return false;
        }

        pointSize = qBound(6, pointSize, 48);

        QFont font(fontFamily.trimmed().isEmpty() ? QStringLiteral("Arial") : fontFamily.trimmed());
        font.setPixelSize(pointSize);
        font.setBold(false);
        font.setLetterSpacing(QFont::AbsoluteSpacing, qBound(-4, letterSpacing, 24));

        QFontMetrics fm(font);
        const QRect textRect = fm.boundingRect(cleanText).adjusted(-1, -1, 2, 2);

        m_pendingTextMask = QImage(qMax(1, textRect.width()), qMax(1, textRect.height()), QImage::Format_ARGB32);
        m_pendingTextMask.fill(Qt::transparent);
        m_pendingTextColor = qBound(0, m_colorIndex, 15);
        m_pendingTextGradientColors.clear();

        for (int c : gradientColors)
            m_pendingTextGradientColors.append(qBound(0, c, 15));

        if (m_pendingTextGradientColors.isEmpty())
            m_pendingTextGradientColors.append(m_pendingTextColor);

        while (m_pendingTextGradientColors.size() > 4)
            m_pendingTextGradientColors.removeLast();

        m_pendingTextDragging = false;
        m_pendingTextArmed = true;

        QPainter painter(&m_pendingTextMask);
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(-textRect.left(), -textRect.top(), cleanText);
        painter.end();

        setCursor(Qt::CrossCursor);
        update();
        updateStatus(QStringLiteral("Text ready: font=%1, vertical ink-row gradient colors=%2, click/drag on canvas, release to place")
                         .arg(font.family())
                         .arg(m_pendingTextGradientColors.size()));
        return true;
    }

    void setGridVisible(bool visible)
    {
        m_gridVisible = visible;
        update();
    }

    bool gridVisible() const
    {
        return m_gridVisible;
    }

    void setPixelGridVisible(bool visible)
    {
        m_pixelGridVisible = visible;
        update();
        updateStatus(visible ? QStringLiteral("Pixel grid ON") : QStringLiteral("Pixel grid OFF"));
    }

    bool pixelGridVisible() const
    {
        return m_pixelGridVisible;
    }

    void setShowTmsConflicts(bool visible)
    {
        m_showTmsConflicts = visible;
        update();
        updateStatus(visible ? QStringLiteral("TMS conflict overlay ON") : QStringLiteral("TMS conflict overlay OFF"));
    }

    bool showTmsConflicts() const
    {
        return m_showTmsConflicts;
    }

    bool loadReferenceImage(const QString& filePath)
    {
        QImage img(filePath);
        if (img.isNull())
            return false;

        m_referenceImage = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        m_referenceOffset = QPoint(0, 0);
        update();
        updateStatus(QStringLiteral("Reference image loaded"));
        return true;
    }

    void clearReferenceImage()
    {
        if (m_referenceImage.isNull())
            return;

        m_referenceImage = QImage();
        update();
        updateStatus(QStringLiteral("Reference image cleared"));
    }

    void setReferenceVisible(bool visible)
    {
        m_referenceVisible = visible;
        update();
        updateStatus(visible ? QStringLiteral("Reference image visible") : QStringLiteral("Reference image hidden"));
    }

    void setSelectionSnapToTile(bool enabled)
    {
        m_snapSelectionToTile = enabled;
        updateStatus(enabled ? QStringLiteral("Selection snap 8x8 ON") : QStringLiteral("Selection snap 8x8 OFF"));
    }

    bool selectionSnapToTile() const
    {
        return m_snapSelectionToTile;
    }

    void armTileBlockSelection(int tileW, int tileH)
    {
        m_pendingTileSelection = QSize(qMax(1, tileW), qMax(1, tileH));
        setCursor(Qt::CrossCursor);
        updateStatus(QStringLiteral("Click canvas to select %1x%2 tile block").arg(m_pendingTileSelection.width()).arg(m_pendingTileSelection.height()));
    }

    void selectTileAtCursor()
    {
        selectTileBlockAtPixel(m_lastCanvasPixel, 1, 1);
    }

    void selectTileBlock(int tileW, int tileH)
    {
        selectTileBlockAtPixel(m_lastCanvasPixel, tileW, tileH);
    }

    void setReferenceOpacity(int opacity)
    {
        m_referenceOpacity = qBound(0, opacity, 100);
        update();
        updateStatus(QStringLiteral("Reference opacity %1%").arg(m_referenceOpacity));
    }

    void moveReference(int dx, int dy)
    {
        if (m_referenceImage.isNull())
            return;

        m_referenceOffset += QPoint(dx, dy);
        update();
        updateStatus(QStringLiteral("Reference moved X=%1 Y=%2").arg(m_referenceOffset.x()).arg(m_referenceOffset.y()));
    }

    void setZoomScale(int scale)
    {
        m_zoomScale = qBound(1, scale, 4);
        setMinimumSize(256 * m_zoomScale + 30, 192 * m_zoomScale + 30);
        setFixedSize(256 * m_zoomScale + 30, 192 * m_zoomScale + 30);
        update();
        updateStatus(QStringLiteral("Zoom %1x").arg(m_zoomScale));
    }

    int zoomScale() const
    {
        return m_zoomScale;
    }

    void clearCanvas()
    {
        if (isCanvasEmpty())
            return;

        pushUndoSnapshot();
        m_pixels.fill(0);
        update();
        notifyChanged();
        notifyUndoStateChanged();
    }

    void clearCanvasToBackground()
    {
        pushUndoSnapshot();
        m_pixels.fill(static_cast<quint8>(m_backgroundColorIndex));
        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Canvas cleared to background color %1").arg(m_backgroundColorIndex));
    }

    void shiftCanvas(int dx, int dy)
    {
        if (dx == 0 && dy == 0)
            return;

        pushUndoSnapshot();

        QVector<quint8> shifted(256 * 192);
        shifted.fill(0);

        for (int y = 0; y < 192; ++y) {
            const int ny = y + dy;
            if (ny < 0 || ny >= 192)
                continue;

            for (int x = 0; x < 256; ++x) {
                const int nx = x + dx;
                if (nx < 0 || nx >= 256)
                    continue;

                shifted[ny * 256 + nx] = m_pixels[y * 256 + x];
            }
        }

        m_pixels = shifted;
        update();
        notifyChanged();
        notifyUndoStateChanged();
    }

    void mirrorHorizontal()
    {
        pushUndoSnapshot();

        for (int y = 0; y < 192; ++y) {
            for (int x = 0; x < 128; ++x) {
                const int a = y * 256 + x;
                const int b = y * 256 + (255 - x);
                std::swap(m_pixels[a], m_pixels[b]);
            }
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
    }

    void mirrorVertical()
    {
        pushUndoSnapshot();

        for (int y = 0; y < 96; ++y) {
            for (int x = 0; x < 256; ++x) {
                const int a = y * 256 + x;
                const int b = (191 - y) * 256 + x;
                std::swap(m_pixels[a], m_pixels[b]);
            }
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
    }

    bool flipSelectionHorizontal()
    {
        const QRect sel = selectionRect();
        if (!sel.isValid()) {
            updateStatus(QStringLiteral("No selection to flip"));
            return false;
        }

        pushUndoSnapshot();

        for (int y = sel.top(); y <= sel.bottom(); ++y) {
            for (int x = 0; x < sel.width() / 2; ++x) {
                const int ax = sel.left() + x;
                const int bx = sel.right() - x;
                const int a = y * 256 + ax;
                const int b = y * 256 + bx;
                std::swap(m_pixels[a], m_pixels[b]);
            }
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Selection flipped horizontally"));
        return true;
    }

    bool flipSelectionVertical()
    {
        const QRect sel = selectionRect();
        if (!sel.isValid()) {
            updateStatus(QStringLiteral("No selection to flip"));
            return false;
        }

        pushUndoSnapshot();

        for (int y = 0; y < sel.height() / 2; ++y) {
            const int ay = sel.top() + y;
            const int by = sel.bottom() - y;
            for (int x = sel.left(); x <= sel.right(); ++x) {
                const int a = ay * 256 + x;
                const int b = by * 256 + x;
                std::swap(m_pixels[a], m_pixels[b]);
            }
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Selection flipped vertically"));
        return true;
    }

    bool rotateSelection90(bool clockwise)
    {
        const QRect sel = selectionRect();
        if (!sel.isValid()) {
            updateStatus(QStringLiteral("No selection to rotate"));
            return false;
        }

        const int w = sel.width();
        const int h = sel.height();
        const int newW = h;
        const int newH = w;

        if (sel.left() + newW > 256 || sel.top() + newH > 192) {
            updateStatus(QStringLiteral("Rotated selection would not fit at current position"));
            return false;
        }

        QVector<quint8> original(w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x)
                original[y * w + x] = static_cast<quint8>(pixelAt(sel.left() + x, sel.top() + y));
        }

        pushUndoSnapshot();

        for (int y = sel.top(); y <= sel.bottom(); ++y) {
            for (int x = sel.left(); x <= sel.right(); ++x)
                setPixel(x, y, 0);
        }

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int rx = 0;
                int ry = 0;

                if (clockwise) {
                    rx = h - 1 - y;
                    ry = x;
                } else {
                    rx = y;
                    ry = w - 1 - x;
                }

                setPixel(sel.left() + rx, sel.top() + ry, original[y * w + x]);
            }
        }

        m_selection = QRect(sel.topLeft(), QSize(newW, newH)).intersected(QRect(0, 0, 256, 192));
        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(clockwise ? QStringLiteral("Selection rotated right 90") : QStringLiteral("Selection rotated left 90"));
        return true;
    }

    bool nudgeSelection(int dx, int dy)
    {
        const QRect sel = selectionRect();
        if (!sel.isValid()) {
            updateStatus(QStringLiteral("No selection to nudge"));
            return false;
        }

        const QRect target = sel.translated(dx, dy);
        if (!QRect(0, 0, 256, 192).contains(target)) {
            updateStatus(QStringLiteral("Selection cannot move outside canvas"));
            return false;
        }

        QVector<quint8> original(sel.width() * sel.height());
        for (int y = 0; y < sel.height(); ++y) {
            for (int x = 0; x < sel.width(); ++x)
                original[y * sel.width() + x] = static_cast<quint8>(pixelAt(sel.left() + x, sel.top() + y));
        }

        pushUndoSnapshot();

        for (int y = sel.top(); y <= sel.bottom(); ++y) {
            for (int x = sel.left(); x <= sel.right(); ++x)
                setPixel(x, y, 0);
        }

        for (int y = 0; y < sel.height(); ++y) {
            for (int x = 0; x < sel.width(); ++x)
                setPixel(target.left() + x, target.top() + y, original[y * sel.width() + x]);
        }

        m_selection = target;
        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Selection nudged to %1,%2").arg(target.left()).arg(target.top()));
        return true;
    }

    int tmsConflictRowCount() const
    {
        int conflicts = 0;
        for (int tileY = 0; tileY < 24; ++tileY) {
            for (int tileX = 0; tileX < 32; ++tileX) {
                for (int row = 0; row < 8; ++row) {
                    bool seen[16] = { false };
                    int used = 0;
                    const int py = tileY * 8 + row;
                    for (int col = 0; col < 8; ++col) {
                        const int c = pixelAt(tileX * 8 + col, py);
                        if (!seen[c]) {
                            seen[c] = true;
                            ++used;
                        }
                    }
                    if (used > 2)
                        ++conflicts;
                }
            }
        }
        return conflicts;
    }

    bool selectTmsConflictFromIndex(int startIndex, bool wrap, const QString& label)
    {
        const int totalRows = 32 * 24 * 8;
        const int safeStart = qBound(0, startIndex, totalRows - 1);

        for (int pass = 0; pass < (wrap ? 2 : 1); ++pass) {
            const int begin = (pass == 0) ? safeStart : 0;
            const int end = (pass == 0) ? totalRows : safeStart;

            for (int index = begin; index < end; ++index) {
                const int tileY = index / (32 * 8);
                const int rem = index % (32 * 8);
                const int tileX = rem / 8;
                const int row = rem % 8;

                bool seen[16] = { false };
                int used = 0;
                const int py = tileY * 8 + row;

                for (int col = 0; col < 8; ++col) {
                    const int c = pixelAt(tileX * 8 + col, py);
                    if (!seen[c]) {
                        seen[c] = true;
                        ++used;
                    }
                }

                if (used > 2) {
                    m_selection = QRect(tileX * 8, py, 8, 1);
                    m_lastTmsConflictIndex = index;
                    update();
                    updateStatus(QStringLiteral("%1 TMS conflict: tile %2,%3 row %4 at Y=%5 (%6 colors)")
                                     .arg(label)
                                     .arg(tileX)
                                     .arg(tileY)
                                     .arg(row)
                                     .arg(py)
                                     .arg(used));
                    return true;
                }
            }
        }

        updateStatus(QStringLiteral("No TMS color conflicts found"));
        return false;
    }

    bool selectFirstTmsConflict()
    {
        return selectTmsConflictFromIndex(0, false, QStringLiteral("First"));
    }

    bool selectNextTmsConflict()
    {
        const int totalRows = 32 * 24 * 8;
        int startIndex = 0;

        if (m_selection.isValid()) {
            const int tileX = qBound(0, m_selection.left() / 8, 31);
            const int py = qBound(0, m_selection.top(), 191);
            const int tileY = py / 8;
            const int row = py % 8;
            startIndex = tileY * 32 * 8 + tileX * 8 + row + 1;
        } else if (m_lastTmsConflictIndex >= 0) {
            startIndex = m_lastTmsConflictIndex + 1;
        }

        if (startIndex >= totalRows)
            startIndex = 0;

        return selectTmsConflictFromIndex(startIndex, true, QStringLiteral("Next"));
    }

    bool reduceSelectedTmsRow()
    {
        if (!m_selection.isValid()) {
            updateStatus(QStringLiteral("No selected TMS row to reduce"));
            return false;
        }

        const int tileX = qBound(0, m_selection.left() / 8, 31);
        const int py = qBound(0, m_selection.top(), 191);
        const int tileY = py / 8;
        const int row = py % 8;

        bool seen[16] = { false };
        int used = 0;
        for (int col = 0; col < 8; ++col) {
            const int c = pixelAt(tileX * 8 + col, tileY * 8 + row);
            if (!seen[c]) {
                seen[c] = true;
                ++used;
            }
        }

        if (used <= 2) {
            updateStatus(QStringLiteral("Selected TMS row already uses %1 color(s)").arg(used));
            return false;
        }

        pushUndoSnapshot();

        const RowCodec codec = encodeTileRow(tileX, tileY, row);
        const int y = tileY * 8 + row;

        for (int col = 0; col < 8; ++col) {
            const bool bit = (codec.pattern & (0x80 >> col)) != 0;
            setPixel(tileX * 8 + col, y, bit ? codec.fg : codec.bg);
        }

        m_selection = QRect(tileX * 8, y, 8, 1);
        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Selected TMS row reduced to colors %1/%2").arg(codec.fg).arg(codec.bg));
        return true;
    }

    void reduceToTmsBitmapColors()
    {
        const int before = tmsConflictRowCount();
        if (before <= 0) {
            updateStatus(QStringLiteral("No TMS color conflicts to reduce"));
            return;
        }

        pushUndoSnapshot();

        for (int tileY = 0; tileY < 24; ++tileY) {
            for (int tileX = 0; tileX < 32; ++tileX) {
                for (int row = 0; row < 8; ++row) {
                    const RowCodec codec = encodeTileRow(tileX, tileY, row);
                    const int py = tileY * 8 + row;

                    for (int col = 0; col < 8; ++col) {
                        const bool bit = (codec.pattern & (0x80 >> col)) != 0;
                        setPixel(tileX * 8 + col, py, bit ? codec.fg : codec.bg);
                    }
                }
            }
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Reduced to TMS bitmap colors (%1 conflict rows fixed)").arg(before));
    }

    bool canUndo() const
    {
        return !m_undoStack.isEmpty();
    }

    bool canRedo() const
    {
        return !m_redoStack.isEmpty();
    }

    void undo()
    {
        if (m_undoStack.isEmpty())
            return;

        m_redoStack.append(m_pixels);
        m_pixels = m_undoStack.takeLast();
        update();
        notifyChanged();
        notifyUndoStateChanged();
    }

    void redo()
    {
        if (m_redoStack.isEmpty())
            return;

        m_undoStack.append(m_pixels);
        m_pixels = m_redoStack.takeLast();
        update();
        notifyChanged();
        notifyUndoStateChanged();
    }


    bool hasSelection() const
    {
        return selectionRect().isValid();
    }

    bool hasClipboardSelection() const
    {
        return !m_clipboardPixels.isEmpty() && m_clipboardSize.isValid();
    }

    void selectAll()
    {
        m_selection = QRect(0, 0, 256, 192);
        update();
        updateStatus(QStringLiteral("Selected full canvas 256x192"));
    }

    void clearSelection()
    {
        m_selection = QRect();
        update();
        updateStatus(QStringLiteral("Selection cleared"));
    }

    bool copySelection()
    {
        const QRect sel = selectionRect();
        if (!sel.isValid()) {
            updateStatus(QStringLiteral("No selection to copy"));
            return false;
        }

        m_clipboardSize = sel.size();
        m_clipboardPixels.resize(sel.width() * sel.height());
        for (int y = 0; y < sel.height(); ++y) {
            for (int x = 0; x < sel.width(); ++x)
                m_clipboardPixels[y * sel.width() + x] = static_cast<quint8>(pixelAt(sel.left() + x, sel.top() + y));
        }

        updateStatus(QStringLiteral("Copied selection %1x%2").arg(sel.width()).arg(sel.height()));
        return true;
    }

    bool cutSelection()
    {
        const QRect sel = selectionRect();
        if (!copySelection())
            return false;

        pushUndoSnapshot();
        for (int y = sel.top(); y <= sel.bottom(); ++y) {
            for (int x = sel.left(); x <= sel.right(); ++x)
                setPixel(x, y, 0);
        }

        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Cut selection %1x%2").arg(sel.width()).arg(sel.height()));
        return true;
    }

    bool armMoveSelection()
    {
        const QRect sel = selectionRect();
        if (!sel.isValid()) {
            updateStatus(QStringLiteral("No selection to move"));
            return false;
        }

        if (!copySelection())
            return false;

        pushUndoSnapshot();
        for (int y = sel.top(); y <= sel.bottom(); ++y) {
            for (int x = sel.left(); x <= sel.right(); ++x)
                setPixel(x, y, 0);
        }

        m_moveSelectionArmed = true;
        update();
        notifyChanged();
        notifyUndoStateChanged();
        setCursor(Qt::CrossCursor);
        updateStatus(QStringLiteral("Click canvas to move selection %1x%2").arg(sel.width()).arg(sel.height()));
        return true;
    }

    bool pasteSelection()
    {
        if (!hasClipboardSelection()) {
            updateStatus(QStringLiteral("No copied selection to paste"));
            return false;
        }

        QRect target = selectionRect();
        QPoint topLeft = target.isValid() ? target.topLeft() : QPoint(0, 0);
        topLeft.setX(qBound(0, topLeft.x(), 255));
        topLeft.setY(qBound(0, topLeft.y(), 191));

        pushUndoSnapshot();
        pasteClipboardAt(topLeft);
        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QStringLiteral("Pasted selection at %1,%2").arg(topLeft.x()).arg(topLeft.y()));
        return true;
    }

    bool makeStampFromSelection()
    {
        if (!copySelection())
            return false;

        updateStatus(QStringLiteral("Stamp captured %1x%2").arg(m_clipboardSize.width()).arg(m_clipboardSize.height()));
        return true;
    }

    void armStampPlacement()
    {
        if (!hasClipboardSelection()) {
            updateStatus(QStringLiteral("No stamp/selection to place"));
            return;
        }

        m_stampPlacementArmed = true;
        setCursor(Qt::CrossCursor);
        updateStatus(QStringLiteral("Click canvas to place stamp %1x%2").arg(m_clipboardSize.width()).arg(m_clipboardSize.height()));
    }

    QJsonObject stampToJson() const
    {
        QJsonObject obj;
        obj["type"] = QStringLiteral("ADAM_PLUS_CVPAINT_STAMP");
        obj["version"] = 1;
        obj["width"] = m_clipboardSize.width();
        obj["height"] = m_clipboardSize.height();

        QJsonArray pixels;
        for (quint8 px : m_clipboardPixels)
            pixels.append(static_cast<int>(px));
        obj["pixels"] = pixels;
        return obj;
    }

    bool stampFromJson(const QJsonObject& obj)
    {
        if (obj.value("type").toString() != QStringLiteral("ADAM_PLUS_CVPAINT_STAMP"))
            return false;

        const int w = obj.value("width").toInt();
        const int h = obj.value("height").toInt();
        const QJsonArray pixels = obj.value("pixels").toArray();

        if (w <= 0 || h <= 0 || w > 256 || h > 192 || pixels.size() != w * h)
            return false;

        QVector<quint8> loaded;
        loaded.reserve(pixels.size());
        for (const QJsonValue& v : pixels)
            loaded.append(static_cast<quint8>(qBound(0, v.toInt(), 15)));

        m_clipboardSize = QSize(w, h);
        m_clipboardPixels = loaded;
        updateStatus(QStringLiteral("Stamp loaded %1x%2").arg(w).arg(h));
        return true;
    }

    void pasteClipboardAt(const QPoint& topLeft)
    {
        const int w = m_clipboardSize.width();
        const int h = m_clipboardSize.height();
        for (int y = 0; y < h; ++y) {
            const int py = topLeft.y() + y;
            if (py < 0 || py >= 192)
                continue;
            for (int x = 0; x < w; ++x) {
                const int px = topLeft.x() + x;
                if (px < 0 || px >= 256)
                    continue;
                const quint8 sourcePixel = m_clipboardPixels[y * w + x];
                if (m_transparentPaste && sourcePixel == 0)
                    continue;
                setPixel(px, py, sourcePixel);
            }
        }

        m_selection = QRect(topLeft, QSize(qMin(w, 256 - topLeft.x()), qMin(h, 192 - topLeft.y())));
    }

    bool importImage(const QImage& sourceImage)
    {
        if (sourceImage.isNull())
            return false;

        pushUndoSnapshot();

        const QImage scaled = sourceImage.scaled(256, 192, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                      .convertToFormat(QImage::Format_ARGB32);

        QVector<quint8> newPixels(256 * 192);
        for (int y = 0; y < 192; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(scaled.constScanLine(y));
            for (int x = 0; x < 256; ++x) {
                const QColor c = QColor::fromRgb(line[x]);
                newPixels[y * 256 + x] = static_cast<quint8>(nearestColecoColor(c));
            }
        }

        m_pixels = newPixels;
        update();
        notifyChanged();
        notifyUndoStateChanged();
        return true;
    }

    bool importImageDithered(const QImage& sourceImage)
    {
        if (sourceImage.isNull())
            return false;

        pushUndoSnapshot();

        const QImage scaled = sourceImage.scaled(256, 192, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                      .convertToFormat(QImage::Format_ARGB32);

        QVector<quint8> newPixels(256 * 192);
        for (int y = 0; y < 192; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(scaled.constScanLine(y));
            for (int x = 0; x < 256; ++x) {
                QColor c = QColor::fromRgb(line[x]);

                // Very small ordered checker dither before nearest palette match.
                // This keeps it fast and deterministic, without pulling in extra dependencies.
                const int delta = ((x + y) & 1) ? 18 : -18;
                c.setRed(qBound(0, c.red() + delta, 255));
                c.setGreen(qBound(0, c.green() + delta, 255));
                c.setBlue(qBound(0, c.blue() + delta, 255));

                newPixels[y * 256 + x] = static_cast<quint8>(nearestColecoColor(c));
            }
        }

        m_pixels = newPixels;
        update();
        notifyChanged();
        notifyUndoStateChanged();
        return true;
    }

    bool importImageTmsConverted(const QImage& sourceImage)
    {
        if (sourceImage.isNull())
            return false;

        pushUndoSnapshot();

        const QImage scaled = sourceImage.scaled(256, 192, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                      .convertToFormat(QImage::Format_ARGB32);

        QVector<quint8> newPixels(256 * 192);

        // True TMS9918 Graphics II friendly conversion:
        // every 8-pixel horizontal row inside a tile may only use 2 colors.
        // For each 8-pixel row, find the best Coleco color pair and map all
        // pixels in that row to one of those two colors.
        for (int y = 0; y < 192; ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(scaled.constScanLine(y));

            for (int x0 = 0; x0 < 256; x0 += 8) {
                int dist[8][16];

                for (int x = 0; x < 8; ++x) {
                    QColor src = QColor::fromRgb(line[x0 + x]);

                    // Small ordered pre-adjustment. It is intentionally modest:
                    // enough to keep gems/details alive, but not so strong that
                    // the row pair chooser becomes noisy.
                    const int threshold = (((x0 + x) ^ y) & 1) ? 10 : -10;
                    src.setRed(qBound(0, src.red() + threshold, 255));
                    src.setGreen(qBound(0, src.green() + threshold, 255));
                    src.setBlue(qBound(0, src.blue() + threshold, 255));

                    for (int c = 0; c < 16; ++c) {
                        const QColor pal = colecoColor(c);
                        const int dr = src.red() - pal.red();
                        const int dg = src.green() - pal.green();
                        const int db = src.blue() - pal.blue();
                        dist[x][c] = dr * dr + dg * dg + db * db;
                    }
                }

                int bestA = 0;
                int bestB = 15;
                int bestScore = std::numeric_limits<int>::max();

                for (int a = 0; a < 16; ++a) {
                    for (int b = a; b < 16; ++b) {
                        int score = 0;
                        for (int x = 0; x < 8; ++x)
                            score += qMin(dist[x][a], dist[x][b]);

                        // Prefer black as one of the two colors when the score is very close.
                        // This keeps black backgrounds stable on title screens.
                        if (a != 0 && a != 1 && b != 0 && b != 1)
                            score += 80;

                        if (score < bestScore) {
                            bestScore = score;
                            bestA = a;
                            bestB = b;
                        }
                    }
                }

                // Put black in the background side if available; otherwise use the
                // most-used nearest color as background.
                int bg = bestA;
                int fg = bestB;
                if (bestB == 0 || bestB == 1) {
                    bg = bestB;
                    fg = bestA;
                } else if (bestA == 0 || bestA == 1) {
                    bg = bestA;
                    fg = bestB;
                }

                for (int x = 0; x < 8; ++x) {
                    const int chosen = (dist[x][fg] < dist[x][bg]) ? fg : bg;
                    newPixels[y * 256 + x0 + x] = static_cast<quint8>(chosen);
                }
            }
        }

        m_pixels = newPixels;
        update();
        notifyChanged();
        notifyUndoStateChanged();
        return true;
    }


    QImage toImage() const
    {
        QImage img(256, 192, QImage::Format_ARGB32);
        for (int y = 0; y < 192; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < 256; ++x) {
                const int idx = pixelAt(x, y);
                line[x] = colecoColor(idx).rgb();
            }
        }
        return img;
    }

    QJsonObject toJson() const
    {
        QJsonObject root;
        root[QStringLiteral("format")] = QStringLiteral("ADAMP_CVPAINT_STEP2");
        root[QStringLiteral("width")] = 256;
        root[QStringLiteral("height")] = 192;
        root[QStringLiteral("fgColor")] = m_colorIndex;

        QJsonArray rows;
        for (int y = 0; y < 192; ++y) {
            QString row;
            row.reserve(256);
            for (int x = 0; x < 256; ++x)
                row.append(QString::number(pixelAt(x, y), 16).toUpper());
            rows.append(row);
        }
        root[QStringLiteral("pixels")] = rows;
        return root;
    }

    bool fromJson(const QJsonObject& root)
    {
        if (root.value(QStringLiteral("width")).toInt() != 256 ||
            root.value(QStringLiteral("height")).toInt() != 192) {
            return false;
        }

        const QJsonArray rows = root.value(QStringLiteral("pixels")).toArray();
        if (rows.size() != 192)
            return false;

        QVector<quint8> newPixels(256 * 192);
        for (int y = 0; y < 192; ++y) {
            const QString row = rows.at(y).toString();
            if (row.size() < 256)
                return false;

            for (int x = 0; x < 256; ++x) {
                bool ok = false;
                const int c = row.mid(x, 1).toInt(&ok, 16);
                newPixels[y * 256 + x] = static_cast<quint8>(ok ? qBound(0, c, 15) : 0);
            }
        }

        m_pixels = newPixels;
        m_colorIndex = qBound(0, root.value(QStringLiteral("fgColor")).toInt(m_colorIndex), 15);
        m_undoStack.clear();
        m_redoStack.clear();
        update();
        notifyChanged();
        notifyUndoStateChanged();
        return true;
    }

    QByteArray bitmapPatternBytes() const
    {
        QByteArray bytes;
        bytes.reserve(32 * 24 * 8);

        for (int tileY = 0; tileY < 24; ++tileY) {
            for (int tileX = 0; tileX < 32; ++tileX) {
                for (int row = 0; row < 8; ++row) {
                    const RowCodec codec = encodeTileRow(tileX, tileY, row);
                    bytes.append(static_cast<char>(codec.pattern));
                }
            }
        }

        return bytes;
    }

    QByteArray bitmapColorBytes() const
    {
        QByteArray bytes;
        bytes.reserve(32 * 24 * 8);

        for (int tileY = 0; tileY < 24; ++tileY) {
            for (int tileX = 0; tileX < 32; ++tileX) {
                for (int row = 0; row < 8; ++row) {
                    const RowCodec codec = encodeTileRow(tileX, tileY, row);
                    bytes.append(static_cast<char>((codec.fg << 4) | codec.bg));
                }
            }
        }

        return bytes;
    }

    QByteArray bitmapNameTableBytes() const
    {
        QByteArray bytes;
        bytes.reserve(32 * 24);

        // TMS Graphics II bitmap mode needs a correct 768-byte name table.
        // 0..255 repeated 3 times maps the top/middle/bottom 64-line sections.
        for (int section = 0; section < 3; ++section) {
            for (int i = 0; i < 256; ++i)
                bytes.append(static_cast<char>(i));
        }

        return bytes;
    }

    QString exportCvBasicBitmapData(const QString& labelPrefix = QStringLiteral("PAINT"), bool addViewerRoutine = false) const
    {
        const QByteArray pattern = bitmapPatternBytes();
        const QByteArray color = bitmapColorBytes();

        QString out;
        QTextStream ts(&out);
        ts << "REM ADAM+ Paint Editor - ColecoVision/TMS bitmap export\n";
        ts << "REM 256x192, 32x24 tiles, 6144 bytes bitmap + 6144 bytes color\n";
        ts << "REM CVBasic DATA is emitted as DATA BYTE, compatible with DEFINE VRAM.\n";
        ts << "REM Each 8-pixel row is reduced to BG + one FG color, because TMS bitmap rows allow 2 colors.\n";
        ts << "REM TMS rows using more than 2 colors before conversion: " << tmsConflictRowCount() << "\n\n";

        if (addViewerRoutine) {
            ts << "REM --- Small viewer routine generated by ADAM+ Paint Editor ---\n";
            ts << "MODE 1\n";
            ts << "SCREEN DISABLE\n";
            ts << "DEFINE VRAM $0000,$1800," << labelPrefix << "_BITMAP\n";
            ts << "DEFINE VRAM $2000,$1800," << labelPrefix << "_COLOR\n";
            ts << "SCREEN ENABLE\n";
            ts << "WHILE 1: WEND\n\n";
        } else {
            ts << "REM Data only. Example viewer:\n";
            ts << "REM   MODE 1\n";
            ts << "REM   SCREEN DISABLE\n";
            ts << "REM   DEFINE VRAM $0000,$1800," << labelPrefix << "_BITMAP\n";
            ts << "REM   DEFINE VRAM $2000,$1800," << labelPrefix << "_COLOR\n";
            ts << "REM   SCREEN ENABLE\n";
            ts << "REM   WHILE 1: WEND\n\n";
        }

        appendByteData(ts, labelPrefix + QStringLiteral("_BITMAP"), pattern);
        ts << "\n";
        appendByteData(ts, labelPrefix + QStringLiteral("_COLOR"), color);
        return out;
    }

    QString exportCvBasicRawData(const QString& labelPrefix = QStringLiteral("PAINT")) const
    {
        QString out;
        QTextStream ts(&out);
        ts << "REM ADAM+ Paint Editor 256x192 raw color index data\n";
        ts << "REM 0..15 = ColecoVision palette index per pixel\n";
        ts << labelPrefix << "_WIDTH:\nDATA 256\n";
        ts << labelPrefix << "_HEIGHT:\nDATA 192\n";
        ts << labelPrefix << "_PIXELS:\n";

        for (int y = 0; y < 192; ++y) {
            ts << "DATA ";
            for (int x = 0; x < 256; ++x) {
                if (x > 0)
                    ts << ",";
                ts << pixelAt(x, y);
            }
            ts << "\n";
        }
        return out;
    }

    std::function<void(int)> onColorPicked;
    std::function<void()> onChanged;
    std::function<void(bool, bool)> onUndoStateChanged;
    std::function<void(const QString&)> onStatus;
    std::function<void(const QPoint&)> onContextMenuRequested;

protected:
    bool tmsRowHasConflict(int tileX, int tileY, int row) const
    {
        bool seen[16] = { false };
        int used = 0;
        const int py = tileY * 8 + row;

        for (int col = 0; col < 8; ++col) {
            const int c = pixelAt(tileX * 8 + col, py);
            if (!seen[c]) {
                seen[c] = true;
                ++used;
            }
        }

        return used > 2;
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor("#242424"));

        const QRect frame = canvasFrameRect();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(frame.adjusted(-8, -8, 8, 8), QColor("#222222"));
        p.fillRect(frame, QColor("#000000"));

        const qreal sx = static_cast<qreal>(frame.width()) / 256.0;
        const qreal sy = static_cast<qreal>(frame.height()) / 192.0;

        if (m_referenceVisible && !m_referenceImage.isNull() && m_referenceOpacity > 0) {
            p.save();
            p.setClipRect(frame);
            p.setOpacity(m_referenceOpacity / 100.0);
            p.setRenderHint(QPainter::SmoothPixmapTransform, false);

            const QRect refTarget(frame.left() + m_referenceOffset.x() * frame.width() / 256,
                                  frame.top() + m_referenceOffset.y() * frame.height() / 192,
                                  m_referenceImage.width() * frame.width() / 256,
                                  m_referenceImage.height() * frame.height() / 192);
            p.drawImage(refTarget, m_referenceImage);
            p.restore();
        }

        QImage img = toImage();
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);
        p.drawImage(frame, img);

        if (m_showTmsConflicts) {
            p.setPen(QPen(QColor(255, 60, 60, 190), qMax(1, m_zoomScale)));
            p.setBrush(QColor(255, 0, 0, 45));

            for (int tileY = 0; tileY < 24; ++tileY) {
                for (int tileX = 0; tileX < 32; ++tileX) {
                    for (int row = 0; row < 8; ++row) {
                        if (!tmsRowHasConflict(tileX, tileY, row))
                            continue;

                        const int px = frame.left() + qRound((tileX * 8) * sx);
                        const int py = frame.top() + qRound((tileY * 8 + row) * sy);
                        const int pw = qMax(1, qRound(8 * sx));
                        const int ph = qMax(1, qRound(1 * sy));
                        p.drawRect(QRect(px, py, pw, ph));
                    }
                }
            }
        }

        if (m_pixelGridVisible && m_zoomScale >= 3) {
            p.setPen(QColor(255, 255, 255, 22));
            for (int x = 1; x < 256; ++x) {
                const int px = frame.left() + qRound(x * sx);
                p.drawLine(px, frame.top(), px, frame.bottom());
            }
            for (int y = 1; y < 192; ++y) {
                const int py = frame.top() + qRound(y * sy);
                p.drawLine(frame.left(), py, frame.right(), py);
            }
        }

        if (m_gridVisible) {
            p.setPen(QColor(255, 220, 90, 120));
            for (int x = 8; x < 256; x += 8) {
                const int px = frame.left() + qRound(x * sx);
                p.drawLine(px, frame.top(), px, frame.bottom());
            }
            for (int y = 8; y < 192; y += 8) {
                const int py = frame.top() + qRound(y * sy);
                p.drawLine(frame.left(), py, frame.right(), py);
            }
        }

        if (m_shapeDragging) {
            const QRectF pr(frame.left() + m_shapePreviewRect.left() * sx,
                            frame.top() + m_shapePreviewRect.top() * sy,
                            m_shapePreviewRect.width() * sx,
                            m_shapePreviewRect.height() * sy);

            QPen shapePen(QColor("#FFFF00"));
            shapePen.setStyle(Qt::DashLine);
            shapePen.setWidth(2);
            p.setPen(shapePen);
            p.setBrush(Qt::NoBrush);

            if (m_toolMode == ToolMode::Line) {
                p.drawLine(frame.left() + m_shapeAnchor.x() * sx,
                           frame.top() + m_shapeAnchor.y() * sy,
                           frame.left() + m_shapeCurrent.x() * sx,
                           frame.top() + m_shapeCurrent.y() * sy);
            } else if (m_toolMode == ToolMode::Ellipse || m_toolMode == ToolMode::FilledEllipse) {
                p.drawEllipse(pr.adjusted(0, 0, -1, -1));
            } else {
                p.drawRect(pr.adjusted(0, 0, -1, -1));
            }
        }

        if (m_pendingTextArmed && !m_pendingTextMask.isNull()) {
            const QRect textRect = pendingTextRectAt(m_pendingTextPos);
            if (textRect.isValid()) {
                const QRectF tr(frame.left() + textRect.left() * sx,
                                frame.top() + textRect.top() * sy,
                                textRect.width() * sx,
                                textRect.height() * sy);

                p.fillRect(tr, QColor(255, 255, 255, 22));
                QPen textPen(QColor("#00E5FF"));
                textPen.setStyle(Qt::DashLine);
                textPen.setWidth(2);
                p.setPen(textPen);
                p.setBrush(Qt::NoBrush);
                p.drawRect(tr.adjusted(0, 0, -1, -1));

                p.setOpacity(0.55);
                p.setPen(colecoColor(m_pendingTextGradientColors.isEmpty() ? m_pendingTextColor : m_pendingTextGradientColors.first()));
                p.setFont(QFont(QStringLiteral("Arial"), qMax(8, qRound(10 * sx))));
                p.drawText(tr.topLeft() + QPointF(2, qMax(10.0, 12.0 * sy)), QStringLiteral("TEXT"));
                p.setOpacity(1.0);
            }
        }

        const QRect sel = selectionRect();
        if (sel.isValid()) {
            const QRectF sr(frame.left() + sel.left() * sx,
                            frame.top() + sel.top() * sy,
                            sel.width() * sx,
                            sel.height() * sy);
            p.fillRect(sr, QColor(255, 255, 255, 28));
            QPen pen(QColor("#FFFFFF"));
            pen.setStyle(Qt::DashLine);
            pen.setWidth(2);
            p.setPen(pen);
            p.drawRect(sr.adjusted(0, 0, -1, -1));
        }

        p.setPen(QColor("#444444"));
        p.drawRect(frame.adjusted(-1, -1, 1, 1));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::RightButton) {
            event->ignore();
            return;
        }

        handleMouse(event->position().toPoint(), event->button(), true);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event->buttons() & Qt::LeftButton)
            handleMouse(event->position().toPoint(), Qt::LeftButton, false);
        else
            updateHoverStatus(event->position().toPoint());
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_pendingTextArmed && m_pendingTextDragging) {
            int x = 0;
            int y = 0;
            if (widgetToPixel(event->position().toPoint(), &x, &y))
                m_pendingTextPos = QPoint(x, y);

            pushUndoSnapshot();
            const int written = placePendingTextAt(m_pendingTextPos);

            m_pendingTextArmed = false;
            m_pendingTextDragging = false;
            m_pendingTextMask = QImage();

            update();
            notifyChanged();
            notifyUndoStateChanged();
            updateStatus(QStringLiteral("Text placed at %1,%2 (%3 pixels)").arg(m_pendingTextPos.x()).arg(m_pendingTextPos.y()).arg(written));
            return;
        }

        if (m_shapeDragging) {
            const int drawColor = m_colorIndex;
            pushUndoSnapshot();

            if (m_toolMode == ToolMode::Line)
                drawLinePixels(m_shapeAnchor, m_shapeCurrent, drawColor);
            else if (m_toolMode == ToolMode::Ellipse || m_toolMode == ToolMode::FilledEllipse)
                drawEllipsePixels(m_shapePreviewRect, drawColor, m_toolMode == ToolMode::FilledEllipse);
            else
                drawRectPixels(m_shapePreviewRect, drawColor, m_toolMode == ToolMode::FilledRect);

            m_shapeDragging = false;
            update();
            notifyChanged();
            notifyUndoStateChanged();
            updateStatus(QStringLiteral("Shape drawn"));
        }

        m_strokeUndoCaptured = false;
        m_selecting = false;
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        if (onContextMenuRequested) {
            onContextMenuRequested(event->globalPos());
            event->accept();
            return;
        }

        QWidget::contextMenuEvent(event);
    }

private:
    RowCodec encodeTileRow(int tileX, int tileY, int row) const
    {
        int counts[16] = { 0 };
        const int py = tileY * 8 + row;

        for (int col = 0; col < 8; ++col) {
            const int c = pixelAt(tileX * 8 + col, py);
            counts[qBound(0, c, 15)]++;
        }

        int bg = 0;
        for (int i = 1; i < 16; ++i) {
            if (counts[i] > counts[bg])
                bg = i;
        }

        int fg = (bg == 0) ? 15 : 0;
        for (int i = 0; i < 16; ++i) {
            if (i == bg)
                continue;
            if (counts[i] > counts[fg])
                fg = i;
        }

        if (counts[fg] == 0)
            fg = (bg == 15) ? 0 : 15;

        RowCodec codec;
        codec.fg = qBound(0, fg, 15);
        codec.bg = qBound(0, bg, 15);

        for (int col = 0; col < 8; ++col) {
            const int c = pixelAt(tileX * 8 + col, py);

            // TMS bitmap rows only allow 2 colors.
            // Preserve the shape first:
            //   BG = most used color in this 8-pixel row
            //   FG = most used non-BG color
            //   every non-BG pixel becomes FG
            //
            // This is much better for imported pictures with many tiny colors:
            // details keep their silhouette instead of disappearing into BG.
            if (c != codec.bg)
                codec.pattern |= static_cast<quint8>(0x80 >> col);
        }

        return codec;
    }

    static void appendByteData(QTextStream& ts, const QString& label, const QByteArray& bytes)
    {
        ts << label << ":\n";
        for (int i = 0; i < bytes.size(); i += 16) {
            ts << "\tDATA BYTE ";
            const int count = qMin(16, bytes.size() - i);
            for (int j = 0; j < count; ++j) {
                if (j > 0)
                    ts << ",";
                const int value = static_cast<unsigned char>(bytes.at(i + j));
                ts << "$" << QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
            }
            ts << "\n";
        }
    }

    QRect canvasFrameRect() const
    {
        const QSize preferred(256 * m_zoomScale, 192 * m_zoomScale);
        QSize s = preferred;
        const QSize maxSize(qMax(64, width() - 30), qMax(48, height() - 30));
        if (s.width() > maxSize.width() || s.height() > maxSize.height())
            s.scale(maxSize, Qt::KeepAspectRatio);

        return QRect(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
    }

    bool widgetToPixel(const QPoint& pos, int* outX, int* outY) const
    {
        const QRect r = canvasFrameRect();
        if (!r.contains(pos))
            return false;

        const int x = qBound(0, static_cast<int>((pos.x() - r.left()) * 256.0 / qMax(1, r.width())), 255);
        const int y = qBound(0, static_cast<int>((pos.y() - r.top()) * 192.0 / qMax(1, r.height())), 191);

        if (outX) *outX = x;
        if (outY) *outY = y;
        return true;
    }


    QRect makeSelectionRect(const QPoint& a, const QPoint& b) const
    {
        int left = qMin(a.x(), b.x());
        int top = qMin(a.y(), b.y());
        int right = qMax(a.x(), b.x());
        int bottom = qMax(a.y(), b.y());

        if (m_snapSelectionToTile) {
            left = (left / 8) * 8;
            top = (top / 8) * 8;
            right = qMin(255, ((right / 8) * 8) + 7);
            bottom = qMin(191, ((bottom / 8) * 8) + 7);
        }

        return QRect(left, top, right - left + 1, bottom - top + 1).intersected(QRect(0, 0, 256, 192));
    }

    void selectTileBlockAtPixel(const QPoint& pixel, int tileW, int tileH)
    {
        QPoint p = pixel;
        if (p.x() < 0 || p.y() < 0)
            p = QPoint(0, 0);

        const int left = (p.x() / 8) * 8;
        const int top = (p.y() / 8) * 8;
        m_selection = QRect(left, top, qMax(1, tileW) * 8, qMax(1, tileH) * 8).intersected(QRect(0, 0, 256, 192));
        update();
        updateStatus(QStringLiteral("Selected %1x%2 tile block at TILE=%3,%4")
                         .arg(tileW)
                         .arg(tileH)
                         .arg(left / 8)
                         .arg(top / 8));
    }

    QRect selectionRect() const
    {
        return m_selection.normalized().intersected(QRect(0, 0, 256, 192));
    }

    int pixelAt(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= 256 || y >= 192)
            return 0;
        return qBound(0, static_cast<int>(m_pixels[y * 256 + x]), 15);
    }

    void setPixel(int x, int y, int color)
    {
        if (x < 0 || y < 0 || x >= 256 || y >= 192)
            return;
        m_pixels[y * 256 + x] = static_cast<quint8>(qBound(0, color, 15));
    }

    QColor colecoColor(int idx) const
    {
        static const QColor pal[16] = {
            QColor("#000000"), QColor("#000000"), QColor("#21C842"), QColor("#5EDC78"),
            QColor("#5455ED"), QColor("#7D76FC"), QColor("#D4524D"), QColor("#42EBF5"),
            QColor("#FC5554"), QColor("#FF7978"), QColor("#D4C154"), QColor("#E6CE80"),
            QColor("#21B03B"), QColor("#C95BBA"), QColor("#CCCCCC"), QColor("#FFFFFF")
        };
        return pal[qBound(0, idx, 15)];
    }

    void drawLinePixels(const QPoint& a, const QPoint& b, int color)
    {
        int x0 = a.x();
        int y0 = a.y();
        const int x1 = b.x();
        const int y1 = b.y();

        const int dx = qAbs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -qAbs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;

        while (true) {
            setPixel(x0, y0, color);
            if (x0 == x1 && y0 == y1)
                break;

            const int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    void drawRectPixels(const QRect& r, int color, bool filled)
    {
        const QRect rr = r.normalized().intersected(QRect(0, 0, 256, 192));
        if (!rr.isValid())
            return;

        if (filled) {
            for (int y = rr.top(); y <= rr.bottom(); ++y) {
                for (int x = rr.left(); x <= rr.right(); ++x)
                    setPixel(x, y, color);
            }
            return;
        }

        for (int x = rr.left(); x <= rr.right(); ++x) {
            setPixel(x, rr.top(), color);
            setPixel(x, rr.bottom(), color);
        }
        for (int y = rr.top(); y <= rr.bottom(); ++y) {
            setPixel(rr.left(), y, color);
            setPixel(rr.right(), y, color);
        }
    }

    void drawEllipsePixels(const QRect& r, int color, bool filled)
    {
        const QRect rr = r.normalized().intersected(QRect(0, 0, 256, 192));
        if (!rr.isValid())
            return;

        const double cx = (rr.left() + rr.right()) / 2.0;
        const double cy = (rr.top() + rr.bottom()) / 2.0;
        const double rx = qMax(0.5, rr.width() / 2.0);
        const double ry = qMax(0.5, rr.height() / 2.0);

        for (int y = rr.top(); y <= rr.bottom(); ++y) {
            for (int x = rr.left(); x <= rr.right(); ++x) {
                const double nx = (x - cx) / rx;
                const double ny = (y - cy) / ry;
                const double v = nx * nx + ny * ny;

                if (filled) {
                    if (v <= 1.0)
                        setPixel(x, y, color);
                } else {
                    const double border = qMax(0.08, 1.8 / qMax(rx, ry));
                    if (qAbs(v - 1.0) <= border)
                        setPixel(x, y, color);
                }
            }
        }
    }

    void drawBrushAt(int cx, int cy, int color)
    {
        const int size = qBound(1, m_brushSize, 8);
        const int half = size / 2;
        const int startX = cx - half;
        const int startY = cy - half;

        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x)
                setPixel(startX + x, startY + y, color);
        }
    }

    void floodFill(int sx, int sy, int newColor)
    {
        const int oldColor = pixelAt(sx, sy);
        newColor = qBound(0, newColor, 15);
        if (oldColor == newColor)
            return;

        QVector<QPoint> stack;
        stack.reserve(4096);
        stack.append(QPoint(sx, sy));

        while (!stack.isEmpty()) {
            const QPoint pt = stack.takeLast();
            const int x = pt.x();
            const int y = pt.y();
            if (x < 0 || y < 0 || x >= 256 || y >= 192)
                continue;
            if (pixelAt(x, y) != oldColor)
                continue;

            setPixel(x, y, newColor);
            stack.append(QPoint(x + 1, y));
            stack.append(QPoint(x - 1, y));
            stack.append(QPoint(x, y + 1));
            stack.append(QPoint(x, y - 1));
        }
    }

    bool isCanvasEmpty() const
    {
        for (quint8 value : m_pixels) {
            if (value != 0)
                return false;
        }
        return true;
    }

    void pushUndoSnapshot()
    {
        if (!m_undoStack.isEmpty() && m_undoStack.last() == m_pixels)
            return;

        m_undoStack.append(m_pixels);
        while (m_undoStack.size() > 50)
            m_undoStack.removeFirst();

        m_redoStack.clear();
        notifyUndoStateChanged();
    }

    void ensureUndoSnapshotForStroke()
    {
        if (m_strokeUndoCaptured)
            return;

        pushUndoSnapshot();
        m_strokeUndoCaptured = true;
    }

    void notifyUndoStateChanged()
    {
        if (onUndoStateChanged)
            onUndoStateChanged(canUndo(), canRedo());
    }

    int placePendingTextAt(const QPoint& pos)
    {
        if (m_pendingTextMask.isNull())
            return 0;

        QVector<int> inkRowIndex(m_pendingTextMask.height(), -1);
        int inkRows = 0;

        for (int y = 0; y < m_pendingTextMask.height(); ++y) {
            bool hasInk = false;
            for (int x = 0; x < m_pendingTextMask.width(); ++x) {
                if (qAlpha(m_pendingTextMask.pixel(x, y)) >= 96) {
                    hasInk = true;
                    break;
                }
            }

            if (hasInk)
                inkRowIndex[y] = inkRows++;
        }

        if (inkRows <= 0)
            return 0;

        int written = 0;
        for (int y = 0; y < m_pendingTextMask.height(); ++y) {
            for (int x = 0; x < m_pendingTextMask.width(); ++x) {
                if (qAlpha(m_pendingTextMask.pixel(x, y)) < 96)
                    continue;

                const int px = pos.x() + x;
                const int py = pos.y() + y;
                if (px < 0 || py < 0 || px >= 256 || py >= 192)
                    continue;

                int drawColor = m_pendingTextColor;
                if (!m_pendingTextGradientColors.isEmpty()) {
                    if (m_pendingTextGradientColors.size() == 1 || inkRows == 1) {
                        drawColor = m_pendingTextGradientColors.first();
                    } else {
                        const int rowIdx = qBound(0, inkRowIndex.value(y, 0), inkRows - 1);
                        const int colorCount = m_pendingTextGradientColors.size();
                        const int idx = qBound(0,
                                               (rowIdx * colorCount) / inkRows,
                                               colorCount - 1);
                        drawColor = m_pendingTextGradientColors.at(idx);
                    }
                }

                setPixel(px, py, drawColor);
                ++written;
            }
        }

        return written;
    }

    QRect pendingTextRectAt(const QPoint& pos) const
    {
        if (m_pendingTextMask.isNull())
            return QRect();

        return QRect(pos, QSize(qMin(m_pendingTextMask.width(), 256 - pos.x()),
                                qMin(m_pendingTextMask.height(), 192 - pos.y())))
            .intersected(QRect(0, 0, 256, 192));
    }

    int nearestColecoColor(const QColor& color) const
    {
        if (color.alpha() < 16)
            return 0;

        int bestIndex = 0;
        int bestDistance = std::numeric_limits<int>::max();
        for (int i = 0; i < 16; ++i) {
            const QColor pal = colecoColor(i);
            const int dr = color.red() - pal.red();
            const int dg = color.green() - pal.green();
            const int db = color.blue() - pal.blue();
            const int distance = dr * dr + dg * dg + db * db;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestIndex = i;
            }
        }
        return bestIndex;
    }

    void handleMouse(const QPoint& pos, Qt::MouseButton button, bool firstPress)
    {
        int x = 0;
        int y = 0;
        if (!widgetToPixel(pos, &x, &y))
            return;

        m_lastCanvasPixel = QPoint(x, y);

        if (m_pendingTextArmed) {
            m_pendingTextPos = QPoint(x, y);
            m_pendingTextDragging = true;
            update();
            updateStatus(QStringLiteral("Text position %1,%2 - release mouse to place").arg(x).arg(y));
            return;
        }

        if (firstPress && m_pendingTileSelection.isValid()) {
            const QSize block = m_pendingTileSelection;
            m_pendingTileSelection = QSize();
            selectTileBlockAtPixel(QPoint(x, y), block.width(), block.height());
            return;
        }

        if (firstPress && m_stampPlacementArmed) {
            m_stampPlacementArmed = false;
            pushUndoSnapshot();
            pasteClipboardAt(QPoint(x, y));
            update();
            notifyChanged();
            notifyUndoStateChanged();
            updateStatus(QStringLiteral("Stamp placed at %1,%2").arg(x).arg(y));
            return;
        }

        if (firstPress && m_moveSelectionArmed) {
            m_moveSelectionArmed = false;
            pasteClipboardAt(QPoint(x, y));
            update();
            notifyChanged();
            notifyUndoStateChanged();
            updateStatus(QStringLiteral("Selection moved to %1,%2").arg(x).arg(y));
            return;
        }

        if (m_toolMode == ToolMode::Line || m_toolMode == ToolMode::Rect || m_toolMode == ToolMode::FilledRect || m_toolMode == ToolMode::Ellipse || m_toolMode == ToolMode::FilledEllipse) {
            if (firstPress) {
                m_shapeDragging = true;
                m_shapeAnchor = QPoint(x, y);
            }

            m_shapeCurrent = QPoint(x, y);
            m_shapePreviewRect = makeSelectionRect(m_shapeAnchor, m_shapeCurrent);
            update();
            updateStatus(QStringLiteral("Shape %1,%2  %3x%4")
                             .arg(m_shapePreviewRect.left())
                             .arg(m_shapePreviewRect.top())
                             .arg(m_shapePreviewRect.width())
                             .arg(m_shapePreviewRect.height()));
            return;
        }

        if (m_toolMode == ToolMode::Select) {
            if (firstPress) {
                m_selecting = true;
                m_selectionAnchor = QPoint(x, y);
            }

            if (m_selecting) {
                m_selection = makeSelectionRect(m_selectionAnchor, QPoint(x, y));
                update();
                updateStatus(QString("Selection %1,%2  %3x%4")
                                 .arg(m_selection.left())
                                 .arg(m_selection.top())
                                 .arg(m_selection.width())
                                 .arg(m_selection.height()));
            }
            return;
        }

        if (m_toolMode == ToolMode::Pipette) {
            if (firstPress) {
                m_colorIndex = pixelAt(x, y);
                if (onColorPicked)
                    onColorPicked(m_colorIndex);
                updateStatus(QString("Picked color %1 at %2,%3").arg(m_colorIndex).arg(x).arg(y));
            }
            return;
        }

        int drawColor = m_colorIndex;
        if (m_toolMode == ToolMode::Eraser)
            drawColor = m_backgroundColorIndex;

        if (m_toolMode == ToolMode::Fill) {
            if (firstPress && pixelAt(x, y) != drawColor) {
                ensureUndoSnapshotForStroke();
                floodFill(x, y, drawColor);
                update();
                notifyChanged();
                notifyUndoStateChanged();
                updateStatus(QString("Fill color %1 at %2,%3").arg(drawColor).arg(x).arg(y));
            }
            return;
        }

        ensureUndoSnapshotForStroke();
        drawBrushAt(x, y, drawColor);
        update();
        notifyChanged();
        notifyUndoStateChanged();
        updateStatus(QString("X=%1 Y=%2 COLOR=%3 BRUSH=%4").arg(x).arg(y).arg(drawColor).arg(m_brushSize));
    }

    void updateHoverStatus(const QPoint& pos)
    {
        int x = 0;
        int y = 0;
        if (widgetToPixel(pos, &x, &y)) {
            m_lastCanvasPixel = QPoint(x, y);
            updateStatus(QString("X=%1 Y=%2 TILE=%3,%4 COLOR=%5").arg(x).arg(y).arg(x / 8).arg(y / 8).arg(pixelAt(x, y)));
        }
    }

    void notifyChanged()
    {
        if (onChanged)
            onChanged();
    }

    void updateStatus(const QString& text)
    {
        if (onStatus)
            onStatus(text);
    }

private:
    QVector<quint8> m_pixels;
    QVector<QVector<quint8>> m_undoStack;
    QVector<QVector<quint8>> m_redoStack;
    int m_colorIndex = 15;
    int m_backgroundColorIndex = 0;
    int m_brushSize = 1;
    bool m_transparentPaste = true;
    int m_zoomScale = 2;
    int m_referenceOpacity = 45;
    bool m_gridVisible = true;
    bool m_pixelGridVisible = false;
    bool m_showTmsConflicts = false;
    bool m_referenceVisible = true;
    bool m_snapSelectionToTile = false;
    int m_lastTmsConflictIndex = -1;
    QImage m_referenceImage;
    QPoint m_referenceOffset;
    QPoint m_lastCanvasPixel = QPoint(-1, -1);
    QSize m_pendingTileSelection;
    bool m_stampPlacementArmed = false;
    bool m_moveSelectionArmed = false;
    bool m_pendingTextArmed = false;
    bool m_pendingTextDragging = false;
    QPoint m_pendingTextPos = QPoint(0, 0);
    QImage m_pendingTextMask;
    int m_pendingTextColor = 15;
    QVector<int> m_pendingTextGradientColors;
    bool m_shapeDragging = false;
    QPoint m_shapeAnchor;
    QPoint m_shapeCurrent;
    QRect m_shapePreviewRect;
    bool m_strokeUndoCaptured = false;
    bool m_selecting = false;
    QPoint m_selectionAnchor;
    QRect m_selection;
    QVector<quint8> m_clipboardPixels;
    QSize m_clipboardSize;
    ToolMode m_toolMode = ToolMode::Pen;
};

class CvBasicPaintEditorPage final : public QWidget
{
public:
    explicit CvBasicPaintEditorPage(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setupUi();
    }

    std::function<void(const QString&)> onInsertRequested;
    std::function<void(const QString&)> onStatusRequested;
    std::function<void(const QString&)> onTitleChanged;

    void refreshPaintProjectTitle()
    {
        updatePaintProjectStatus();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event && event->type() == QEvent::Wheel && m_canvas) {
            const bool fromPaintArea =
                watched == m_canvas ||
                watched == m_canvasScroll ||
                (m_canvasScroll && watched == m_canvasScroll->viewport());

            if (fromPaintArea) {
                QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
                const int delta = wheelEvent->angleDelta().y();
                if (delta != 0) {
                    const int oldZoom = m_canvas->zoomScale();
                    const int newZoom = qBound(1, oldZoom + (delta > 0 ? 1 : -1), 4);

                    if (newZoom != oldZoom)
                        setPaintZoom(newZoom);

                    wheelEvent->accept();
                    return true;
                }
            }
        }

        return QWidget::eventFilter(watched, event);
    }

private:
    QColor colecoColor(int idx) const
    {
        static const QColor pal[16] = {
            QColor("#000000"), QColor("#000000"), QColor("#21C842"), QColor("#5EDC78"),
            QColor("#5455ED"), QColor("#7D76FC"), QColor("#D4524D"), QColor("#42EBF5"),
            QColor("#FC5554"), QColor("#FF7978"), QColor("#D4C154"), QColor("#E6CE80"),
            QColor("#21B03B"), QColor("#C95BBA"), QColor("#CCCCCC"), QColor("#FFFFFF")
        };
        return pal[qBound(0, idx, 15)];
    }

    QPushButton* makeToolButton(const QString& text, const QString& tooltip)
    {
        QPushButton* b = new QPushButton(text, this);
        b->setCheckable(true);
        b->setToolTip(tooltip);
        b->setMinimumHeight(28);
        return b;
    }

    QPushButton* makeSideButton(const QString& text, const QString& tooltip = QString())
    {
        QPushButton* b = new QPushButton(text, this);
        b->setMinimumHeight(28);
        b->setToolTip(tooltip);
        return b;
    }

    QGroupBox* makeSideGroup(const QString& title, QVBoxLayout** outLayout)
    {
        QGroupBox* box = new QGroupBox(title, this);
        QVBoxLayout* layout = new QVBoxLayout(box);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(6);
        if (outLayout)
            *outLayout = layout;
        return box;
    }

    void setupUi()
    {
        setObjectName(QStringLiteral("cvBasicPaintEditorPage"));
        setStyleSheet(
            "#cvBasicPaintEditorPage { background-color:#3A3A3A; color:#FFFFFF; }"
            "QFrame#paintCard { background-color:#3A3A3A; border:1px solid #555555; border-radius:0px; }"
            "QLabel { background-color:#3A3A3A; color:#FFFFFF; }"
            "QGroupBox { background-color:#3A3A3A; color:#FFFFFF; border:1px solid #555555; margin-top:8px; font-weight:bold; }"
            "QGroupBox::title { subcontrol-origin: margin; left:8px; padding:0 4px; }"
            "QScrollArea#paintSidePanel { background-color:#303030; border:1px solid #555555; }"
            "QWidget#paintSidePanelContents { background-color:#303030; }"
            "QTabWidget#paintSideTabs::pane { background-color:#303030; border:1px solid #555555; }"
            "QTabWidget#paintSideTabs QTabBar::tab { background-color:#242424; color:#FFFFFF; border:1px solid #555555; padding:8px 5px; min-height:54px; min-width:28px; font-weight:normal; }"
            "QTabWidget#paintSideTabs QTabBar::tab:selected { background-color:#4A4A4A; }"
            "QTabWidget#paintSideTabs QTabBar::tab:hover { background-color:#3A3A3A; }"
            "QWidget#paintSidePanelContents QPushButton { font-weight:normal; }"
            "QToolBar#paintPageToolbar { background-color:#3A3A3A; border:1px solid #555555; spacing:4px; padding:4px; }"
            "QToolBar#paintEditToolbar { background-color:#3A3A3A; border:1px solid #555555; spacing:4px; padding:4px; }"
            "QToolButton { background-color:#242424; color:#FFFFFF; border:1px solid #5C5C5C; padding:4px 8px; }"
            "QToolButton:hover { background-color:#4A4A4A; }"
            "QToolButton:pressed { background-color:#5A5A5A; padding-top:5px; padding-left:9px; }"
            "QPushButton { background-color:#242424; color:#FFFFFF; border:1px solid #666666; padding:4px 8px; font-weight:bold; }"
            "QPushButton:hover { background-color:#4A4A4A; }"
            "QPushButton:checked { background-color:#555555; color:#FFFFFF; border:1px solid #888888; }"
            "QPushButton#dangerButton { color:#FF8A8A; }"
        );

        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(8, 8, 8, 8);
        root->setSpacing(6);

        QToolBar* paintToolbar = new QToolBar(tr("Paint"), this);
        paintToolbar->setObjectName(QStringLiteral("paintPageToolbar"));
        paintToolbar->setMovable(false);
        paintToolbar->setFloatable(false);
        paintToolbar->setIconSize(QSize(20, 20));

        QAction* actNewPaint = paintToolbar->addAction(tr("New"));
        QAction* actOpenPaint = paintToolbar->addAction(tr("Open"));
        QAction* actSavePaint = paintToolbar->addAction(tr("Save"));
        QAction* actSaveAsPaint = paintToolbar->addAction(tr("Save As"));
        QAction* actRevertPaint = paintToolbar->addAction(tr("Revert"));
        QAction* actImportPng = paintToolbar->addAction(tr("Import PNG"));
        QAction* actImportPngDither = paintToolbar->addAction(tr("Import Dither"));
        QAction* actImportPngTms = paintToolbar->addAction(tr("Import TMS"));
        actImportPngTms->setToolTip(tr("Import image using TMS/Coleco 2-colors-per-8-pixel-row conversion"));
        paintToolbar->addSeparator();
        QAction* actInsertData = paintToolbar->addAction(tr("Insert Bitmap DATA"));
        paintToolbar->addSeparator();
        QAction* actClearCanvas = paintToolbar->addAction(tr("Clear"));

        QWidget* paintToolbarSpacer = new QWidget(this);
        paintToolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        paintToolbar->addWidget(paintToolbarSpacer);

        QAction* actUndoPaint = paintToolbar->addAction(tr("Undo"));
        QAction* actRedoPaint = paintToolbar->addAction(tr("Redo"));
        actUndoPaint->setEnabled(false);
        actRedoPaint->setEnabled(false);
        m_undoAction = actUndoPaint;
        m_redoAction = actRedoPaint;
        root->addWidget(paintToolbar);

        QFrame* card = new QFrame(this);
        card->setObjectName(QStringLiteral("paintCard"));
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(10, 10, 10, 10);
        cardLayout->setSpacing(8);
        root->addWidget(card, 1);

        QHBoxLayout* workLayout = new QHBoxLayout();
        workLayout->setSpacing(8);

        QWidget* paletteWidget = new QWidget(this);
        QVBoxLayout* paletteLayout = new QVBoxLayout(paletteWidget);
        paletteLayout->setContentsMargins(0, 0, 0, 0);
        paletteLayout->setSpacing(3);
        QLabel* palLabel = new QLabel(tr("PAL"), this);
        palLabel->setAlignment(Qt::AlignCenter);
        paletteLayout->addWidget(palLabel);

        for (int i = 0; i < 16; ++i) {
            QPushButton* sw = new QPushButton(QString::number(i), this);
            sw->setFixedSize(34, 24);
            sw->setToolTip(tr("Color %1").arg(i));
            sw->setCheckable(true);

            const QColor color = colecoColor(i);
            const int luma = (color.red() * 299 + color.green() * 587 + color.blue() * 114) / 1000;
            const QString textColor = (luma < 128) ? QStringLiteral("#FFFFFF") : QStringLiteral("#000000");

            sw->setStyleSheet(QString("QPushButton { background-color: %1; color: %2; border: 1px solid #333; border-radius: 3px; font-weight: bold; } QPushButton:checked { border: 3px solid #FFFFFF; }")
                                  .arg(color.name(), textColor));
            if (i == 15)
                sw->setChecked(true);
            m_paletteButtons.append(sw);
            paletteLayout->addWidget(sw);

            connect(sw, &QPushButton::clicked, this, [this, i]() { setSelectedColor(i); });
            sw->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(sw, &QPushButton::customContextMenuRequested, this, [this, i](const QPoint&) { setBackgroundColor(i); });
        }

        paletteLayout->addSpacing(10);

        QWidget* fgBgWidget = new QWidget(this);
        QHBoxLayout* fgBgLayout = new QHBoxLayout(fgBgWidget);
        fgBgLayout->setContentsMargins(0, 0, 0, 0);
        fgBgLayout->setSpacing(6);

        m_selectedColorLabel = new QLabel(tr("FG 15"), this);
        m_selectedColorLabel->setAlignment(Qt::AlignCenter);
        m_selectedColorLabel->setFixedSize(42, 24);
        m_selectedColorLabel->setStyleSheet(QStringLiteral("QLabel { background-color:#202020; color:#FFFFFF; border:1px solid #333333; border-radius:3px; font-weight:bold; }"));
        fgBgLayout->addWidget(m_selectedColorLabel);

        m_backgroundColorLabel = new QLabel(tr("BG 0"), this);
        m_backgroundColorLabel->setAlignment(Qt::AlignCenter);
        m_backgroundColorLabel->setFixedSize(42, 24);
        m_backgroundColorLabel->setStyleSheet(QStringLiteral("QLabel { background-color:#000000; color:#FFFFFF; border:1px solid #333333; border-radius:3px; font-weight:bold; }"));
        fgBgLayout->addWidget(m_backgroundColorLabel);

        paletteLayout->addWidget(fgBgWidget, 0, Qt::AlignHCenter);

        QPushButton* swapFgBgBtn = new QPushButton(tr("Swap"), this);
        swapFgBgBtn->setToolTip(tr("Swap FG and BG colors"));
        swapFgBgBtn->setMinimumHeight(24);
        swapFgBgBtn->setStyleSheet(QStringLiteral("QPushButton { background:#2E2E2E; color:#FFFFFF; border:1px solid #555555; border-radius:3px; font-weight:bold; } QPushButton:hover { background:#3A3A3A; }"));
        paletteLayout->addWidget(swapFgBgBtn);
        connect(swapFgBgBtn, &QPushButton::clicked, this, [this]() { swapForegroundBackgroundColors(); });

        QLabel* bgHelpLabel = new QLabel(tr("Right click palette = BG"), this);
        bgHelpLabel->setAlignment(Qt::AlignCenter);
        bgHelpLabel->setWordWrap(true);
        bgHelpLabel->setStyleSheet(QStringLiteral("QLabel { color:#BBBBBB; font-size:10px; }"));
        paletteLayout->addWidget(bgHelpLabel);

        paletteLayout->addStretch(1);
        workLayout->addWidget(paletteWidget, 0);

        m_canvas = new CvBasicPaintCanvas(this);

        m_canvasScroll = new QScrollArea(this);
        m_canvasScroll->setWidget(m_canvas);
        m_canvasScroll->setWidgetResizable(false);
        m_canvasScroll->setFrameShape(QFrame::NoFrame);
        m_canvasScroll->setAlignment(Qt::AlignCenter);
        m_canvasScroll->setStyleSheet(QStringLiteral("QScrollArea { background-color:#242424; border:1px solid #555555; } QScrollBar { background:#2F2F2F; }"));

        m_canvas->installEventFilter(this);
        m_canvasScroll->installEventFilter(this);
        m_canvasScroll->viewport()->installEventFilter(this);

        workLayout->addWidget(m_canvasScroll, 1);

        QTabWidget* sideTabs = new QTabWidget(this);
        sideTabs->setObjectName(QStringLiteral("paintSideTabs"));
        sideTabs->setTabPosition(QTabWidget::East);
        sideTabs->setDocumentMode(false);
        sideTabs->setUsesScrollButtons(true);
        sideTabs->setMinimumWidth(320);
        sideTabs->setMaximumWidth(350);

        QWidget* tabTools = new QWidget(sideTabs);
        tabTools->setObjectName(QStringLiteral("paintSidePanelContents"));
        QVBoxLayout* tabToolsLayout = new QVBoxLayout(tabTools);
        tabToolsLayout->setContentsMargins(8, 8, 8, 8);
        tabToolsLayout->setSpacing(8);

        QWidget* tabReference = new QWidget(sideTabs);
        tabReference->setObjectName(QStringLiteral("paintSidePanelContents"));
        QVBoxLayout* tabReferenceLayout = new QVBoxLayout(tabReference);
        tabReferenceLayout->setContentsMargins(8, 8, 8, 8);
        tabReferenceLayout->setSpacing(8);

        QWidget* tabTransform = new QWidget(sideTabs);
        tabTransform->setObjectName(QStringLiteral("paintSidePanelContents"));
        QVBoxLayout* tabTransformLayout = new QVBoxLayout(tabTransform);
        tabTransformLayout->setContentsMargins(8, 8, 8, 8);
        tabTransformLayout->setSpacing(8);

        QWidget* tabTile = new QWidget(sideTabs);
        tabTile->setObjectName(QStringLiteral("paintSidePanelContents"));
        QVBoxLayout* tabTileLayout = new QVBoxLayout(tabTile);
        tabTileLayout->setContentsMargins(8, 8, 8, 8);
        tabTileLayout->setSpacing(8);

        QWidget* tabShape = new QWidget(sideTabs);
        tabShape->setObjectName(QStringLiteral("paintSidePanelContents"));
        QVBoxLayout* tabShapeLayout = new QVBoxLayout(tabShape);
        tabShapeLayout->setContentsMargins(8, 8, 8, 8);
        tabShapeLayout->setSpacing(8);

        QWidget* tabColor = new QWidget(sideTabs);
        tabColor->setObjectName(QStringLiteral("paintSidePanelContents"));
        QVBoxLayout* tabColorLayout = new QVBoxLayout(tabColor);
        tabColorLayout->setContentsMargins(8, 8, 8, 8);
        tabColorLayout->setSpacing(8);

        QWidget* tabStamp = new QWidget(sideTabs);
        tabStamp->setObjectName(QStringLiteral("paintSidePanelContents"));
        QVBoxLayout* tabStampLayout = new QVBoxLayout(tabStamp);
        tabStampLayout->setContentsMargins(8, 8, 8, 8);
        tabStampLayout->setSpacing(8);

        QWidget* tabExport = new QWidget(sideTabs);
        tabExport->setObjectName(QStringLiteral("paintSidePanelContents"));
        QVBoxLayout* tabExportLayout = new QVBoxLayout(tabExport);
        tabExportLayout->setContentsMargins(8, 8, 8, 8);
        tabExportLayout->setSpacing(8);

        QVBoxLayout* toolsGroupLayout = nullptr;
        QGroupBox* toolsGroup = makeSideGroup(tr("Tools"), &toolsGroupLayout);
        QPushButton* penBtn = makeToolButton(tr("Pen"), tr("Draw with selected color"));
        QPushButton* eraserBtn = makeToolButton(tr("Eraser"), tr("Erase to transparent/black color 0"));
        QPushButton* fillBtn = makeToolButton(tr("Fill"), tr("Flood fill"));
        QPushButton* pipetteBtn = makeToolButton(tr("Pipette"), tr("Pick color from canvas"));
        QPushButton* selectBtn = makeToolButton(tr("Select"), tr("Select a block for copy/cut/paste"));
        m_toolButtons = { penBtn, eraserBtn, fillBtn, pipetteBtn, selectBtn };
        penBtn->setChecked(true);

        QGridLayout* toolsGrid = new QGridLayout();
        toolsGrid->setSpacing(6);
        toolsGrid->addWidget(penBtn, 0, 0);
        toolsGrid->addWidget(eraserBtn, 0, 1);
        toolsGrid->addWidget(fillBtn, 1, 0);
        toolsGrid->addWidget(pipetteBtn, 1, 1);
        toolsGrid->addWidget(selectBtn, 2, 0, 1, 2);
        toolsGroupLayout->addLayout(toolsGrid);

        QLabel* brushLabel = new QLabel(tr("Brush Size"), this);
        toolsGroupLayout->addWidget(brushLabel);

        QHBoxLayout* brushSizeLayout = new QHBoxLayout();
        brushSizeLayout->setSpacing(4);
        QPushButton* brush1Btn = makeSideButton(tr("1"), tr("Brush size 1 pixel"));
        QPushButton* brush2Btn = makeSideButton(tr("2"), tr("Brush size 2 pixels"));
        QPushButton* brush4Btn = makeSideButton(tr("4"), tr("Brush size 4 pixels"));
        QPushButton* brush8Btn = makeSideButton(tr("8"), tr("Brush size 8 pixels"));
        m_brushSizeButtons = { brush1Btn, brush2Btn, brush4Btn, brush8Btn };
        for (QPushButton* b : std::as_const(m_brushSizeButtons))
            b->setCheckable(true);
        brush1Btn->setChecked(true);
        brushSizeLayout->addWidget(brush1Btn);
        brushSizeLayout->addWidget(brush2Btn);
        brushSizeLayout->addWidget(brush4Btn);
        brushSizeLayout->addWidget(brush8Btn);
        toolsGroupLayout->addLayout(brushSizeLayout);

        tabToolsLayout->addWidget(toolsGroup);

        QVBoxLayout* viewGroupLayout = nullptr;
        QGroupBox* viewGroup = makeSideGroup(tr("View"), &viewGroupLayout);
        QPushButton* gridBtn = makeSideButton(tr("Tile Grid"), tr("Show/hide 8x8 tile grid"));
        gridBtn->setCheckable(true);
        gridBtn->setChecked(true);

        QPushButton* pixelGridBtn = makeSideButton(tr("Pixel Grid"), tr("Show/hide 1x1 pixel grid, visible from zoom 3x"));
        pixelGridBtn->setCheckable(true);
        pixelGridBtn->setChecked(false);

        QHBoxLayout* zoomLayout = new QHBoxLayout();
        zoomLayout->setSpacing(4);
        QPushButton* zoom1Btn = makeSideButton(tr("1x"), tr("Canvas zoom 1x"));
        QPushButton* zoom2Btn = makeSideButton(tr("2x"), tr("Canvas zoom 2x"));
        QPushButton* zoom3Btn = makeSideButton(tr("3x"), tr("Canvas zoom 3x"));
        QPushButton* zoom4Btn = makeSideButton(tr("4x"), tr("Canvas zoom 4x"));
        m_zoomButtons = { zoom1Btn, zoom2Btn, zoom3Btn, zoom4Btn };
        for (QPushButton* b : std::as_const(m_zoomButtons))
            b->setCheckable(true);
        zoom2Btn->setChecked(true);
        zoomLayout->addWidget(zoom1Btn);
        zoomLayout->addWidget(zoom2Btn);
        zoomLayout->addWidget(zoom3Btn);
        zoomLayout->addWidget(zoom4Btn);

        viewGroupLayout->addWidget(gridBtn);
        viewGroupLayout->addWidget(pixelGridBtn);
        viewGroupLayout->addLayout(zoomLayout);
        tabToolsLayout->addWidget(viewGroup);

        QVBoxLayout* referenceGroupLayout = nullptr;
        QGroupBox* referenceGroup = makeSideGroup(tr("Reference"), &referenceGroupLayout);
        QPushButton* refVisibleBtn = makeSideButton(tr("Show Reference"), tr("Show/hide reference image"));
        refVisibleBtn->setCheckable(true);
        refVisibleBtn->setChecked(true);

        QSlider* refOpacitySlider = new QSlider(Qt::Horizontal, this);
        refOpacitySlider->setRange(0, 100);
        refOpacitySlider->setValue(45);
        refOpacitySlider->setToolTip(tr("Reference image opacity"));

        QGridLayout* refMoveGrid = new QGridLayout();
        refMoveGrid->setSpacing(4);
        QPushButton* refUpBtn = makeSideButton(tr("R↑"), tr("Move reference image up 1 pixel"));
        QPushButton* refDownBtn = makeSideButton(tr("R↓"), tr("Move reference image down 1 pixel"));
        QPushButton* refLeftBtn = makeSideButton(tr("R←"), tr("Move reference image left 1 pixel"));
        QPushButton* refRightBtn = makeSideButton(tr("R→"), tr("Move reference image right 1 pixel"));
        refMoveGrid->addWidget(refUpBtn, 0, 1);
        refMoveGrid->addWidget(refLeftBtn, 1, 0);
        refMoveGrid->addWidget(refRightBtn, 1, 2);
        refMoveGrid->addWidget(refDownBtn, 2, 1);

        QPushButton* loadRefBtn = makeSideButton(tr("Load Ref"), tr("Load reference image"));
        QPushButton* clearRefBtn = makeSideButton(tr("Clear Ref"), tr("Clear reference image"));
        referenceGroupLayout->addWidget(refVisibleBtn);
        referenceGroupLayout->addWidget(new QLabel(tr("Opacity"), this));
        referenceGroupLayout->addWidget(refOpacitySlider);
        referenceGroupLayout->addLayout(refMoveGrid);
        referenceGroupLayout->addWidget(loadRefBtn);
        referenceGroupLayout->addWidget(clearRefBtn);
        tabReferenceLayout->addWidget(referenceGroup);

        QVBoxLayout* transformGroupLayout = nullptr;
        QGroupBox* transformGroup = makeSideGroup(tr("Transform"), &transformGroupLayout);
        QGridLayout* scrollGrid = new QGridLayout();
        scrollGrid->setSpacing(4);
        QPushButton* scrollUpBtn = makeSideButton(tr("↑8"), tr("Scroll canvas up 1 tile (8 pixels)"));
        QPushButton* scrollDownBtn = makeSideButton(tr("↓8"), tr("Scroll canvas down 1 tile (8 pixels)"));
        QPushButton* scrollLeftBtn = makeSideButton(tr("←8"), tr("Scroll canvas left 1 tile (8 pixels)"));
        QPushButton* scrollRightBtn = makeSideButton(tr("→8"), tr("Scroll canvas right 1 tile (8 pixels)"));
        scrollGrid->addWidget(scrollUpBtn, 0, 1);
        scrollGrid->addWidget(scrollLeftBtn, 1, 0);
        scrollGrid->addWidget(scrollRightBtn, 1, 2);
        scrollGrid->addWidget(scrollDownBtn, 2, 1);

        QPushButton* mirrorHBtn = makeSideButton(tr("Mirror H"), tr("Mirror the full canvas horizontally"));
        QPushButton* mirrorVBtn = makeSideButton(tr("Mirror V"), tr("Mirror the full canvas vertically"));
        QPushButton* flipSelHBtn = makeSideButton(tr("Flip Selection H"), tr("Flip only the current selection horizontally"));
        QPushButton* flipSelVBtn = makeSideButton(tr("Flip Selection V"), tr("Flip only the current selection vertically"));
        QPushButton* rotateSelLeftBtn = makeSideButton(tr("Rotate Selection L"), tr("Rotate current selection 90 degrees left"));
        QPushButton* rotateSelRightBtn = makeSideButton(tr("Rotate Selection R"), tr("Rotate current selection 90 degrees right"));
        QPushButton* moveSelBtn = makeSideButton(tr("Move Selection"), tr("Cut current selection and place it by clicking the canvas"));

        QGridLayout* nudgeGrid = new QGridLayout();
        nudgeGrid->setSpacing(4);
        QPushButton* nudgeUpBtn = makeSideButton(tr("S↑"), tr("Move selection up 1 pixel"));
        QPushButton* nudgeDownBtn = makeSideButton(tr("S↓"), tr("Move selection down 1 pixel"));
        QPushButton* nudgeLeftBtn = makeSideButton(tr("S←"), tr("Move selection left 1 pixel"));
        QPushButton* nudgeRightBtn = makeSideButton(tr("S→"), tr("Move selection right 1 pixel"));
        nudgeGrid->addWidget(nudgeUpBtn, 0, 1);
        nudgeGrid->addWidget(nudgeLeftBtn, 1, 0);
        nudgeGrid->addWidget(nudgeRightBtn, 1, 2);
        nudgeGrid->addWidget(nudgeDownBtn, 2, 1);

        transformGroupLayout->addLayout(scrollGrid);
        transformGroupLayout->addWidget(mirrorHBtn);
        transformGroupLayout->addWidget(mirrorVBtn);
        transformGroupLayout->addWidget(flipSelHBtn);
        transformGroupLayout->addWidget(flipSelVBtn);
        transformGroupLayout->addWidget(rotateSelLeftBtn);
        transformGroupLayout->addWidget(rotateSelRightBtn);
        transformGroupLayout->addWidget(moveSelBtn);
        transformGroupLayout->addWidget(new QLabel(tr("Nudge Selection"), this));
        transformGroupLayout->addLayout(nudgeGrid);
        tabTransformLayout->addWidget(transformGroup);

        QVBoxLayout* shapeGroupLayout = nullptr;
        QGroupBox* shapeGroup = makeSideGroup(tr("Shape Tools"), &shapeGroupLayout);

        QPushButton* lineBtn = makeToolButton(tr("Line"), tr("Draw a straight line"));
        QPushButton* rectBtn = makeToolButton(tr("Rect"), tr("Draw a rectangle outline"));
        QPushButton* filledRectBtn = makeToolButton(tr("Filled Rect"), tr("Draw a filled rectangle"));
        QPushButton* ellipseBtn = makeToolButton(tr("Ellipse"), tr("Draw an ellipse outline"));
        QPushButton* filledEllipseBtn = makeToolButton(tr("Filled Ellipse"), tr("Draw a filled ellipse"));
        m_toolButtons.append(lineBtn);
        m_toolButtons.append(rectBtn);
        m_toolButtons.append(filledRectBtn);
        m_toolButtons.append(ellipseBtn);
        m_toolButtons.append(filledEllipseBtn);

        shapeGroupLayout->addWidget(lineBtn);
        shapeGroupLayout->addWidget(rectBtn);
        shapeGroupLayout->addWidget(filledRectBtn);
        shapeGroupLayout->addWidget(ellipseBtn);
        shapeGroupLayout->addWidget(filledEllipseBtn);

        QLabel* textLabel = new QLabel(tr("Text"), this);
        QLineEdit* textInput = new QLineEdit(this);
        textInput->setPlaceholderText(tr("Enter text..."));
        textInput->setClearButtonEnabled(true);

        QLabel* fontLabel = new QLabel(tr("Font"), this);
        QFontComboBox* textFontCombo = new QFontComboBox(this);
        textFontCombo->setCurrentFont(QFont(QStringLiteral("Arial")));
        textFontCombo->setToolTip(tr("Choose the font used for text drawing and CVBasic text font export"));
        m_textFontCombo = textFontCombo;

        QHBoxLayout* textSizeLayout = new QHBoxLayout();
        textSizeLayout->setSpacing(4);
        QLabel* textSizeLabel = new QLabel(tr("Size"), this);
        QSpinBox* textSizeSpin = new QSpinBox(this);
        textSizeSpin->setRange(6, 48);
        textSizeSpin->setValue(12);
        m_textSizeSpin = textSizeSpin;
        textSizeLayout->addWidget(textSizeLabel);
        textSizeLayout->addWidget(textSizeSpin, 1);

        QHBoxLayout* textSpacingLayout = new QHBoxLayout();
        textSpacingLayout->setSpacing(4);
        QLabel* textSpacingLabel = new QLabel(tr("Spacing"), this);
        QSpinBox* textSpacingSpin = new QSpinBox(this);
        textSpacingSpin->setRange(-4, 24);
        textSpacingSpin->setValue(0);
        textSpacingSpin->setToolTip(tr("Letter spacing: negative = closer, positive = wider"));
        m_textSpacingSpin = textSpacingSpin;
        textSpacingLayout->addWidget(textSpacingLabel);
        textSpacingLayout->addWidget(textSpacingSpin, 1);

        auto gradientComboColor = [](int idx) -> QColor {
            static const QColor pal[16] = {
                QColor("#000000"), QColor("#000000"), QColor("#21C842"), QColor("#5EDC78"),
                QColor("#5455ED"), QColor("#7D76FC"), QColor("#D4524D"), QColor("#42EBF5"),
                QColor("#FC5554"), QColor("#FF7978"), QColor("#D4C154"), QColor("#E6CE80"),
                QColor("#21B03B"), QColor("#C95BBA"), QColor("#CCCCCC"), QColor("#FFFFFF")
            };
            return pal[qBound(0, idx, 15)];
        };

        auto updateGradientComboColor = [gradientComboColor](QComboBox* combo) {
            if (!combo)
                return;

            const int idx = qBound(0, combo->currentData().toInt(), 15);
            const QColor color = gradientComboColor(idx);
            const int brightness = (color.red() * 299 + color.green() * 587 + color.blue() * 114) / 1000;
            const QString textColor = brightness > 140 ? QStringLiteral("#000000") : QStringLiteral("#FFFFFF");

            combo->setStyleSheet(QStringLiteral(
                "QComboBox { background:%1; color:%2; border:1px solid #777777; padding-left:4px; }"
                "QComboBox::drop-down { border-left:1px solid #777777; width:12px; }"
                "QComboBox QAbstractItemView { background:#202020; color:#FFFFFF; selection-background-color:#444444; }")
                                     .arg(color.name())
                                     .arg(textColor));
        };

        auto makeGradientColorCombo = [this, gradientComboColor, updateGradientComboColor](int defaultColor) {
            QComboBox* combo = new QComboBox(this);
            for (int c = 0; c < 16; ++c) {
                combo->addItem(QStringLiteral("%1").arg(c), c);
                combo->setItemData(c, gradientComboColor(c), Qt::DecorationRole);
            }

            combo->setCurrentIndex(qBound(0, defaultColor, 15));
            combo->setMinimumWidth(46);
            combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            combo->setToolTip(tr("Text color"));
            updateGradientComboColor(combo);

            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), combo, [combo, updateGradientComboColor](int) {
                updateGradientComboColor(combo);
            });

            return combo;
        };

        QComboBox* textGradColor1 = makeGradientColorCombo(15);
        QComboBox* textGradColor2 = makeGradientColorCombo(14);
        QComboBox* textGradColor3 = makeGradientColorCombo(7);
        QComboBox* textGradColor4 = makeGradientColorCombo(8);
        m_textGradColor1Combo = textGradColor1;
        m_textGradColor2Combo = textGradColor2;
        m_textGradColor3Combo = textGradColor3;
        m_textGradColor4Combo = textGradColor4;

        QLabel* textPreviewLabel = new QLabel(tr("Preview"), this);
        textPreviewLabel->setMinimumHeight(34);
        textPreviewLabel->setAlignment(Qt::AlignCenter);
        textPreviewLabel->setWordWrap(true);
        textPreviewLabel->setStyleSheet(QStringLiteral(
            "QLabel { background:#151515; color:#FFFFFF; border:1px solid #555555; border-radius:4px; padding:4px; }"));

        auto previewColecoColor = [](int idx) -> QColor {
            static const QColor pal[16] = {
                QColor("#000000"), QColor("#000000"), QColor("#21C842"), QColor("#5EDC78"),
                QColor("#5455ED"), QColor("#7D76FC"), QColor("#D4524D"), QColor("#42EBF5"),
                QColor("#FC5554"), QColor("#FF7978"), QColor("#D4C154"), QColor("#E6CE80"),
                QColor("#21B03B"), QColor("#C95BBA"), QColor("#CCCCCC"), QColor("#FFFFFF")
            };
            return pal[qBound(0, idx, 15)];
        };

        auto updateTextPreview = [textInput, textFontCombo, textSizeSpin, textSpacingSpin, textPreviewLabel,
                                  textGradColor1, textGradColor2, textGradColor3, textGradColor4,
                                  previewColecoColor]() {
            QFont previewFont = textFontCombo->currentFont();
            previewFont.setPixelSize(qBound(6, textSizeSpin->value(), 48));
            previewFont.setLetterSpacing(QFont::AbsoluteSpacing, qBound(-4, textSpacingSpin->value(), 24));

            const QString previewText = textInput->text().isEmpty()
                ? QStringLiteral("Preview")
                : textInput->text();

            const QVector<int> gradColors = {
                textGradColor1->currentData().toInt(),
                textGradColor2->currentData().toInt(),
                textGradColor3->currentData().toInt(),
                textGradColor4->currentData().toInt()
            };

            QFontMetrics fm(previewFont);
            const QRect bounds = fm.boundingRect(previewText).adjusted(-4, -4, 8, 8);
            const int w = qMax(210, bounds.width() + 12);
            const int h = qMax(52, bounds.height() + 14);

            QImage mask(w, h, QImage::Format_ARGB32);
            mask.fill(Qt::transparent);

            QPainter maskPainter(&mask);
            maskPainter.setFont(previewFont);
            maskPainter.setPen(Qt::white);
            maskPainter.drawText(6 - bounds.left(), 7 - bounds.top(), previewText);
            maskPainter.end();

            QImage preview(w, h, QImage::Format_ARGB32);
            preview.fill(QColor("#151515"));

            QPainter bgPainter(&preview);
            bgPainter.setPen(QColor("#555555"));
            bgPainter.drawRect(preview.rect().adjusted(0, 0, -1, -1));
            bgPainter.end();

            QVector<int> inkRowIndex(h, -1);
            int inkRows = 0;
            for (int y = 0; y < h; ++y) {
                bool hasInk = false;
                for (int x = 0; x < w; ++x) {
                    if (qAlpha(mask.pixel(x, y)) >= 96) {
                        hasInk = true;
                        break;
                    }
                }

                if (hasInk)
                    inkRowIndex[y] = inkRows++;
            }

            for (int y = 0; y < h; ++y) {
                int gradIdx = 0;
                if (inkRows > 1) {
                    const int rowIdx = qBound(0, inkRowIndex.value(y, 0), inkRows - 1);
                    gradIdx = qBound(0, (rowIdx * gradColors.size()) / inkRows, gradColors.size() - 1);
                }

                const QColor lineColor = previewColecoColor(gradColors.at(gradIdx));

                for (int x = 0; x < w; ++x) {
                    const QRgb mp = mask.pixel(x, y);
                    if (qAlpha(mp) < 96)
                        continue;
                    preview.setPixelColor(x, y, lineColor);
                }
            }

            textPreviewLabel->setText(QString());
            textPreviewLabel->setPixmap(QPixmap::fromImage(preview));
            textPreviewLabel->setMinimumHeight(h + 4);
            textPreviewLabel->setToolTip(QStringLiteral("Vertical preview: C1=%1 C2=%2 C3=%3 C4=%4")
                                             .arg(gradColors.at(0))
                                             .arg(gradColors.at(1))
                                             .arg(gradColors.at(2))
                                             .arg(gradColors.at(3)));
        };
        updateTextPreview();

        QPushButton* gradText1Btn = makeSideButton(tr("T1"), tr("Use Color 1 for text"));
        QPushButton* gradText2Btn = makeSideButton(tr("T2"), tr("Use Color 1 and 2 for vertical text gradient"));
        QPushButton* gradText3Btn = makeSideButton(tr("T3"), tr("Use Color 1, 2 and 3 for vertical text gradient"));
        QPushButton* gradText4Btn = makeSideButton(tr("T4"), tr("Use Color 1, 2, 3 and 4 for vertical text gradient"));

        for (QPushButton* b : { gradText1Btn, gradText2Btn, gradText3Btn, gradText4Btn }) {
            b->setMinimumWidth(46);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        }

        QHBoxLayout* gradColorRow = new QHBoxLayout();
        gradColorRow->setSpacing(4);
        gradColorRow->addWidget(textGradColor1, 1);
        gradColorRow->addWidget(textGradColor2, 1);
        gradColorRow->addWidget(textGradColor3, 1);
        gradColorRow->addWidget(textGradColor4, 1);

        QHBoxLayout* gradButtonRow = new QHBoxLayout();
        gradButtonRow->setSpacing(4);
        gradButtonRow->addWidget(gradText1Btn, 1);
        gradButtonRow->addWidget(gradText2Btn, 1);
        gradButtonRow->addWidget(gradText3Btn, 1);
        gradButtonRow->addWidget(gradText4Btn, 1);

        shapeGroupLayout->addWidget(textLabel);
        shapeGroupLayout->addWidget(textInput);
        shapeGroupLayout->addWidget(fontLabel);
        shapeGroupLayout->addWidget(textFontCombo);
        shapeGroupLayout->addLayout(textSizeLayout);
        shapeGroupLayout->addLayout(textSpacingLayout);
        shapeGroupLayout->addWidget(textPreviewLabel);
        shapeGroupLayout->addLayout(gradColorRow);
        shapeGroupLayout->addLayout(gradButtonRow);

        QLabel* shapeHelp = new QLabel(tr("Click and drag on the canvas. Shape uses the selected palette color. Press Gradient Text 1..4, then click/drag on the canvas and release to place the text."), this);
        shapeHelp->setWordWrap(true);
        shapeGroupLayout->addWidget(shapeHelp);
        tabShapeLayout->addWidget(shapeGroup);

        QVBoxLayout* tileGroupLayout = nullptr;
        QGroupBox* tileGroup = makeSideGroup(tr("Tile Tools"), &tileGroupLayout);

        QPushButton* snapTileBtn = makeSideButton(tr("Snap selection 8x8"), tr("Snap selection rectangles to tile boundaries"));
        snapTileBtn->setCheckable(true);

        QPushButton* selectTileBtn = makeSideButton(tr("Select 1x1 Tile"), tr("Click this, then click the canvas to select one 8x8 tile"));
        QPushButton* select2x2TileBtn = makeSideButton(tr("Select 2x2 Tiles"), tr("Click this, then click the canvas to select a 16x16 tile block"));
        QPushButton* select4x3TileBtn = makeSideButton(tr("Select 4x3 Tiles"), tr("Click this, then click the canvas to select a 32x24 tile block"));

        QLabel* tileHelp = new QLabel(tr("Choose a tile block button, then click on the canvas where the selection should start. Copy/Cut/Paste stays under right-click."), this);
        tileHelp->setWordWrap(true);

        tileGroupLayout->addWidget(snapTileBtn);
        tileGroupLayout->addWidget(selectTileBtn);
        tileGroupLayout->addWidget(select2x2TileBtn);
        tileGroupLayout->addWidget(select4x3TileBtn);
        tileGroupLayout->addWidget(tileHelp);
        tabTileLayout->addWidget(tileGroup);

        QVBoxLayout* colorGroupLayout = nullptr;
        QGroupBox* colorGroup = makeSideGroup(tr("Color Remap"), &colorGroupLayout);

        QHBoxLayout* fromToLayout = new QHBoxLayout();
        fromToLayout->setSpacing(6);

        QComboBox* fromColorCombo = new QComboBox(this);
        QComboBox* toColorCombo = new QComboBox(this);
        for (int i = 0; i < 16; ++i) {
            const QString label = tr("Color %1").arg(i);
            fromColorCombo->addItem(label, i);
            toColorCombo->addItem(label, i);
        }
        toColorCombo->setCurrentIndex(15);

        QVBoxLayout* fromLayout = new QVBoxLayout();
        fromLayout->addWidget(new QLabel(tr("From"), this));
        fromLayout->addWidget(fromColorCombo);

        QVBoxLayout* toLayout = new QVBoxLayout();
        toLayout->addWidget(new QLabel(tr("To"), this));
        toLayout->addWidget(toColorCombo);

        fromToLayout->addLayout(fromLayout);
        fromToLayout->addLayout(toLayout);

        QPushButton* replaceColorBtn = makeSideButton(tr("Replace Color"), tr("Replace all pixels from one color to another"));
        QPushButton* clearColorBtn = makeSideButton(tr("Clear From Color"), tr("Replace selected From color with BG color"));
        QPushButton* replaceSelectionColorBtn = makeSideButton(tr("Replace In Selection"), tr("Replace From color with To color only inside selection"));
        QPushButton* clearSelectionColorBtn = makeSideButton(tr("Clear In Selection"), tr("Replace From color with BG color only inside selection"));
        QPushButton* swapFgBgPixelsBtn = makeSideButton(tr("Swap FG/BG Canvas"), tr("Swap all pixels using current FG and BG colors"));
        QPushButton* swapFgBgSelectionBtn = makeSideButton(tr("Swap FG/BG Selection"), tr("Swap FG and BG pixels only inside selection"));
        QPushButton* fillSelectionFgBtn = makeSideButton(tr("Fill Selection FG"), tr("Fill current selection with FG color"));
        QPushButton* fillSelectionBgBtn = makeSideButton(tr("Fill Selection BG"), tr("Fill current selection with BG color"));
        QPushButton* outlineSelectionFgBtn = makeSideButton(tr("Outline Selection FG"), tr("Draw selection border with FG color"));
        QPushButton* outlineSelectionBgBtn = makeSideButton(tr("Outline Selection BG"), tr("Draw selection border with BG color"));

        QLabel* colorHelp = new QLabel(tr("Useful after PNG import: remap one Coleco color without repainting the whole image."), this);
        colorHelp->setWordWrap(true);

        colorGroupLayout->addLayout(fromToLayout);
        colorGroupLayout->addWidget(replaceColorBtn);
        colorGroupLayout->addWidget(clearColorBtn);
        colorGroupLayout->addWidget(replaceSelectionColorBtn);
        colorGroupLayout->addWidget(clearSelectionColorBtn);
        colorGroupLayout->addWidget(swapFgBgPixelsBtn);
        colorGroupLayout->addWidget(swapFgBgSelectionBtn);
        colorGroupLayout->addWidget(fillSelectionFgBtn);
        colorGroupLayout->addWidget(fillSelectionBgBtn);
        colorGroupLayout->addWidget(outlineSelectionFgBtn);
        colorGroupLayout->addWidget(outlineSelectionBgBtn);
        colorGroupLayout->addWidget(colorHelp);
        tabColorLayout->addWidget(colorGroup);

        QVBoxLayout* stampGroupLayout = nullptr;
        QGroupBox* stampGroup = makeSideGroup(tr("Stamp"), &stampGroupLayout);

        QPushButton* makeStampBtn = makeSideButton(tr("Make Stamp from Selection"), tr("Copy current selection as reusable stamp"));
        QPushButton* placeStampBtn = makeSideButton(tr("Place Stamp"), tr("Click this, then click canvas to place the stamp"));
        QPushButton* saveStampBtn = makeSideButton(tr("Save Stamp"), tr("Save current stamp to .cvstamp"));
        QPushButton* loadStampBtn = makeSideButton(tr("Load Stamp"), tr("Load a .cvstamp file"));

        QCheckBox* transparentPasteCheck = new QCheckBox(tr("Transparent color 0"), this);
        transparentPasteCheck->setChecked(true);
        transparentPasteCheck->setToolTip(tr("When pasting stamps/selections, color 0 will not overwrite the canvas"));

        QLabel* stampHelp = new QLabel(tr("Select an area first. Make Stamp stores it. Place Stamp lets you click the canvas to reuse it. Stamps can also be saved as .cvstamp."), this);
        stampHelp->setWordWrap(true);

        stampGroupLayout->addWidget(makeStampBtn);
        stampGroupLayout->addWidget(placeStampBtn);
        stampGroupLayout->addWidget(saveStampBtn);
        stampGroupLayout->addWidget(loadStampBtn);
        stampGroupLayout->addWidget(transparentPasteCheck);
        stampGroupLayout->addWidget(stampHelp);
        tabStampLayout->addWidget(stampGroup);

        QVBoxLayout* exportGroupLayout = nullptr;
        QGroupBox* exportGroup = makeSideGroup(tr("Export Files"), &exportGroupLayout);

        QPushButton* analyzeTmsBtn = makeSideButton(tr("Analyze TMS Colors"), tr("Check rows that use more than 2 colors"));
        QPushButton* showTmsConflictsBtn = makeSideButton(tr("Show TMS Conflicts"), tr("Show/hide red overlay on rows with more than 2 colors"));
        showTmsConflictsBtn->setCheckable(true);
        QPushButton* findTmsBtn = makeSideButton(tr("Find First TMS Conflict"), tr("Select the first 8-pixel row with more than 2 colors"));
        QPushButton* nextTmsBtn = makeSideButton(tr("Find Next TMS Conflict"), tr("Select the next 8-pixel row with more than 2 colors"));
        QPushButton* reduceSelectedTmsBtn = makeSideButton(tr("Reduce Selected TMS Row"), tr("Reduce only the selected 8-pixel TMS row to 2 colors"));
        QPushButton* reduceSelectedNextTmsBtn = makeSideButton(tr("Reduce Selected + Next"), tr("Reduce selected TMS row and jump to the next conflict"));
        QPushButton* reduceTmsBtn = makeSideButton(tr("Reduce to TMS Colors"), tr("Modify canvas to match exported TMS 2-color rows"));
        QGroupBox* routinesGroup = new QGroupBox(tr("Routines"), this);
        QVBoxLayout* routinesLayout = new QVBoxLayout(routinesGroup);
        routinesLayout->setContentsMargins(8, 8, 8, 8);
        routinesLayout->setSpacing(4);

        m_addViewerRoutineCheck = new QCheckBox(tr("With viewer / example"), routinesGroup);
        m_addViewerRoutineCheck->setChecked(false);
        m_addViewerRoutineCheck->setToolTip(tr("When enabled, Insert Bitmap DATA adds a small runnable/example viewer for the selected routine."));

        m_routineFontMode0Radio = new QRadioButton(tr("Font routine MODE 0"), routinesGroup);
        m_routineFontMode0Radio->setToolTip(tr("Insert generated 8x8 text font plus MODE 0 PRINT example."));

        m_routineFontMode1Radio = new QRadioButton(tr("Font routine MODE 1"), routinesGroup);
        m_routineFontMode1Radio->setToolTip(tr("Insert generated 8x8 text font plus MODE 1 color DATA using the selected text gradient colors."));

        m_routineBitmapScreenRadio = new QRadioButton(tr("Bitmap screen"), routinesGroup);
        m_routineBitmapScreenRadio->setToolTip(tr("Insert bitmap/color DATA for the paint canvas."));
        m_routineBitmapScreenRadio->setChecked(true);

        routinesLayout->addWidget(m_addViewerRoutineCheck);
        routinesLayout->addWidget(m_routineFontMode0Radio);
        routinesLayout->addWidget(m_routineFontMode1Radio);
        routinesLayout->addWidget(m_routineBitmapScreenRadio);

        QPushButton* exportPngBtn = makeSideButton(tr("Export PNG"), tr("Export the current paint canvas as PNG"));
        QPushButton* savePatternBtn = makeSideButton(tr("Save .pattern"), tr("Save TMS bitmap pattern table"));
        QPushButton* saveColorBtn = makeSideButton(tr("Save .color"), tr("Save TMS bitmap color table"));

        QLabel* exportHelp = new QLabel(tr("File exports only. Insert Bitmap DATA stays in the top toolbar because it inserts directly into the CVBasic editor. In Routines choose Bitmap screen, Font routine MODE 0, or Font routine MODE 1. Enable With viewer / example only when you want a standalone test main. Leave it off when you use your own main."), this);
        exportHelp->setWordWrap(true);

        exportGroupLayout->addWidget(analyzeTmsBtn);
        exportGroupLayout->addWidget(showTmsConflictsBtn);
        exportGroupLayout->addWidget(findTmsBtn);
        exportGroupLayout->addWidget(nextTmsBtn);
        exportGroupLayout->addWidget(reduceSelectedTmsBtn);
        exportGroupLayout->addWidget(reduceSelectedNextTmsBtn);
        exportGroupLayout->addWidget(reduceTmsBtn);
        exportGroupLayout->addWidget(routinesGroup);
        exportGroupLayout->addWidget(exportPngBtn);
        exportGroupLayout->addWidget(savePatternBtn);
        exportGroupLayout->addWidget(saveColorBtn);
        exportGroupLayout->addWidget(exportHelp);
        tabExportLayout->addWidget(exportGroup);

        tabToolsLayout->addStretch(1);
        tabReferenceLayout->addStretch(1);
        tabTransformLayout->addStretch(1);
        tabTileLayout->addStretch(1);
        tabShapeLayout->addStretch(1);
        tabColorLayout->addStretch(1);
        tabStampLayout->addStretch(1);
        tabExportLayout->addStretch(1);

        QScrollArea* tabHelp = new QScrollArea(sideTabs);
        tabHelp->setWidgetResizable(true);
        tabHelp->setFrameShape(QFrame::NoFrame);
        tabHelp->setStyleSheet(QStringLiteral("QScrollArea { background:transparent; border:0px; }"));

        QWidget* tabHelpContents = new QWidget(tabHelp);
        tabHelpContents->setObjectName(QStringLiteral("paintSidePanelContents"));
        QVBoxLayout* tabHelpLayout = new QVBoxLayout(tabHelpContents);
        tabHelpLayout->setContentsMargins(8, 8, 8, 8);
        tabHelpLayout->setSpacing(6);

        QLabel* shortcutsHelp = new QLabel(
            tr("<b>Paint Shortcuts</b><br><br>"
               "<b>Tools</b><br>"
               "P = Pen<br>"
               "E = Eraser<br>"
               "F = Fill<br>"
               "I = Pipette<br>"
               "S = Select<br><br>"
               "<b>Palette</b><br>"
               "Left click palette color = FG<br>"
               "Right click palette color = BG<br>"
               "X = Swap active FG/BG colors<br>"
               "Shift+X = Swap FG/BG pixels in selection<br>"
               "Alt+X = Swap FG/BG pixels on canvas<br>"
               "Canvas right click = popup menu<br><br>"
               "<b>Brush</b><br>"
               "1 = 1 px<br>"
               "2 = 2 px<br>"
               "3 = 4 px<br>"
               "4 = 8 px<br><br>"
               "<b>Zoom</b><br>"
               "+ = Zoom in<br>"
               "- = Zoom out<br>"
               "0 = Zoom 2x<br>"
               "Mouse wheel = Zoom 1x..4x<br><br>"
               "<b>Text</b><br>"
               "Shape tab: enter text and press Draw Text or Enter<br>"
               "Text position = selection top-left or last mouse position<br><br>"
               "<b>Selection</b><br>"
               "Ctrl+C = Copy<br>"
               "Ctrl+X = Cut<br>"
               "Ctrl+V = Paste<br>"
               "Ctrl+A = Select All<br>"
               "Delete = Cut<br>"
               "Esc = Clear Selection<br>"
               "Arrow keys = Move selection 1 px<br>"
               "Shift + Arrow = Move selection 8 px<br>"
               "Shift+F = Fill selection with FG<br>"
               "Shift+B = Fill selection with BG<br>"
               "Shift+O = Outline selection with FG<br>"
               "Ctrl+Shift+O = Outline selection with BG<br>"
               "Color tab can replace/clear/swap colors inside selection<br><br>"
               "<b>Undo</b><br>"
               "Ctrl+Z = Undo<br>"
               "Ctrl+Y = Redo<br><br>"
               "<b>Project / Export</b><br>"
               "Ctrl+N = New Paint<br>"
               "Ctrl+O = Open Paint<br>"
               "Ctrl+S = Save Paint<br>"
               "Ctrl+Shift+S = Save Paint As<br>"
               "Ctrl+R = Revert Paint<br>"
               "Ctrl+I = Import PNG<br>"
               "Ctrl+Shift+I = Import Dither<br>"
               "Ctrl+B = Insert Bitmap DATA<br>"
               "Export tab: Add viewer routine = runnable display code"),
            tabHelpContents);
        shortcutsHelp->setWordWrap(true);
        shortcutsHelp->setTextFormat(Qt::RichText);
        shortcutsHelp->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        shortcutsHelp->setStyleSheet(QStringLiteral("QLabel { color:#EAEAEA; background:transparent; }"));
        tabHelpLayout->addWidget(shortcutsHelp);
        tabHelpLayout->addStretch(1);
        tabHelp->setWidget(tabHelpContents);

        sideTabs->addTab(tabTools, tr("TOOLS"));
        sideTabs->addTab(tabTile, tr("TILE"));
        sideTabs->addTab(tabShape, tr("SHAPE"));
        sideTabs->addTab(tabColor, tr("COLOR"));
        sideTabs->addTab(tabStamp, tr("STAMP"));
        sideTabs->addTab(tabReference, tr("REF"));
        sideTabs->addTab(tabTransform, tr("MOVE"));
        sideTabs->addTab(tabExport, tr("EXPORT"));
        sideTabs->addTab(tabHelp, tr("HELP"));

        workLayout->addWidget(sideTabs, 0);

        cardLayout->addLayout(workLayout, 1);

        auto addPaintShortcut = [this](const QKeySequence& key, const std::function<void()>& fn) {
            QAction* action = new QAction(this);
            action->setShortcut(key);
            action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            addAction(action);
            connect(action, &QAction::triggered, this, fn);
        };

        addPaintShortcut(QKeySequence(Qt::Key_P), [this, penBtn]() {
            selectToolButton(penBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Pen);
            setStatus(tr("Tool: Pen"));
        });
        addPaintShortcut(QKeySequence(Qt::Key_E), [this, eraserBtn]() {
            selectToolButton(eraserBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Eraser);
            setStatus(tr("Tool: Eraser"));
        });
        addPaintShortcut(QKeySequence(Qt::Key_F), [this, fillBtn]() {
            selectToolButton(fillBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Fill);
            setStatus(tr("Tool: Fill"));
        });
        addPaintShortcut(QKeySequence(Qt::Key_I), [this, pipetteBtn]() {
            selectToolButton(pipetteBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Pipette);
            setStatus(tr("Tool: Pipette"));
        });
        addPaintShortcut(QKeySequence(Qt::Key_S), [this, selectBtn]() {
            selectToolButton(selectBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Select);
            setStatus(tr("Tool: Select - drag a block on the canvas"));
        });

        addPaintShortcut(QKeySequence(Qt::Key_1), [this]() { setPaintBrushSize(1); });
        addPaintShortcut(QKeySequence(Qt::Key_2), [this]() { setPaintBrushSize(2); });
        addPaintShortcut(QKeySequence(Qt::Key_3), [this]() { setPaintBrushSize(4); });
        addPaintShortcut(QKeySequence(Qt::Key_4), [this]() { setPaintBrushSize(8); });
        addPaintShortcut(QKeySequence(Qt::Key_X), [this]() { swapForegroundBackgroundColors(); });
        addPaintShortcut(QKeySequence(Qt::SHIFT | Qt::Key_X), [this]() {
            if (m_canvas)
                m_canvas->swapColorsInSelection(m_canvas->colorIndex(), m_canvas->backgroundColorIndex());
        });
        addPaintShortcut(QKeySequence(Qt::ALT | Qt::Key_X), [this]() {
            if (m_canvas)
                m_canvas->swapColors(m_canvas->colorIndex(), m_canvas->backgroundColorIndex());
        });
        addPaintShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F), [this]() {
            if (m_canvas)
                m_canvas->fillSelectionWithColor(m_canvas->colorIndex());
        });
        addPaintShortcut(QKeySequence(Qt::SHIFT | Qt::Key_B), [this]() {
            if (m_canvas)
                m_canvas->fillSelectionWithColor(m_canvas->backgroundColorIndex());
        });
        addPaintShortcut(QKeySequence(Qt::SHIFT | Qt::Key_O), [this]() {
            if (m_canvas)
                m_canvas->outlineSelectionWithColor(m_canvas->colorIndex());
        });
        addPaintShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), [this]() {
            if (m_canvas)
                m_canvas->outlineSelectionWithColor(m_canvas->backgroundColorIndex());
        });

        addPaintShortcut(QKeySequence(Qt::Key_Plus), [this]() { setPaintZoom(m_canvas->zoomScale() + 1); });
        addPaintShortcut(QKeySequence(Qt::Key_Minus), [this]() { setPaintZoom(m_canvas->zoomScale() - 1); });
        addPaintShortcut(QKeySequence(Qt::Key_0), [this]() { setPaintZoom(2); });

        addPaintShortcut(QKeySequence::Undo, [this]() { m_canvas->undo(); });
        addPaintShortcut(QKeySequence::Redo, [this]() { m_canvas->redo(); });
        addPaintShortcut(QKeySequence::Copy, [this]() { m_canvas->copySelection(); });
        addPaintShortcut(QKeySequence::Cut, [this]() { m_canvas->cutSelection(); });
        addPaintShortcut(QKeySequence::Paste, [this]() { m_canvas->pasteSelection(); });
        addPaintShortcut(QKeySequence::SelectAll, [this]() { m_canvas->selectAll(); });
        addPaintShortcut(QKeySequence(Qt::Key_Escape), [this]() { m_canvas->clearSelection(); });
        addPaintShortcut(QKeySequence(Qt::Key_Delete), [this]() { m_canvas->cutSelection(); });

        addPaintShortcut(QKeySequence(Qt::Key_Left), [this]() { m_canvas->nudgeSelection(-1, 0); });
        addPaintShortcut(QKeySequence(Qt::Key_Right), [this]() { m_canvas->nudgeSelection(1, 0); });
        addPaintShortcut(QKeySequence(Qt::Key_Up), [this]() { m_canvas->nudgeSelection(0, -1); });
        addPaintShortcut(QKeySequence(Qt::Key_Down), [this]() { m_canvas->nudgeSelection(0, 1); });

        addPaintShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Left), [this]() { m_canvas->nudgeSelection(-8, 0); });
        addPaintShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Right), [this]() { m_canvas->nudgeSelection(8, 0); });
        addPaintShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Up), [this]() { m_canvas->nudgeSelection(0, -8); });
        addPaintShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Down), [this]() { m_canvas->nudgeSelection(0, 8); });

        addPaintShortcut(QKeySequence::New, [this]() { newPaintProject(); });
        addPaintShortcut(QKeySequence::Open, [this]() { openPaintProject(); });
        addPaintShortcut(QKeySequence::Save, [this]() { savePaintProject(); });
        addPaintShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), [this]() { savePaintProjectAs(); });
        addPaintShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), [this]() { revertPaintProject(); });
        addPaintShortcut(QKeySequence(Qt::CTRL | Qt::Key_I), [this]() { importPng(); });
        addPaintShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), [this]() { importPngDithered(); });
        addPaintShortcut(QKeySequence(Qt::CTRL | Qt::Key_B), [this]() { insertBitmapDataInEditor(); });

        connect(penBtn, &QPushButton::clicked, this, [this, penBtn]() {
            selectToolButton(penBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Pen);
            setStatus(tr("Tool: Pen"));
        });
        connect(eraserBtn, &QPushButton::clicked, this, [this, eraserBtn]() {
            selectToolButton(eraserBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Eraser);
            setStatus(tr("Tool: Eraser"));
        });
        connect(fillBtn, &QPushButton::clicked, this, [this, fillBtn]() {
            selectToolButton(fillBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Fill);
            setStatus(tr("Tool: Fill"));
        });
        connect(pipetteBtn, &QPushButton::clicked, this, [this, pipetteBtn]() {
            selectToolButton(pipetteBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Pipette);
            setStatus(tr("Tool: Pipette"));
        });
        connect(selectBtn, &QPushButton::clicked, this, [this, selectBtn]() {
            selectToolButton(selectBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Select);
            setStatus(tr("Tool: Select - drag a block on the canvas"));
        });
        connect(lineBtn, &QPushButton::clicked, this, [this, lineBtn]() {
            selectToolButton(lineBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Line);
            setStatus(tr("Tool: Line"));
        });
        connect(rectBtn, &QPushButton::clicked, this, [this, rectBtn]() {
            selectToolButton(rectBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Rect);
            setStatus(tr("Tool: Rectangle"));
        });
        connect(filledRectBtn, &QPushButton::clicked, this, [this, filledRectBtn]() {
            selectToolButton(filledRectBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::FilledRect);
            setStatus(tr("Tool: Filled Rectangle"));
        });
        connect(ellipseBtn, &QPushButton::clicked, this, [this, ellipseBtn]() {
            selectToolButton(ellipseBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::Ellipse);
            setStatus(tr("Tool: Ellipse"));
        });
        connect(filledEllipseBtn, &QPushButton::clicked, this, [this, filledEllipseBtn]() {
            selectToolButton(filledEllipseBtn);
            m_canvas->setToolMode(CvBasicPaintCanvas::ToolMode::FilledEllipse);
            setStatus(tr("Tool: Filled Ellipse"));
        });
        connect(textInput, &QLineEdit::textChanged, this, updateTextPreview);
        connect(textFontCombo, &QFontComboBox::currentFontChanged, this, updateTextPreview);
        connect(textSizeSpin, qOverload<int>(&QSpinBox::valueChanged), this, updateTextPreview);
        connect(textSpacingSpin, qOverload<int>(&QSpinBox::valueChanged), this, updateTextPreview);
        connect(textGradColor1, qOverload<int>(&QComboBox::currentIndexChanged), this, updateTextPreview);
        connect(textGradColor2, qOverload<int>(&QComboBox::currentIndexChanged), this, updateTextPreview);
        connect(textGradColor3, qOverload<int>(&QComboBox::currentIndexChanged), this, updateTextPreview);
        connect(textGradColor4, qOverload<int>(&QComboBox::currentIndexChanged), this, updateTextPreview);

        auto armGradientText = [this, textInput, textSizeSpin, textFontCombo, textSpacingSpin,
                                textGradColor1, textGradColor2, textGradColor3, textGradColor4](int colorCount) {
            if (!m_canvas)
                return;

            QVector<int> colors;
            if (colorCount >= 1) colors.append(textGradColor1->currentData().toInt());
            if (colorCount >= 2) colors.append(textGradColor2->currentData().toInt());
            if (colorCount >= 3) colors.append(textGradColor3->currentData().toInt());
            if (colorCount >= 4) colors.append(textGradColor4->currentData().toInt());

            m_canvas->armTextPlacement(textInput->text(),
                                       textSizeSpin->value(),
                                       textFontCombo->currentFont().family(),
                                       textSpacingSpin->value(),
                                       colors);
        };

        connect(gradText1Btn, &QPushButton::clicked, this, [armGradientText]() { armGradientText(1); });
        connect(gradText2Btn, &QPushButton::clicked, this, [armGradientText]() { armGradientText(2); });
        connect(gradText3Btn, &QPushButton::clicked, this, [armGradientText]() { armGradientText(3); });
        connect(gradText4Btn, &QPushButton::clicked, this, [armGradientText]() { armGradientText(4); });
        connect(textInput, &QLineEdit::returnPressed, this, [armGradientText]() { armGradientText(1); });
        connect(brush1Btn, &QPushButton::clicked, this, [this]() { setPaintBrushSize(1); });
        connect(brush2Btn, &QPushButton::clicked, this, [this]() { setPaintBrushSize(2); });
        connect(brush4Btn, &QPushButton::clicked, this, [this]() { setPaintBrushSize(4); });
        connect(brush8Btn, &QPushButton::clicked, this, [this]() { setPaintBrushSize(8); });
        connect(gridBtn, &QPushButton::toggled, this, [this](bool checked) {
            m_canvas->setGridVisible(checked);
            setStatus(checked ? tr("Tile grid on") : tr("Tile grid off"));
        });
        connect(pixelGridBtn, &QPushButton::toggled, this, [this](bool checked) {
            m_canvas->setPixelGridVisible(checked);
            setStatus(checked ? tr("Pixel grid on") : tr("Pixel grid off"));
        });
        connect(zoom1Btn, &QPushButton::clicked, this, [this]() { setPaintZoom(1); });
        connect(zoom2Btn, &QPushButton::clicked, this, [this]() { setPaintZoom(2); });
        connect(zoom3Btn, &QPushButton::clicked, this, [this]() { setPaintZoom(3); });
        connect(zoom4Btn, &QPushButton::clicked, this, [this]() { setPaintZoom(4); });
        connect(refVisibleBtn, &QPushButton::toggled, this, [this](bool checked) { m_canvas->setReferenceVisible(checked); });
        connect(refOpacitySlider, &QSlider::valueChanged, this, [this](int value) { m_canvas->setReferenceOpacity(value); });
        connect(refUpBtn, &QPushButton::clicked, this, [this]() { m_canvas->moveReference(0, -1); });
        connect(refDownBtn, &QPushButton::clicked, this, [this]() { m_canvas->moveReference(0, 1); });
        connect(refLeftBtn, &QPushButton::clicked, this, [this]() { m_canvas->moveReference(-1, 0); });
        connect(refRightBtn, &QPushButton::clicked, this, [this]() { m_canvas->moveReference(1, 0); });
        connect(snapTileBtn, &QPushButton::toggled, this, [this](bool checked) { m_canvas->setSelectionSnapToTile(checked); });
        connect(selectTileBtn, &QPushButton::clicked, this, [this]() { m_canvas->armTileBlockSelection(1, 1); });
        connect(select2x2TileBtn, &QPushButton::clicked, this, [this]() { m_canvas->armTileBlockSelection(2, 2); });
        connect(select4x3TileBtn, &QPushButton::clicked, this, [this]() { m_canvas->armTileBlockSelection(4, 3); });
        connect(replaceColorBtn, &QPushButton::clicked, this, [this, fromColorCombo, toColorCombo]() {
            const int fromColor = fromColorCombo->currentData().toInt();
            const int toColor = toColorCombo->currentData().toInt();
            m_canvas->replaceColor(fromColor, toColor);
        });
        connect(clearColorBtn, &QPushButton::clicked, this, [this, fromColorCombo]() {
            const int fromColor = fromColorCombo->currentData().toInt();
            m_canvas->clearColor(fromColor);
        });
        connect(replaceSelectionColorBtn, &QPushButton::clicked, this, [this, fromColorCombo, toColorCombo]() {
            const int fromColor = fromColorCombo->currentData().toInt();
            const int toColor = toColorCombo->currentData().toInt();
            m_canvas->replaceColorInSelection(fromColor, toColor);
        });
        connect(clearSelectionColorBtn, &QPushButton::clicked, this, [this, fromColorCombo]() {
            const int fromColor = fromColorCombo->currentData().toInt();
            m_canvas->clearColorInSelection(fromColor);
        });
        connect(swapFgBgPixelsBtn, &QPushButton::clicked, this, [this]() {
            if (m_canvas)
                m_canvas->swapColors(m_canvas->colorIndex(), m_canvas->backgroundColorIndex());
        });
        connect(swapFgBgSelectionBtn, &QPushButton::clicked, this, [this]() {
            if (m_canvas)
                m_canvas->swapColorsInSelection(m_canvas->colorIndex(), m_canvas->backgroundColorIndex());
        });
        connect(fillSelectionFgBtn, &QPushButton::clicked, this, [this]() {
            if (m_canvas)
                m_canvas->fillSelectionWithColor(m_canvas->colorIndex());
        });
        connect(fillSelectionBgBtn, &QPushButton::clicked, this, [this]() {
            if (m_canvas)
                m_canvas->fillSelectionWithColor(m_canvas->backgroundColorIndex());
        });
        connect(outlineSelectionFgBtn, &QPushButton::clicked, this, [this]() {
            if (m_canvas)
                m_canvas->outlineSelectionWithColor(m_canvas->colorIndex());
        });
        connect(outlineSelectionBgBtn, &QPushButton::clicked, this, [this]() {
            if (m_canvas)
                m_canvas->outlineSelectionWithColor(m_canvas->backgroundColorIndex());
        });
        connect(makeStampBtn, &QPushButton::clicked, this, [this]() { m_canvas->makeStampFromSelection(); });
        connect(placeStampBtn, &QPushButton::clicked, this, [this]() { m_canvas->armStampPlacement(); });
        connect(saveStampBtn, &QPushButton::clicked, this, [this]() { saveStamp(); });
        connect(loadStampBtn, &QPushButton::clicked, this, [this]() { loadStamp(); });
        connect(transparentPasteCheck, &QCheckBox::toggled, this, [this](bool checked) { m_canvas->setTransparentPaste(checked); });
        connect(loadRefBtn, &QPushButton::clicked, this, [this]() { loadReferenceImage(); });
        connect(clearRefBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->clearReferenceImage();
            setStatus(tr("Reference image cleared"));
        });
        m_canvas->onContextMenuRequested = [this](const QPoint& globalPos) {
            QMenu menu(this);
            QAction* actCopy = menu.addAction(tr("Copy"));
            QAction* actCut = menu.addAction(tr("Cut"));
            QAction* actPaste = menu.addAction(tr("Paste"));
            menu.addSeparator();
            QAction* actSelectAll = menu.addAction(tr("All"));
            QAction* actClearSelection = menu.addAction(tr("No Sel"));

            actCopy->setEnabled(m_canvas->hasSelection());
            actCut->setEnabled(m_canvas->hasSelection());
            actPaste->setEnabled(m_canvas->hasClipboardSelection());
            actClearSelection->setEnabled(m_canvas->hasSelection());

            QAction* chosen = menu.exec(globalPos);
            if (chosen == actCopy)
                m_canvas->copySelection();
            else if (chosen == actCut)
                m_canvas->cutSelection();
            else if (chosen == actPaste)
                m_canvas->pasteSelection();
            else if (chosen == actSelectAll)
                m_canvas->selectAll();
            else if (chosen == actClearSelection)
                m_canvas->clearSelection();
        };
        connect(scrollUpBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->shiftCanvas(0, -8);
            setStatus(tr("Canvas scrolled up 1 tile"));
        });
        connect(scrollDownBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->shiftCanvas(0, 8);
            setStatus(tr("Canvas scrolled down 1 tile"));
        });
        connect(scrollLeftBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->shiftCanvas(-8, 0);
            setStatus(tr("Canvas scrolled left 1 tile"));
        });
        connect(scrollRightBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->shiftCanvas(8, 0);
            setStatus(tr("Canvas scrolled right 1 tile"));
        });
        connect(mirrorHBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->mirrorHorizontal();
            setStatus(tr("Canvas mirrored horizontally"));
        });
        connect(mirrorVBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->mirrorVertical();
            setStatus(tr("Canvas mirrored vertically"));
        });
        connect(flipSelHBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->flipSelectionHorizontal();
        });
        connect(flipSelVBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->flipSelectionVertical();
        });
        connect(rotateSelLeftBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->rotateSelection90(false);
        });
        connect(rotateSelRightBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->rotateSelection90(true);
        });
        connect(moveSelBtn, &QPushButton::clicked, this, [this]() {
            m_canvas->armMoveSelection();
        });
        connect(nudgeUpBtn, &QPushButton::clicked, this, [this]() { m_canvas->nudgeSelection(0, -1); });
        connect(nudgeDownBtn, &QPushButton::clicked, this, [this]() { m_canvas->nudgeSelection(0, 1); });
        connect(nudgeLeftBtn, &QPushButton::clicked, this, [this]() { m_canvas->nudgeSelection(-1, 0); });
        connect(nudgeRightBtn, &QPushButton::clicked, this, [this]() { m_canvas->nudgeSelection(1, 0); });
        connect(actUndoPaint, &QAction::triggered, this, [this]() {
            m_canvas->undo();
            setStatus(tr("Undo"));
        });
        connect(actRedoPaint, &QAction::triggered, this, [this]() {
            m_canvas->redo();
            setStatus(tr("Redo"));
        });
        connect(actClearCanvas, &QAction::triggered, this, [this]() {
            if (QMessageBox::question(this, tr("Clear canvas"), tr("Clear the complete 256x192 paint canvas to the current BG color?")) != QMessageBox::Yes)
                return;
            m_canvas->clearCanvasToBackground();
            setStatus(tr("Canvas cleared to BG color"));
        });
        connect(actNewPaint, &QAction::triggered, this, [this]() { newPaintProject(); });
        connect(actOpenPaint, &QAction::triggered, this, [this]() { openPaintProject(); });
        connect(actSavePaint, &QAction::triggered, this, [this]() { savePaintProject(); });
        connect(actSaveAsPaint, &QAction::triggered, this, [this]() { savePaintProjectAs(); });
        connect(actRevertPaint, &QAction::triggered, this, [this]() { revertPaintProject(); });
        connect(actImportPng, &QAction::triggered, this, [this]() { importPng(); });
        connect(actImportPngDither, &QAction::triggered, this, [this]() { importPngDithered(); });
        connect(actImportPngTms, &QAction::triggered, this, [this]() { importPngTmsConverted(); });
        connect(actInsertData, &QAction::triggered, this, [this]() { insertBitmapDataInEditor(); });
        connect(analyzeTmsBtn, &QPushButton::clicked, this, [this]() { analyzeTmsColors(); });
        connect(showTmsConflictsBtn, &QPushButton::toggled, this, [this](bool checked) { m_canvas->setShowTmsConflicts(checked); });
        connect(findTmsBtn, &QPushButton::clicked, this, [this]() { findFirstTmsConflict(); });
        connect(nextTmsBtn, &QPushButton::clicked, this, [this]() { findNextTmsConflict(); });
        connect(reduceSelectedTmsBtn, &QPushButton::clicked, this, [this]() { reduceSelectedTmsRow(); });
        connect(reduceSelectedNextTmsBtn, &QPushButton::clicked, this, [this]() { reduceSelectedTmsRowAndNext(); });
        connect(reduceTmsBtn, &QPushButton::clicked, this, [this]() { reduceToTmsColors(); });
        connect(exportPngBtn, &QPushButton::clicked, this, [this]() { exportPng(); });
        connect(savePatternBtn, &QPushButton::clicked, this, [this]() { savePatternFile(); });
        connect(saveColorBtn, &QPushButton::clicked, this, [this]() { saveColorFile(); });

        m_canvas->onColorPicked = [this](int color) { setSelectedColor(color); };
        m_canvas->onChanged = [this]() { setPaintModified(true); };
        m_canvas->onUndoStateChanged = [this](bool canUndo, bool canRedo) {
            if (m_undoAction)
                m_undoAction->setEnabled(canUndo);
            if (m_redoAction)
                m_redoAction->setEnabled(canRedo);
        };
        m_canvas->onStatus = [this](const QString& text) { setStatus(text); };
    }

    QString paintProjectDisplayName() const
    {
        if (m_currentPaintProjectPath.isEmpty())
            return tr("Untitled");
        return QFileInfo(m_currentPaintProjectPath).fileName();
    }

    void updatePaintProjectStatus()
    {
        const QString marker = m_paintModified ? QStringLiteral("*") : QString();
        const QString display = tr("%1%2").arg(paintProjectDisplayName(), marker);
        setStatus(tr("Paint project: %1").arg(display));

        if (onTitleChanged)
            onTitleChanged(display);
    }

    void setPaintModified(bool modified)
    {
        if (m_paintModified == modified)
            return;

        m_paintModified = modified;
        updatePaintProjectStatus();
    }

    void selectToolButton(QPushButton* active)
    {
        for (QPushButton* b : std::as_const(m_toolButtons))
            b->setChecked(b == active);
    }

    void selectButtonInGroup(const QVector<QPushButton*>& buttons, QPushButton* active)
    {
        for (QPushButton* b : buttons)
            b->setChecked(b == active);
    }

    void setPaintBrushSize(int size)
    {
        size = qBound(1, size, 8);
        if (m_canvas)
            m_canvas->setBrushSize(size);

        QPushButton* active = nullptr;
        if (size == 1 && m_brushSizeButtons.size() > 0)
            active = m_brushSizeButtons[0];
        else if (size == 2 && m_brushSizeButtons.size() > 1)
            active = m_brushSizeButtons[1];
        else if (size == 4 && m_brushSizeButtons.size() > 2)
            active = m_brushSizeButtons[2];
        else if (size == 8 && m_brushSizeButtons.size() > 3)
            active = m_brushSizeButtons[3];

        selectButtonInGroup(m_brushSizeButtons, active);
        setStatus(tr("Brush size %1").arg(size));
    }

    void setPaintZoom(int zoom)
    {
        zoom = qBound(1, zoom, 4);
        if (m_canvas)
            m_canvas->setZoomScale(zoom);

        QPushButton* active = nullptr;
        if (zoom >= 1 && zoom <= m_zoomButtons.size())
            active = m_zoomButtons[zoom - 1];

        selectButtonInGroup(m_zoomButtons, active);
        setStatus(tr("Zoom %1x").arg(zoom));
    }

    void setSelectedColor(int color)
    {
        color = qBound(0, color, 15);
        for (int i = 0; i < m_paletteButtons.size(); ++i)
            m_paletteButtons[i]->setChecked(i == color);

        if (m_selectedColorLabel) {
            const QColor c = colecoColor(color);
            const int luma = (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000;
            const QString textColor = (luma < 128) ? QStringLiteral("#FFFFFF") : QStringLiteral("#000000");
            m_selectedColorLabel->setText(tr("FG %1").arg(color));
            m_selectedColorLabel->setStyleSheet(QString("QLabel { background-color:%1; color:%2; border:1px solid #555555; border-radius:3px; font-weight:bold; }")
                                                    .arg(c.name(), textColor));
        }

        if (m_canvas)
            m_canvas->setColorIndex(color);
        setStatus(tr("Selected FG color %1").arg(color));
    }

    void setBackgroundColor(int color)
    {
        color = qBound(0, color, 15);

        if (m_backgroundColorLabel) {
            const QColor c = colecoColor(color);
            const int luma = (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000;
            const QString textColor = (luma < 128) ? QStringLiteral("#FFFFFF") : QStringLiteral("#000000");
            m_backgroundColorLabel->setText(tr("BG %1").arg(color));
            m_backgroundColorLabel->setStyleSheet(QString("QLabel { background-color:%1; color:%2; border:1px solid #555555; border-radius:3px; font-weight:bold; }")
                                                      .arg(c.name(), textColor));
        }

        if (m_canvas)
            m_canvas->setBackgroundColorIndex(color);
        setStatus(tr("Selected BG color %1").arg(color));
    }

    void swapForegroundBackgroundColors()
    {
        if (!m_canvas)
            return;

        const int oldFg = m_canvas->colorIndex();
        const int oldBg = m_canvas->backgroundColorIndex();

        setSelectedColor(oldBg);
        setBackgroundColor(oldFg);
        setStatus(tr("Swapped FG/BG: FG %1, BG %2").arg(oldBg).arg(oldFg));
    }

    void setStatus(const QString& text)
    {
        if (onStatusRequested)
            onStatusRequested(text);
    }

    bool maybeSavePaintChanges()
    {
        if (!m_paintModified)
            return true;

        const QMessageBox::StandardButton answer =
            QMessageBox::question(this,
                                  tr("Unsaved Paint Changes"),
                                  tr("The current paint project has unsaved changes.\n\nDo you want to save them first?"),
                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                  QMessageBox::Save);

        if (answer == QMessageBox::Cancel)
            return false;

        if (answer == QMessageBox::Save) {
            savePaintProject();
            return !m_paintModified;
        }

        return true;
    }

    void newPaintProject()
    {
        if (!maybeSavePaintChanges())
            return;

        m_canvas->clearCanvas();
        m_currentPaintProjectPath.clear();
        m_paintModified = false;
        setStatus(tr("New paint project created"));
        updatePaintProjectStatus();
    }

    bool loadPaintProjectFromFile(const QString& filePath)
    {
        if (filePath.isEmpty())
            return false;

        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Open Paint Project"), f.errorString());
            return false;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject() || !m_canvas->fromJson(doc.object())) {
            QMessageBox::warning(this, tr("Open Paint Project"), tr("This is not a valid ADAM+ Paint project."));
            return false;
        }

        setSelectedColor(m_canvas->colorIndex());
        m_currentPaintProjectPath = filePath;
        m_paintModified = false;
        setStatus(tr("Loaded %1").arg(QFileInfo(filePath).fileName()));
        updatePaintProjectStatus();
        return true;
    }

    void openPaintProject()
    {
        if (!maybeSavePaintChanges())
            return;

        const QString filePath = QFileDialog::getOpenFileName(this, tr("Open Paint Project"), QString(), tr("ADAM+ Paint Project (*.cvpaint);;JSON files (*.json);;All files (*.*)"));
        if (filePath.isEmpty())
            return;

        loadPaintProjectFromFile(filePath);
    }

    void revertPaintProject()
    {
        if (m_currentPaintProjectPath.isEmpty()) {
            QMessageBox::information(this,
                                     tr("Revert Paint Project"),
                                     tr("This paint project has not been saved yet."));
            return;
        }

        const int answer = QMessageBox::question(this,
                                                 tr("Revert Paint Project"),
                                                 tr("Reload the last saved version of:\n\n%1\n\nAll unsaved paint changes will be lost.")
                                                     .arg(QFileInfo(m_currentPaintProjectPath).fileName()),
                                                 QMessageBox::Yes | QMessageBox::No,
                                                 QMessageBox::No);

        if (answer != QMessageBox::Yes)
            return;

        loadPaintProjectFromFile(m_currentPaintProjectPath);
    }

    QString ensurePaintProjectExtension(const QString& filePath) const
    {
        return ensureFileExtension(filePath, QStringLiteral("cvpaint"));
    }

    QString ensureFileExtension(const QString& filePath, const QString& extension) const
    {
        if (filePath.isEmpty())
            return filePath;

        QFileInfo info(filePath);
        if (!info.suffix().isEmpty())
            return filePath;

        QString normalizedExtension = extension;
        if (normalizedExtension.startsWith(QLatin1Char('.')))
            normalizedExtension.remove(0, 1);

        return filePath + QLatin1Char('.') + normalizedExtension;
    }

    bool writePaintProjectToFile(const QString& filePath)
    {
        const QString normalizedPath = ensurePaintProjectExtension(filePath);
        if (normalizedPath.isEmpty())
            return false;

        QFile f(normalizedPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Save Paint Project"), f.errorString());
            return false;
        }

        f.write(QJsonDocument(m_canvas->toJson()).toJson(QJsonDocument::Indented));
        m_currentPaintProjectPath = normalizedPath;
        m_paintModified = false;
        setStatus(tr("Saved %1").arg(QFileInfo(normalizedPath).fileName()));
        updatePaintProjectStatus();
        return true;
    }

    void savePaintProject()
    {
        if (m_currentPaintProjectPath.isEmpty()) {
            savePaintProjectAs();
            return;
        }

        writePaintProjectToFile(m_currentPaintProjectPath);
    }

    void savePaintProjectAs()
    {
        const QString startPath = m_currentPaintProjectPath.isEmpty() ? QString() : m_currentPaintProjectPath;
        const QString filePath = QFileDialog::getSaveFileName(this, tr("Save Paint Project As"), startPath, tr("ADAM+ Paint Project (*.cvpaint);;JSON files (*.json);;All files (*.*)"));
        if (filePath.isEmpty())
            return;

        writePaintProjectToFile(filePath);
    }

    void reduceToTmsColors()
    {
        const int conflicts = m_canvas->tmsConflictRowCount();
        if (conflicts <= 0) {
            QMessageBox::information(this,
                                     tr("Reduce to TMS Colors"),
                                     tr("No TMS bitmap color conflicts found. Nothing to reduce."));
            setStatus(tr("Reduce TMS: no color conflicts"));
            return;
        }

        const QString msg = tr("%1 TMS bitmap rows use more than 2 colors.\n\n"
                               "This will permanently reduce the canvas to the same 2-color-per-row result used by .pattern/.color export.\n"
                               "You can still use Undo after this operation.\n\n"
                               "Continue?")
                                .arg(conflicts);

        if (QMessageBox::question(this, tr("Reduce to TMS Colors"), msg) != QMessageBox::Yes)
            return;

        m_canvas->reduceToTmsBitmapColors();
    }

    void findFirstTmsConflict()
    {
        if (!m_canvas->selectFirstTmsConflict()) {
            QMessageBox::information(this,
                                     tr("Find First TMS Conflict"),
                                     tr("No TMS bitmap color conflicts found."));
            setStatus(tr("Find TMS conflict: none"));
            return;
        }

        setStatus(tr("First TMS color conflict selected"));
    }

    void findNextTmsConflict()
    {
        if (!m_canvas->selectNextTmsConflict()) {
            QMessageBox::information(this,
                                     tr("Find Next TMS Conflict"),
                                     tr("No more TMS bitmap color conflicts found."));
            setStatus(tr("Next TMS conflict: none"));
            return;
        }

        setStatus(tr("Next TMS color conflict selected"));
    }

    void reduceSelectedTmsRow()
    {
        if (!m_canvas->reduceSelectedTmsRow()) {
            setStatus(tr("Selected TMS row was not reduced"));
            return;
        }

        setStatus(tr("Selected TMS row reduced"));
    }

    void reduceSelectedTmsRowAndNext()
    {
        if (!m_canvas->reduceSelectedTmsRow()) {
            setStatus(tr("Selected TMS row was not reduced"));
            return;
        }

        if (!m_canvas->selectNextTmsConflict()) {
            QMessageBox::information(this,
                                     tr("Reduce Selected + Next"),
                                     tr("Selected TMS row was reduced. No more TMS bitmap color conflicts found."));
            setStatus(tr("Selected TMS row reduced - no more conflicts"));
            return;
        }

        setStatus(tr("Selected TMS row reduced - next conflict selected"));
    }

    void analyzeTmsColors()
    {
        const int conflicts = m_canvas->tmsConflictRowCount();
        const int totalRows = 32 * 24 * 8;

        if (conflicts <= 0) {
            QMessageBox::information(this,
                                     tr("TMS Color Analyzer"),
                                     tr("Perfect. No TMS bitmap color conflicts found.\n\nAll %1 tile rows use maximum 2 colors.")
                                         .arg(totalRows));
            setStatus(tr("TMS analyzer: no color conflicts"));
            return;
        }

        QMessageBox::information(this,
                                 tr("TMS Color Analyzer"),
                                 tr("%1 of %2 TMS bitmap rows use more than 2 colors.\n\n"
                                    "During .pattern/.color export these rows will be reduced to the two most used colors in that row.")
                                     .arg(conflicts)
                                     .arg(totalRows));
        setStatus(tr("TMS analyzer: %1 color-conflict rows").arg(conflicts));
    }

    void saveStamp()
    {
        if (!m_canvas->hasClipboardSelection()) {
            QMessageBox::information(this, tr("Save Stamp"), tr("No stamp available. Select an area and use Make Stamp first."));
            return;
        }

        const QString selectedPath = QFileDialog::getSaveFileName(this, tr("Save Stamp"), QString(), tr("ADAM+ Paint Stamp (*.cvstamp);;JSON files (*.json);;All files (*.*)"));
        const QString filePath = ensureFileExtension(selectedPath, QStringLiteral("cvstamp"));
        if (filePath.isEmpty())
            return;

        QFile f(filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Save Stamp"), f.errorString());
            return;
        }

        f.write(QJsonDocument(m_canvas->stampToJson()).toJson(QJsonDocument::Indented));
        setStatus(tr("Saved stamp %1").arg(QFileInfo(filePath).fileName()));
    }

    void loadStamp()
    {
        const QString filePath = QFileDialog::getOpenFileName(this, tr("Load Stamp"), QString(), tr("ADAM+ Paint Stamp (*.cvstamp);;JSON files (*.json);;All files (*.*)"));
        if (filePath.isEmpty())
            return;

        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Load Stamp"), f.errorString());
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject() || !m_canvas->stampFromJson(doc.object())) {
            QMessageBox::warning(this, tr("Load Stamp"), tr("This is not a valid ADAM+ Paint stamp."));
            return;
        }

        setStatus(tr("Loaded stamp %1").arg(QFileInfo(filePath).fileName()));
    }

    void loadReferenceImage()
    {
        const QString filePath = QFileDialog::getOpenFileName(this, tr("Load Reference Image"), QString(), tr("Images (*.png *.bmp *.jpg *.jpeg);;All files (*.*)"));
        if (filePath.isEmpty())
            return;

        if (!m_canvas->loadReferenceImage(filePath)) {
            QMessageBox::warning(this, tr("Load Reference Image"), tr("Could not load reference image."));
            return;
        }

        setStatus(tr("Loaded reference %1").arg(QFileInfo(filePath).fileName()));
    }

    void importPng()
    {
        const QString filePath = QFileDialog::getOpenFileName(this, tr("Import PNG"), QString(), tr("Images (*.png *.bmp *.jpg *.jpeg);;All files (*.*)"));
        if (filePath.isEmpty())
            return;

        QImage img(filePath);
        if (img.isNull() || !m_canvas->importImage(img)) {
            QMessageBox::warning(this, tr("Import PNG"), tr("Could not import image file."));
            return;
        }

        setStatus(tr("Imported %1 and converted to Coleco palette").arg(QFileInfo(filePath).fileName()));
    }

    void importPngDithered()
    {
        const QString filePath = QFileDialog::getOpenFileName(this, tr("Import PNG Dithered"), QString(), tr("Images (*.png *.bmp *.jpg *.jpeg);;All files (*.*)"));
        if (filePath.isEmpty())
            return;

        QImage img(filePath);
        if (img.isNull() || !m_canvas->importImageDithered(img)) {
            QMessageBox::warning(this, tr("Import PNG Dithered"), tr("Could not import image file."));
            return;
        }

        setStatus(tr("Imported %1 with checker dither").arg(QFileInfo(filePath).fileName()));
    }

    void importPngTmsConverted()
    {
        const QString filePath = QFileDialog::getOpenFileName(this, tr("Import PNG TMS/Coleco"), QString(), tr("Images (*.png *.bmp *.jpg *.jpeg);;All files (*.*)"));
        if (filePath.isEmpty())
            return;

        QImage img(filePath);
        if (img.isNull() || !m_canvas->importImageTmsConverted(img)) {
            QMessageBox::warning(this, tr("Import PNG TMS/Coleco"), tr("Could not import image file."));
            return;
        }

        setStatus(tr("Imported %1 with TMS/Coleco 2-color row conversion").arg(QFileInfo(filePath).fileName()));
    }


    void exportPng()
    {
        const QString selectedPath = QFileDialog::getSaveFileName(this, tr("Export PNG"), QString(), tr("PNG image (*.png);;All files (*.*)"));
        const QString filePath = ensureFileExtension(selectedPath, QStringLiteral("png"));
        if (filePath.isEmpty())
            return;

        if (!m_canvas->toImage().save(filePath, "PNG")) {
            QMessageBox::warning(this, tr("Export PNG"), tr("Could not save PNG file."));
            return;
        }

        setStatus(tr("Exported %1").arg(QFileInfo(filePath).fileName()));
    }

    void savePatternFile()
    {
        saveBinaryFile(tr("Save Pattern Data"), tr("Pattern data (*.pattern);;Binary files (*.bin);;All files (*.*)"), m_canvas->bitmapPatternBytes(), QStringLiteral("pattern"));
    }

    void saveColorFile()
    {
        saveBinaryFile(tr("Save Color Data"), tr("Color data (*.color);;Binary files (*.bin);;All files (*.*)"), m_canvas->bitmapColorBytes(), QStringLiteral("color"));
    }

    void saveBinaryFile(const QString& title, const QString& filter, const QByteArray& bytes, const QString& defaultExtension)
    {
        const QString selectedPath = QFileDialog::getSaveFileName(this, title, QString(), filter);
        const QString filePath = ensureFileExtension(selectedPath, defaultExtension);
        if (filePath.isEmpty())
            return;

        QFile f(filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, title, f.errorString());
            return;
        }

        f.write(bytes);
        setStatus(tr("Saved %1 (%2 bytes)").arg(QFileInfo(filePath).fileName()).arg(bytes.size()));
    }

    QByteArray buildCvBasicTextFontBytes() const
    {
        QByteArray bytes;
        bytes.reserve(96 * 8);

        QFont font = m_textFontCombo ? m_textFontCombo->currentFont() : QFont(QStringLiteral("Arial"));

        // TMS character patterns are always 8x8. We keep the selected family,
        // but clamp the pixel size so normal ASCII characters fit inside 8 rows.
        const int requestedSize = m_textSizeSpin ? m_textSizeSpin->value() : 8;
        font.setPixelSize(qBound(6, requestedSize, 9));

        if (m_textSpacingSpin)
            font.setLetterSpacing(QFont::AbsoluteSpacing, qBound(-2, m_textSpacingSpin->value(), 2));

        font.setBold(false);
        font.setItalic(false);

        QFontMetrics fm(font);

        for (int ch = 32; ch <= 127; ++ch) {
            const QString s = QString(QChar(ch));
            QImage glyph(8, 8, QImage::Format_ARGB32);
            glyph.fill(Qt::transparent);

            QRect br = fm.boundingRect(s);
            if (!s.trimmed().isEmpty()) {
                QPainter gp(&glyph);
                gp.setRenderHint(QPainter::TextAntialiasing, false);
                gp.setFont(font);
                gp.setPen(Qt::white);

                const int x = (8 - br.width()) / 2 - br.left();
                const int y = (8 - br.height()) / 2 - br.top();
                gp.drawText(QPoint(x, y), s);
                gp.end();
            }

            for (int y = 0; y < 8; ++y) {
                quint8 row = 0;
                for (int x = 0; x < 8; ++x) {
                    if (qAlpha(glyph.pixel(x, y)) >= 96)
                        row |= static_cast<quint8>(0x80 >> x);
                }
                bytes.append(static_cast<char>(row));
            }
        }

        return bytes;
    }

    QVector<int> selectedTextGradientColors(int colorCount = 4) const
    {
        QVector<int> colors;
        if (colorCount >= 1)
            colors.append(m_textGradColor1Combo ? m_textGradColor1Combo->currentData().toInt() : 15);
        if (colorCount >= 2)
            colors.append(m_textGradColor2Combo ? m_textGradColor2Combo->currentData().toInt() : 14);
        if (colorCount >= 3)
            colors.append(m_textGradColor3Combo ? m_textGradColor3Combo->currentData().toInt() : 7);
        if (colorCount >= 4)
            colors.append(m_textGradColor4Combo ? m_textGradColor4Combo->currentData().toInt() : 8);

        for (int& c : colors)
            c = qBound(0, c, 15);

        return colors;
    }

    QByteArray buildCvBasicMode1TextColorBytes() const
    {
        // Graphics II / MODE 1 color table byte:
        // high nibble = foreground color, low nibble = background color.
        // ASCII 32..127 = 96 characters * 8 rows = 768 bytes.
        QByteArray bytes;
        bytes.reserve(96 * 8);

        const QVector<int> colors = selectedTextGradientColors(4);
        const int bg = qBound(0, m_canvas ? m_canvas->backgroundColorIndex() : 0, 15);

        for (int ch = 32; ch <= 127; ++ch) {
            Q_UNUSED(ch);
            for (int row = 0; row < 8; ++row) {
                const int idx = qBound(0, (row * colors.size()) / 8, colors.size() - 1);
                const int fg = qBound(0, colors.at(idx), 15);
                bytes.append(static_cast<char>((fg << 4) | bg));
            }
        }

        return bytes;
    }


    QString cvBasicTextFontRoutine(const QString& labelPrefix, bool addViewerRoutine, int mode) const
    {
        const QByteArray fontBytes = buildCvBasicTextFontBytes();
        const QByteArray mode1ColorBytes = (mode == 1) ? buildCvBasicMode1TextColorBytes() : QByteArray();

        QString out;
        QTextStream ts(&out);

        const QString family = m_textFontCombo ? m_textFontCombo->currentFont().family() : QStringLiteral("Arial");
        const int sourceSize = m_textSizeSpin ? m_textSizeSpin->value() : 8;
        const int sourceSpacing = m_textSpacingSpin ? m_textSpacingSpin->value() : 0;

        auto appendLocalByteData = [](QTextStream& stream, const QString& label, const QByteArray& bytes) {
            stream << label << ":\n";
            for (int i = 0; i < bytes.size(); i += 16) {
                stream << "\tDATA BYTE ";
                const int count = qMin(16, bytes.size() - i);
                for (int j = 0; j < count; ++j) {
                    if (j > 0)
                        stream << ",";
                    const int value = static_cast<unsigned char>(bytes.at(i + j));
                    stream << "$" << QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
                }
                stream << "\n";
            }
        };

        ts << "\n";
        ts << "REM --- ADAM+ generated CVBasic text font ---\n";
        ts << "REM Source font: " << family << ", UI size=" << sourceSize << ", spacing=" << sourceSpacing << "\n";
        ts << "REM TMS/CVBasic character patterns are 8x8; original direct 8x8 rendering.\n";
        ts << "REM ASCII 32..127, 96 characters, 768 bytes.\n";
        ts << "REM MODE 0 uses PAINT_TEXT_FONT. MODE 1 uses PAINT_TEXT_FONT_BANK0/1/2 and PAINT_TEXT_COLOR_BANK0/1/2.\n";
        if (mode == 1) {
            const QVector<int> colors = selectedTextGradientColors(4);
            ts << "REM MODE 1 color DATA included as 3 separate pattern/color bank labels.\n";
            ts << "REM Text gradient colors top-to-bottom: "
               << colors.value(0, 15) << ","
               << colors.value(1, 14) << ","
               << colors.value(2, 7) << ","
               << colors.value(3, 8) << "\n";
        }
        ts << "\n";

        ts << "REM Selected routine: MODE " << mode << "\n";
        ts << "REM MODE 0 uses PAINT_TEXT_FONT. MODE 1 uses PAINT_TEXT_FONT_BANK0/1/2 and PAINT_TEXT_COLOR_BANK0/1/2.\n\n";

        if (addViewerRoutine) {
            ts << "REM --- Small runnable text font example, only generated when With viewer / example is ON ---\n";
            ts << "MODE " << mode << "\n";
            ts << "SCREEN DISABLE\n";
            if (mode == 1) {
                ts << "REM MODE 1 / Graphics II uses 3 pattern/color banks for rows 0..7, 8..15, 16..23\n";
                ts << "REM You can edit or replace each bank label separately.\n";
                ts << "DEFINE VRAM $0100,$0300," << labelPrefix << "_TEXT_FONT_BANK0\n";
                ts << "DEFINE VRAM $0900,$0300," << labelPrefix << "_TEXT_FONT_BANK1\n";
                ts << "DEFINE VRAM $1100,$0300," << labelPrefix << "_TEXT_FONT_BANK2\n";
                ts << "DEFINE VRAM $2100,$0300," << labelPrefix << "_TEXT_COLOR_BANK0\n";
                ts << "DEFINE VRAM $2900,$0300," << labelPrefix << "_TEXT_COLOR_BANK1\n";
                ts << "DEFINE VRAM $3100,$0300," << labelPrefix << "_TEXT_COLOR_BANK2\n";
            } else {
                ts << "DEFINE VRAM $0100,$0300," << labelPrefix << "_TEXT_FONT\n";
            }
            ts << "SCREEN ENABLE\n";
            ts << "CLS\n";

            if (mode == 1) {
                ts << "REM MODE 1 full text-screen example: 32 columns x 24 rows\n";
                ts << "PRINT AT 0,\" !\\\"#$%&'()*+,-./0123456789:;<=>?\"\n";
                ts << "PRINT AT 32,\"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\\\]^_\"\n";
                ts << "PRINT AT 64,\"`abcdefghijklmnopqrstuvwxyz{|}~\"\n";
                ts << "PRINT AT 640,\"ADAM+ TEXT FONT\"\n";
                ts << "PRINT AT 672,\"STRING PRINT TEST\"\n";
            } else {
                ts << "REM MODE 0 text example\n";
                ts << "PRINT AT 96,\"ADAM+ TEXT FONT\"\n";
                ts << "PRINT AT 128,\"STRING PRINT TEST\"\n";
            }

            ts << "WHILE 1: WEND\n\n";
        } else {
            ts << "REM Data only: use these DEFINE VRAM lines inside your own main program.\n";
            if (mode == 1) {
                ts << "REM BANK0 rows 0..7   : DEFINE VRAM $0100,$0300," << labelPrefix << "_TEXT_FONT_BANK0\n";
                ts << "REM BANK0 colors      : DEFINE VRAM $2100,$0300," << labelPrefix << "_TEXT_COLOR_BANK0\n";
                ts << "REM BANK1 rows 8..15  : DEFINE VRAM $0900,$0300," << labelPrefix << "_TEXT_FONT_BANK1\n";
                ts << "REM BANK1 colors      : DEFINE VRAM $2900,$0300," << labelPrefix << "_TEXT_COLOR_BANK1\n";
                ts << "REM BANK2 rows 16..23 : DEFINE VRAM $1100,$0300," << labelPrefix << "_TEXT_FONT_BANK2\n";
                ts << "REM BANK2 colors      : DEFINE VRAM $3100,$0300," << labelPrefix << "_TEXT_COLOR_BANK2\n";
                ts << "REM Example for your own main, lines 20 and 21:\n";
                ts << "REM   SCREEN DISABLE\n";
                ts << "REM   DEFINE VRAM $1100,$0300," << labelPrefix << "_TEXT_FONT_BANK2\n";
                ts << "REM   DEFINE VRAM $3100,$0300," << labelPrefix << "_TEXT_COLOR_BANK2\n";
                ts << "REM   SCREEN ENABLE\n";
                ts << "REM   PRINT AT 640,\"ADAM+ TEXT FONT\"\n";
                ts << "REM   PRINT AT 672,\"STRING PRINT TEST\"\n\n";
            } else {
                ts << "REM MODE 0 font       : DEFINE VRAM $0100,$0300," << labelPrefix << "_TEXT_FONT\n";
                ts << "REM Example for your own main:\n";
                ts << "REM   SCREEN DISABLE\n";
                ts << "REM   DEFINE VRAM $0100,$0300," << labelPrefix << "_TEXT_FONT\n";
                ts << "REM   SCREEN ENABLE\n";
                ts << "REM   PRINT AT 96,\"ADAM+ TEXT FONT\"\n";
                ts << "REM   PRINT AT 128,\"STRING PRINT TEST\"\n\n";
            }
        }

        if (mode == 1) {
            ts << "REM --- MODE 1 bank 0: screen rows 0..7 ---\n";
            appendLocalByteData(ts, labelPrefix + QStringLiteral("_TEXT_FONT_BANK0"), fontBytes);
            ts << "\n";
            appendLocalByteData(ts, labelPrefix + QStringLiteral("_TEXT_COLOR_BANK0"), mode1ColorBytes);

            ts << "\nREM --- MODE 1 bank 1: screen rows 8..15 ---\n";
            appendLocalByteData(ts, labelPrefix + QStringLiteral("_TEXT_FONT_BANK1"), fontBytes);
            ts << "\n";
            appendLocalByteData(ts, labelPrefix + QStringLiteral("_TEXT_COLOR_BANK1"), mode1ColorBytes);

            ts << "\nREM --- MODE 1 bank 2: screen rows 16..23 ---\n";
            appendLocalByteData(ts, labelPrefix + QStringLiteral("_TEXT_FONT_BANK2"), fontBytes);
            ts << "\n";
            appendLocalByteData(ts, labelPrefix + QStringLiteral("_TEXT_COLOR_BANK2"), mode1ColorBytes);
        } else {
            appendLocalByteData(ts, labelPrefix + QStringLiteral("_TEXT_FONT"), fontBytes);
        }
        return out;
    }

    void insertBitmapDataInEditor()
    {
        const bool addViewerRoutine = m_addViewerRoutineCheck && m_addViewerRoutineCheck->isChecked();

        QString data;
        QString routineName;

        if (m_routineFontMode0Radio && m_routineFontMode0Radio->isChecked()) {
            data = cvBasicTextFontRoutine(QStringLiteral("PAINT"), addViewerRoutine, 0);
            routineName = tr("font MODE 0");
        } else if (m_routineFontMode1Radio && m_routineFontMode1Radio->isChecked()) {
            data = cvBasicTextFontRoutine(QStringLiteral("PAINT"), addViewerRoutine, 1);
            routineName = tr("font MODE 1");
        } else {
            data = m_canvas->exportCvBasicBitmapData(QStringLiteral("PAINT"), addViewerRoutine);
            routineName = tr("bitmap screen");
        }

        if (onInsertRequested)
            onInsertRequested(data);

        const int conflicts = m_canvas->tmsConflictRowCount();
        if ((m_routineBitmapScreenRadio && m_routineBitmapScreenRadio->isChecked()) && conflicts > 0) {
            setStatus(tr("Paint %1 inserted - viewer/example=%2 - %3 TMS rows had more than 2 colors and were reduced")
                          .arg(routineName)
                          .arg(addViewerRoutine ? tr("ON") : tr("OFF"))
                          .arg(conflicts));
        } else {
            setStatus(tr("Paint %1 inserted - viewer/example=%2")
                          .arg(routineName)
                          .arg(addViewerRoutine ? tr("ON") : tr("OFF")));
        }
    }

private:
    CvBasicPaintCanvas* m_canvas = nullptr;
    QScrollArea* m_canvasScroll = nullptr;
    QString m_currentPaintProjectPath;
    bool m_paintModified = false;
    QVector<QPushButton*> m_paletteButtons;
    QLabel* m_selectedColorLabel = nullptr;
    QLabel* m_backgroundColorLabel = nullptr;
    QVector<QPushButton*> m_toolButtons;
    QVector<QPushButton*> m_brushSizeButtons;
    QVector<QPushButton*> m_zoomButtons;
    QCheckBox* m_addViewerRoutineCheck = nullptr;
    QRadioButton* m_routineFontMode0Radio = nullptr;
    QRadioButton* m_routineFontMode1Radio = nullptr;
    QRadioButton* m_routineBitmapScreenRadio = nullptr;
    QFontComboBox* m_textFontCombo = nullptr;
    QSpinBox* m_textSizeSpin = nullptr;
    QSpinBox* m_textSpacingSpin = nullptr;
    QComboBox* m_textGradColor1Combo = nullptr;
    QComboBox* m_textGradColor2Combo = nullptr;
    QComboBox* m_textGradColor3Combo = nullptr;
    QComboBox* m_textGradColor4Combo = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
};

// ============================================================================
// Integrated CVBasic sprite editor
// ============================================================================

struct CvBasicSpriteData
{
    QString name = "Sprite1";
    int size = 16;        // 8 or 16 per hardware sprite part
    int grid = 1;         // 1, 2 or 3: grouped sprite type 1x1 / 2x2 / 3x3
    int color = 15;       // current draw color / fallback color
    QVector<quint8> pixels; // 0 = transparent, 1..15 = Coleco color
};

class CvBasicSpriteCanvas final : public QWidget
{
public:
    enum class ToolMode
    {
        Pen,
        Fill
    };

    explicit CvBasicSpriteCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(360, 360);
        setMouseTracking(true);
    }

    void setComposite(QVector<CvBasicSpriteData>* sprites, int groupStart, int grid)
    {
        m_sprites = sprites;
        m_groupStart = qMax(0, groupStart);
        m_grid = qBound(1, grid, 3);
        update();
    }

    void setCurrentColor(int color)
    {
        m_currentColor = qBound(1, color, 15);
        update();
    }

    void setToolMode(ToolMode mode)
    {
        m_toolMode = mode;
    }

    bool loadOverlayImage(const QString& filePath)
    {
        QImage img(filePath);
        if (img.isNull())
            return false;

        m_overlayImage = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        fitOverlayToCanvas();
        update();
        return true;
    }

    void clearOverlayImage()
    {
        m_overlayImage = QImage();
        m_overlayScale = 1.0;
        m_overlayPos = QPointF(0.0, 0.0);
        update();
    }

    bool hasOverlayImage() const
    {
        return !m_overlayImage.isNull();
    }

    void scaleOverlay(qreal factor)
    {
        zoomOverlayAt(logicalSize() / 2.0, logicalSize() / 2.0, factor);
    }

    void zoomOverlayAt(qreal logicalX, qreal logicalY, qreal factor)
    {
        if (m_overlayImage.isNull() || factor <= 0.0)
            return;

        const qreal oldScale = m_overlayScale;
        const qreal newScale = qBound(0.05, m_overlayScale * factor, 128.0);

        if (qFuzzyCompare(oldScale, newScale))
            return;

        // Keep the point under the mouse stable while zooming.
        const qreal imageX = (logicalX - m_overlayPos.x()) / oldScale;
        const qreal imageY = (logicalY - m_overlayPos.y()) / oldScale;

        m_overlayScale = newScale;
        m_overlayPos = QPointF(logicalX - imageX * newScale,
                               logicalY - imageY * newScale);

        update();
    }

    void moveOverlay(int dx, int dy)
    {
        if (m_overlayImage.isNull() || !m_overlayMoveEnabled)
            return;

        m_overlayPos += QPointF(dx, dy);
        update();
    }

    void setOverlayMoveEnabled(bool enabled)
    {
        m_overlayMoveEnabled = enabled;
        m_draggingOverlay = false;
        setCursor(enabled && !m_overlayImage.isNull() ? Qt::OpenHandCursor : Qt::ArrowCursor);
        update();
    }

    bool overlayMoveEnabled() const
    {
        return m_overlayMoveEnabled;
    }

    std::function<void()> onChanged;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor("#242424"));

        if (!m_sprites || m_sprites->isEmpty())
            return;

        const int tile = tileSize();
        const int grid = qBound(1, m_grid, 3);
        const int logicalSize = tile * grid;

        const int cell = qMax(6, qMin(width() / qMax(1, logicalSize), height() / qMax(1, logicalSize)));
        const int drawSize = logicalSize * cell;
        const int x0 = (width() - drawSize) / 2;
        const int y0 = (height() - drawSize) / 2;

        const QRect drawRect(x0, y0, drawSize, drawSize);
        p.fillRect(drawRect, QColor("#000000"));

        if (!m_overlayImage.isNull()) {
            p.save();
            p.setClipRect(drawRect);
            p.setOpacity(0.45);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);

            const QRectF targetRect(
                x0 + m_overlayPos.x() * cell,
                y0 + m_overlayPos.y() * cell,
                m_overlayImage.width() * m_overlayScale * cell,
                m_overlayImage.height() * m_overlayScale * cell);

            p.drawImage(targetRect, m_overlayImage);
            p.restore();
        }

        for (int gy = 0; gy < logicalSize; ++gy) {
            for (int gx = 0; gx < logicalSize; ++gx) {
                const int c = pixelAtGlobal(gx, gy);

                if (c != 0)
                    p.fillRect(x0 + gx * cell, y0 + gy * cell, cell, cell, colecoColor(c));

                p.setPen(QColor("#4A4A4A"));
                p.drawRect(x0 + gx * cell, y0 + gy * cell, cell, cell);
            }
        }

        p.setPen(QPen(QColor("#8A8A8A"), 2));
        for (int i = 1; i < grid; ++i) {
            const int pos = i * tile * cell;
            p.drawLine(x0 + pos, y0, x0 + pos, y0 + drawSize);
            p.drawLine(x0, y0 + pos, x0 + drawSize, y0 + pos);
        }

        p.setPen(QColor("#BDBDBD"));
        p.drawRect(QRect(x0, y0, drawSize, drawSize).adjusted(0, 0, -1, -1));

        p.setPen(QColor("#9E9E9E"));
        QString statusText = QString("Edit %1x%1  (%2x%2 sprites of %3x%3)")
                                 .arg(logicalSize)
                                 .arg(grid)
                                 .arg(tile);
        if (!m_overlayImage.isNull())
            statusText += m_overlayMoveEnabled ? QString("  |  PNG move ON") : QString("  |  PNG locked");

        p.drawText(QRect(0, height() - 20, width(), 18),
                   Qt::AlignCenter,
                   statusText);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (m_overlayMoveEnabled && !m_overlayImage.isNull() && event->button() == Qt::LeftButton) {
            m_draggingOverlay = true;
            m_lastOverlayDragPos = event->position().toPoint();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }

        if (m_overlayMoveEnabled && !m_overlayImage.isNull()) {
            event->accept();
            return;
        }

        apply(event->position().toPoint(), event->button() == Qt::RightButton);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_overlayMoveEnabled && !m_overlayImage.isNull()) {
            if (m_draggingOverlay && (event->buttons() & Qt::LeftButton)) {
                const QPoint currentPos = event->position().toPoint();
                const QPoint delta = currentPos - m_lastOverlayDragPos;
                m_lastOverlayDragPos = currentPos;

                const int n = logicalSize();
                const int cell = qMax(6, qMin(width() / qMax(1, n), height() / qMax(1, n)));

                if (cell > 0) {
                    m_overlayPos += QPointF(static_cast<qreal>(delta.x()) / cell,
                                            static_cast<qreal>(delta.y()) / cell);
                    update();
                }
            }

            event->accept();
            return;
        }

        // Fill only on click, not while dragging.
        if (m_toolMode == ToolMode::Fill)
            return;

        if (event->buttons() & Qt::LeftButton)
            apply(event->position().toPoint(), false);
        else if (event->buttons() & Qt::RightButton)
            apply(event->position().toPoint(), true);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_draggingOverlay && event->button() == Qt::LeftButton) {
            m_draggingOverlay = false;
            setCursor(m_overlayMoveEnabled ? Qt::OpenHandCursor : Qt::ArrowCursor);
            event->accept();
            return;
        }

        QWidget::mouseReleaseEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (m_overlayMoveEnabled && !m_overlayImage.isNull()) {
            const int n = logicalSize();
            const int cell = qMax(6, qMin(width() / qMax(1, n), height() / qMax(1, n)));
            const int drawSize = n * cell;
            const int x0 = (width() - drawSize) / 2;
            const int y0 = (height() - drawSize) / 2;

            const QPointF pos = event->position();
            const qreal logicalX = (pos.x() - x0) / qMax(1, cell);
            const qreal logicalY = (pos.y() - y0) / qMax(1, cell);

            const qreal factor = event->angleDelta().y() > 0 ? 1.10 : (1.0 / 1.10);
            zoomOverlayAt(logicalX, logicalY, factor);

            event->accept();
            return;
        }

        QWidget::wheelEvent(event);
    }

private:
    int tileSize() const
    {
        if (!m_sprites || m_groupStart < 0 || m_groupStart >= m_sprites->size())
            return 16;

        const int s = m_sprites->at(m_groupStart).size;
        return (s == 8) ? 8 : 16;
    }

    int logicalSize() const
    {
        return tileSize() * qBound(1, m_grid, 3);
    }

    int spriteIndexForGlobalPixel(int gx, int gy) const
    {
        const int tile = tileSize();
        const int col = gx / tile;
        const int row = gy / tile;
        const int part = row * qBound(1, m_grid, 3) + col;
        return m_groupStart + part;
    }

    int pixelAtGlobal(int gx, int gy) const
    {
        if (!m_sprites)
            return 0;

        const int tile = tileSize();
        const int spriteIndex = spriteIndexForGlobalPixel(gx, gy);
        if (spriteIndex < 0 || spriteIndex >= m_sprites->size())
            return 0;

        const CvBasicSpriteData& s = m_sprites->at(spriteIndex);
        const int lx = gx % tile;
        const int ly = gy % tile;
        const int idx = ly * s.size + lx;

        if (idx < 0 || idx >= s.pixels.size())
            return 0;

        return qBound(0, static_cast<int>(s.pixels[idx]), 15);
    }

    void setPixelAtGlobal(int gx, int gy, int color)
    {
        if (!m_sprites)
            return;

        const int tile = tileSize();
        const int spriteIndex = spriteIndexForGlobalPixel(gx, gy);
        if (spriteIndex < 0 || spriteIndex >= m_sprites->size())
            return;

        CvBasicSpriteData& s = (*m_sprites)[spriteIndex];
        const int lx = gx % tile;
        const int ly = gy % tile;
        const int idx = ly * s.size + lx;

        if (idx < 0 || idx >= s.pixels.size())
            return;

        s.pixels[idx] = static_cast<quint8>(qBound(0, color, 15));
    }

    QColor colecoColor(int idx) const
    {
        static const QColor pal[16] = {
            QColor("#000000"),
            QColor("#000000"),
            QColor("#21C842"),
            QColor("#5EDC78"),
            QColor("#5455ED"),
            QColor("#7D76FC"),
            QColor("#D4524D"),
            QColor("#42EBF5"),
            QColor("#FC5554"),
            QColor("#FF7978"),
            QColor("#D4C154"),
            QColor("#E6CE80"),
            QColor("#21B03B"),
            QColor("#C95BBA"),
            QColor("#CCCCCC"),
            QColor("#FFFFFF")
        };
        return pal[qBound(0, idx, 15)];
    }

    void floodFillAt(int gx, int gy, int newColor)
    {
        const int n = logicalSize();
        if (gx < 0 || gy < 0 || gx >= n || gy >= n)
            return;

        const int oldColor = pixelAtGlobal(gx, gy);
        newColor = qBound(0, newColor, 15);

        if (oldColor == newColor)
            return;

        QVector<QPoint> stack;
        stack.reserve(n * n);
        stack.append(QPoint(gx, gy));

        while (!stack.isEmpty()) {
            const QPoint pt = stack.takeLast();
            const int x = pt.x();
            const int y = pt.y();

            if (x < 0 || y < 0 || x >= n || y >= n)
                continue;

            if (pixelAtGlobal(x, y) != oldColor)
                continue;

            setPixelAtGlobal(x, y, newColor);

            stack.append(QPoint(x + 1, y));
            stack.append(QPoint(x - 1, y));
            stack.append(QPoint(x, y + 1));
            stack.append(QPoint(x, y - 1));
        }
    }

    void fitOverlayToCanvas()
    {
        if (m_overlayImage.isNull())
            return;

        const int n = logicalSize();
        const qreal sx = static_cast<qreal>(n) / qMax(1, m_overlayImage.width());
        const qreal sy = static_cast<qreal>(n) / qMax(1, m_overlayImage.height());
        m_overlayScale = qMax<qreal>(0.05, qMin(sx, sy));
        m_overlayPos = QPointF((n - m_overlayImage.width() * m_overlayScale) / 2.0,
                               (n - m_overlayImage.height() * m_overlayScale) / 2.0);
    }

    void apply(const QPoint& pos, bool erase)
    {
        if (!m_sprites || m_sprites->isEmpty())
            return;

        const int n = logicalSize();
        const int cell = qMax(6, qMin(width() / qMax(1, n), height() / qMax(1, n)));
        const int drawSize = n * cell;
        const int x0 = (width() - drawSize) / 2;
        const int y0 = (height() - drawSize) / 2;

        if (pos.x() < x0 || pos.y() < y0 || pos.x() >= x0 + drawSize || pos.y() >= y0 + drawSize)
            return;

        const int gx = (pos.x() - x0) / cell;
        const int gy = (pos.y() - y0) / cell;
        const int color = erase ? 0 : m_currentColor;

        if (m_toolMode == ToolMode::Fill)
            floodFillAt(gx, gy, color);
        else
            setPixelAtGlobal(gx, gy, color);

        update();

        if (onChanged)
            onChanged();
    }

private:
    QVector<CvBasicSpriteData>* m_sprites = nullptr;
    int m_groupStart = 0;
    int m_grid = 1;
    int m_currentColor = 15;
    ToolMode m_toolMode = ToolMode::Pen;
    QImage m_overlayImage;
    QPointF m_overlayPos = QPointF(0.0, 0.0);
    qreal m_overlayScale = 1.0;
    bool m_overlayMoveEnabled = false;
    bool m_draggingOverlay = false;
    QPoint m_lastOverlayDragPos;
};


class CvBasicSpritePreviewWidget final : public QWidget
{
public:
    explicit CvBasicSpritePreviewWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(180, 180);
    }

    void setSprites(const QVector<CvBasicSpriteData>* sprites)
    {
        m_sprites = sprites;
        update();
    }

    void setCurrentIndex(int index)
    {
        m_currentIndex = index;
        update();
    }

    void setGrid(int grid)
    {
        m_grid = qBound(1, grid, 3);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor("#000000"));

        if (!m_sprites || m_sprites->isEmpty())
            return;

        const int grid = qBound(1, m_grid, 3);
        const int maxItems = grid * grid;

        // Base size remains logically "real size":
        // 2x2 with 16x16 = 32x32 composition.
        // Then the full composition may be scaled proportionally.
        int tileSize = 8;
        bool foundAnySprite = false;

        for (int i = 0; i < maxItems; ++i) {
            const int spriteIndex = m_currentIndex + i;
            if (spriteIndex < 0 || spriteIndex >= m_sprites->size())
                break;

            const int s = m_sprites->at(spriteIndex).size;
            if (s == 8 || s == 16) {
                tileSize = foundAnySprite ? qMax(tileSize, s) : s;
                foundAnySprite = true;
            }
        }

        if (!foundAnySprite)
            tileSize = 8;

        const int logicalW = tileSize * grid;
        const int logicalH = tileSize * grid;

        const int margin = 10;
        const int availableW = qMax(1, width() - margin * 2);
        const int availableH = qMax(1, height() - margin * 2);

        // Stretch while preserving aspect ratio.
        // Integer scale keeps pixels sharp. In small windows, minimum is 1x.
        int scale = qMax(1, qMin(availableW / qMax(1, logicalW),
                                 availableH / qMax(1, logicalH)));

        const int compositeW = logicalW * scale;
        const int compositeH = logicalH * scale;

        const int x0 = (width() - compositeW) / 2;
        const int y0 = (height() - compositeH) / 2;

        QRect compositeRect(x0, y0, compositeW, compositeH);
        p.fillRect(compositeRect, QColor("#111111"));

        for (int i = 0; i < maxItems; ++i) {
            const int spriteIndex = m_currentIndex + i;
            if (spriteIndex < 0 || spriteIndex >= m_sprites->size())
                break;

            const CvBasicSpriteData& s = m_sprites->at(spriteIndex);
            const int col = i % grid;
            const int row = i / grid;

            // No gaps: sprites are placed directly next to each other.
            const int spriteX0 = x0 + col * tileSize * scale;
            const int spriteY0 = y0 + row * tileSize * scale;

            for (int y = 0; y < s.size; ++y) {
                for (int x = 0; x < s.size; ++x) {
                    const int idx = y * s.size + x;
                    const int c = idx < s.pixels.size() ? s.pixels[idx] : 0;
                    if (c != 0)
                        p.fillRect(spriteX0 + x * scale,
                                   spriteY0 + y * scale,
                                   scale,
                                   scale,
                                   colecoColor(c));
                }
            }
        }

        // Thin border around the complete composed sprite.
        p.setPen(QColor("#666666"));
        p.drawRect(compositeRect.adjusted(0, 0, -1, -1));

        // Guide lines between sprites, without adding spacing.
        p.setPen(QColor("#333333"));
        for (int i = 1; i < grid; ++i) {
            const int vx = x0 + i * tileSize * scale;
            const int hy = y0 + i * tileSize * scale;
            p.drawLine(vx, y0, vx, y0 + compositeH - 1);
            p.drawLine(x0, hy, x0 + compositeW - 1, hy);
        }

        // Small status at the bottom: logical size + scale.
        p.setPen(QColor("#9E9E9E"));
        p.drawText(QRect(0, height() - 20, width(), 18),
                   Qt::AlignCenter,
                   QString("%1x%2  x%3").arg(logicalW).arg(logicalH).arg(scale));
    }

private:
    QColor colecoColor(int idx) const
    {
        static const QColor pal[16] = {
            QColor("#000000"), QColor("#000000"), QColor("#21C842"), QColor("#5EDC78"),
            QColor("#5455ED"), QColor("#7D76FC"), QColor("#D4524D"), QColor("#42EBF5"),
            QColor("#FC5554"), QColor("#FF7978"), QColor("#D4C154"), QColor("#E6CE80"),
            QColor("#21B03B"), QColor("#C95BBA"), QColor("#CCCCCC"), QColor("#FFFFFF")
        };
        return pal[qBound(0, idx, 15)];
    }

private:
    const QVector<CvBasicSpriteData>* m_sprites = nullptr;
    int m_currentIndex = 0;
    int m_grid = 1;
};


static QIcon spriteResourceIcon(const QString& resourceName)
{
    // Expected resource aliases:
    //   :/SPR_PEN
    //   :/SPR_FILL
    //   :/SPR_LEFT
    //   :/SPR_RIGHT
    //   :/SPR_UP
    //   :/SPR_DOWN
    //   :/PNG_LOAD
    //   :/PNG_DEL
    //   :/PNG_LOCK
    //   :/SPR_CLEAR
    //
    // Extra fallback paths are included so it also works if you
    // later place them in a folder such as /icons or /sprites in the .qrc.
    const QStringList candidates = {
        ":/images/images/" + resourceName + ".png"
    };

    for (const QString& path : candidates) {
        if (QFile::exists(path))
            return QIcon(path);
    }

    // Fallback so the toolbar is not empty when the .qrc aliases are not yet
    // added. Normally you will not see this text if the resources exist.
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#FFFFFF"), 2));
    p.setBrush(QColor("#242424"));
    p.drawRoundedRect(QRect(1, 1, 30, 30), 4, 4);
    p.drawText(QRect(0, 0, 32, 32), Qt::AlignCenter, resourceName.mid(4, 1));

    return QIcon(pix);
}

static QIcon spriteTextIcon(const QString& text)
{
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#FFFFFF"), 1));
    p.setBrush(QColor("#242424"));
    p.drawRoundedRect(QRect(1, 1, 30, 30), 4, 4);

    QFont f = p.font();
    f.setBold(true);
    if (text.size() <= 1)
        f.setPointSize(14);
    else if (text.size() == 2)
        f.setPointSize(10);
    else
        f.setPointSize(8);
    p.setFont(f);
    p.drawText(QRect(0, 0, 32, 32), Qt::AlignCenter, text);

    return QIcon(pix);
}


class CvBasicSpriteDialog final : public QWidget
{
public:
    explicit CvBasicSpriteDialog(const QString& sourceDir, QWidget* parent = nullptr)
        : QWidget(parent),
          m_sourceDir(sourceDir)
    {
        setObjectName("cvBasicSpritePage");
        resize(1050, 700);

        setupUi();
        newProject(false);
    }

    QString outputText() const
    {
        return selectedSpriteOutput();
    }

    QString selectedSpriteOutput() const
    {
        return buildOutput();
    }

    bool canUndoSpriteEdit() const
    {
        return m_undoStack.size() > 1;
    }

    bool canRedoSpriteEdit() const
    {
        return !m_redoStack.isEmpty();
    }

    void undoSpriteEdit()
    {
        if (m_undoStack.size() <= 1)
            return;

        const QVector<CvBasicSpriteData> current = m_undoStack.takeLast();
        m_redoStack.append(current);

        restoreSpriteState(m_undoStack.last());
    }

    void redoSpriteEdit()
    {
        if (m_redoStack.isEmpty())
            return;

        const QVector<CvBasicSpriteData> state = m_redoStack.takeLast();
        m_undoStack.append(state);

        restoreSpriteState(state);
    }

    void menuNewProject()
    {
        newProject(true);
    }

    void menuOpenProject()
    {
        openProject();
    }

    void menuSaveProject()
    {
        saveProject();
    }

    void menuSaveAsProject()
    {
        saveProjectAs();
    }

    std::function<void(const QString&)> onInsertRequested;

private:
    void pushSpriteUndoState()
    {
        if (m_restoringUndoRedo)
            return;

        if (!m_undoStack.isEmpty() && spriteStatesEqual(m_undoStack.last(), m_sprites))
            return;

        m_undoStack.append(m_sprites);

        constexpr int maxUndoSteps = 100;
        while (m_undoStack.size() > maxUndoSteps)
            m_undoStack.removeFirst();

        m_redoStack.clear();
    }

    void resetSpriteUndoHistory()
    {
        m_undoStack.clear();
        m_redoStack.clear();
        m_undoStack.append(m_sprites);
    }

    bool spriteStatesEqual(const QVector<CvBasicSpriteData>& a,
                           const QVector<CvBasicSpriteData>& b) const
    {
        if (a.size() != b.size())
            return false;

        for (int i = 0; i < a.size(); ++i) {
            const CvBasicSpriteData& sa = a[i];
            const CvBasicSpriteData& sb = b[i];

            if (sa.name != sb.name ||
                sa.size != sb.size ||
                sa.grid != sb.grid ||
                sa.color != sb.color ||
                sa.pixels != sb.pixels) {
                return false;
            }
        }

        return true;
    }

    void restoreSpriteState(const QVector<CvBasicSpriteData>& state)
    {
        m_restoringUndoRedo = true;

        const int currentStart = qMax(0, currentIndex());

        m_sprites = state;
        normalizeLoadedSprites();

        if (m_sprites.isEmpty())
            m_sprites.append(makeSprite("Sprite1"));

        m_dirty = true;

        updateList();
        reloadCurrentSpriteView(qMin(currentStart, m_sprites.size() - 1));

        m_restoringUndoRedo = false;
    }

    void setupUi()
    {
        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(8, 8, 8, 8);
        root->setSpacing(6);

        QToolBar* spriteToolbar = new QToolBar(tr("Sprites"), this);
        spriteToolbar->setObjectName("spritePageToolbar");
        spriteToolbar->setMovable(false);
        spriteToolbar->setFloatable(false);
        spriteToolbar->setIconSize(QSize(20, 20));

        QAction* actNewSpriteProject = spriteToolbar->addAction(tr("New"));
        QAction* actOpenSpriteProject = spriteToolbar->addAction(tr("Open"));
        QAction* actSaveSpriteProject = spriteToolbar->addAction(tr("Save"));
        QAction* actSaveSpriteProjectAs = spriteToolbar->addAction(tr("Save As"));
        spriteToolbar->addSeparator();
        QAction* actCopyOutput = spriteToolbar->addAction(tr("Copy Selected"));
        QAction* actInsertInEditor = spriteToolbar->addAction(tr("Insert Selected in Editor"));
        spriteToolbar->addSeparator();
        QAction* actSpriteUndo = spriteToolbar->addAction(tr("Undo"));
        QAction* actSpriteRedo = spriteToolbar->addAction(tr("Redo"));

        root->addWidget(spriteToolbar);

        connect(actNewSpriteProject, &QAction::triggered, this, [this]() { newProject(true); });
        connect(actOpenSpriteProject, &QAction::triggered, this, [this]() { openProject(); });
        connect(actSaveSpriteProject, &QAction::triggered, this, [this]() { saveProject(); });
        connect(actSaveSpriteProjectAs, &QAction::triggered, this, [this]() { saveProjectAs(); });
        connect(actCopyOutput, &QAction::triggered, this, [this]() {
            QApplication::clipboard()->setText(selectedSpriteOutput());
        });
        connect(actInsertInEditor, &QAction::triggered, this, [this]() {
            m_insertText = selectedSpriteOutput();
            if (onInsertRequested)
                onInsertRequested(m_insertText);
        });

        connect(actSpriteUndo, &QAction::triggered, this, [this]() {
            undoSpriteEdit();
        });

        connect(actSpriteRedo, &QAction::triggered, this, [this]() {
            redoSpriteEdit();
        });

        QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
        root->addWidget(splitter, 1);

        // Left side
        QWidget* left = new QWidget(splitter);
        QVBoxLayout* leftLayout = new QVBoxLayout(left);

        QHBoxLayout* spriteTop = new QHBoxLayout();
        spriteTop->addWidget(new QLabel(tr("Name:"), left));
        m_nameEdit = new QLineEdit(left);
        spriteTop->addWidget(m_nameEdit, 1);

        spriteTop->addWidget(new QLabel(tr("Size:"), left));
        m_sizeCombo = new QComboBox(left);
        m_sizeCombo->addItem("8x8", 8);
        m_sizeCombo->addItem("16x16", 16);
        spriteTop->addWidget(m_sizeCombo);

        spriteTop->addWidget(new QLabel(tr("Layout:"), left));
        m_previewGridCombo = new QComboBox(left);
        m_previewGridCombo->addItem("1 x 1", 1);
        m_previewGridCombo->addItem("2 x 2", 2);
        m_previewGridCombo->addItem("3 x 3", 3);
        spriteTop->addWidget(m_previewGridCombo);

        QPushButton* applySettingsBtn = new QPushButton(tr("OK"), left);
        applySettingsBtn->setFixedWidth(54);
        spriteTop->addWidget(applySettingsBtn);

        leftLayout->addLayout(spriteTop);

        // Name / Size / Layout are only applied when OK is pressed.
        // This lets you change settings without rebuilding the sprite group immediately.
        connect(applySettingsBtn, &QPushButton::clicked, this, [this]() {
            applySpriteSettings();
        });

        QLabel* editTitle = new QLabel(tr("Edit Sprite"), left);
        leftLayout->addWidget(editTitle);

        QToolBar* editSpriteToolbar = new QToolBar(tr("Edit Sprite Tools"), left);
        editSpriteToolbar->setObjectName("editSpriteToolbar");
        editSpriteToolbar->setMovable(false);
        editSpriteToolbar->setFloatable(false);
        editSpriteToolbar->setIconSize(QSize(38, 38));
        editSpriteToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

        QActionGroup* drawToolGroup = new QActionGroup(editSpriteToolbar);
        drawToolGroup->setExclusive(true);

        QAction* actPenTool = editSpriteToolbar->addAction(spriteResourceIcon("SPR_PEN"), tr("Pen"));
        actPenTool->setCheckable(true);
        actPenTool->setChecked(true);
        drawToolGroup->addAction(actPenTool);

        QAction* actFillTool = editSpriteToolbar->addAction(spriteResourceIcon("SPR_FILL"), tr("Fill"));
        actFillTool->setCheckable(true);
        drawToolGroup->addAction(actFillTool);

        QAction* actClearSprite = editSpriteToolbar->addAction(spriteResourceIcon("SPR_CLEAR"), tr("Clear Sprite"));

        editSpriteToolbar->addSeparator();

        QAction* actLoadOverlayPng = editSpriteToolbar->addAction(spriteResourceIcon("PNG_LOAD"), tr("Load Background PNG"));
        QAction* actRemoveOverlayPng = editSpriteToolbar->addAction(spriteResourceIcon("PNG_DEL"), tr("Remove Background PNG"));
        QAction* actOverlayMoveMode = editSpriteToolbar->addAction(spriteResourceIcon("PNG_LOCK"), tr("Move / Lock Background PNG"));
        actOverlayMoveMode->setCheckable(true);

        editSpriteToolbar->addSeparator();

        QAction* actMoveLeft = editSpriteToolbar->addAction(spriteResourceIcon("SPR_LEFT"), tr("Move Left"));
        QAction* actMoveRight = editSpriteToolbar->addAction(spriteResourceIcon("SPR_RIGHT"), tr("Move Right"));
        QAction* actMoveUp = editSpriteToolbar->addAction(spriteResourceIcon("SPR_UP"), tr("Move Up"));
        QAction* actMoveDown = editSpriteToolbar->addAction(spriteResourceIcon("SPR_DOWN"), tr("Move Down"));

        leftLayout->addWidget(editSpriteToolbar);

        m_canvas = new CvBasicSpriteCanvas(left);

        connect(actPenTool, &QAction::triggered, this, [this]() {
            if (m_canvas)
                m_canvas->setToolMode(CvBasicSpriteCanvas::ToolMode::Pen);
        });

        connect(actFillTool, &QAction::triggered, this, [this]() {
            if (m_canvas)
                m_canvas->setToolMode(CvBasicSpriteCanvas::ToolMode::Fill);
        });

        connect(actClearSprite, &QAction::triggered, this, [this]() {
            clearCurrentSpritePixels();
        });

        connect(actLoadOverlayPng, &QAction::triggered, this, [this]() {
            const QString filePath = QFileDialog::getOpenFileName(
                this,
                tr("Load Background PNG"),
                m_sourceDir,
                tr("PNG Images (*.png)"));

            if (filePath.isEmpty())
                return;

            if (m_canvas && !m_canvas->loadOverlayImage(filePath))
                QMessageBox::warning(this, tr("Load Background PNG"), tr("The selected PNG could not be loaded."));
        });

        connect(actRemoveOverlayPng, &QAction::triggered, this, [this, actOverlayMoveMode]() {
            if (m_canvas)
                m_canvas->clearOverlayImage();
            actOverlayMoveMode->setChecked(false);
        });

        connect(actOverlayMoveMode, &QAction::toggled, this, [this](bool checked) {
            if (m_canvas)
                m_canvas->setOverlayMoveEnabled(checked);
        });

        connect(actMoveLeft, &QAction::triggered, this, [this]() { moveCompositePixels(-1, 0); });
        connect(actMoveRight, &QAction::triggered, this, [this]() { moveCompositePixels(1, 0); });
        connect(actMoveUp, &QAction::triggered, this, [this]() { moveCompositePixels(0, -1); });
        connect(actMoveDown, &QAction::triggered, this, [this]() { moveCompositePixels(0, 1); });

        m_canvas->onChanged = [this]() {
            m_dirty = true;
            if (m_preview)
                m_preview->update();

            pushSpriteUndoState();
            refreshOutput();
        };
        leftLayout->addWidget(m_canvas, 1);

        QGroupBox* palBox = new QGroupBox(tr("Coleco 16 Color Palette / Preview"), left);
        QHBoxLayout* palMainLayout = new QHBoxLayout(palBox);

        QWidget* palettePanel = new QWidget(palBox);
        QGridLayout* palLayout = new QGridLayout(palettePanel);

        static const QStringList pal = {
            "#000000", "#000000", "#21C842", "#5EDC78",
            "#5455ED", "#7D76FC", "#D4524D", "#42EBF5",
            "#FC5554", "#FF7978", "#D4C154", "#E6CE80",
            "#21B03B", "#C95BBA", "#CCCCCC", "#FFFFFF"
        };

        for (int i = 1; i < 16; ++i) {
            QPushButton* b = new QPushButton(QString::number(i), palettePanel);
            b->setFixedSize(42, 28);
            const QString fg = (i == 10 || i == 11 || i == 14 || i == 15) ? "#000000" : "#FFFFFF";
            b->setStyleSheet(QString("QPushButton{background:%1;color:%2;border:1px solid #666;}").arg(pal[i], fg));
            connect(b, &QPushButton::clicked, this, [this, i]() { setCurrentColor(i); });
            palLayout->addWidget(b, (i - 1) / 5, (i - 1) % 5);
        }

        palMainLayout->addWidget(palettePanel, 0);

        QWidget* previewPanel = new QWidget(palBox);
        QVBoxLayout* previewLayout = new QVBoxLayout(previewPanel);
        previewLayout->setContentsMargins(0, 0, 0, 0);
        previewLayout->addWidget(new QLabel(tr("Preview"), previewPanel));

        m_preview = new CvBasicSpritePreviewWidget(previewPanel);
        m_preview->setSprites(&m_sprites);
        previewLayout->addWidget(m_preview, 1);

        palMainLayout->addWidget(previewPanel, 1);

        leftLayout->addWidget(palBox);

        // Right side
        QWidget* right = new QWidget(splitter);
        QVBoxLayout* rightLayout = new QVBoxLayout(right);

        QGroupBox* listBox = new QGroupBox(tr("Sprites"), right);
        QVBoxLayout* listLayout = new QVBoxLayout(listBox);

        m_list = new QListWidget(listBox);
        listLayout->addWidget(m_list, 1);
        connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
            if (!current)
                return;

            const int startIndex = current->data(Qt::UserRole).toInt();
            loadSprite(startIndex);
        });

        connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
            if (!item)
                return;

            const int startIndex = item->data(Qt::UserRole).toInt();
            loadSprite(startIndex);

            if (m_canvas)
                m_canvas->setFocus(Qt::OtherFocusReason);
        });

        QGridLayout* listButtons = new QGridLayout();
        QPushButton* addBtn = new QPushButton(tr("Add"), listBox);
        QPushButton* dupBtn = new QPushButton(tr("Duplicate"), listBox);
        QPushButton* delBtn = new QPushButton(tr("Delete"), listBox);
        QPushButton* upBtn = new QPushButton(tr("Up"), listBox);
        QPushButton* downBtn = new QPushButton(tr("Down"), listBox);
        listButtons->addWidget(addBtn, 0, 0);
        listButtons->addWidget(dupBtn, 0, 1);
        listButtons->addWidget(delBtn, 0, 2);
        listButtons->addWidget(upBtn, 1, 0);
        listButtons->addWidget(downBtn, 1, 1);
        listLayout->addLayout(listButtons);
        rightLayout->addWidget(listBox, 1);

        connect(addBtn, &QPushButton::clicked, this, [this]() { addSprite(); });
        connect(dupBtn, &QPushButton::clicked, this, [this]() { duplicateSprite(); });
        connect(delBtn, &QPushButton::clicked, this, [this]() { deleteSprite(); });
        connect(upBtn, &QPushButton::clicked, this, [this]() { moveSprite(-1); });
        connect(downBtn, &QPushButton::clicked, this, [this]() { moveSprite(1); });

        QGroupBox* outBox = new QGroupBox(tr("CVBasic DATA"), right);
        QVBoxLayout* outLayout = new QVBoxLayout(outBox);

        QHBoxLayout* outTop = new QHBoxLayout();
        m_verboseOutputCheck = new QCheckBox(tr("Extended text"), outBox);
        m_verboseOutputCheck->setChecked(true);
        outTop->addWidget(m_verboseOutputCheck);

        m_placeSpriteTextCheck = new QCheckBox(tr("Place sprite pos X,Y text"), outBox);
        m_placeSpriteTextCheck->setChecked(false);
        outTop->addWidget(m_placeSpriteTextCheck);

        outTop->addStretch(1);
        outLayout->addLayout(outTop);

        m_output = new QPlainTextEdit(outBox);
        m_output->setReadOnly(true);
        outLayout->addWidget(m_output, 1);
        rightLayout->addWidget(outBox, 1);

        connect(m_verboseOutputCheck, &QCheckBox::toggled, this, [this]() {
            refreshOutput();
        });

        connect(m_placeSpriteTextCheck, &QCheckBox::toggled, this, [this]() {
            refreshOutput();
        });

        splitter->addWidget(left);
        splitter->addWidget(right);
        splitter->setSizes({560, 480});

        setStyleSheet(
            "QDialog { background-color:#3A3A3A; color:#FFFFFF; }"
            "QWidget { background-color:#3A3A3A; color:#FFFFFF; }"
            "QGroupBox { border:1px solid #666666; margin-top:8px; }"
            "QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; }"
            "QLineEdit, QComboBox, QListWidget, QPlainTextEdit { background-color:#242424; color:#FFFFFF; border:1px solid #666666; }"
            "QCheckBox { background-color:#3A3A3A; color:#FFFFFF; }"
            "QToolBar#spritePageToolbar { background-color:#3A3A3A; border:1px solid #555555; spacing:4px; padding:4px; }"
            "QToolBar#editSpriteToolbar { background-color:#3A3A3A; border:1px solid #555555; spacing:4px; padding:4px; }"
            "QToolBar#editSpriteToolbar QToolButton { background:transparent; border:none; padding:3px; margin:1px; }"
            "QToolBar#editSpriteToolbar QToolButton:hover { background-color:#4A4A4A; border:1px solid #666666; padding:2px; }"
            "QToolBar#editSpriteToolbar QToolButton:checked { background-color:#555555; border:1px solid #888888; padding:2px; }"
            "QToolBar#editSpriteToolbar QToolButton:pressed { background-color:#5A5A5A; border:1px solid #AAAAAA; padding:2px; }"
            "QToolButton { background-color:#242424; color:#FFFFFF; border:1px solid #5C5C5C; padding:4px 8px; }"
            "QToolButton:hover { background-color:#4A4A4A; }"
            "QToolButton:pressed { background-color:#5A5A5A; padding-top:5px; padding-left:9px; }"
            "QPushButton { background-color:#242424; color:#FFFFFF; border:1px solid #666666; padding:4px 8px; }"
            "QPushButton:hover { background-color:#4A4A4A; }"
        );
    }

    CvBasicSpriteData makeSprite(const QString& name = QString()) const
    {
        CvBasicSpriteData s;
        s.name = name.isEmpty() ? QString("Sprite%1").arg(m_sprites.size() + 1) : name;
        s.size = 16;
        s.grid = 1;
        s.color = 15;
        s.pixels.fill(0, s.size * s.size);
        return s;
    }

    QString safeName(QString s, int index) const
    {
        s = s.trimmed();
        if (s.isEmpty())
            s = QString("Sprite%1").arg(index + 1);
        s.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
        if (s.at(0).isDigit())
            s.prepend("SPR_");
        return s;
    }

    int currentIndex() const
    {
        if (!m_list)
            return -1;

        QListWidgetItem* item = m_list->currentItem();
        if (!item)
            return -1;

        return item->data(Qt::UserRole).toInt();
    }

    int groupGridForIndex(int index) const
    {
        if (index < 0 || index >= m_sprites.size())
            return qBound(1, m_grid, 3);

        return qBound(1, m_sprites[index].grid, 3);
    }

    int groupSizeForIndex(int index) const
    {
        const int g = groupGridForIndex(index);
        return qMax(1, g * g);
    }

    int nextGroupStart(int index) const
    {
        if (index < 0 || index >= m_sprites.size())
            return m_sprites.size();

        return qMin(m_sprites.size(), index + groupSizeForIndex(index));
    }

    void selectGroupStart(int start)
    {
        if (!m_list)
            return;

        for (int row = 0; row < m_list->count(); ++row) {
            QListWidgetItem* item = m_list->item(row);
            if (item && item->data(Qt::UserRole).toInt() == start) {
                m_list->setCurrentRow(row);
                return;
            }
        }

        if (m_list->count() > 0)
            m_list->setCurrentRow(0);
    }


    QString defaultProjectPath() const
    {
        QDir dir(m_sourceDir);
        if (!dir.exists())
            dir.mkpath(".");
        return QDir::cleanPath(dir.filePath("sprites.cvsprj"));
    }

    bool maybeSave()
    {
        if (!m_dirty)
            return true;

        const QMessageBox::StandardButton ret = QMessageBox::question(
            this, tr("Sprite Project"),
            tr("Sprite project has changed. Save it?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);

        if (ret == QMessageBox::Save)
            return saveProject();
        if (ret == QMessageBox::Discard)
            return true;
        return false;
    }

    QJsonObject spritePartToJson(const CvBasicSpriteData& s) const
    {
        QJsonObject o;
        o["name"] = s.name;
        o["size"] = s.size;
        o["grid"] = s.grid;
        o["color"] = s.color;

        QJsonArray px;
        for (quint8 v : s.pixels)
            px.append(static_cast<int>(qBound(0, static_cast<int>(v), 15)));

        o["pixels"] = px;
        return o;
    }

    CvBasicSpriteData spritePartFromJson(const QJsonObject& o,
                                         int fallbackGrid,
                                         int fallbackSize,
                                         const QString& fallbackName) const
    {
        CvBasicSpriteData s;
        s.name = o.value("name").toString(fallbackName);

        s.size = o.value("size").toInt(fallbackSize);
        if (s.size != 8 && s.size != 16)
            s.size = fallbackSize;

        s.grid = qBound(1, o.value("grid").toInt(fallbackGrid), 3);
        s.color = qBound(1, o.value("color").toInt(15), 15);

        const QJsonArray px = o.value("pixels").toArray();
        s.pixels.resize(s.size * s.size);

        for (int i = 0; i < s.pixels.size() && i < px.size(); ++i)
            s.pixels[i] = static_cast<quint8>(qBound(0, px.at(i).toInt(), 15));

        return s;
    }

    QJsonArray buildProjectGroupsJson() const
    {
        QJsonArray groups;

        for (int start = 0; start < m_sprites.size(); ) {
            const int grid = groupGridForIndex(start);
            const int groupSize = groupSizeForIndex(start);
            const int size = (start >= 0 && start < m_sprites.size()) ? m_sprites[start].size : 16;
            const QString base = (start >= 0 && start < m_sprites.size())
                ? baseNameFromPart(m_sprites[start].name)
                : QString("Sprite");

            QJsonObject group;
            group["name"] = base;
            group["grid"] = grid;
            group["size"] = size;

            QJsonArray parts;
            for (int i = 0; i < groupSize && (start + i) < m_sprites.size(); ++i)
                parts.append(spritePartToJson(m_sprites[start + i]));

            group["parts"] = parts;
            groups.append(group);

            start += groupSize;
        }

        return groups;
    }

    bool loadProjectFromJson(const QJsonObject& root)
    {
        m_sprites.clear();

        const QJsonArray groups = root.value("groups").toArray();

        if (!groups.isEmpty()) {
            for (const QJsonValue& gv : groups) {
                const QJsonObject group = gv.toObject();
                const QString baseName = group.value("name").toString("Sprite");
                const int grid = qBound(1, group.value("grid").toInt(1), 3);
                const int size = (group.value("size").toInt(16) == 8) ? 8 : 16;
                const int expectedCount = grid * grid;

                const QJsonArray parts = group.value("parts").toArray();

                for (int i = 0; i < expectedCount; ++i) {
                    const QString partName = QString("%1_%2").arg(safeName(baseName, 0), suffixForIndex(i));
                    CvBasicSpriteData s;

                    if (i < parts.size())
                        s = spritePartFromJson(parts.at(i).toObject(), grid, size, partName);
                    else
                        s = makeSprite(partName);

                    s.name = partName;
                    s.grid = grid;
                    s.size = size;

                    if (s.pixels.size() != s.size * s.size)
                        s.pixels.resize(s.size * s.size);

                    m_sprites.append(s);
                }
            }

            return true;
        }

        // Legacy project format support: old files stored all internal sprite parts directly in "sprites".
        const int fallbackGrid = qBound(1, root.value("grid").toInt(1), 3);
        const QJsonArray arr = root.value("sprites").toArray();

        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            CvBasicSpriteData s = spritePartFromJson(o, fallbackGrid, 16, "Sprite");
            m_sprites.append(s);
        }

        return !m_sprites.isEmpty();
    }

    void clearSpriteEditor()
    {
        m_sprites.clear();
        m_grid = 1;
        m_projectPath.clear();
        m_dirty = false;

        if (m_list)
            m_list->clear();

        if (m_preview) {
            m_preview->setSprites(&m_sprites);
            m_preview->update();
        }

        if (m_canvas)
            m_canvas->update();

        refreshOutput();
    }


    void newProject(bool ask)
    {
        if (ask && !maybeSave())
            return;

        clearSpriteEditor();

        m_grid = m_previewGridCombo ? m_previewGridCombo->currentData().toInt() : 1;
        createCompositeSprites("Sprite1", m_grid);
        m_dirty = false;

        updateList();
        reloadCurrentSpriteView(0);
        resetSpriteUndoHistory();
    }

    void openProject()
    {
        if (!maybeSave())
            return;

        const QString file = QFileDialog::getOpenFileName(
            this, tr("Open Sprite Project"),
            m_sourceDir,
            tr("CVBasic Sprite Project (*.cvsprj);;All Files (*.*)"));

        if (file.isEmpty())
            return;

        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Open failed"), f.errorString());
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) {
            QMessageBox::warning(this, tr("Open failed"), tr("Invalid sprite project file."));
            return;
        }

        // Clear the editor first, then reload the complete project.
        clearSpriteEditor();

        const QJsonObject root = doc.object();
        if (!loadProjectFromJson(root)) {
            m_sprites.append(makeSprite("Sprite1"));
        }

        normalizeLoadedSprites();

        m_projectPath = file;
        m_dirty = false;

        updateList();
        reloadCurrentSpriteView(0);
        resetSpriteUndoHistory();

        QMessageBox::information(
            this,
            tr("Open Sprite Project"),
            tr("Loaded %1 sprite part(s) from project.").arg(m_sprites.size())
        );
    }

    bool saveProject()
    {
        if (m_projectPath.isEmpty())
            return saveProjectAs();

        normalizeLoadedSprites();

        QJsonObject root;
        root["format"] = "ADAMP_CVBasic_SpriteProject";
        root["type"] = "project";
        root["version"] = 2;
        root["backend"] = "TMS9918";
        root["grid"] = m_grid;

        // Main project format: grouped sprites.
        // This preserves mixed 1x1 / 2x2 / 3x3 sprite projects.
        root["groups"] = buildProjectGroupsJson();

        // Compatibility format: raw internal sprite parts.
        QJsonArray arr;
        for (const CvBasicSpriteData& s : m_sprites)
            arr.append(spritePartToJson(s));
        root["sprites"] = arr;

        QFile f(m_projectPath);
        QDir().mkpath(QFileInfo(m_projectPath).absolutePath());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, tr("Save failed"), f.errorString());
            return false;
        }

        if (f.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
            QMessageBox::warning(this, tr("Save failed"), f.errorString());
            return false;
        }

        f.close();

        m_dirty = false;

        QMessageBox::information(
            this,
            tr("Save Sprite Project"),
            tr("Saved complete sprite project:\\n%1\\n\\nSprite parts saved: %2")
                .arg(QDir::toNativeSeparators(m_projectPath))
                .arg(m_sprites.size())
        );

        return true;
    }

    bool saveProjectAs()
    {
        const QString file = QFileDialog::getSaveFileName(
            this, tr("Save Sprite Project"),
            m_projectPath.isEmpty() ? defaultProjectPath() : m_projectPath,
            tr("CVBasic Sprite Project (*.cvsprj);;All Files (*.*)"));

        if (file.isEmpty())
            return false;

        m_projectPath = file.endsWith(".cvsprj", Qt::CaseInsensitive) ? file : file + ".cvsprj";
        return saveProject();
    }

    void normalizeLoadedSprites()
    {
        if (m_sprites.isEmpty())
            return;

        for (CvBasicSpriteData& s : m_sprites) {
            if (s.size != 8 && s.size != 16)
                s.size = 16;

            s.grid = qBound(1, s.grid, 3);
            s.color = qBound(1, s.color, 15);

            const int expectedPixels = s.size * s.size;

            if (s.pixels.size() != expectedPixels)
                s.pixels.resize(expectedPixels);

            for (int i = 0; i < s.pixels.size(); ++i)
                s.pixels[i] = static_cast<quint8>(qBound(0, static_cast<int>(s.pixels[i]), 15));
        }
    }

    void reloadCurrentSpriteView(int startIndex)
    {
        if (m_sprites.isEmpty())
            return;

        startIndex = groupStartForRow(qBound(0, startIndex, m_sprites.size() - 1));

        selectGroupStart(startIndex);
        loadSprite(startIndex);

        if (m_canvas) {
            m_canvas->setComposite(&m_sprites, startIndex, groupGridForIndex(startIndex));
            m_canvas->updateGeometry();
            m_canvas->update();
        }

        if (m_preview) {
            m_preview->setSprites(&m_sprites);
            m_preview->setGrid(groupGridForIndex(startIndex));
            m_preview->setCurrentIndex(startIndex);
            m_preview->updateGeometry();
            m_preview->update();
        }

        refreshOutput();
    }


    void updateList()
    {
        if (!m_list)
            return;

        int oldStart = currentIndex();
        if (oldStart < 0 || oldStart >= m_sprites.size())
            oldStart = 0;

        m_list->clear();

        int groupNumber = 1;
        for (int i = 0; i < m_sprites.size(); ) {
            const int grid = groupGridForIndex(i);
            const int groupSize = groupSizeForIndex(i);
            const QString base = baseNameFromPart(m_sprites[i].name);

            QListWidgetItem* item = new QListWidgetItem(
                QString("%1. %2_%3x%3").arg(groupNumber).arg(base).arg(grid),
                m_list
            );
            item->setData(Qt::UserRole, i);
            item->setToolTip(QString("Grouped sprite: %1 part(s)").arg(groupSize));

            i += groupSize;
            ++groupNumber;
        }

        if (!m_sprites.isEmpty())
            selectGroupStart(oldStart >= 0 ? oldStart : 0);
    }


    void loadSprite(int row)
    {
        if (row < 0 || row >= m_sprites.size())
            return;

        const int start = groupStartForRow(row);
        CvBasicSpriteData& s = m_sprites[start];
        m_grid = groupGridForIndex(start);

        m_nameEdit->blockSignals(true);
        m_nameEdit->setText(baseNameFromPart(s.name));
        m_nameEdit->blockSignals(false);

        m_sizeCombo->blockSignals(true);
        m_sizeCombo->setCurrentIndex(s.size == 8 ? 0 : 1);
        m_sizeCombo->blockSignals(false);

        if (m_previewGridCombo) {
            m_previewGridCombo->blockSignals(true);
            m_previewGridCombo->setCurrentIndex(qBound(0, m_grid - 1, 2));
            m_previewGridCombo->blockSignals(false);
        }

        m_canvas->setCurrentColor(s.color);
        m_canvas->setComposite(&m_sprites, start, m_grid);

        if (m_preview) {
            m_preview->setSprites(&m_sprites);
            m_preview->setGrid(m_grid);
            m_preview->setCurrentIndex(start);
            m_preview->update();
        }

        if (m_canvas)
            m_canvas->update();

        refreshOutput();
    }

    void addSprite()
    {
        m_grid = m_previewGridCombo ? m_previewGridCombo->currentData().toInt() : m_grid;
        m_grid = qBound(1, m_grid, 3);

        const QString base = QString("Sprite%1").arg(m_list ? (m_list->count() + 1) : (m_sprites.size() + 1));
        const int insertAt = m_sprites.size();

        QVector<CvBasicSpriteData> group = makeCompositeSprites(base, m_grid);
        for (const CvBasicSpriteData& s : group)
            m_sprites.append(s);

        m_dirty = true;
        updateList();
        if (m_preview) m_preview->setSprites(&m_sprites);
        reloadCurrentSpriteView(insertAt);
        pushSpriteUndoState();
    }

    void duplicateSprite()
    {
        const int row = currentIndex();
        if (row < 0 || row >= m_sprites.size())
            return;

        const int start = groupStartForRow(row);
        const int groupSize = groupSizeForIndex(start);
        const int end = qMin(start + groupSize, m_sprites.size());

        QVector<CvBasicSpriteData> copy;
        for (int i = start; i < end; ++i)
            copy.append(m_sprites[i]);

        const QString base = baseNameFromPart(m_sprites[start].name) + "_copy";
        applyCompositeNames(copy, base);

        int insertAt = end;
        for (const CvBasicSpriteData& s : copy)
            m_sprites.insert(insertAt++, s);

        m_dirty = true;
        updateList();
        if (m_preview) m_preview->setSprites(&m_sprites);
        reloadCurrentSpriteView(end);
        pushSpriteUndoState();
    }

    void deleteSprite()
    {
        const int row = currentIndex();
        if (row < 0 || row >= m_sprites.size())
            return;

        const int start = groupStartForRow(row);
        const int groupSize = groupSizeForIndex(start);
        if (m_sprites.size() <= groupSize)
            return;
        const int count = qMin(groupSize, m_sprites.size() - start);
        for (int i = 0; i < count; ++i)
            m_sprites.removeAt(start);

        m_dirty = true;
        updateList();
        if (m_preview) m_preview->setSprites(&m_sprites);
        reloadCurrentSpriteView(qMin(start, m_sprites.size() - 1));
        pushSpriteUndoState();
    }

    void moveSprite(int dir)
    {
        const int row = currentIndex();
        if (row < 0 || row >= m_sprites.size())
            return;

        const int start = groupStartForRow(row);
        const int groupSize = groupSizeForIndex(start);
        const int targetStart = start + (dir * groupSize);

        if (targetStart < 0 || targetStart >= m_sprites.size())
            return;

        QVector<CvBasicSpriteData> group;
        for (int i = 0; i < groupSize && start < m_sprites.size(); ++i) {
            group.append(m_sprites[start]);
            m_sprites.removeAt(start);
        }

        int insertAt = qBound(0, targetStart, m_sprites.size());
        for (const CvBasicSpriteData& s : group)
            m_sprites.insert(insertAt++, s);

        m_dirty = true;
        updateList();
        if (m_preview) m_preview->setSprites(&m_sprites);
        reloadCurrentSpriteView(qBound(0, targetStart, m_sprites.size() - 1));
        pushSpriteUndoState();
    }

    QString suffixForIndex(int index) const
    {
        static const QStringList suffixes = {"a","b","c","d","e","f","g","h","i"};
        return suffixes.value(index, QString::number(index + 1));
    }

    QString baseNameFromPart(QString name) const
    {
        name = name.trimmed();
        QRegularExpression re(QStringLiteral("^(.*)_([a-iA-I])$"));
        const QRegularExpressionMatch m = re.match(name);
        if (m.hasMatch())
            return m.captured(1);
        return name;
    }

    int groupStartForRow(int row) const
    {
        if (row <= 0)
            return 0;

        int i = 0;
        while (i < m_sprites.size()) {
            const int groupSize = groupSizeForIndex(i);
            if (row >= i && row < i + groupSize)
                return i;
            i += groupSize;
        }

        return qBound(0, row, qMax(0, m_sprites.size() - 1));
    }

    QVector<CvBasicSpriteData> makeCompositeSprites(const QString& baseName, int grid) const
    {
        QVector<CvBasicSpriteData> result;
        grid = qBound(1, grid, 3);
        const int count = grid * grid;
        for (int i = 0; i < count; ++i) {
            CvBasicSpriteData s = makeSprite(QString("%1_%2").arg(safeName(baseName, 0), suffixForIndex(i)));
            s.grid = grid;
            result.append(s);
        }
        return result;
    }

    void createCompositeSprites(const QString& baseName, int grid)
    {
        QVector<CvBasicSpriteData> group = makeCompositeSprites(baseName, grid);
        for (const CvBasicSpriteData& s : group)
            m_sprites.append(s);
    }

    void applyCompositeNames(QVector<CvBasicSpriteData>& group, const QString& baseName) const
    {
        const int grid = qBound(1, qRound(qSqrt(group.size())), 3);
        for (int i = 0; i < group.size(); ++i) {
            group[i].name = QString("%1_%2").arg(safeName(baseName, 0), suffixForIndex(i));
            group[i].grid = grid;
        }
    }

    void renameComposite(const QString& text)
    {
        const int row = currentIndex();
        if (row < 0 || row >= m_sprites.size())
            return;

        const int groupSize = qMax(1, m_grid * m_grid);
        const int start = groupStartForRow(row);
        const QString base = safeName(text, start);

        for (int i = 0; i < groupSize && (start + i) < m_sprites.size(); ++i)
            m_sprites[start + i].name = QString("%1_%2").arg(base, suffixForIndex(i));

        m_dirty = true;
        updateList();
        m_list->setCurrentRow(row);
        if (m_preview) m_preview->setSprites(&m_sprites);
        refreshOutput();
    }

    void setCompositeGrid(int grid)
    {
        grid = qBound(1, grid, 3);

        const int row = qMax(0, currentIndex());
        const int oldStart = groupStartForRow(row);

        if (oldStart < 0 || oldStart >= m_sprites.size()) {
            m_grid = grid;
            if (m_preview)
                m_preview->setGrid(grid);
            return;
        }

        const int oldGrid = groupGridForIndex(oldStart);
        if (grid == oldGrid) {
            m_grid = grid;
            if (m_preview)
                m_preview->setGrid(grid);
            return;
        }

        const QString base = baseNameFromPart(m_sprites[oldStart].name);
        const int newCount = grid * grid;
        const int oldCount = oldGrid * oldGrid;

        QVector<CvBasicSpriteData> oldGroup;
        for (int i = 0; i < oldCount && (oldStart + i) < m_sprites.size(); ++i)
            oldGroup.append(m_sprites[oldStart + i]);

        for (int i = 0; i < oldCount && oldStart < m_sprites.size(); ++i)
            m_sprites.removeAt(oldStart);

        QVector<CvBasicSpriteData> newGroup = makeCompositeSprites(base, grid);

        for (int i = 0; i < qMin(oldGroup.size(), newGroup.size()); ++i) {
            newGroup[i].pixels = oldGroup[i].pixels;
            newGroup[i].size = oldGroup[i].size;
            newGroup[i].color = oldGroup[i].color;
            newGroup[i].grid = grid;
        }

        for (int i = 0; i < newGroup.size(); ++i)
            m_sprites.insert(oldStart + i, newGroup[i]);

        m_grid = grid;
        m_dirty = true;

        updateList();
        if (m_preview) {
            m_preview->setSprites(&m_sprites);
            m_preview->setGrid(m_grid);
            m_preview->setCurrentIndex(oldStart);
        }

        selectGroupStart(oldStart);
        loadSprite(currentIndex());
        refreshOutput();
    }

    int pixelFromGroup(int start, int grid, int gx, int gy) const
    {
        if (start < 0 || start >= m_sprites.size())
            return 0;

        const int tile = (m_sprites[start].size == 8) ? 8 : 16;
        const int col = gx / tile;
        const int row = gy / tile;
        const int part = row * grid + col;
        const int spriteIndex = start + part;

        if (spriteIndex < 0 || spriteIndex >= m_sprites.size())
            return 0;

        const CvBasicSpriteData& s = m_sprites[spriteIndex];
        const int lx = gx % tile;
        const int ly = gy % tile;
        const int idx = ly * s.size + lx;

        if (idx < 0 || idx >= s.pixels.size())
            return 0;

        return qBound(0, static_cast<int>(s.pixels[idx]), 15);
    }

    void setPixelInGroup(int start, int grid, int gx, int gy, int color)
    {
        if (start < 0 || start >= m_sprites.size())
            return;

        const int tile = (m_sprites[start].size == 8) ? 8 : 16;
        const int col = gx / tile;
        const int row = gy / tile;
        const int part = row * grid + col;
        const int spriteIndex = start + part;

        if (spriteIndex < 0 || spriteIndex >= m_sprites.size())
            return;

        CvBasicSpriteData& s = m_sprites[spriteIndex];
        const int lx = gx % tile;
        const int ly = gy % tile;
        const int idx = ly * s.size + lx;

        if (idx < 0 || idx >= s.pixels.size())
            return;

        s.pixels[idx] = static_cast<quint8>(qBound(0, color, 15));
    }

    void clearCurrentSpritePixels()
    {
        const int row = currentIndex();
        if (row < 0 || row >= m_sprites.size())
            return;

        const int start = groupStartForRow(row);
        const int groupSize = groupSizeForIndex(start);
        const int end = qMin(start + groupSize, m_sprites.size());

        bool changed = false;

        for (int i = start; i < end; ++i) {
            for (int p = 0; p < m_sprites[i].pixels.size(); ++p) {
                if (m_sprites[i].pixels[p] != 0) {
                    m_sprites[i].pixels[p] = 0;
                    changed = true;
                }
            }
        }

        if (!changed)
            return;

        m_dirty = true;

        if (m_canvas)
            m_canvas->update();
        if (m_preview)
            m_preview->update();

        pushSpriteUndoState();
        refreshOutput();
    }

    void moveCompositePixels(int dx, int dy)
    {
        const int row = currentIndex();
        if (row < 0 || row >= m_sprites.size())
            return;

        const int start = groupStartForRow(row);
        const int grid = groupGridForIndex(start);
        const int tile = (m_sprites[start].size == 8) ? 8 : 16;
        const int n = tile * grid;

        QVector<int> oldPixels;
        oldPixels.resize(n * n);

        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x)
                oldPixels[y * n + x] = pixelFromGroup(start, grid, x, y);
        }

        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                const int srcX = x - dx;
                const int srcY = y - dy;
                const int c = (srcX >= 0 && srcY >= 0 && srcX < n && srcY < n)
                    ? oldPixels[srcY * n + srcX]
                    : 0;
                setPixelInGroup(start, grid, x, y, c);
            }
        }

        m_dirty = true;

        if (m_canvas)
            m_canvas->update();
        if (m_preview)
            m_preview->update();

        refreshOutput();
        pushSpriteUndoState();
    }


    void resizeSpritePixels(CvBasicSpriteData& s, int newSize)
    {
        if (newSize != 8 && newSize != 16)
            newSize = 16;

        if (s.size == newSize)
            return;

        const QVector<quint8> old = s.pixels;
        const int oldSize = s.size;

        s.size = newSize;
        s.pixels.fill(0, newSize * newSize);

        for (int y = 0; y < qMin(oldSize, newSize); ++y) {
            for (int x = 0; x < qMin(oldSize, newSize); ++x)
                s.pixels[y * newSize + x] = old[y * oldSize + x];
        }
    }

    void applySpriteSettings()
    {
        const int row = currentIndex();
        if (row < 0 || row >= m_sprites.size())
            return;

        const int start = groupStartForRow(row);
        if (start < 0 || start >= m_sprites.size())
            return;

        const int oldGrid = groupGridForIndex(start);
        const int oldCount = oldGrid * oldGrid;

        const QString base = safeName(m_nameEdit ? m_nameEdit->text() : QString(), start);
        const int newSize = m_sizeCombo ? m_sizeCombo->currentData().toInt() : m_sprites[start].size;
        const int newGrid = m_previewGridCombo ? qBound(1, m_previewGridCombo->currentData().toInt(), 3)
                                               : groupGridForIndex(start);
        const int newCount = newGrid * newGrid;

        QVector<CvBasicSpriteData> oldGroup;
        for (int i = 0; i < oldCount && (start + i) < m_sprites.size(); ++i)
            oldGroup.append(m_sprites[start + i]);

        QVector<CvBasicSpriteData> newGroup = makeCompositeSprites(base, newGrid);

        for (int i = 0; i < newGroup.size(); ++i) {
            if (i < oldGroup.size()) {
                newGroup[i].pixels = oldGroup[i].pixels;
                newGroup[i].size = oldGroup[i].size;
                newGroup[i].color = oldGroup[i].color;
            }

            newGroup[i].grid = newGrid;
            newGroup[i].name = QString("%1_%2").arg(base, suffixForIndex(i));
            resizeSpritePixels(newGroup[i], newSize);
        }

        for (int i = 0; i < oldCount && start < m_sprites.size(); ++i)
            m_sprites.removeAt(start);

        for (int i = 0; i < newCount; ++i)
            m_sprites.insert(start + i, newGroup[i]);

        m_grid = newGrid;
        m_dirty = true;

        updateList();

        if (m_preview) {
            m_preview->setSprites(&m_sprites);
            m_preview->setGrid(newGrid);
            m_preview->setCurrentIndex(start);
        }

        selectGroupStart(start);
        loadSprite(start);

        if (m_canvas)
            m_canvas->update();
        if (m_preview)
            m_preview->update();

        refreshOutput();
    }


    void resizeCurrentSprite(int newSize)
    {
        const int row = currentIndex();
        if (row < 0 || row >= m_sprites.size())
            return;

        if (newSize != 8 && newSize != 16)
            newSize = 16;

        const int groupSize = qMax(1, m_grid * m_grid);
        const int start = groupStartForRow(row);

        for (int i = 0; i < groupSize && (start + i) < m_sprites.size(); ++i) {
            CvBasicSpriteData& s = m_sprites[start + i];
            if (s.size == newSize)
                continue;

            const QVector<quint8> old = s.pixels;
            const int oldSize = s.size;
            s.size = newSize;
            s.pixels.fill(0, newSize * newSize);

            for (int y = 0; y < qMin(oldSize, newSize); ++y) {
                for (int x = 0; x < qMin(oldSize, newSize); ++x)
                    s.pixels[y * newSize + x] = old[y * oldSize + x];
            }
        }

        m_dirty = true;
        loadSprite(row);
        refreshOutput();
    }

    void setCurrentColor(int color)
    {
        const int row = currentIndex();
        if (row < 0 || row >= m_sprites.size())
            return;

        m_sprites[row].color = qBound(1, color, 15);
        m_canvas->setCurrentColor(m_sprites[row].color);
        m_dirty = true;
        refreshOutput();
    }

    QString buildOutput() const
    {
        const bool verbose = !m_verboseOutputCheck || m_verboseOutputCheck->isChecked();

        QString out;

        if (verbose) {
            out += "REM === CVBasic sprite data, TMS9918 compatible multi-color export ===\n";
            out += "REM Only the selected grouped sprite is exported.\n";
            out += "REM Coleco/TMS sprites are single-color per hardware sprite.\n";
            out += "REM Multi-color sprites are exported as layered masks: one hardware sprite per used color.\n";
            out += "REM F18A can use this data in TMS-compatible sprite mode.\n";
            out += "REM Attribute format below: Y, X, PATTERN_INDEX, COLOR.\n\n";
        }

        if (m_sprites.isEmpty()) {
            out += "REM No sprite selected.\n";
            return out;
        }

        int start = currentIndex();
        if (start < 0 || start >= m_sprites.size())
            start = 0;

        start = groupStartForRow(start);

        const bool includePlacement = m_placeSpriteTextCheck && m_placeSpriteTextCheck->isChecked();
        const QString base = safeConstName(baseNameFromPart(m_sprites[start].name));

        if (includePlacement) {
            out += QString("REM Jump over sprite data for standalone test\n");
            out += QString("GOTO %1_TEST_MAIN\n\n").arg(base);
        }

        int patternIndex = 0;
        out += exportSpriteGroup(start, patternIndex, verbose);

        if (includePlacement)
            out += exportSpritePlacementExample(start, patternIndex, verbose);

        return out;
    }


    QVector<int> rowMajorPartOrder(int grid) const
    {
        QVector<int> order;
        grid = qBound(1, grid, 3);

        for (int row = 0; row < grid; ++row) {
            for (int col = 0; col < grid; ++col)
                order.append(row * grid + col);
        }

        return order;
    }


    QList<int> usedColorsForSprite(const CvBasicSpriteData& s) const
    {
        QSet<int> colors;
        for (quint8 px : s.pixels) {
            const int c = qBound(0, static_cast<int>(px), 15);
            if (c != 0)
                colors.insert(c);
        }

        QList<int> colorList = colors.values();
        std::sort(colorList.begin(), colorList.end());
        return colorList;
    }

    QString exportSpriteAttrBlock(const CvBasicSpriteData& s, int baseAttrIndex, bool verbose) const
    {
        const QString base = safeConstName(s.name.isEmpty() ? QString("SPRITE") : s.name);
        const QList<int> colorList = usedColorsForSprite(s);

        if (colorList.isEmpty())
            return QString();

        QString out;
        out += QString("%1_ATTR:\n").arg(base);

        if (verbose) {
            out += "REM Y, X, LOGICAL_ATTR_INDEX, COLOR per hardware sprite layer\n";
            out += "REM This ATTR helper block is placed after all pattern data,\n";
            out += "REM so DEFINE SPRITE will not read it as sprite bitmap data.\n";
            out += "REM Note: placement code for 16x16 still uses raw pattern starts 0,4,8,12.\n";
        }

        const int defaultY = 96;
        const int defaultX = 128;

        for (int i = 0; i < colorList.size(); ++i) {
            out += QString("DATA %1,%2,%3,%4")
                       .arg(defaultY)
                       .arg(defaultX)
                       .arg(baseAttrIndex + i)
                       .arg(colorList[i]);

            if (verbose && i == 0)
                out += "  REM " + base;

            out += "\n";
        }

        out += "\n";
        return out;
    }


    QString exportSpriteGroup(int start, int& patternIndex, bool verbose) const
    {
        if (start < 0 || start >= m_sprites.size())
            return QString();

        const int grid = groupGridForIndex(start);
        const int groupSize = groupSizeForIndex(start);
        const QString base = safeConstName(baseNameFromPart(m_sprites[start].name));

        QString out;

        if (verbose) {
            out += QString("REM ==================================================\n");
            out += QString("REM GROUP %1_%2x%2  (%2x%2 parts, grouped sprite)\n").arg(base).arg(grid);
            out += QString("REM Pattern DATA comes first. ATTR helper DATA is emitted after all pattern DATA.\n");
            out += QString("REM This is required because DEFINE SPRITE reads sequential DATA from %1_SPRITES.\n").arg(base);
            out += QString("REM ==================================================\n");
        } else {
            out += QString("REM --- %1_%2x%2 grouped sprite ---\n").arg(base).arg(grid);
        }

        out += QString("%1_SPRITES:\n").arg(base);

        const QVector<int> order = rowMajorPartOrder(grid);

        QVector<QPair<int, int>> attrParts; // partIndex, logical ATTR index
        int attrIndex = 0;

        for (int partIndex : order) {
            if (partIndex >= groupSize || (start + partIndex) >= m_sprites.size())
                continue;

            const int baseAttrIndex = attrIndex;

            out += exportSprite(m_sprites[start + partIndex], start + partIndex, patternIndex, verbose);
            attrParts.append(qMakePair(partIndex, baseAttrIndex));

            attrIndex += usedColorsForSprite(m_sprites[start + partIndex]).size();
        }

        out += "\n";

        if (!attrParts.isEmpty()) {
            if (verbose)
                out += "REM --- Optional ATTR helper DATA, not part of DEFINE SPRITE bitmap data ---\n";

            for (const QPair<int, int>& item : attrParts) {
                const int partIndex = item.first;
                const int baseAttrIndex = item.second;

                if (partIndex >= groupSize || (start + partIndex) >= m_sprites.size())
                    continue;

                out += exportSpriteAttrBlock(m_sprites[start + partIndex], baseAttrIndex, verbose);
            }
        }

        return out;
    }

    QString exportSpritePlacementExample(int start, int totalPatternCount, bool verbose) const
    {
        if (start < 0 || start >= m_sprites.size())
            return QString();

        const int grid = groupGridForIndex(start);
        const int groupSize = groupSizeForIndex(start);
        const int tile = (m_sprites[start].size == 8) ? 8 : 16;
        const QString base = safeConstName(baseNameFromPart(m_sprites[start].name));

        QString out;
        out += "\n";
        out += QString("REM --- Place %1_%2x%2 on screen ---\n").arg(base).arg(grid);
        out += QString("%1_TEST_MAIN:\n").arg(base);

        if (verbose) {
            out += "REM First load the sprite patterns into VRAM.\n";
            out += "REM x and y are the visual top-left position of the grouped sprite.\n";
            out += "REM CVBasic/TMS needs y-1 in the SPRITE statement.\n";
        }

        out += "REM Minimal display setup for a directly compilable test\n";
        out += "MODE 1\n";
        out += "CLS\n";

        if (tile == 8) {
            out += "REM Enable TMS 8x8 sprite mode\n";
            out += "ASM HALT\n";
            out += "ASM LD BC,$C001\n";
            out += "ASM CALL WRTVDP\n";
        }

        const int defineSpriteCount = (tile == 16)
            ? qMax(1, (totalPatternCount + 3) / 4)
            : qMax(1, totalPatternCount);

        out += "\n";
        out += QString("DEFINE SPRITE 0,%1,%2_SPRITES\n\n").arg(defineSpriteCount).arg(base);

        out += "REM Example position\n";
        out += "x = 128\n";
        out += "y = 96\n\n";

        out += "WAIT\n";
        out += "WAIT\n\n";

        int spriteIndex = 0;
        int patternIndex = 0;

        const QVector<int> order = rowMajorPartOrder(grid);

        for (int part : order) {
            if (part >= groupSize || (start + part) >= m_sprites.size())
                continue;

            const CvBasicSpriteData& s = m_sprites[start + part];

            QSet<int> colors;
            for (quint8 px : s.pixels) {
                const int c = qBound(0, static_cast<int>(px), 15);
                if (c != 0)
                    colors.insert(c);
            }

            QList<int> colorList = colors.values();
            std::sort(colorList.begin(), colorList.end());

            const int col = part % grid;
            const int row = part / grid;

            const QString xExpr = (col == 0)
                ? QString("x")
                : QString("x+%1").arg(col * tile);

            // CVBasic/TMS sprites are shown one pixel lower than the value passed.
            // To display at visual y + offset, pass y + offset - 1.
            const int yOffset = row * tile;
            const QString yExpr = (yOffset == 0)
                ? QString("y-1")
                : QString("y+%1").arg(yOffset - 1);

            const QChar partLetter = QChar('A' + part);

            if (verbose) {
                QString where;
                if (row == 0 && col == 0)
                    where = "left/top";
                else if (row == 0)
                    where = "right/top";
                else if (col == 0)
                    where = "left/bottom";
                else
                    where = "right/bottom";

                out += QString("REM %1 = row %2, column %3, X offset %4, Y offset %5\n")
                           .arg(partLetter)
                           .arg(row + 1)
                           .arg(col + 1)
                           .arg(col * tile)
                           .arg(row * tile);
            } else {
                out += QString("REM %1\n").arg(partLetter);
            }

            for (int color : colorList) {
                out += QString("SPRITE %1,%2,%3,%4,%5\n")
                           .arg(spriteIndex)
                           .arg(yExpr)
                           .arg(xExpr)
                           .arg(patternIndex)
                           .arg(color);

                ++spriteIndex;
                patternIndex += (tile == 16) ? 4 : 1;
            }

            out += "\n";
        }

        out += "REM Keep program alive so the sprite remains visible\n";
        out += QString("%1_TEST_LOOP:\n").arg(base);
        out += "WAIT\n";
        out += QString("GOTO %1_TEST_LOOP\n\n").arg(base);

        return out;
    }

    QString exportSprite(const CvBasicSpriteData& s, int index, int& patternIndex, bool verbose) const
    {
        const QString base = safeConstName(s.name.isEmpty() ? QString("SPRITE_%1").arg(index + 1) : s.name);
        const QList<int> colorList = usedColorsForSprite(s);

        QString out;
        out += QString("REM --- %1, %2x%2, %3 color layer(s) ---\n")
                   .arg(base)
                   .arg(s.size)
                   .arg(colorList.size());

        if (colorList.isEmpty()) {
            if (verbose)
                out += QString("REM %1 is empty.\n\n").arg(base);
            return out;
        }

        for (int color : colorList) {
            out += QString("%1_C%2_PATTERN:\n").arg(base).arg(color);

            QList<int> bytes = buildTmsMaskBytesForColor(s, color);
            for (int i = 0; i < bytes.size(); ++i) {
                if ((i % 8) == 0)
                    out += "DATA BYTE ";

                out += QString::number(bytes[i]);

                if (i != bytes.size() - 1 && (i % 8) != 7)
                    out += ",";

                if ((i % 8) == 7 || i == bytes.size() - 1)
                    out += "\n";
            }

            patternIndex += (s.size == 16) ? 4 : 1;
        }

        out += "\n";
        return out;
    }

    QList<int> buildTmsMaskBytesForColor(const CvBasicSpriteData& s, int color) const
    {
        QList<int> bytes;

        if (s.size == 8) {
            for (int y = 0; y < 8; ++y) {
                int b = 0;
                for (int x = 0; x < 8; ++x) {
                    const int c = s.pixels[y * 8 + x];
                    b |= ((c == color ? 1 : 0) << (7 - x));
                }
                bytes.append(b);
            }
        } else {
            // TMS/CVBasic 16x16 pattern order is column-major:
            // TL, BL, TR, BR.
            // If exported as TL, TR, BL, BR, the sprite appears as 1,3,2,4.
            const int off[4][2] = {
                {0, 0},  // top-left
                {0, 8},  // bottom-left
                {8, 0},  // top-right
                {8, 8}   // bottom-right
            };
            for (int q = 0; q < 4; ++q) {
                for (int y = 0; y < 8; ++y) {
                    int b = 0;
                    for (int x = 0; x < 8; ++x) {
                        const int sx = off[q][0] + x;
                        const int sy = off[q][1] + y;
                        const int c = s.pixels[sy * 16 + sx];
                        b |= ((c == color ? 1 : 0) << (7 - x));
                    }
                    bytes.append(b);
                }
            }
        }

        return bytes;
    }


    QString safeConstName(QString name) const
    {
        name = name.trimmed().toUpper();
        name.replace(QRegularExpression("[^A-Z0-9_]"), "_");
        if (name.isEmpty())
            name = "SPRITE";
        if (name.at(0).isDigit())
            name.prepend("SPR_");
        return name;
    }

    void refreshOutput()
    {
        if (m_output)
            m_output->setPlainText(buildOutput());
    }

public:
    QString m_insertText;

private:
    QString m_sourceDir;
    QString m_projectPath;
    bool m_dirty = false;
    int m_grid = 1;

    QVector<CvBasicSpriteData> m_sprites;
    QVector<QVector<CvBasicSpriteData>> m_undoStack;
    QVector<QVector<CvBasicSpriteData>> m_redoStack;
    bool m_restoringUndoRedo = false;

    QListWidget* m_list = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_sizeCombo = nullptr;
    QComboBox* m_previewGridCombo = nullptr;
    CvBasicSpriteCanvas* m_canvas = nullptr;
    CvBasicSpritePreviewWidget* m_preview = nullptr;
    QCheckBox* m_verboseOutputCheck = nullptr;
    QCheckBox* m_placeSpriteTextCheck = nullptr;
    QPlainTextEdit* m_output = nullptr;
};


// ============================================================================
// CvBasicEditorWindow
// ============================================================================

// ============================================================================
// Integrated CVBasic sound / PSG tracker editor
// ============================================================================
static QPixmap soundResourcePixmap(const QString& resourceName)
{
    const QStringList candidates = {
        ":/images/images/" + resourceName + ".png",
        ":/icons/" + resourceName + ".png",
        ":/" + resourceName + ".png"
    };

    for (const QString& path : candidates) {
        if (QFile::exists(path))
            return QPixmap(path);
    }

    QPixmap fallback(900, 180);
    fallback.fill(QColor("#F4F4F4"));
    QPainter p(&fallback);
    p.setPen(QPen(QColor("#111111"), 2));
    p.setFont(QFont("Arial", 16, QFont::Bold));
    p.drawRect(fallback.rect().adjusted(1, 1, -2, -2));
    p.drawText(fallback.rect(), Qt::AlignCenter, resourceName);
    return fallback;
}


class SoundKeyboardOverlayWidget final : public QWidget
{
public:
    explicit SoundKeyboardOverlayWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("soundKeyboardOverlayWidget");
        setMinimumHeight(120);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        m_keyboardPixmap = soundResourcePixmap("SNDKEYS");

        // Breedtes per knop. Deze waarden zijn relatieve gewichten.
        // De knoppen blijven automatisch aan elkaar hangen, zonder gaten.
        //
        // Bovenste rij = 20 zones.
        // Onderste rij = 12 zones.
        //
        // Voorbeeld:
        //   zet m_topKeyWidths[0] groter, dan wordt knop 1 breder,
        //   en alle volgende knoppen schuiven automatisch op.
        m_topKeyWidths = {
            45, 40, 45, 40, 60,
            60, 40, 45, 40, 60,
            60, 40, 45, 40, 45,
            40, 60, 60, 45, 45
        };

        m_bottomKeyWidths = {
            45, 50, 50, 52, 50, 50,
            50, 54, 50, 52, 52, 41
        };

        // Top row: 20 buttons over the upper keys / zones.
        // Tijdelijk uitgeschakeld: ze blijven zichtbaar, maar triggeren geen noot.
        for (int i = 0; i < 20; ++i) {
            QPushButton* btn = new QPushButton(QString::number(i + 1), this);
            btn->setObjectName("soundKeyboardOverlayButtonDisabled");
            btn->setFocusPolicy(Qt::NoFocus);
            btn->setCursor(Qt::ArrowCursor);
            btn->setEnabled(false);
            btn->setToolTip(tr("Top keys temporarily disabled"));
            btn->show();

            m_topButtons.append(btn);
        }

        // Bottom row: 12 buttons over the full-width piano keys.
        const QStringList bottomLabels = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int i = 0; i < bottomLabels.size(); ++i) {
            QPushButton* btn = new QPushButton(bottomLabels[i], this);
            btn->setObjectName("soundKeyboardOverlayButton");
            btn->setFocusPolicy(Qt::NoFocus);
            btn->setCursor(Qt::PointingHandCursor);
            btn->show();

            connect(btn, &QPushButton::clicked, this, [this, bottomLabels, i]() {
                if (onKeyPressed)
                    onKeyPressed(bottomLabels[i]);
            });

            m_bottomButtons.append(btn);
        }

        applyKeyboardHelpToolStyle();
    }

    std::function<void(const QString&)> onKeyPressed;

    void setKeyboardHelpTool(bool enabled)
    {
        keyboard_helptool = enabled;
        applyKeyboardHelpToolStyle();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor("#242424"));

        if (!m_keyboardPixmap.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawPixmap(rect(), m_keyboardPixmap);
        }
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);

        const QRect r = rect().adjusted(0, 0, -1, -1);
        const int totalW = qMax(1, r.width() + 1);
        const int totalH = qMax(1, r.height() + 1);

        // Approximate split based on the PNG example: upper row / lower row.
        const int topY = 0;
        const int topH = qMax(24, int(totalH * 0.52));
        const int bottomY = topH;
        const int bottomH = qMax(24, totalH - topH);

        applyWeightedButtonGeometry(m_topButtons, m_topKeyWidths, topY, topH, totalW);
        applyWeightedButtonGeometry(m_bottomButtons, m_bottomKeyWidths, bottomY, bottomH, totalW);
    }

private:
    void applyKeyboardHelpToolStyle()
    {
        const QStringList bottomLabels = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        for (int i = 0; i < m_topButtons.size(); ++i)
            m_topButtons[i]->setText(keyboard_helptool ? QString::number(i + 1) : QString());

        for (int i = 0; i < m_bottomButtons.size(); ++i)
            m_bottomButtons[i]->setText(keyboard_helptool ? bottomLabels.value(i) : QString());

        if (keyboard_helptool) {
            setStyleSheet(
                "QPushButton#soundKeyboardOverlayButton {"
                " background: rgba(255,255,255,35);"
                " color: #FF4040;"
                " border: 1px solid #FF0000;"
                " font-weight: bold;"
                " padding: 0px;"
                " }"
                "QPushButton#soundKeyboardOverlayButton:hover {"
                " background: rgba(255,255,255,60);"
                " }"
                "QPushButton#soundKeyboardOverlayButton:pressed {"
                " background: rgba(255,150,150,90);"
                " }"
                "QPushButton#soundKeyboardOverlayButtonDisabled {"
                " background: rgba(80,80,80,35);"
                " color: rgba(180,180,180,130);"
                " border: 1px solid rgba(120,120,120,80);"
                " font-weight: bold;"
                " padding: 0px;"
                " }"
            );
        } else {
            // Helper uit: geen rode kaders en geen cijfers/labels.
            // Wel duidelijke klik-feedback + handcursor blijft actief.
            setStyleSheet(
                "QPushButton#soundKeyboardOverlayButton {"
                " background: rgba(255,255,255,0);"
                " color: rgba(255,255,255,0);"
                " border: none;"
                " padding: 0px;"
                " }"
                "QPushButton#soundKeyboardOverlayButton:hover {"
                " background: rgba(255,255,255,0);"
                " border: none;"
                " }"
                "QPushButton#soundKeyboardOverlayButton:pressed {"
                " background: rgba(255,255,255,105);"
                " border: 1px solid rgba(255,255,255,140);"
                " }"
                "QPushButton#soundKeyboardOverlayButtonDisabled {"
                " background: rgba(255,255,255,0);"
                " color: rgba(255,255,255,0);"
                " border: none;"
                " padding: 0px;"
                " }"
            );
        }
    }

    void applyWeightedButtonGeometry(const QVector<QPushButton*>& buttons,
                                     const QVector<int>& widths,
                                     int y,
                                     int h,
                                     int totalW)
    {
        if (buttons.isEmpty())
            return;

        int x = 0;
        int remainingTotalWidth = totalW;

        for (int i = 0; i < buttons.size(); ++i) {
            int remainingWeight = 0;
            for (int j = i; j < buttons.size(); ++j) {
                const int w = (j < widths.size()) ? widths[j] : 50;
                remainingWeight += qMax(1, w);
            }

            const int w = (i < widths.size()) ? widths[i] : 50;
            const int weight = qMax(1, w);

            const int buttonWidth = (i == buttons.size() - 1 || remainingWeight <= 0)
                                        ? remainingTotalWidth
                                        : qRound((static_cast<double>(weight) / static_cast<double>(remainingWeight)) * remainingTotalWidth);

            buttons[i]->setGeometry(x, y, qMax(1, buttonWidth), h);
            x += buttonWidth;
            remainingTotalWidth = qMax(0, totalW - x);
        }
    }

private:
    QPixmap m_keyboardPixmap;
    QVector<QPushButton*> m_topButtons;
    QVector<QPushButton*> m_bottomButtons;

    QVector<int> m_topKeyWidths;
    QVector<int> m_bottomKeyWidths;

    bool keyboard_helptool = false;
};




class SoundPatternDelegate final : public QStyledItemDelegate
{
public:
    explicit SoundPatternDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    void setVuLevel(int, int)
    {
    }

    void setVuLevels(int, int, int, int)
    {
    }

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);

        QColor borderColor;

        switch (index.column()) {
        case 0:
            borderColor = QColor("#FFFFFF");
            break; // ROW right border - white
        case 4:
            borderColor = QColor("#FFE340");
            break; // CH1 Fx right border - yellow
        case 8:
            borderColor = QColor("#FF4FC8");
            break; // CH2 Fx right border - pink
        case 12:
            borderColor = QColor("#68FF87");
            break; // CH3 Fx right border - green
        case 16:
            borderColor = QColor("#FFB24A");
            break; // NOISE Fx right border - orange
        default:
            return;
        }

        painter->save();
        painter->setPen(QPen(borderColor, 2));
        const int x = option.rect.right();
        painter->drawLine(x, option.rect.top(), x, option.rect.bottom());
        painter->restore();
    }
};



class SoundVuLedBarWidget final : public QWidget
{
public:
    explicit SoundVuLedBarWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(168, 92);
        setFixedSize(168, 92);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        for (int i = 0; i < 4; ++i) {
            m_targetLevels[i] = 0;
            m_displayLevels[i] = 0;
            m_peakLevels[i] = 0;
            m_peakHoldTicks[i] = 0;
            m_silenceTicks[i] = 0;
        }

        m_animTimer = new QTimer(this);
        m_animTimer->setInterval(35);
        m_animTimer->setTimerType(Qt::PreciseTimer);

        connect(m_animTimer, &QTimer::timeout, this, [this]() {
            bool changed = false;
            bool needsTimer = false;

            for (int ch = 0; ch < 4; ++ch) {
                // Display meter: fast up, realistic faster down.
                if (m_displayLevels[ch] < m_targetLevels[ch]) {
                    m_displayLevels[ch] = m_targetLevels[ch];
                    changed = true;
                } else if (m_displayLevels[ch] > m_targetLevels[ch]) {
                    const int diff = m_displayLevels[ch] - m_targetLevels[ch];

                    // Grotere verschillen vallen sneller. Zo zie je echt beweging.
                    const int step = (diff >= 8) ? 3 : (diff >= 4 ? 2 : 1);
                    m_displayLevels[ch] = qMax(m_targetLevels[ch], m_displayLevels[ch] - step);
                    changed = true;
                }

                // Peak marker: blijft kort hangen en valt dan apart terug.
                if (m_peakLevels[ch] < m_displayLevels[ch]) {
                    m_peakLevels[ch] = m_displayLevels[ch];
                    m_peakHoldTicks[ch] = 8;
                    changed = true;
                } else if (m_peakLevels[ch] > m_displayLevels[ch]) {
                    if (m_peakHoldTicks[ch] > 0) {
                        --m_peakHoldTicks[ch];
                    } else {
                        --m_peakLevels[ch];
                        changed = true;
                    }
                }

                if (m_displayLevels[ch] != m_targetLevels[ch] ||
                    m_peakLevels[ch] != m_displayLevels[ch] ||
                    m_peakHoldTicks[ch] > 0) {
                    needsTimer = true;
                }
            }

            if (changed)
                update();

            if (!needsTimer)
                m_animTimer->stop();
        });
    }

    void setChannelLevel(int channel, int level)
    {
        if (channel < 0 || channel >= 4)
            return;

        level = qBound(0, level, 15);

        // Kleine smoothing tegen zenuwachtig flikkeren bij snelle preview-calls.
        // Stijgen blijft direct, dalen wordt door de timer afgehandeld.
        m_targetLevels[channel] = level;

        if (level > m_displayLevels[channel]) {
            m_displayLevels[channel] = level;

            if (level >= m_peakLevels[channel]) {
                m_peakLevels[channel] = level;
                m_peakHoldTicks[channel] = 8;
            }

            update();
        }

        if (m_animTimer && !m_animTimer->isActive())
            m_animTimer->start();
    }

    void setLevels(int ch1, int ch2, int ch3, int noise)
    {
        const int levels[4] = {
            qBound(0, ch1, 15),
            qBound(0, ch2, 15),
            qBound(0, ch3, 15),
            qBound(0, noise, 15)
        };

        bool changed = false;

        for (int ch = 0; ch < 4; ++ch) {
            m_targetLevels[ch] = levels[ch];

            if (levels[ch] > m_displayLevels[ch]) {
                m_displayLevels[ch] = levels[ch];
                changed = true;
            }

            if (levels[ch] >= m_peakLevels[ch]) {
                m_peakLevels[ch] = levels[ch];
                m_peakHoldTicks[ch] = 8;
                changed = true;
            }
        }

        if (changed)
            update();

        if (m_animTimer && !m_animTimer->isActive())
            m_animTimer->start();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        // Transparant: geen zwart veld achter de VU-meter.

        const QString labels[4] = {
            QStringLiteral("CH1"),
            QStringLiteral("CH2"),
            QStringLiteral("CH3"),
            QStringLiteral("NOI")
        };

        const int leftLabelW = 34;
        const int rightValueW = 22;
        const int rowGap = 5;
        const int topMargin = 2;
        const int bottomMargin = 2;
        const int usableH = qMax(1, height() - topMargin - bottomMargin - rowGap * 3);
        const int rowH = qMax(20, usableH / 4);

        const int barX = 8 + leftLabelW;
        const int barW = qMax(80, width() - barX - rightValueW - 5);

        const int segGap = 3;
        const int fullSegW = qMax(4, (barW - segGap * 14) / 15);
        const int segW = qMax(3, fullSegW / 2);

        QFont labelFont = p.font();
        labelFont.setBold(true);
        labelFont.setPointSizeF(labelFont.pointSizeF() - 0.5);
        p.setFont(labelFont);

        for (int ch = 0; ch < 4; ++ch) {
            const int y = topMargin + ch * (rowH + rowGap);
            const QRect labelRect(8, y, leftLabelW - 4, rowH);
            const QRect valueRect(width() - rightValueW - 2, y, rightValueW, rowH);

            p.setPen(QColor("#DADADA"));
            p.drawText(labelRect, Qt::AlignVCenter | Qt::AlignLeft, labels[ch]);
            p.drawText(valueRect, Qt::AlignVCenter | Qt::AlignRight,
                       QString::number(qBound(0, m_displayLevels[ch], 15)).rightJustified(2, QLatin1Char('0')));

            for (int i = 0; i < 15; ++i) {
                const int ledH = qMax(16, rowH - 3);
                const QRect segRect(barX + i * (segW + segGap),
                                    y + (rowH - ledH) / 2,
                                    segW,
                                    ledH);

                QColor onColor;
                if (i < 8)
                    onColor = QColor("#35D04F");
                else if (i < 12)
                    onColor = QColor("#F5A623");
                else
                    onColor = QColor("#E53935");

                QColor offColor = onColor;
                offColor.setAlpha(34);

                const bool isOn = i < qBound(0, m_displayLevels[ch], 15);
                const bool isPeak = (i + 1) == qBound(0, m_peakLevels[ch], 15) && m_peakLevels[ch] > 0;

                if (isPeak && !isOn) {
                    QColor peakColor = onColor.lighter(160);
                    p.setPen(QColor("#FFFFFF"));
                    p.setBrush(peakColor);
                } else {
                    p.setPen(QColor(0, 0, 0, isOn ? 90 : 50));
                    p.setBrush(isOn ? onColor : offColor);
                }

                p.drawRoundedRect(segRect, 1.5, 1.5);

                // Extra witte highlight op de actieve peak, ook als die binnen de huidige bar valt.
                if (isPeak) {
                    p.setPen(QPen(QColor("#FFFFFF"), 1));
                    p.drawLine(segRect.left() + 1, segRect.top() + 1,
                               segRect.right() - 1, segRect.top() + 1);
                }
            }
        }
    }

private:
    int m_targetLevels[4] = {0, 0, 0, 0};
    int m_displayLevels[4] = {0, 0, 0, 0};
    int m_peakLevels[4] = {0, 0, 0, 0};
    int m_peakHoldTicks[4] = {0, 0, 0, 0};
    int m_silenceTicks[4] = {0, 0, 0, 0};
    QTimer* m_animTimer = nullptr;
};



// ============================================================================
// Sound instrument visual editor widgets
// ============================================================================

class SoundInstrumentWavePreviewWidget final : public QWidget
{
public:
    explicit SoundInstrumentWavePreviewWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(110);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setInstrumentParams(int type, int env, int x, int y, int volume)
    {
        m_type = type;
        m_env = env;
        m_x = qBound(0, x, 100);
        m_y = qBound(0, y, 100);
        m_volume = qBound(0, volume, 15);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor("#121B2A"));

        QRectF r = rect().adjusted(8, 8, -8, -22);
        p.setPen(QPen(QColor("#566983"), 1));
        p.drawRect(r);

        p.setPen(QPen(QColor(255, 255, 255, 25), 1));
        for (int i = 1; i < 4; ++i) {
            const qreal y = r.top() + i * r.height() / 4.0;
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }

        QPainterPath path;
        const int samples = qMax(32, width() - 24);
        const double amp = (0.18 + (m_volume / 15.0) * 0.38) * r.height();
        const double duty = qBound(0.20, 0.28 + (m_x / 100.0) * 0.42, 0.75);
        const double smooth = (100 - m_y) / 100.0;

        for (int i = 0; i < samples; ++i) {
            const double t = static_cast<double>(i) / qMax(1, samples - 1);
            const double phase = std::fmod(t * 7.0, 1.0);
            double v = 0.0;

            if (m_type == 1) {
                quint32 n = static_cast<quint32>((i * 1103515245u + 12345u + m_x * 97u + m_y * 53u));
                v = (n & 0x10000u) ? 1.0 : -1.0;
                v *= 0.35 + (m_y / 100.0) * 0.65;
            } else {
                v = (phase < duty) ? 1.0 : -1.0;

                if (smooth > 0.05)
                    v = v * (1.0 - smooth * 0.35) + std::sin(2.0 * M_PI * phase) * smooth * 0.35;

                if ((m_env & 0x0F) == 0x08)
                    v *= (0.65 + 0.35 * std::sin(2.0 * M_PI * phase * 2.0));
            }

            const qreal x = r.left() + t * r.width();
            const qreal y = r.center().y() - v * amp;

            if (i == 0)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
        }

        p.setPen(QPen(QColor("#50C43A"), 1.4));
        p.drawPath(path);

        p.setPen(QColor("#B8C6D8"));
        QFont f = p.font();
        f.setPixelSize(10);
        p.setFont(f);
        p.drawText(rect().adjusted(8, 0, -8, -4), Qt::AlignLeft | Qt::AlignBottom,
                   QString("Waveform preview  X:%1  Y:%2  Env:%3")
                       .arg(m_x)
                       .arg(m_y)
                       .arg(m_env, 2, 16, QLatin1Char('0')).toUpper());
    }

private:
    int m_type = 0;
    int m_env = 3;
    int m_x = 50;
    int m_y = 50;
    int m_volume = 15;
};


class SoundInstrumentEnvelopePreviewWidget final : public QWidget
{
public:
    explicit SoundInstrumentEnvelopePreviewWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(110);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setEnvelopeParams(int env, int volume, int fadeout)
    {
        m_env = qBound(0, env, 15);
        m_volume = qBound(0, volume, 15);
        m_fadeout = qBound(0, fadeout, 15);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor("#121B2A"));

        QRectF r = rect().adjusted(8, 8, -8, -22);
        p.setPen(QPen(QColor("#566983"), 1));
        p.drawRect(r);

        p.setPen(QPen(QColor(60, 130, 170, 60), 1, Qt::DotLine));
        for (int i = 1; i < 4; ++i) {
            const qreal y = r.top() + i * r.height() / 4.0;
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }

        auto envValue = [this](double x) -> double {
            switch (m_env & 0x0F) {
            case 0x01: return qMax(0.35, 1.0 - x * 0.85);
            case 0x03: return x < 0.08 ? x / 0.08 : 0.92;
            case 0x04: return qMax(0.45, 1.0 - x * 0.65);
            case 0x05: return x < 0.25 ? (x / 0.25) * 0.70 : 0.70;
            case 0x06: return 0.86 + 0.12 * std::sin(2.0 * M_PI * 5.0 * x);
            case 0x07: return std::fmod(x * 8.0, 1.0) < 0.55 ? 0.95 : 0.45;
            case 0x08: return qMax(0.30, std::exp(-2.0 * x));
            case 0x09: return x < 0.18 ? 1.0 - x * 5.0 : 0.0;
            case 0x0A: return qMax(0.0, 1.0 - x * 2.0);
            case 0x0B: return qMax(0.0, 1.0 - x * 4.0);
            case 0x0D: return qBound(0.3, 0.3 + x * 0.8, 1.0);
            default: return 1.0;
            }
        };

        QPainterPath path;
        const int samples = qMax(24, width() - 20);
        const double volScale = m_volume / 15.0;

        for (int i = 0; i < samples; ++i) {
            const double xNorm = static_cast<double>(i) / qMax(1, samples - 1);
            double v = envValue(xNorm) * volScale;

            if (m_fadeout > 0)
                v *= qMax(0.0, 1.0 - xNorm * (m_fadeout / 18.0));

            const qreal x = r.left() + xNorm * r.width();
            const qreal y = r.bottom() - v * r.height();

            if (i == 0)
                path.moveTo(x, y);
            else
                path.lineTo(x, y);
        }

        p.setPen(QPen(QColor("#7DE34D"), 1.5));
        p.drawPath(path);

        p.setBrush(QColor("#FFC430"));
        p.setPen(QPen(QColor("#111111"), 1));
        for (double xNorm : {0.0, 0.25, 0.55, 0.85, 1.0}) {
            double v = envValue(xNorm) * volScale;
            if (m_fadeout > 0)
                v *= qMax(0.0, 1.0 - xNorm * (m_fadeout / 18.0));
            QPointF pt(r.left() + xNorm * r.width(), r.bottom() - v * r.height());
            p.drawEllipse(pt, 3.5, 3.5);
        }

        p.setPen(QColor("#B8C6D8"));
        QFont f = p.font();
        f.setPixelSize(10);
        p.setFont(f);
        p.drawText(rect().adjusted(8, 0, -8, -4), Qt::AlignLeft | Qt::AlignBottom,
                   QString("Volume envelope  Vol:%1  Env:%2  Fade:%3")
                       .arg(m_volume)
                       .arg(m_env, 2, 16, QLatin1Char('0')).toUpper()
                       .arg(m_fadeout));
    }

private:
    int m_env = 3;
    int m_volume = 15;
    int m_fadeout = 0;
};







class SoundChannelHeaderLabel final : public QLabel
{
public:
    explicit SoundChannelHeaderLabel(const QString& text, QWidget* parent = nullptr)
        : QLabel(text, parent)
    {
        setCursor(Qt::PointingHandCursor);
    }

    std::function<void()> onClicked;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event && event->button() == Qt::LeftButton && onClicked) {
            onClicked();
            event->accept();
            return;
        }

        QLabel::mousePressEvent(event);
    }
};


class CvBasicSoundEditorPage final : public QWidget
{
public:
    explicit CvBasicSoundEditorPage(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("cvBasicSoundPage");
        setupUi();

        // Start leeg: de gebruiker moet eerst een song openen of importeren.
        // Geen demo-song meer automatisch laden.
        createEmptySoundSong();

        rebuildOutput();
    }

    QString selectedSoundOutput() const
    {
        if (m_addPlayerCheck && m_addPlayerCheck->isChecked())
            return buildCvBasicPlayerInsert().trimmed();

        return m_outputEdit ? m_outputEdit->toPlainText().trimmed() : QString();
    }

    std::function<void(const QString&)> onInsertRequested;
    std::function<void(int, int, int, int, int, int)> onPreviewNoteRequested;
    std::function<void()> onStopAllPreviewRequested;
    std::function<void(const QVariantList&, int, bool)> onStreamPlayRequested;
    std::function<void()> onStreamStopRequested;

    void setSoundChannelVuLevel(int channel, int level)
    {
        channel = qBound(0, channel, 3);
        level = qBound(0, level, 15);

        m_vuLevels[channel] = level;

        if (m_vuLedBar)
            m_vuLedBar->setChannelLevel(channel, level);

        // Headers must not recolor with music/VU.
    }

    void setSoundPreviewVuLevels(int ch1, int ch2, int ch3, int noise)
    {
        m_vuLevels[0] = qBound(0, ch1, 15);
        m_vuLevels[1] = qBound(0, ch2, 15);
        m_vuLevels[2] = qBound(0, ch3, 15);
        m_vuLevels[3] = qBound(0, noise, 15);

        if (m_vuLedBar)
            m_vuLedBar->setLevels(m_vuLevels[0], m_vuLevels[1], m_vuLevels[2], m_vuLevels[3]);

        // Headers must not recolor with music/VU.
    }

private:
    QPushButton* makeToolbarButton(const QString& text)
    {
        QPushButton* b = new QPushButton(text, this);
        b->setMinimumHeight(32);
        return b;
    }

    QLabel* makeChannelHeader(const QString& text, const QString& colorName)
    {
        QLabel* lbl = new QLabel(text, this);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet(QString("QLabel { color:%1; font-weight:bold; background:transparent; }").arg(colorName));
        return lbl;
    }

    QColor soundVuColor(int channel) const
    {
        static const QColor baseColors[4] = {
            QColor("#FFE340"),
            QColor("#FF4FC8"),
            QColor("#68FF87"),
            QColor("#FFB24A")
        };

        channel = qBound(0, channel, 3);
        const int level = qBound(0, m_vuLevels[channel], 15);
        const QColor base = baseColors[channel];

        if (level <= 0)
            return base.darker(170);
        if (level < 6)
            return base.lighter(115);
        if (level < 11)
            return base.lighter(145);
        return QColor("#FF4040");
    }
    void updateSoundVuHeader(int channel)
    {
        if (channel < 0 || channel >= 4 || !m_channelHeaderLabels[channel])
            return;

        static const QString baseText[4] = {
            QStringLiteral("CH1 (Tone1)"),
            QStringLiteral("CH2 (Tone 2)"),
            QStringLiteral("CH3 (Tone 3)"),
            QStringLiteral("Noise")
        };

        static const QColor baseColor[4] = {
            QColor("#50E35A"),
            QColor("#66D9EF"),
            QColor("#C586C0"),
            QColor("#FFB24A")
        };

        // STATIC HEADER COLORS ONLY.
        // AAN = vaste kanaalkleur.
        // UIT = grijs.
        // GEEN VU, GEEN muziek, GEEN soundVuColor(), GEEN volume-kleur.
        const bool audible = m_channelAudible[channel];
        const QColor c = audible ? baseColor[channel] : QColor("#808080");
        const QString bg = audible ? QStringLiteral("#242424") : QStringLiteral("#1B1B1B");

        m_channelHeaderLabels[channel]->setText(baseText[channel]);
        m_channelHeaderLabels[channel]->setToolTip(audible
            ? tr("Click to switch this channel OFF in the Sound Editor")
            : tr("Click to switch this channel ON in the Sound Editor"));

        m_channelHeaderLabels[channel]->setStyleSheet(QString(
            "QLabel {"
            " color:%1;"
            " font-weight:bold;"
            " background:%2;"
            " border:1px solid %1;"
            " padding:4px 2px;"
            "}"
        ).arg(c.name(), bg));
    }

    void setChannelAudible(int channel, bool audible)
    {
        channel = qBound(0, channel, 3);

        if (m_channelAudible[channel] == audible)
            return;

        m_channelAudible[channel] = audible;

        // Runtime Sound Editor monitoring only:
        // - editor playback/preview may be silenced
        // - pattern data is not changed
        // - CVBasic export is not changed
        // - state is not saved in .adpsnd
        if (!audible) {
            m_vuLevels[channel] = 0;

            if (m_vuLedBar)
                m_vuLedBar->setChannelLevel(channel, 0);

            requestPreviewTone(channel, 0, 0);
        }

        updateSoundVuHeader(channel);
        autoRebuildSoundOutput();
        scheduleLiveInstrumentPlaybackRefresh();

        if (m_noteInfoLabel) {
            static const QString names[4] = {
                QStringLiteral("CH1"),
                QStringLiteral("CH2"),
                QStringLiteral("CH3"),
                QStringLiteral("Noise")
            };

            m_noteInfoLabel->setText(QString("%1 %2")
                                         .arg(names[channel])
                                         .arg(audible ? "ON" : "OFF"));
        }
    }

    void toggleChannelAudible(int channel)
    {
        channel = qBound(0, channel, 3);
        setChannelAudible(channel, !m_channelAudible[channel]);
    }

    void setupUi()
    {
        QVBoxLayout* root = new QVBoxLayout(this);
        root->setContentsMargins(8, 8, 8, 8);
        root->setSpacing(6);

        // Top button row
        QWidget* topButtons = new QWidget(this);
        QHBoxLayout* topButtonsLayout = new QHBoxLayout(topButtons);
        topButtonsLayout->setContentsMargins(0, 0, 0, 0);
        topButtonsLayout->setSpacing(8);

        QPushButton* newSongBtn = makeToolbarButton(tr("New Song"));
        QPushButton* openSongBtn = makeToolbarButton(tr("Open Song"));
        QPushButton* importMidiBtn = makeToolbarButton(tr("Import MIDI"));
        QPushButton* saveSongBtn = makeToolbarButton(tr("Save Song"));
        QPushButton* saveSongAsBtn = makeToolbarButton(tr("Save Song As"));
        QPushButton* insertBtn = makeToolbarButton(tr("Insert Selected in Editor"));
        QPushButton* copyBtn = makeToolbarButton(tr("Copy Selected"));
        QPushButton* exportBtn = makeToolbarButton(tr("Export ASM"));
        QPushButton* undoBtn = makeToolbarButton(tr("Undo"));
        QPushButton* redoBtn = makeToolbarButton(tr("Redo"));

        topButtonsLayout->addWidget(newSongBtn);
        topButtonsLayout->addWidget(openSongBtn);
        topButtonsLayout->addWidget(importMidiBtn);
        topButtonsLayout->addWidget(saveSongBtn);
        topButtonsLayout->addWidget(saveSongAsBtn);
        topButtonsLayout->addSpacing(10);
        topButtonsLayout->addWidget(insertBtn);
        topButtonsLayout->addWidget(copyBtn);
        topButtonsLayout->addWidget(exportBtn);
        topButtonsLayout->addStretch(1);
        topButtonsLayout->addWidget(undoBtn);
        topButtonsLayout->addWidget(redoBtn);

        root->addWidget(topButtons);

        connect(copyBtn, &QPushButton::clicked, this, [this]() {
            QApplication::clipboard()->setText(selectedSoundOutput());
        });

        connect(insertBtn, &QPushButton::clicked, this, [this]() {
            if (onInsertRequested)
                onInsertRequested(selectedSoundOutput());
        });

        connect(exportBtn, &QPushButton::clicked, this, [this]() {
            QDir().mkpath(soundBuildDefaultDir());

            const QString filePath = QFileDialog::getSaveFileName(
                this,
                tr("Export Sound DATA"),
                QDir(soundBuildDefaultDir()).filePath("sound_data.asm"),
                tr("ASM / Text (*.asm *.txt);;All Files (*.*)")
            );

            if (filePath.isEmpty())
                return;

            QFile f(filePath);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QMessageBox::warning(this, tr("Export failed"), f.errorString());
                return;
            }

            f.write(selectedSoundOutput().toUtf8());
            f.close();
        });

        auto layoutOnly = [this](const QString& name) {
            QMessageBox::information(this, tr("Sound Editor"),
                                     tr("%1 is voorlopig nog layout-only.").arg(name));
        };

        connect(newSongBtn, &QPushButton::clicked, this, [this]() { newSoundSong(); });
        connect(openSongBtn, &QPushButton::clicked, this, [this]() { openSoundSong(); });
        connect(importMidiBtn, &QPushButton::clicked, this, [this]() { importMidiFile(); });
        connect(saveSongBtn, &QPushButton::clicked, this, [this]() { saveSoundSong(); });
        connect(saveSongAsBtn, &QPushButton::clicked, this, [this]() { saveSoundSongAs(); });
        connect(undoBtn, &QPushButton::clicked, this, [this]() { undoSoundEdit(); });
        connect(redoBtn, &QPushButton::clicked, this, [this]() { redoSoundEdit(); });

        // Main body
        QWidget* body = new QWidget(this);
        QVBoxLayout* bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        bodyLayout->setSpacing(6);

        // Upper row: Song info / Order / Playback
        QWidget* upperRow = new QWidget(body);
        QHBoxLayout* upperLayout = new QHBoxLayout(upperRow);
        upperLayout->setContentsMargins(0, 0, 0, 0);
        upperLayout->setSpacing(8);

        // Song info
        QGroupBox* songInfoBox = new QGroupBox(tr("Song Info"), upperRow);
        QGridLayout* songLayout = new QGridLayout(songInfoBox);
        songLayout->setContentsMargins(8, 10, 8, 8);
        songLayout->setHorizontalSpacing(8);
        songLayout->setVerticalSpacing(6);

        m_songNameEdit = new QLineEdit(songInfoBox);
        m_songNameEdit->setText("Breaking the walls");
        m_authorEdit = new QLineEdit(songInfoBox);
        m_authorEdit->setText("CVBasic Dev");

        m_tempoSpin = new QSpinBox(songInfoBox);
        m_tempoSpin->setRange(32, 255);
        m_tempoSpin->setValue(125);

        m_speedSpin = new QSpinBox(songInfoBox);
        m_speedSpin->setRange(1, 16);
        m_speedSpin->setValue(6);

        m_rowsSpin = new QSpinBox(songInfoBox);
        m_rowsSpin->setRange(8, 64);
        m_rowsSpin->setValue(16);

        m_defaultInstrumentSpin = new QSpinBox(songInfoBox);
        m_defaultInstrumentSpin->setRange(0, 15);
        m_defaultInstrumentSpin->setDisplayIntegerBase(16);
        m_defaultInstrumentSpin->setPrefix("0");
        m_defaultInstrumentSpin->setValue(1);


        songLayout->addWidget(new QLabel(tr("Song Name:"), songInfoBox), 0, 0);
        songLayout->addWidget(m_songNameEdit, 0, 1, 1, 3);
        songLayout->addWidget(new QLabel(tr("Author:"), songInfoBox), 1, 0);
        songLayout->addWidget(m_authorEdit, 1, 1, 1, 3);
        songLayout->addWidget(new QLabel(tr("Tempo:"), songInfoBox), 2, 0);
        songLayout->addWidget(m_tempoSpin, 2, 1);
        songLayout->addWidget(new QLabel(tr("Speed:"), songInfoBox), 2, 2);
        songLayout->addWidget(m_speedSpin, 2, 3);
        songLayout->addWidget(new QLabel(tr("Rows/Pat:"), songInfoBox), 3, 0);
        songLayout->addWidget(m_rowsSpin, 3, 1);
        songLayout->addWidget(new QLabel(tr("Default Instr:"), songInfoBox), 3, 2);
        songLayout->addWidget(m_defaultInstrumentSpin, 3, 3);

        connect(m_rowsSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int rows) {
            resizePatternRows(rows);
        });

        connect(m_tempoSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int) {
            autoRebuildSoundOutput();
            restartCurrentStreamPlayback();
        });

        connect(m_speedSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int) {
            autoRebuildSoundOutput();
            restartCurrentStreamPlayback();
        });
        connect(m_defaultInstrumentSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int value) {
            if (m_instrumentsTable)
                m_instrumentsTable->selectRow(qBound(0, value, m_instrumentsTable->rowCount() - 1));
            autoRebuildSoundOutput();
        });


        // Order box
        QGroupBox* orderBox = new QGroupBox(tr("Order List (Sequence)"), upperRow);
        QVBoxLayout* orderLayout = new QVBoxLayout(orderBox);
        orderLayout->setContentsMargins(8, 10, 8, 8);
        orderLayout->setSpacing(6);

        m_orderTable = new QTableWidget(1, 16, orderBox);
        m_orderTable->setObjectName("soundOrderTable");
        m_orderTable->verticalHeader()->setVisible(true);
        m_orderTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        m_orderTable->verticalHeader()->setDefaultSectionSize(28);
        m_orderTable->verticalHeader()->setMinimumWidth(26);
        m_orderTable->setVerticalHeaderLabels({ "Pat" });
        m_orderTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_orderTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_orderTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_orderTable->setFixedHeight(62);
        QStringList orderHeaders;
        for (int i = 0; i < 16; ++i)
            orderHeaders << QString("%1").arg(i, 2, 10, QLatin1Char('0'));
        m_orderTable->setHorizontalHeaderLabels(orderHeaders);
        orderLayout->addWidget(m_orderTable);

        connect(m_orderTable, &QTableWidget::itemChanged,
                this, [this](QTableWidgetItem*) {
            if (!m_restoringSoundUndo)
                autoRebuildSoundOutput();
        });

        QHBoxLayout* orderButtons = new QHBoxLayout();
        QPushButton* orderAddBtn = new QPushButton(tr("Add"), orderBox);
        QPushButton* orderDeleteBtn = new QPushButton(tr("Delete"), orderBox);
        QPushButton* orderInsertBtn = new QPushButton(tr("Insert"), orderBox);
        QPushButton* orderClearBtn = new QPushButton(tr("Clear"), orderBox);
        QPushButton* orderExpandBtn = new QPushButton(tr("Expand"), orderBox);
        QPushButton* orderShrinkBtn = new QPushButton(tr("Shrink"), orderBox);

        for (QPushButton* b : { orderAddBtn, orderDeleteBtn, orderInsertBtn, orderClearBtn, orderExpandBtn, orderShrinkBtn }) {
            b->setMinimumHeight(28);
            orderButtons->addWidget(b);
        }

        connect(orderAddBtn, &QPushButton::clicked, this, [this]() { addOrderColumn(); });
        connect(orderDeleteBtn, &QPushButton::clicked, this, [this]() { deleteOrderColumn(); });
        connect(orderInsertBtn, &QPushButton::clicked, this, [this]() { insertOrderColumn(); });
        connect(orderClearBtn, &QPushButton::clicked, this, [this]() { clearOrderColumn(); });
        connect(orderExpandBtn, &QPushButton::clicked, this, [this]() { expandOrderList(); });
        connect(orderShrinkBtn, &QPushButton::clicked, this, [this]() { shrinkOrderList(); });

        orderLayout->addLayout(orderButtons);

        // Playback
        QGroupBox* playBox = new QGroupBox(tr("Playback Controls"), upperRow);
        QGridLayout* playLayout = new QGridLayout(playBox);
        playLayout->setContentsMargins(8, 10, 8, 8);
        playLayout->setHorizontalSpacing(8);
        playLayout->setVerticalSpacing(6);

        auto makePlaybackIconButton = [&](const QString& resourceName, const QString& tooltip) -> QPushButton* {
            QPushButton* b = new QPushButton(playBox);
            b->setToolTip(tooltip);
            b->setFixedSize(52, 52);
            b->setIconSize(QSize(48, 48));
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::NoFocus);

            QPixmap px = soundResourcePixmap(resourceName);
            if (!px.isNull())
                b->setIcon(QIcon(px));
            else
                b->setText(tooltip.left(1));

            return b;
        };

        QPushButton* playBtn   = makePlaybackIconButton("SND_PLAY",    tr("Play  (F5)"));
        QPushButton* stopBtn   = makePlaybackIconButton("SND_STOP",    tr("Stop  (Esc)"));
        QPushButton* rewindBtn = makePlaybackIconButton("SND_RESTART", tr("Rewind  (Home)"));
        QPushButton* loopBtn   = makePlaybackIconButton("SND_LOOP",    tr("Loop On/Off  (F6)"));

        playBtn->setCheckable(true);
        stopBtn->setCheckable(true);
        loopBtn->setCheckable(true);

        const QString playButtonStyle =
            "QPushButton { border: 1px solid #555; background: #252525; border-radius: 4px; }"
            "QPushButton:hover { background: #303030; }"
            "QPushButton:pressed { background: #3A3A3A; }"
            "QPushButton:checked { border: 2px solid #44D062; background: #173A22; }";

        const QString stopButtonStyle =
            "QPushButton { border: 1px solid #555; background: #252525; border-radius: 4px; }"
            "QPushButton:hover { background: #303030; }"
            "QPushButton:pressed { background: #3A3A3A; }"
            "QPushButton:checked { border: 2px solid #E04B4B; background: #3A1B1B; }";

        const QString neutralButtonStyle =
            "QPushButton { border: 1px solid #555; background: #252525; border-radius: 4px; }"
            "QPushButton:hover { background: #303030; }"
            "QPushButton:pressed { background: #3A3A3A; }";

        const QString loopButtonStyle =
            "QPushButton { border: 1px solid #555; background: #252525; border-radius: 4px; }"
            "QPushButton:hover { background: #303030; }"
            "QPushButton:pressed { background: #3A3A3A; }"
            "QPushButton:checked { border: 2px solid #50E3C2; background: #173A34; }";

        playBtn->setStyleSheet(playButtonStyle);
        stopBtn->setStyleSheet(stopButtonStyle);
        rewindBtn->setStyleSheet(neutralButtonStyle);
        loopBtn->setStyleSheet(loopButtonStyle);

        m_playbackPlayBtn = playBtn;
        m_playbackStopBtn = stopBtn;
        m_playbackRewindBtn = rewindBtn;
        m_playbackLoopBtn = loopBtn;

        setPlaybackUiPlaying(false);

        playBtn->setShortcut(QKeySequence(Qt::Key_F5));
        stopBtn->setShortcut(QKeySequence(Qt::Key_Escape));
        rewindBtn->setShortcut(QKeySequence(Qt::Key_Home));
        loopBtn->setShortcut(QKeySequence(Qt::Key_F6));

        m_playPatternSpin = new QSpinBox(playBox);
        m_playPatternSpin->setRange(0, 255);
        m_playPatternSpin->setDisplayIntegerBase(16);
        m_playPatternSpin->setPrefix("");
        m_playPatternSpin->setValue(0);

        m_playFromSpin = new QSpinBox(playBox);
        m_playFromSpin->setRange(0, 255);
        m_playFromSpin->setDisplayIntegerBase(16);
        m_playFromSpin->setPrefix("");
        m_playFromSpin->setValue(0);

        connect(m_playFromSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int) {
            if (m_isPatternPlaying)
                rewindPatternPlayback();
        });

        m_loopSongCheck = new QCheckBox(tr("Loop Song"), playBox);
        m_loopSongCheck->setChecked(true);
        loopBtn->setChecked(m_loopSongCheck->isChecked());

        connect(m_loopSongCheck, &QCheckBox::toggled,
                loopBtn, &QPushButton::setChecked);

        connect(m_loopSongCheck, &QCheckBox::toggled,
                this, [this](bool) {
            restartCurrentStreamPlayback();
        });

        m_followPlayCheck = new QCheckBox(tr("Follow Play"), playBox);
        m_followPlayCheck->setChecked(true);

        m_playbackStatusLabel = new QLabel(tr("Stopped"), playBox);
        m_playbackStatusLabel->setAlignment(Qt::AlignCenter);
        m_playbackStatusLabel->setStyleSheet(
            "QLabel {"
            " color: #DADADA;"
            " background: #151515;"
            " border: 1px solid #404040;"
            " border-radius: 4px;"
            " padding: 3px;"
            " font-weight: bold;"
            " }"
        );

        m_vuLedBar = new SoundVuLedBarWidget(playBox);
        m_vuLedBar->setLevels(0, 0, 0, 0);

        playLayout->addWidget(playBtn, 0, 0);
        playLayout->addWidget(stopBtn, 0, 1);
        playLayout->addWidget(rewindBtn, 0, 2);
        playLayout->addWidget(loopBtn, 0, 3);
        playLayout->addWidget(m_vuLedBar, 0, 4, 4, 1);
        playLayout->addWidget(new QLabel(tr("Play Pattern:"), playBox), 1, 0, 1, 2);
        playLayout->addWidget(m_playPatternSpin, 1, 2, 1, 2);
        playLayout->addWidget(new QLabel(tr("Play From:"), playBox), 2, 0, 1, 2);
        playLayout->addWidget(m_playFromSpin, 2, 2, 1, 2);
        playLayout->addWidget(m_loopSongCheck, 3, 0, 1, 2);
        playLayout->addWidget(m_followPlayCheck, 3, 2, 1, 2);
        playLayout->addWidget(m_playbackStatusLabel, 4, 0, 1, 5);
        playLayout->setColumnStretch(4, 0);

        connect(playBtn, &QPushButton::clicked, this, [this]() {
            startPatternPlayback();
        });
        connect(stopBtn, &QPushButton::clicked, this, [this]() {
            stopPatternPlayback();
        });
        connect(rewindBtn, &QPushButton::clicked, this, [this]() {
            rewindPatternPlayback();
        });
        connect(loopBtn, &QPushButton::clicked, this, [this]() {
            if (m_loopSongCheck)
                m_loopSongCheck->setChecked(!m_loopSongCheck->isChecked());
        });

        upperLayout->addWidget(songInfoBox, 3);
        upperLayout->addWidget(orderBox, 5);
        upperLayout->addWidget(playBox, 3);

        bodyLayout->addWidget(upperRow, 0);

        // Middle row: left editor tabs / right instruments+output
        // Sound Editor only: no splitter, fixed layout.
        QWidget* soundMiddleRow = new QWidget(body);
        QHBoxLayout* soundMiddleLayout = new QHBoxLayout(soundMiddleRow);
        soundMiddleLayout->setContentsMargins(0, 0, 0, 0);
        soundMiddleLayout->setSpacing(6);

        QWidget* leftPanel = new QWidget(soundMiddleRow);
        QVBoxLayout* leftPanelLayout = new QVBoxLayout(leftPanel);
        leftPanelLayout->setContentsMargins(0, 0, 0, 0);
        leftPanelLayout->setSpacing(6);

        QWidget* rightPanel = new QWidget(soundMiddleRow);
        QVBoxLayout* rightPanelLayout = new QVBoxLayout(rightPanel);
        rightPanelLayout->setContentsMargins(0, 0, 0, 0);
        rightPanelLayout->setSpacing(6);

        m_editorTabs = new QTabWidget(leftPanel);
        m_editorTabs->setDocumentMode(false);
        m_editorTabs->setMovable(false);

        QWidget* patternPage = new QWidget(m_editorTabs);
        QVBoxLayout* patternPageLayout = new QVBoxLayout(patternPage);
        patternPageLayout->setContentsMargins(4, 4, 4, 4);
        patternPageLayout->setSpacing(6);

        // Colored grouped channel headers aligned with ROW + 4 equal channel blocks
        QWidget* channelHeader = new QWidget(patternPage);
        QHBoxLayout* channelHeaderLayout = new QHBoxLayout(channelHeader);
        channelHeaderLayout->setContentsMargins(0, 0, 0, 0);
        channelHeaderLayout->setSpacing(0);

        // ROW has its own width so we can tune it separately from the 4 channel blocks.
        const int rowHeaderWidth = 50;
        const int rowColumnWidth = 50;

        QLabel* rowHeaderLabel = new QLabel(tr("ROW"), channelHeader);
        rowHeaderLabel->setAlignment(Qt::AlignCenter);
        rowHeaderLabel->setFixedWidth(rowHeaderWidth);
        rowHeaderLabel->setStyleSheet("QLabel { color:#FFFFFF; font-weight:bold; background:#242424; border:1px solid #555555; border-right:none; padding:4px 2px; }");
        channelHeaderLayout->addWidget(rowHeaderLabel);

        auto styleChannelHeader = [](QLabel* lbl, const QString& color, int fixedWidth) {
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setFixedWidth(fixedWidth);
            lbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
            lbl->setStyleSheet(QString("QLabel { color:%1; font-weight:bold; background:#242424; border:1px solid #555555; padding:4px 2px; }").arg(color));
        };

        // Breedteverdeling van één kanaalblok:
        // CHx-header = Note + Inst + Vol + Fx.
        // Deze waarden worden als stretch-factoren gebruikt zodat de header
        // exact over dezelfde ruimte valt als de 4 kolommen eronder.
        const int noteHeaderWidth = 49;
        const int instHeaderWidth = 49;
        const int volHeaderWidth  = 49;
        const int fxHeaderWidth   = 49;

        const int ch1HeaderWidth   = noteHeaderWidth + instHeaderWidth + volHeaderWidth + fxHeaderWidth;
        const int ch2HeaderWidth   = noteHeaderWidth + instHeaderWidth + volHeaderWidth + fxHeaderWidth;
        const int ch3HeaderWidth   = noteHeaderWidth + instHeaderWidth + volHeaderWidth + fxHeaderWidth;
        const int noiseHeaderWidth = noteHeaderWidth + instHeaderWidth + volHeaderWidth + fxHeaderWidth;

        auto* ch1Header = new SoundChannelHeaderLabel(tr("CH1 (Tone1)"), channelHeader);
        auto* ch2Header = new SoundChannelHeaderLabel(tr("CH2 (Tone 2)"), channelHeader);
        auto* ch3Header = new SoundChannelHeaderLabel(tr("CH3 (Tone 3)"), channelHeader);
        auto* noiseHeader = new SoundChannelHeaderLabel(tr("Noise"), channelHeader);

        m_channelHeaderLabels[0] = ch1Header;
        m_channelHeaderLabels[1] = ch2Header;
        m_channelHeaderLabels[2] = ch3Header;
        m_channelHeaderLabels[3] = noiseHeader;

        styleChannelHeader(ch1Header, "#FFE340", ch1HeaderWidth);
        styleChannelHeader(ch2Header, "#FF4FC8", ch2HeaderWidth);
        styleChannelHeader(ch3Header, "#68FF87", ch3HeaderWidth);
        styleChannelHeader(noiseHeader, "#FFB24A", noiseHeaderWidth);

        ch1Header->onClicked = [this]() { toggleChannelAudible(0); };
        ch2Header->onClicked = [this]() { toggleChannelAudible(1); };
        ch3Header->onClicked = [this]() { toggleChannelAudible(2); };
        noiseHeader->onClicked = [this]() { toggleChannelAudible(3); };

        for (int ch = 0; ch < 4; ++ch)
            updateSoundVuHeader(ch);


        for (int ch = 0; ch < 4; ++ch)
            updateSoundVuHeader(ch);


        // Fixed kanaalheader-rij: geen stretch meer.
        channelHeaderLayout->addWidget(ch1Header);
        channelHeaderLayout->addWidget(ch2Header);
        channelHeaderLayout->addWidget(ch3Header);
        channelHeaderLayout->addWidget(noiseHeader);
        channelHeaderLayout->addStretch(1);
        patternPageLayout->addWidget(channelHeader, 0);

        m_patternTable = new QTableWidget(16, 17, patternPage);
        m_patternTable->setObjectName("soundPatternTable");
        m_patternDelegate = new SoundPatternDelegate(m_patternTable);
        m_patternTable->setItemDelegate(m_patternDelegate);
        m_patternTable->verticalHeader()->hide();
        m_patternTable->setAlternatingRowColors(true);
        m_patternTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_patternTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_patternTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
        m_patternTable->setHorizontalHeaderLabels({
            tr("Row"),
            tr("Note"), tr("Inst"), tr("Vol"), tr("Fx"),
            tr("Note"), tr("Inst"), tr("Vol"), tr("Fx"),
            tr("Note"), tr("Inst"), tr("Vol"), tr("Fx"),
            tr("Note"), tr("Inst"), tr("Vol"), tr("Fx")
        });
        m_patternTable->horizontalHeader()->setStretchLastSection(false);
        m_patternTable->horizontalHeader()->setMinimumSectionSize(36);
        m_patternTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
        m_patternTable->setColumnWidth(0, rowColumnWidth);

        // De 4 subkolommen per kanaal gebruiken dezelfde basisbreedtes
        // als de header-berekening hierboven: Note + Inst + Vol + Fx.
        // Daardoor komt CH1/CH2/CH3/NOISE mooi boven hun eigen 4 kolommen te staan.
        for (int block = 0; block < 4; ++block) {
            const int base = 1 + block * 4;
            m_patternTable->horizontalHeader()->setSectionResizeMode(base + 0, QHeaderView::Interactive);
            m_patternTable->horizontalHeader()->setSectionResizeMode(base + 1, QHeaderView::Interactive);
            m_patternTable->horizontalHeader()->setSectionResizeMode(base + 2, QHeaderView::Interactive);
            m_patternTable->horizontalHeader()->setSectionResizeMode(base + 3, QHeaderView::Interactive);

            m_patternTable->setColumnWidth(base + 0, noteHeaderWidth);
            m_patternTable->setColumnWidth(base + 1, instHeaderWidth);
            m_patternTable->setColumnWidth(base + 2, volHeaderWidth);
            m_patternTable->setColumnWidth(base + 3, fxHeaderWidth);
        }

        patternPageLayout->addWidget(m_patternTable, 1);

        connect(m_patternTable, &QTableWidget::itemChanged,
                this, [this](QTableWidgetItem*) {
            if (!m_restoringSoundUndo && !m_loadingSoundPattern) {
                saveCurrentPatternToMemory();
                autoRebuildSoundOutput();
            }
        });

        QWidget* patternBottom = new QWidget(patternPage);
        QVBoxLayout* patternBottomLayout = new QVBoxLayout(patternBottom);
        patternBottomLayout->setContentsMargins(0, 0, 0, 0);
        patternBottomLayout->setSpacing(4);

        QHBoxLayout* patternBottomLine1 = new QHBoxLayout();
        patternBottomLine1->setContentsMargins(0, 0, 0, 0);
        patternBottomLine1->setSpacing(6);

        QHBoxLayout* patternBottomLine2 = new QHBoxLayout();
        patternBottomLine2->setContentsMargins(0, 0, 0, 0);
        patternBottomLine2->setSpacing(6);

        QHBoxLayout* patternBottomLine3 = new QHBoxLayout();
        patternBottomLine3->setContentsMargins(0, 0, 0, 0);
        patternBottomLine3->setSpacing(6);

        m_patternSpin = new QSpinBox(patternBottom);
        m_patternSpin->setRange(0, 255);
        m_patternSpin->setDisplayIntegerBase(16);
        m_patternSpin->setValue(0);

        connect(m_patternSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int patternIndex) {
            switchSoundPattern(patternIndex);
        });

        m_rowSpin = new QSpinBox(patternBottom);
        m_rowSpin->setRange(0, 63);
        m_rowSpin->setDisplayIntegerBase(16);
        m_rowSpin->setValue(6);

        connect(m_rowSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int row) {
            if (m_patternTable)
                m_patternTable->selectRow(qBound(0, row, m_patternTable->rowCount() - 1));
        });

        m_stepSpin = new QSpinBox(patternBottom);
        m_stepSpin->setRange(1, 16);
        m_stepSpin->setValue(1);

        m_activeChannelCombo = new QComboBox(patternBottom);
        m_activeChannelCombo->addItem(tr("CH1"), 0);
        m_activeChannelCombo->addItem(tr("CH2"), 1);
        m_activeChannelCombo->addItem(tr("CH3"), 2);
        m_activeChannelCombo->addItem(tr("NOISE"), 3);
        m_activeChannelCombo->setCurrentIndex(0);

        m_volumeSpin = new QSpinBox(patternBottom);
        m_volumeSpin->setRange(0, 15);
        m_volumeSpin->setDisplayIntegerBase(16);
        m_volumeSpin->setPrefix("V");
        m_volumeSpin->setValue(15);

        m_keyboardTestOnlyCheck = new QCheckBox(tr("Test Only"), patternBottom);
        m_keyboardTestOnlyCheck->setToolTip(tr("Speel toetsen zonder de pattern table in te vullen"));

        connect(m_activeChannelCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) {
            if (m_noteInfoLabel)
                m_noteInfoLabel->setText(QString("Active channel: %1").arg(activeSoundChannelName()));
        });

        connect(m_volumeSpin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int) { autoRebuildSoundOutput(); });

        m_noteInfoLabel = new QLabel(tr("Active: CH1   Note: ---   PSG: ---"), patternBottom);

        // Lijn 1: alleen de note/PSG info zodat het keyboard zijn breedte behoudt.
        patternBottomLine1->addWidget(m_noteInfoLabel);
        patternBottomLine1->addStretch(1);

        // Lijn 2: alle edit-controls compact op één regel.
        patternBottomLine2->addWidget(new QLabel(tr("Pattern:"), patternBottom));
        patternBottomLine2->addWidget(m_patternSpin);
        patternBottomLine2->addWidget(new QLabel(tr("Row:"), patternBottom));
        patternBottomLine2->addWidget(m_rowSpin);
        patternBottomLine2->addSpacing(10);
        patternBottomLine2->addWidget(new QLabel(tr("Channel:"), patternBottom));
        patternBottomLine2->addWidget(m_activeChannelCombo);
        patternBottomLine2->addWidget(new QLabel(tr("Vol:"), patternBottom));
        patternBottomLine2->addWidget(m_volumeSpin);
        patternBottomLine2->addSpacing(10);
        patternBottomLine2->addWidget(m_keyboardTestOnlyCheck);
        patternBottomLine2->addSpacing(10);
        patternBottomLine2->addWidget(new QLabel(tr("Step:"), patternBottom));
        patternBottomLine2->addWidget(m_stepSpin);
        patternBottomLine2->addStretch(1);

        QPushButton* insertRowBtn = new QPushButton(tr("Insert Row"), patternBottom);
        QPushButton* deleteRowBtn = new QPushButton(tr("Delete Row"), patternBottom);
        QPushButton* cutRowBtn = new QPushButton(tr("Cut"), patternBottom);
        QPushButton* copyRowBtn = new QPushButton(tr("Copy"), patternBottom);
        QPushButton* pasteRowBtn = new QPushButton(tr("Paste"), patternBottom);
        QPushButton* clearRowBtn = new QPushButton(tr("Clear"), patternBottom);

        for (QPushButton* b : { insertRowBtn, deleteRowBtn, cutRowBtn, copyRowBtn, pasteRowBtn, clearRowBtn })
            b->setMinimumHeight(26);

        // Lijn 2: Insert/Delete rechts uitgelijnd.
        patternBottomLine2->addStretch(1);
        patternBottomLine2->addWidget(insertRowBtn);
        patternBottomLine2->addWidget(deleteRowBtn);

        QPushButton* copyPatternBtn = new QPushButton(tr("Copy Pattern"), patternBottom);
        QPushButton* pastePatternBtn = new QPushButton(tr("Paste Pattern"), patternBottom);
        QPushButton* clearPatternBtn = new QPushButton(tr("Clear Pattern"), patternBottom);
        QPushButton* duplicatePatternBtn = new QPushButton(tr("Duplicate → Next"), patternBottom);

        for (QPushButton* b : { copyPatternBtn, pastePatternBtn, clearPatternBtn, duplicatePatternBtn })
            b->setMinimumHeight(26);

        // Lijn 3:
        // links  = pattern-level functies
        // rechts = row clipboard/edit functies
        patternBottomLine3->addWidget(copyPatternBtn);
        patternBottomLine3->addWidget(pastePatternBtn);
        patternBottomLine3->addWidget(clearPatternBtn);
        patternBottomLine3->addWidget(duplicatePatternBtn);
        patternBottomLine3->addStretch(1);
        patternBottomLine3->addWidget(cutRowBtn);
        patternBottomLine3->addWidget(copyRowBtn);
        patternBottomLine3->addWidget(pasteRowBtn);
        patternBottomLine3->addWidget(clearRowBtn);

        connect(copyPatternBtn, &QPushButton::clicked, this, [this]() {
            copyCurrentPattern();
        });
        connect(pastePatternBtn, &QPushButton::clicked, this, [this]() {
            pastePatternToCurrent();
        });
        connect(clearPatternBtn, &QPushButton::clicked, this, [this]() {
            clearCurrentPattern();
        });
        connect(duplicatePatternBtn, &QPushButton::clicked, this, [this]() {
            duplicateCurrentPatternToNext();
        });

        connect(insertRowBtn, &QPushButton::clicked, this, [this]() {
            insertEmptyPatternRow();
        });
        connect(deleteRowBtn, &QPushButton::clicked, this, [this]() {
            deleteCurrentPatternRow();
        });
        connect(cutRowBtn, &QPushButton::clicked, this, [this]() {
            cutCurrentPatternRow();
        });
        connect(copyRowBtn, &QPushButton::clicked, this, [this]() {
            copyCurrentPatternRow();
        });
        connect(pasteRowBtn, &QPushButton::clicked, this, [this]() {
            pastePatternRow();
        });
        connect(clearRowBtn, &QPushButton::clicked, this, [this]() {
            clearCurrentPatternRow();
        });

        patternBottomLayout->addLayout(patternBottomLine1);
        patternBottomLayout->addLayout(patternBottomLine2);
        patternBottomLayout->addLayout(patternBottomLine3);

        patternPageLayout->addWidget(patternBottom, 0);

        QWidget* keyboardPanel = new QWidget(patternPage);
        keyboardPanel->setObjectName("soundKeyboardPanel");
        keyboardPanel->setMinimumHeight(140);
        keyboardPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        QHBoxLayout* keyboardLayout = new QHBoxLayout(keyboardPanel);
        keyboardLayout->setContentsMargins(0, 0, 0, 0);
        keyboardLayout->setSpacing(10);

        QWidget* octaveBox = new QWidget(keyboardPanel);
        QVBoxLayout* octaveLayout = new QVBoxLayout(octaveBox);
        octaveLayout->setContentsMargins(0, 0, 0, 0);
        octaveLayout->addWidget(new QLabel(tr("Octave"), octaveBox));
        m_octaveSpin = new QSpinBox(octaveBox);
        m_octaveSpin->setRange(1, 8);
        m_octaveSpin->setValue(4);
        octaveLayout->addWidget(m_octaveSpin);
        octaveLayout->addStretch(1);

        SoundKeyboardOverlayWidget* keysLabel = new SoundKeyboardOverlayWidget(keyboardPanel);
        keysLabel->setMinimumHeight(120);
        keysLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        keysLabel->setObjectName("soundKeyboardImage");

        QWidget* rightKeyboardControls = new QWidget(keyboardPanel);
        QVBoxLayout* rightKeyboardLayout = new QVBoxLayout(rightKeyboardControls);
        rightKeyboardLayout->setContentsMargins(0, 0, 0, 0);
        rightKeyboardLayout->setSpacing(8);

        QPushButton* noteOffBtn = new QPushButton(tr("Note Off"), rightKeyboardControls);
        QPushButton* insertNoteBtn = new QPushButton(tr("Insert"), rightKeyboardControls);
        noteOffBtn->setMinimumHeight(32);
        insertNoteBtn->setMinimumHeight(32);
        m_mirrorCheck = new QCheckBox(tr("Mirror"), rightKeyboardControls);
        m_fillDownCheck = new QCheckBox(tr("Fill Down"), rightKeyboardControls);

        rightKeyboardLayout->addWidget(noteOffBtn);
        rightKeyboardLayout->addWidget(insertNoteBtn);
        rightKeyboardLayout->addStretch(1);
        rightKeyboardLayout->addWidget(m_mirrorCheck);
        rightKeyboardLayout->addWidget(m_fillDownCheck);

        keyboardLayout->addWidget(octaveBox, 0);
        keyboardLayout->addWidget(keysLabel, 1);
        keyboardLayout->addWidget(rightKeyboardControls, 0);

        connect(noteOffBtn, &QPushButton::clicked, this, [this]() {
            stopActivePreviewChannel();
        });
        connect(insertNoteBtn, &QPushButton::clicked, this, [=]() { layoutOnly(tr("Insert")); });

        keysLabel->onKeyPressed = [this](const QString& keyName) {
            insertKeyboardKeyIntoPattern(keyName);
        };

        patternPageLayout->addWidget(keyboardPanel, 0);

        QWidget* instrumentsPage = new QWidget(m_editorTabs);
        QVBoxLayout* instrumentsPageLayout = new QVBoxLayout(instrumentsPage);
        instrumentsPageLayout->setContentsMargins(8, 8, 8, 8);
        instrumentsPageLayout->setSpacing(8);

        QGroupBox* visualInstrumentBox = new QGroupBox(tr("Visual Instrument Editor (CVBasic compatible PSG)"), instrumentsPage);
        QVBoxLayout* visualInstrumentRoot = new QVBoxLayout(visualInstrumentBox);
        visualInstrumentRoot->setContentsMargins(8, 10, 8, 8);
        visualInstrumentRoot->setSpacing(8);

        QGridLayout* visualTopGrid = new QGridLayout();
        visualTopGrid->setHorizontalSpacing(8);
        visualTopGrid->setVerticalSpacing(6);

        m_instNameEdit = new QLineEdit(visualInstrumentBox);
        m_instTypeCombo = new QComboBox(visualInstrumentBox);
        m_instTypeCombo->addItems({ "Tone", "Noise", "---" });

        m_instVolumeSpin = new QSpinBox(visualInstrumentBox);
        m_instVolumeSpin->setRange(0, 15);
        m_instVolumeSpin->setDisplayIntegerBase(16);
        m_instVolumeSpin->setPrefix("0");
        m_instVolumeSpin->setFixedWidth(52);

        m_instVolumeSlider = new QSlider(Qt::Horizontal, visualInstrumentBox);
        m_instVolumeSlider->setRange(0, 15);
        m_instVolumeSlider->setTickPosition(QSlider::TicksBelow);
        m_instVolumeSlider->setTickInterval(1);
        m_instVolumeSlider->setFixedWidth(125);
        m_instVolumeSlider->setToolTip(tr("Instrument base volume 0..15"));

        m_instEnvSpin = new QSpinBox(visualInstrumentBox);
        m_instEnvSpin->setRange(0, 15);
        m_instEnvSpin->setDisplayIntegerBase(16);
        m_instEnvSpin->setPrefix("0");
        m_instEnvSpin->setFixedWidth(52);

        m_instEnvSlider = new QSlider(Qt::Horizontal, visualInstrumentBox);
        m_instEnvSlider->setRange(0, 15);
        m_instEnvSlider->setTickPosition(QSlider::TicksBelow);
        m_instEnvSlider->setTickInterval(1);
        m_instEnvSlider->setFixedWidth(125);
        m_instEnvSlider->setToolTip(tr("Envelope preset 0..15"));

        m_instFadeoutSpin = new QSpinBox(visualInstrumentBox);
        m_instFadeoutSpin->setRange(0, 15);
        m_instFadeoutSpin->setDisplayIntegerBase(16);
        m_instFadeoutSpin->setPrefix("0");
        m_instFadeoutSpin->setFixedWidth(52);

        m_instFadeoutSlider = new QSlider(Qt::Horizontal, visualInstrumentBox);
        m_instFadeoutSlider->setRange(0, 15);
        m_instFadeoutSlider->setTickPosition(QSlider::TicksBelow);
        m_instFadeoutSlider->setTickInterval(1);
        m_instFadeoutSlider->setFixedWidth(125);
        m_instFadeoutSlider->setToolTip(tr("Fadeout 0..15"));

        m_instWaveXSpin = new QSpinBox(visualInstrumentBox);
        m_instWaveXSpin->setRange(0, 100);
        m_instWaveXSpin->setFixedWidth(58);

        m_instWaveXSlider = new QSlider(Qt::Horizontal, visualInstrumentBox);
        m_instWaveXSlider->setRange(0, 100);
        m_instWaveXSlider->setTickPosition(QSlider::TicksBelow);
        m_instWaveXSlider->setTickInterval(25);
        m_instWaveXSlider->setFixedWidth(125);

        m_instWaveYSpin = new QSpinBox(visualInstrumentBox);
        m_instWaveYSpin->setRange(0, 100);
        m_instWaveYSpin->setFixedWidth(58);

        m_instWaveYSlider = new QSlider(Qt::Horizontal, visualInstrumentBox);
        m_instWaveYSlider->setRange(0, 100);
        m_instWaveYSlider->setTickPosition(QSlider::TicksBelow);
        m_instWaveYSlider->setTickInterval(25);
        m_instWaveYSlider->setFixedWidth(125);

        visualTopGrid->addWidget(new QLabel(tr("Name:"), visualInstrumentBox), 0, 0);
        visualTopGrid->addWidget(m_instNameEdit, 0, 1, 1, 5);

        QGroupBox* waveGroup = new QGroupBox(tr("Wave"), visualInstrumentBox);
        QGridLayout* waveGrid = new QGridLayout(waveGroup);
        waveGrid->setContentsMargins(8, 10, 8, 8);
        waveGrid->setHorizontalSpacing(6);
        waveGrid->setVerticalSpacing(6);
        waveGrid->addWidget(new QLabel(tr("Wave X:"), waveGroup), 0, 0);
        waveGrid->addWidget(m_instWaveXSlider, 0, 1);
        waveGrid->addWidget(m_instWaveXSpin, 0, 2);
        waveGrid->addWidget(new QLabel(tr("Wave Y:"), waveGroup), 1, 0);
        waveGrid->addWidget(m_instWaveYSlider, 1, 1);
        waveGrid->addWidget(m_instWaveYSpin, 1, 2);

        QGroupBox* toneGroup = new QGroupBox(tr("Tone / Envelope"), visualInstrumentBox);
        QGridLayout* toneGrid = new QGridLayout(toneGroup);
        toneGrid->setContentsMargins(8, 10, 8, 8);
        toneGrid->setHorizontalSpacing(6);
        toneGrid->setVerticalSpacing(6);
        toneGrid->addWidget(new QLabel(tr("Tone:"), toneGroup), 0, 0);
        toneGrid->addWidget(m_instTypeCombo, 0, 1, 1, 2);
        toneGrid->addWidget(new QLabel(tr("Volume:"), toneGroup), 1, 0);
        toneGrid->addWidget(m_instVolumeSlider, 1, 1);
        toneGrid->addWidget(m_instVolumeSpin, 1, 2);
        toneGrid->addWidget(new QLabel(tr("Envelope:"), toneGroup), 2, 0);
        toneGrid->addWidget(m_instEnvSlider, 2, 1);
        toneGrid->addWidget(m_instEnvSpin, 2, 2);
        toneGrid->addWidget(new QLabel(tr("Fadeout:"), toneGroup), 3, 0);
        toneGrid->addWidget(m_instFadeoutSlider, 3, 1);
        toneGrid->addWidget(m_instFadeoutSpin, 3, 2);

        QHBoxLayout* instrumentGroups = new QHBoxLayout();
        instrumentGroups->setSpacing(8);
        instrumentGroups->addWidget(waveGroup, 0);
        instrumentGroups->addWidget(toneGroup, 0);
        instrumentGroups->addStretch(1);

        visualInstrumentRoot->addLayout(visualTopGrid);
        visualInstrumentRoot->addLayout(instrumentGroups);

        QHBoxLayout* visualMid = new QHBoxLayout();
        visualMid->setSpacing(8);

        m_instWavePreview = new SoundInstrumentWavePreviewWidget(visualInstrumentBox);

        visualMid->addWidget(m_instWavePreview, 1);

        visualInstrumentRoot->addLayout(visualMid);

        m_instEnvPreview = new SoundInstrumentEnvelopePreviewWidget(visualInstrumentBox);
        visualInstrumentRoot->addWidget(m_instEnvPreview);

        QLabel* visualHelp = new QLabel(
            tr("ENV is the main PSG envelope preset. The Sound Editor targets the standard SN76489/TMS9919 PSG only: tone/noise/volume."),
            visualInstrumentBox
        );
        visualHelp->setWordWrap(true);
        visualHelp->setStyleSheet("QLabel { color:#B8C6D8; background:transparent; }");
        visualInstrumentRoot->addWidget(visualHelp);

        QHBoxLayout* visualTestButtons = new QHBoxLayout();
        QPushButton* visualTestKeyBtn = new QPushButton(tr("Test Key C-4"), visualInstrumentBox);
        QPushButton* visualStopKeyBtn = new QPushButton(tr("Stop Key"), visualInstrumentBox);
        visualTestButtons->addWidget(visualTestKeyBtn);
        visualTestButtons->addWidget(visualStopKeyBtn);
        visualTestButtons->addStretch(1);
        visualInstrumentRoot->addLayout(visualTestButtons);

        connect(visualTestKeyBtn, &QPushButton::clicked, this, [this]() {
            previewSelectedInstrumentKey();
        });
        connect(visualStopKeyBtn, &QPushButton::clicked, this, [this]() {
            stopInstrumentTestKey();
        });

        QHBoxLayout* visualBankButtons = new QHBoxLayout();
        QPushButton* visualSaveBankBtn = new QPushButton(tr("Save Instrument Bank"), visualInstrumentBox);
        QPushButton* visualLoadBankBtn = new QPushButton(tr("Load Instrument Bank"), visualInstrumentBox);
        visualBankButtons->addStretch(1);
        visualBankButtons->addWidget(visualSaveBankBtn);
        visualBankButtons->addWidget(visualLoadBankBtn);
        visualInstrumentRoot->addLayout(visualBankButtons);

        connect(visualSaveBankBtn, &QPushButton::clicked, this, [this]() {
            saveInstrumentBank();
        });
        connect(visualLoadBankBtn, &QPushButton::clicked, this, [this]() {
            loadInstrumentBank();
        });

        instrumentsPageLayout->addWidget(visualInstrumentBox, 1);

        connect(m_instNameEdit, &QLineEdit::editingFinished, this, [this]() {
            applyInstrumentEditorToTable();
        });
        connect(m_instTypeCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
            applyInstrumentEditorToTable();
        });
        connect(m_instVolumeSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
            if (m_instVolumeSlider && m_instVolumeSlider->value() != value)
                m_instVolumeSlider->setValue(value);
            applyInstrumentEditorToTable();
        });
        connect(m_instVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
            if (m_instVolumeSpin && m_instVolumeSpin->value() != value)
                m_instVolumeSpin->setValue(value);
            applyInstrumentEditorToTable();
        });

        connect(m_instEnvSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
            if (m_instEnvSlider && m_instEnvSlider->value() != value)
                m_instEnvSlider->setValue(value);
            applyInstrumentEditorToTable();
        });
        connect(m_instEnvSlider, &QSlider::valueChanged, this, [this](int value) {
            if (m_instEnvSpin && m_instEnvSpin->value() != value)
                m_instEnvSpin->setValue(value);
            applyInstrumentEditorToTable();
        });
        connect(m_instFadeoutSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
            if (m_instFadeoutSlider && m_instFadeoutSlider->value() != value)
                m_instFadeoutSlider->setValue(value);
            applyInstrumentEditorToTable();
        });
        connect(m_instFadeoutSlider, &QSlider::valueChanged, this, [this](int value) {
            if (m_instFadeoutSpin && m_instFadeoutSpin->value() != value)
                m_instFadeoutSpin->setValue(value);
            applyInstrumentEditorToTable();
        });
        connect(m_instWaveXSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
            if (m_instWaveXSlider && m_instWaveXSlider->value() != value)
                m_instWaveXSlider->setValue(value);
            applyInstrumentEditorToTable();
        });
        connect(m_instWaveXSlider, &QSlider::valueChanged, this, [this](int value) {
            if (m_instWaveXSpin && m_instWaveXSpin->value() != value)
                m_instWaveXSpin->setValue(value);
            applyInstrumentEditorToTable();
        });
        connect(m_instWaveYSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value) {
            if (m_instWaveYSlider && m_instWaveYSlider->value() != value)
                m_instWaveYSlider->setValue(value);
            applyInstrumentEditorToTable();
        });
        connect(m_instWaveYSlider, &QSlider::valueChanged, this, [this](int value) {
            if (m_instWaveYSpin && m_instWaveYSpin->value() != value)
                m_instWaveYSpin->setValue(value);
            applyInstrumentEditorToTable();
        });

        m_editorTabs->addTab(patternPage, tr("Pattern Editor"));
        m_editorTabs->addTab(instrumentsPage, tr("Instruments"));

        leftPanelLayout->addWidget(m_editorTabs, 1);

        // Right side
        QGroupBox* instrumentsBox = new QGroupBox(tr("Instruments"), rightPanel);
        instrumentsBox->setFixedWidth(425);
        instrumentsBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

        QVBoxLayout* instrumentsLayout = new QVBoxLayout(instrumentsBox);
        instrumentsLayout->setContentsMargins(8, 10, 8, 8);
        instrumentsLayout->setSpacing(6);

        m_instrumentsTable = new QTableWidget(16, 8, instrumentsBox);
        m_instrumentsTable->setObjectName("soundInstrumentsTable");
        m_instrumentsTable->verticalHeader()->hide();
        m_instrumentsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_instrumentsTable->setSelectionMode(QAbstractItemView::SingleSelection);
        m_instrumentsTable->setHorizontalHeaderLabels({ tr("#"), tr("Name"), tr("Type"), tr("Vol"), tr("Env"), tr("Fade"), tr("WX"), tr("WY") });
        m_instrumentsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_instrumentsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_instrumentsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        m_instrumentsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        m_instrumentsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        m_instrumentsTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        m_instrumentsTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
        m_instrumentsTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
        instrumentsLayout->addWidget(m_instrumentsTable, 1);

        connect(m_instrumentsTable, &QTableWidget::itemChanged,
                this, [this](QTableWidgetItem*) {
            if (!m_restoringSoundUndo)
                autoRebuildSoundOutput();
        });

        QHBoxLayout* instBtns = new QHBoxLayout();
        QPushButton* instAddBtn = new QPushButton(tr("Add"), instrumentsBox);
        QPushButton* instEditBtn = new QPushButton(tr("Edit"), instrumentsBox);
        QPushButton* instDeleteBtn = new QPushButton(tr("Delete"), instrumentsBox);
        QPushButton* instSaveBankBtn = new QPushButton(tr("Save Bank"), instrumentsBox);
        QPushButton* instLoadBankBtn = new QPushButton(tr("Load Bank"), instrumentsBox);
        QPushButton* instTestBtn = new QPushButton(tr("Test"), instrumentsBox);
        instBtns->addWidget(instAddBtn);
        instBtns->addWidget(instEditBtn);
        instBtns->addWidget(instDeleteBtn);
        instBtns->addWidget(instTestBtn);
        instBtns->addWidget(instSaveBankBtn);
        instBtns->addWidget(instLoadBankBtn);
        instrumentsLayout->addLayout(instBtns);

        connect(instAddBtn, &QPushButton::clicked, this, [this]() {
            addInstrumentPreset();
        });
        connect(instEditBtn, &QPushButton::clicked, this, [this]() {
            editSelectedInstrument();
        });
        connect(instDeleteBtn, &QPushButton::clicked, this, [this]() {
            clearSelectedInstrument();
        });
        connect(instSaveBankBtn, &QPushButton::clicked, this, [this]() {
            saveInstrumentBank();
        });
        connect(instLoadBankBtn, &QPushButton::clicked, this, [this]() {
            loadInstrumentBank();
        });
        connect(instTestBtn, &QPushButton::clicked, this, [this]() {
            previewSelectedInstrumentKey();
        });

        connect(m_instrumentsTable, &QTableWidget::cellClicked,
                this, [this](int row, int) {
            selectInstrumentFromTable(row);
        });

        connect(m_instrumentsTable, &QTableWidget::cellDoubleClicked,
                this, [this](int row, int) {
            selectInstrumentFromTable(row);
        });

        rightPanelLayout->addWidget(instrumentsBox, 1);

        QGroupBox* outputBox = new QGroupBox(tr("Output / CVBasic DATA"), rightPanel);
        outputBox->setFixedWidth(425);
        QVBoxLayout* outputLayout = new QVBoxLayout(outputBox);
        outputLayout->setContentsMargins(8, 10, 8, 8);
        outputLayout->setSpacing(6);

        QHBoxLayout* outputTop = new QHBoxLayout();
        outputTop->addStretch(1);
        m_autoUpdateCheck = new QCheckBox(tr("Auto Update"), outputBox);
        m_autoUpdateCheck->setChecked(true);

        m_addPlayerCheck = new QCheckBox(tr("Add Player"), outputBox);
        m_addPlayerCheck->setChecked(false);
        m_addPlayerCheck->setToolTip(tr("Voeg bij Insert Selected in Editor automatisch een CV player toe zodat de song speelt bij compile/run"));

        QPushButton* refreshBtn = new QPushButton(tr("Refresh"), outputBox);
        outputTop->addWidget(m_autoUpdateCheck);
        outputTop->addWidget(m_addPlayerCheck);
        outputTop->addWidget(refreshBtn);

        m_outputEdit = new QPlainTextEdit(outputBox);
        m_outputEdit->setReadOnly(true);

        outputLayout->addLayout(outputTop);
        outputLayout->addWidget(m_outputEdit, 1);

        rightPanelLayout->addWidget(outputBox, 1);

        soundMiddleLayout->addWidget(leftPanel, 1);
        soundMiddleLayout->addWidget(rightPanel, 0);

        bodyLayout->addWidget(soundMiddleRow, 1);
        root->addWidget(body, 1);

        connect(m_songNameEdit, &QLineEdit::textChanged, this, [this]() {
            if (!m_autoUpdateCheck || m_autoUpdateCheck->isChecked())
                rebuildOutput();
        });
        connect(m_authorEdit, &QLineEdit::textChanged, this, [this]() {
            if (!m_autoUpdateCheck || m_autoUpdateCheck->isChecked())
                rebuildOutput();
        });
        connect(m_tempoSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
            if (!m_autoUpdateCheck || m_autoUpdateCheck->isChecked())
                rebuildOutput();
        });
        connect(m_speedSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) {
            if (!m_autoUpdateCheck || m_autoUpdateCheck->isChecked())
                rebuildOutput();
        });
        connect(refreshBtn, &QPushButton::clicked, this, [this]() {
            rebuildOutput();
        });

        connect(m_addPlayerCheck, &QCheckBox::toggled, this, [this](bool enabled) {
            if (m_noteInfoLabel) {
                m_noteInfoLabel->setText(enabled
                    ? tr("Add Player ON: Insert Selected in Editor will include CV player")
                    : tr("Add Player OFF: Insert Selected in Editor inserts data/output only"));
            }
        });

        setStyleSheet(
            "QWidget#cvBasicSoundPage { background-color:#3A3A3A; color:#FFFFFF; }"
            "QGroupBox { border:1px solid #666666; margin-top:8px; }"
            "QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; }"
            "QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QTableWidget { background-color:#242424; color:#FFFFFF; border:1px solid #666666; }"
            "QCheckBox { background-color:#3A3A3A; color:#FFFFFF; }"
            "QPushButton { background-color:#242424; color:#FFFFFF; border:1px solid #666666; padding:4px 10px; }"
            "QPushButton:hover { background-color:#4A4A4A; }"
            "QPushButton:pressed { background-color:#5A5A5A; }"
            "QHeaderView::section { background-color:#3A3A3A; color:#FFFFFF; border:1px solid #555555; padding:3px; }"
            "QTableWidget { gridline-color:#555555; selection-background-color:#5864D8; selection-color:#FFFFFF; alternate-background-color:#2C2C2C; }"
            "QTabWidget::pane { border:1px solid #555555; background-color:#3A3A3A; top:-1px; }"
            "QTabBar::tab { background-color:#242424; color:#FFFFFF; border:1px solid #6A6A6A; border-bottom:none; padding:5px 12px; min-width:70px; margin-right:2px; }"
            "QTabBar::tab:selected { background-color:#3A3A3A; color:#FFFFFF; }"
            "QWidget#soundKeyboardImage { background-color:#242424; border:1px solid #666666; }"
        );
    }


    void createEmptySoundSong()
    {
        if (!m_orderTable || !m_patternTable || !m_instrumentsTable)
            return;

        m_loadingSoundPattern = true;

        resetDefaultInstrumentsTable();

        if (m_songNameEdit)
            m_songNameEdit->setText("Untitled");

        if (m_authorEdit)
            m_authorEdit->setText("CVBasic Dev");

        if (m_tempoSpin)
            m_tempoSpin->setValue(125);

        if (m_speedSpin)
            m_speedSpin->setValue(6);

        if (m_rowsSpin) {
            QSignalBlocker blocker(m_rowsSpin);
            m_rowsSpin->setValue(16);
        }

        if (m_defaultInstrumentSpin)
            m_defaultInstrumentSpin->setValue(1);


        if (m_octaveSpin)
            m_octaveSpin->setValue(4);

        if (m_volumeSpin)
            m_volumeSpin->setValue(15);

        if (m_activeChannelCombo)
            m_activeChannelCombo->setCurrentIndex(0);

        if (m_stepSpin)
            m_stepSpin->setValue(1);

        // Order List leeg: onmiddellijk FF op eerste positie.
        m_orderTable->setColumnCount(16);

        QStringList orderHeaders;
        for (int c = 0; c < 16; ++c)
            orderHeaders << QString("%1").arg(c, 2, 10, QLatin1Char('0'));
        m_orderTable->setHorizontalHeaderLabels(orderHeaders);

        m_orderTable->setRowCount(1);
        m_orderTable->setVerticalHeaderLabels({ "Pat" });

        for (int c = 0; c < 16; ++c) {
            QTableWidgetItem* patItem = new QTableWidgetItem("FF");
            patItem->setTextAlignment(Qt::AlignCenter);
            m_orderTable->setItem(0, c, patItem);
        }

        // Pattern 00 leeg maken.
        m_currentPatternIndex = 0;
        m_soundPatterns.clear();

        const int rows = 16;
        m_patternTable->setRowCount(rows);

        for (int r = 0; r < rows; ++r) {
            setPatternCell(r, 0, QString("%1").arg(r, 2, 16, QLatin1Char('0')).toUpper());

            for (int col = 1; col < m_patternTable->columnCount(); ++col)
                setPatternCell(r, col, defaultPatternValueForColumn(col));
        }

        m_soundPatterns[0] = tableToJson(m_patternTable);

        if (m_patternSpin) {
            QSignalBlocker blocker(m_patternSpin);
            m_patternSpin->setValue(0);
        }

        if (m_playPatternSpin)
            m_playPatternSpin->setValue(0);

        if (m_playFromSpin)
            m_playFromSpin->setValue(0);

        if (m_rowSpin) {
            m_rowSpin->setRange(0, rows - 1);
            m_rowSpin->setValue(0);
        }

        // Instrument list wel initialiseren, maar zonder demo-song/pattern.
        struct InstrumentRow {
            const char* id;
            const char* name;
            const char* type;
            const char* vol;
            const char* env;
            const char* fade;
            const char* wx;
            const char* wy;
        };

        const InstrumentRow instruments[] = {
            {"00","---","---","---","---","00","50","50"},
            {"01","Lead Melody","Tone","0F","06","02","55","72"},
            {"02","Soft Harmony","Tone","08","05","04","62","40"},
            {"03","Bass Tone","Tone","0A","03","02","50","28"},
            {"04","Soft Pad","Tone","0C","05","05","68","30"},
            {"05","Brass Stab","Tone","0F","04","03","42","76"},
            {"06","Bell","Tone","0E","08","06","30","84"},
            {"07","Perc Click","Noise","0F","09","01","35","90"},
            {"08","Snare Hit","Noise","0F","0A","04","55","76"},
            {"09","HiHat","Noise","0E","0B","02","75","95"},
            {"0A","Explosion","Noise","0F","0C","08","70","62"},
            {"0B","PowerUp","Noise","0F","0D","03","85","82"},
            {"0C","---","---","---","---","00","50","50"},
            {"0D","---","---","---","---","00","50","50"},
            {"0E","---","---","---","---","00","50","50"},
            {"0F","---","---","---","---","00","50","50"}
        };

        m_instrumentsTable->setRowCount(16);

        for (int r = 0; r < 16; ++r) {
            const QStringList values = {
                instruments[r].id,
                instruments[r].name,
                instruments[r].type,
                instruments[r].vol,
                instruments[r].env,
                instruments[r].fade,
                instruments[r].wx,
                instruments[r].wy
            };

            for (int c = 0; c < values.size(); ++c) {
                QTableWidgetItem* item = new QTableWidgetItem(values[c]);
                if (c != 1)
                    item->setTextAlignment(Qt::AlignCenter);
                m_instrumentsTable->setItem(r, c, item);
            }
        }

        m_instrumentsTable->selectRow(1);
        m_patternTable->selectRow(0);

        m_loadingSoundPattern = false;

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText("No song loaded. Use Open Song or Import MIDI.");

        setPlaybackStatusText("No song");
    }


    void populateDemoSong()
    {
        if (!m_orderTable || !m_patternTable || !m_instrumentsTable)
            return;

        const QStringList orderValues = {
            "00", "01", "02", "03", "04", "05", "06", "07",
            "04", "03", "02", "01", "FF", "FF", "FF", "FF"
        };

        m_orderTable->setRowCount(1);
        m_orderTable->setVerticalHeaderLabels({ "Pat" });

        for (int c = 0; c < 16; ++c) {
            QTableWidgetItem* item = new QTableWidgetItem(orderValues.value(c, "FF"));
            item->setTextAlignment(Qt::AlignCenter);
            m_orderTable->setItem(0, c, item);
        }

        struct PatternRow {
            QString ch1n, ch1i, ch1v, ch1f;
            QString ch2n, ch2i, ch2v, ch2f;
            QString ch3n, ch3i, ch3v, ch3f;
            QString nzn, nzi, nzv, nzf;
        };

        const QVector<PatternRow> demo = {
            {"C-4","01","V03","---","E-4","01","V03","---","G-4","01","V03","---","---","---","V03","---"},
            {"D-4","01","V03","---","F-4","01","V03","---","A-4","01","V03","---","---","---","V08","---"},
            {"E-4","01","V03","---","G-4","01","V03","---","B-4","01","V03","---","---","---","V08","---"},
            {"F-4","01","V03","---","A-4","01","V03","---","C-5","01","V03","---","---","---","V08","---"},
            {"G-4","01","V03","---","B-4","01","V03","---","D-5","01","V03","---","---","---","V08","---"},
            {"A-4","01","V03","---","C-5","01","V03","---","E-5","01","V03","---","---","---","V08","---"},
            {"B-4","01","V03","---","D-5","01","V03","---","F-5","01","V03","---","---","---","V08","---"},
            {"C-5","01","V03","---","E-5","01","V03","---","G-5","01","V03","---","---","---","V08","---"},
            {"---","---","---","---","---","---","---","---","---","---","---","---","---","---","V0A","S03"},
            {"---","---","---","---","---","---","---","---","---","---","---","---","---","---","V08","S02"},
            {"---","---","---","---","---","---","---","---","---","---","---","---","---","---","V08","S01"},
            {"C-4","01","V02","A03","---","---","---","---","---","---","---","---","---","---","V0A","---"},
            {"---","---","---","---","E-4","01","V02","A03","---","---","---","---","---","---","V08","---"},
            {"---","---","---","---","---","---","---","---","G-4","01","V02","A03","---","---","V08","---"},
            {"---","---","---","---","---","---","---","---","---","---","---","---","---","---","V08","---"},
            {"---","---","---","---","---","---","---","---","---","---","---","---","---","---","V08","---"}
        };

        for (int r = 0; r < demo.size(); ++r) {
            int c = 0;
            QTableWidgetItem* rowItem = new QTableWidgetItem(QString("%1").arg(r, 2, 16, QLatin1Char('0')).toUpper());
            rowItem->setTextAlignment(Qt::AlignCenter);
            m_patternTable->setItem(r, c++, rowItem);

            const PatternRow& d = demo[r];
            const QStringList values = {
                d.ch1n, d.ch1i, d.ch1v, d.ch1f,
                d.ch2n, d.ch2i, d.ch2v, d.ch2f,
                d.ch3n, d.ch3i, d.ch3v, d.ch3f,
                d.nzn, d.nzi, d.nzv, d.nzf
            };

            for (const QString& value : values) {
                QTableWidgetItem* item = new QTableWidgetItem(value);
                item->setTextAlignment(Qt::AlignCenter);
                m_patternTable->setItem(r, c++, item);
            }
        }

        m_patternTable->selectRow(6);

        struct InstrumentRow {
            const char* id;
            const char* name;
            const char* type;
            const char* vol;
            const char* env;
            const char* fade;
            const char* wx;
            const char* wy;
        };

        const InstrumentRow instruments[] = {
            {"00","---","---","---","---"},
            {"01","Square Bass","Tone","0F","03"},
            {"02","Lead Synth","Tone","0F","06"},
            {"03","Arp Pluck","Tone","0E","07"},
            {"04","Soft Pad","Tone","0C","05"},
            {"05","Brass Stab","Tone","0F","04"},
            {"06","Bell","Tone","0E","08"},
            {"07","Perc Click","Noise","0F","09"},
            {"08","Snare Hit","Noise","0F","0A"},
            {"09","HiHat","Noise","0E","0B"},
            {"0A","Explosion","Noise","0F","0C"},
            {"0B","PowerUp","Noise","0F","0D"},
            {"0C","---","---","---","---"},
            {"0D","---","---","---","---"},
            {"0E","---","---","---","---"},
            {"0F","---","---","---","---"}
        };

        for (int r = 0; r < 16; ++r) {
            const QStringList values = {
                instruments[r].id, instruments[r].name, instruments[r].type, instruments[r].vol, instruments[r].env
            };
            for (int c = 0; c < values.size(); ++c) {
                QTableWidgetItem* item = new QTableWidgetItem(values[c]);
                if (c != 1)
                    item->setTextAlignment(Qt::AlignCenter);
                m_instrumentsTable->setItem(r, c, item);
            }
        }

        m_instrumentsTable->selectRow(1);
    }




    void debugDumpSoundState(const QString& tag, int maxRows = 8) const
    {
        qDebug().noquote() << "\n========== [ADAMP SOUND DEBUG]" << tag << "==========";

        qDebug().noquote()
            << "[SONG]"
            << "name=" << (m_songNameEdit ? m_songNameEdit->text() : QString())
            << "tempo=" << (m_tempoSpin ? m_tempoSpin->value() : -1)
            << "speed=" << (m_speedSpin ? m_speedSpin->value() : -1)
            << "rowsSpin=" << (m_rowsSpin ? m_rowsSpin->value() : -1)
            << "defaultInstr=" << (m_defaultInstrumentSpin ? m_defaultInstrumentSpin->value() : -1)
            << "currentPattern=" << m_currentPatternIndex
            << "patternsInMemory=" << m_soundPatterns.size();

        if (m_orderTable) {
            QStringList order;
            for (int c = 0; c < m_orderTable->columnCount(); ++c) {
                QTableWidgetItem* item = m_orderTable->item(0, c);
                order << (item ? item->text() : QString("--"));
            }
            qDebug().noquote() << "[ORDER]" << order.join(" ");
        } else {
            qDebug().noquote() << "[ORDER] <null>";
        }

        if (m_instrumentsTable) {
            for (int r = 0; r < m_instrumentsTable->rowCount(); ++r) {
                QStringList cols;
                for (int c = 0; c < m_instrumentsTable->columnCount(); ++c) {
                    QTableWidgetItem* item = m_instrumentsTable->item(r, c);
                    cols << (item ? item->text() : QString());
                }
                qDebug().noquote() << QString("[INSTR %1]").arg(r, 2, 16, QLatin1Char('0')).toUpper()
                                   << cols.join(" | ");
            }
        } else {
            qDebug().noquote() << "[INSTR] <null>";
        }

        if (m_patternTable) {
            const int rows = qMin(maxRows, m_patternTable->rowCount());
            for (int r = 0; r < rows; ++r) {
                QStringList cols;
                for (int c = 0; c < m_patternTable->columnCount(); ++c) {
                    QTableWidgetItem* item = m_patternTable->item(r, c);
                    cols << (item ? item->text() : QString());
                }
                qDebug().noquote() << QString("[PATROW %1]").arg(r, 2, 16, QLatin1Char('0')).toUpper()
                                   << cols.join(" | ");
            }
        } else {
            qDebug().noquote() << "[PATTERN] <null>";
        }

        qDebug().noquote() << "========== [/ADAMP SOUND DEBUG]" << tag << "==========\n";
    }


    void hardResetLoadedSongState()
    {
        qDebug().noquote() << "[ADAMP SOUND] hardResetLoadedSongState BEGIN";

        // Atomische songwissel reset:
        // audio stoppen, UI-playback stoppen, stream-player resetten,
        // preview stoppen, patterns/current state wissen en instrumenten defaults.
        m_isPatternPlaying = false;
        m_streamPlayerUiFollow = false;

        if (m_playTimer)
            m_playTimer->stop();

        if (m_liveInstrumentRestartTimer)
            m_liveInstrumentRestartTimer->stop();

        if (onStreamStopRequested)
            onStreamStopRequested();

        if (onStopAllPreviewRequested)
            onStopAllPreviewRequested();

        for (int ch = 0; ch < 4; ++ch) {
            m_playbackHeldActive[ch] = false;
            m_playbackHeldPeriod[ch] = 0;
            m_playbackHeldVolume[ch] = 0;
        }

        m_soundPatterns.clear();
        m_currentPatternIndex = 0;
        m_playingOrderColumn = 0;
        m_playingPatternIndex = 0;
        m_playingRow = 0;

        if (m_vuLedBar)
            m_vuLedBar->setLevels(0, 0, 0, 0);

        for (int ch = 0; ch < 4; ++ch) {
            m_channelAudible[ch] = true;
            updateSoundVuHeader(ch);
        }
setPlaybackUiPlaying(false);
        setPlaybackStatusText("Stopped");

        resetDefaultInstrumentsTable();

        qDebug().noquote() << "[ADAMP SOUND] hardResetLoadedSongState END";
    }


    void resetDefaultInstrumentsTable()
    {
        if (!m_instrumentsTable)
            return;

        struct InstrumentRow {
            const char* id;
            const char* name;
            const char* type;
            const char* vol;
            const char* env;
            const char* fade;
            const char* wx;
            const char* wy;
        };

        const InstrumentRow instruments[] = {
            {"00","---","---","---","---","00","50","50"},
            {"01","Lead Melody","Tone","0F","06","02","55","72"},
            {"02","Soft Harmony","Tone","08","05","04","62","40"},
            {"03","Bass Tone","Tone","0A","03","02","50","28"},
            {"04","Soft Pad","Tone","0C","05","05","68","30"},
            {"05","Brass Stab","Tone","0F","04","03","42","76"},
            {"06","Bell","Tone","0E","08","06","30","84"},
            {"07","Perc Click","Noise","0F","09","01","35","90"},
            {"08","Snare Hit","Noise","0F","0A","04","55","76"},
            {"09","HiHat","Noise","0E","0B","02","75","95"},
            {"0A","Explosion","Noise","0F","0C","08","70","62"},
            {"0B","PowerUp","Noise","0F","0D","03","85","82"},
            {"0C","---","---","---","---","00","50","50"},
            {"0D","---","---","---","---","00","50","50"},
            {"0E","---","---","---","---","00","50","50"},
            {"0F","---","---","---","---","00","50","50"}
        };

        m_instrumentsTable->clearContents();
        m_instrumentsTable->setRowCount(16);

        for (int r = 0; r < 16; ++r) {
            const QStringList values = {
                instruments[r].id,
                instruments[r].name,
                instruments[r].type,
                instruments[r].vol,
                instruments[r].env,
                instruments[r].fade,
                instruments[r].wx,
                instruments[r].wy
            };

            for (int c = 0; c < values.size(); ++c) {
                QTableWidgetItem* item = new QTableWidgetItem(values[c]);
                if (c != 1)
                    item->setTextAlignment(Qt::AlignCenter);
                m_instrumentsTable->setItem(r, c, item);
            }
        }

        m_instrumentsTable->selectRow(1);
    }



    QString instrumentCellText(int row, int col, const QString& fallback = QString()) const
    {
        if (!m_instrumentsTable)
            return fallback;

        if (row < 0 || row >= m_instrumentsTable->rowCount() ||
            col < 0 || col >= m_instrumentsTable->columnCount())
            return fallback;

        QTableWidgetItem* item = m_instrumentsTable->item(row, col);
        const QString txt = item ? item->text().trimmed() : QString();
        return txt.isEmpty() ? fallback : txt;
    }

    int instrumentHexCell(int row, int col, int fallback, int minValue = 0, int maxValue = 15) const
    {
        const QString txt = instrumentCellText(row, col, QString("%1").arg(fallback, 2, 16, QLatin1Char('0'))).toUpper();
        bool ok = false;
        int v = txt.toInt(&ok, 16);
        if (!ok)
            v = fallback;

        return qBound(minValue, v, maxValue);
    }

    int instrumentDecCell(int row, int col, int fallback, int minValue = 0, int maxValue = 100) const
    {
        const QString txt = instrumentCellText(row, col, QString::number(fallback));
        bool ok = false;
        int v = txt.toInt(&ok, 10);
        if (!ok)
            v = fallback;

        return qBound(minValue, v, maxValue);
    }

    int instrumentFadeout(int instrumentId) const
    {
        if (!m_instrumentsTable)
            return 0;
        const int row = qBound(0, instrumentId, m_instrumentsTable->rowCount() - 1);
        return instrumentHexCell(row, 5, 0, 0, 15);
    }

    int instrumentWaveX(int instrumentId) const
    {
        if (!m_instrumentsTable)
            return 50;
        const int row = qBound(0, instrumentId, m_instrumentsTable->rowCount() - 1);
        return instrumentDecCell(row, 6, 50, 0, 100);
    }

    int instrumentWaveY(int instrumentId) const
    {
        if (!m_instrumentsTable)
            return 50;
        const int row = qBound(0, instrumentId, m_instrumentsTable->rowCount() - 1);
        return instrumentDecCell(row, 7, 50, 0, 100);
    }

    int instrumentTypeIndex(int instrumentId) const
    {
        if (!m_instrumentsTable)
            return 0;

        const int row = qBound(0, instrumentId, m_instrumentsTable->rowCount() - 1);
        const QString type = instrumentCellText(row, 2, "Tone").trimmed().toLower();
        return type == "noise" ? 1 : 0;
    }

    void updateInstrumentVisuals()
    {
        const int row = currentInstrumentRow();
        const int type = instrumentTypeIndex(row);
        const int vol = instrumentVolume(row);
        const int env = instrumentEnvelope(row);
        const int fade = instrumentFadeout(row);
        const int wx = instrumentWaveX(row);
        const int wy = instrumentWaveY(row);


        if (m_instWavePreview)
            m_instWavePreview->setInstrumentParams(type, env, wx, wy, vol);

        if (m_instEnvPreview)
            m_instEnvPreview->setEnvelopeParams(env, vol, fade);
    }

    void loadInstrumentIntoEditor(int row)
    {
        if (!m_instrumentsTable)
            return;

        row = qBound(0, row, m_instrumentsTable->rowCount() - 1);

        m_updatingInstrumentEditor = true;

        if (m_instNameEdit)
            m_instNameEdit->setText(instrumentCellText(row, 1, "---"));

        if (m_instTypeCombo) {
            const QString type = instrumentCellText(row, 2, "Tone");
            const int idx = m_instTypeCombo->findText(type, Qt::MatchFixedString);
            m_instTypeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        }

        if (m_instVolumeSpin)
            m_instVolumeSpin->setValue(instrumentVolume(row));

        if (m_instVolumeSlider)
            m_instVolumeSlider->setValue(instrumentVolume(row));

        if (m_instEnvSpin)
            m_instEnvSpin->setValue(instrumentEnvelope(row));

        if (m_instEnvSlider)
            m_instEnvSlider->setValue(instrumentEnvelope(row));

        if (m_instFadeoutSpin)
            m_instFadeoutSpin->setValue(instrumentFadeout(row));

        if (m_instFadeoutSlider)
            m_instFadeoutSlider->setValue(instrumentFadeout(row));

        if (m_instWaveXSpin)
            m_instWaveXSpin->setValue(instrumentWaveX(row));

        if (m_instWaveXSlider)
            m_instWaveXSlider->setValue(instrumentWaveX(row));

        if (m_instWaveYSpin)
            m_instWaveYSpin->setValue(instrumentWaveY(row));

        if (m_instWaveYSlider)
            m_instWaveYSlider->setValue(instrumentWaveY(row));

        m_updatingInstrumentEditor = false;
        updateInstrumentVisuals();
    }


    void scheduleLiveInstrumentPlaybackRefresh()
    {
        if (!m_isPatternPlaying)
            return;

        // Tijdens slepen van sliders/XY-pad komen veel valueChanged events binnen.
        // Niet bij elke pixel de audio herstarten; bundel dit kort.
        if (!m_liveInstrumentRestartTimer) {
            m_liveInstrumentRestartTimer = new QTimer(this);
            m_liveInstrumentRestartTimer->setSingleShot(true);
            m_liveInstrumentRestartTimer->setInterval(90);
            m_liveInstrumentRestartTimer->setTimerType(Qt::PreciseTimer);

            connect(m_liveInstrumentRestartTimer, &QTimer::timeout, this, [this]() {
                if (!m_isPatternPlaying)
                    return;

                saveCurrentPatternToMemory();

                if (m_noteInfoLabel)
                    m_noteInfoLabel->setText("Live instrument update");

                restartCurrentStreamPlayback();
            });
        }

        m_liveInstrumentRestartTimer->start();
    }


    void applyInstrumentEditorToTable()
    {
        if (m_updatingInstrumentEditor || !m_instrumentsTable)
            return;

        const int row = currentInstrumentRow();
        if (row <= 0 || row >= m_instrumentsTable->rowCount())
            return;

        setInstrumentCell(row, 0, QString("%1").arg(row, 2, 16, QLatin1Char('0')).toUpper());

        if (m_instNameEdit)
            setInstrumentCell(row, 1, m_instNameEdit->text().trimmed().isEmpty() ? QString("---") : m_instNameEdit->text().trimmed());

        if (m_instTypeCombo)
            setInstrumentCell(row, 2, m_instTypeCombo->currentText());

        if (m_instVolumeSpin)
            setInstrumentCell(row, 3, QString("%1").arg(m_instVolumeSpin->value(), 2, 16, QLatin1Char('0')).toUpper());

        if (m_instEnvSpin)
            setInstrumentCell(row, 4, QString("%1").arg(m_instEnvSpin->value(), 2, 16, QLatin1Char('0')).toUpper());

        if (m_instFadeoutSpin)
            setInstrumentCell(row, 5, QString("%1").arg(m_instFadeoutSpin->value(), 2, 16, QLatin1Char('0')).toUpper());

        if (m_instWaveXSpin)
            setInstrumentCell(row, 6, QString::number(m_instWaveXSpin->value()));

        if (m_instWaveYSpin)
            setInstrumentCell(row, 7, QString::number(m_instWaveYSpin->value()));

        updateInstrumentVisuals();
        autoRebuildSoundOutput();
        scheduleLiveInstrumentPlaybackRefresh();
    }


    int instrumentVolume(int instrumentId) const
    {
        if (!m_instrumentsTable)
            return 15;

        const int row = qBound(0, instrumentId, m_instrumentsTable->rowCount() - 1);
        return instrumentHexCell(row, 3, 15, 0, 15);
    }

    int instrumentEnvelope(int instrumentId) const
    {
        if (!m_instrumentsTable)
            return 3;

        const int row = qBound(0, instrumentId, m_instrumentsTable->rowCount() - 1);
        return instrumentHexCell(row, 4, 3, 0, 15);
    }

    void setInstrumentCell(int row, int col, const QString& text)
    {
        if (!m_instrumentsTable)
            return;

        if (row < 0 || row >= m_instrumentsTable->rowCount() || col < 0 || col >= m_instrumentsTable->columnCount())
            return;

        QTableWidgetItem* item = m_instrumentsTable->item(row, col);
        if (!item) {
            item = new QTableWidgetItem();
            m_instrumentsTable->setItem(row, col, item);
        }

        item->setText(text);

        if (col != 1)
            item->setTextAlignment(Qt::AlignCenter);
    }

    int currentInstrumentRow() const
    {
        if (!m_instrumentsTable)
            return 0;

        int row = m_instrumentsTable->currentRow();

        if (row < 0 && m_defaultInstrumentSpin)
            row = m_defaultInstrumentSpin->value();

        return qBound(0, row, m_instrumentsTable->rowCount() - 1);
    }

    void editInstrumentRow(int row)
    {
        if (!m_instrumentsTable)
            return;

        pushSoundUndoState();

        row = qBound(0, row, m_instrumentsTable->rowCount() - 1);

        QDialog dlg(this);
        dlg.setWindowTitle(tr("Edit Instrument %1")
                               .arg(row, 2, 16, QLatin1Char('0')).toUpper());

        QVBoxLayout* root = new QVBoxLayout(&dlg);
        QGridLayout* grid = new QGridLayout();

        QLineEdit* nameEdit = new QLineEdit(&dlg);
        QComboBox* typeCombo = new QComboBox(&dlg);
        QSpinBox* volSpin = new QSpinBox(&dlg);
        QSpinBox* envSpin = new QSpinBox(&dlg);

        typeCombo->addItem("Tone");
        typeCombo->addItem("Noise");
        typeCombo->addItem("---");

        volSpin->setRange(0, 15);
        volSpin->setDisplayIntegerBase(16);
        volSpin->setPrefix("0");

        envSpin->setRange(0, 15);
        envSpin->setDisplayIntegerBase(16);
        envSpin->setPrefix("0");

        const QString curName = m_instrumentsTable->item(row, 1) ? m_instrumentsTable->item(row, 1)->text() : QString("---");
        const QString curType = m_instrumentsTable->item(row, 2) ? m_instrumentsTable->item(row, 2)->text() : QString("Tone");
        const QString curVol  = m_instrumentsTable->item(row, 3) ? m_instrumentsTable->item(row, 3)->text() : QString("0F");
        const QString curEnv  = m_instrumentsTable->item(row, 4) ? m_instrumentsTable->item(row, 4)->text() : QString("03");

        nameEdit->setText(curName);

        const int typeIndex = typeCombo->findText(curType, Qt::MatchFixedString);
        typeCombo->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);

        bool okVol = false;
        bool okEnv = false;
        volSpin->setValue(qBound(0, curVol.toInt(&okVol, 16), 15));
        envSpin->setValue(qBound(0, curEnv.toInt(&okEnv, 16), 15));

        grid->addWidget(new QLabel(tr("Name:"), &dlg), 0, 0);
        grid->addWidget(nameEdit, 0, 1);
        grid->addWidget(new QLabel(tr("Type:"), &dlg), 1, 0);
        grid->addWidget(typeCombo, 1, 1);
        grid->addWidget(new QLabel(tr("Vol:"), &dlg), 2, 0);
        grid->addWidget(volSpin, 2, 1);
        grid->addWidget(new QLabel(tr("Env:"), &dlg), 3, 0);
        grid->addWidget(envSpin, 3, 1);

        QLabel* help = new QLabel(
            tr("Env examples: 03 Bass, 05 Pad, 06 Lead vibrato, 07 Arp, 08 Bell, 0D PowerUp"),
            &dlg
        );
        help->setWordWrap(true);

        QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

        root->addLayout(grid);
        root->addWidget(help);
        root->addWidget(buttons);

        if (dlg.exec() != QDialog::Accepted)
            return;

        setInstrumentCell(row, 0, QString("%1").arg(row, 2, 16, QLatin1Char('0')).toUpper());
        setInstrumentCell(row, 1, nameEdit->text().trimmed().isEmpty() ? QString("---") : nameEdit->text().trimmed());
        setInstrumentCell(row, 2, typeCombo->currentText());
        setInstrumentCell(row, 3, QString("%1").arg(volSpin->value(), 2, 16, QLatin1Char('0')).toUpper());
        setInstrumentCell(row, 4, QString("%1").arg(envSpin->value(), 2, 16, QLatin1Char('0')).toUpper());

        selectInstrumentFromTable(row);

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Edited instrument %1 - %2")
                                         .arg(row, 2, 16, QLatin1Char('0')).toUpper()
                                         .arg(nameEdit->text().trimmed()));
    }

    void editSelectedInstrument()
    {
        editInstrumentRow(currentInstrumentRow());
    }

    void addInstrumentPreset()
    {
        pushSoundUndoState();
        if (!m_instrumentsTable)
            return;

        int row = currentInstrumentRow();

        // Zoek een lege instrument-slot vanaf 01.
        for (int r = 1; r < m_instrumentsTable->rowCount(); ++r) {
            const QString name = m_instrumentsTable->item(r, 1) ? m_instrumentsTable->item(r, 1)->text().trimmed() : QString();
            if (name.isEmpty() || name == "---") {
                row = r;
                break;
            }
        }

        setInstrumentCell(row, 0, QString("%1").arg(row, 2, 16, QLatin1Char('0')).toUpper());
        setInstrumentCell(row, 1, tr("New Instrument"));
        setInstrumentCell(row, 2, tr("Tone"));
        setInstrumentCell(row, 3, "0F");
        setInstrumentCell(row, 4, "03");
        setInstrumentCell(row, 5, "02");
        setInstrumentCell(row, 6, "50");
        setInstrumentCell(row, 7, "50");

        selectInstrumentFromTable(row);
        editInstrumentRow(row);
    }

    void clearSelectedInstrument()
    {
        pushSoundUndoState();
        const int row = currentInstrumentRow();

        if (row == 0) {
            if (m_noteInfoLabel)
                m_noteInfoLabel->setText("Instrument 00 cannot be cleared");
            return;
        }

        setInstrumentCell(row, 0, QString("%1").arg(row, 2, 16, QLatin1Char('0')).toUpper());
        setInstrumentCell(row, 1, "---");
        setInstrumentCell(row, 2, "---");
        setInstrumentCell(row, 3, "---");
        setInstrumentCell(row, 4, "---");
        setInstrumentCell(row, 5, "00");
        setInstrumentCell(row, 6, "50");
        setInstrumentCell(row, 7, "50");

        loadInstrumentIntoEditor(row);

        if (m_defaultInstrumentSpin && m_defaultInstrumentSpin->value() == row)
            m_defaultInstrumentSpin->setValue(1);

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Cleared instrument %1")
                                         .arg(row, 2, 16, QLatin1Char('0')).toUpper());
    }


    void normalizeInstrumentTable()
    {
        if (!m_instrumentsTable)
            return;

        if (m_instrumentsTable->columnCount() < 8)
            m_instrumentsTable->setColumnCount(8);

        for (int r = 0; r < m_instrumentsTable->rowCount(); ++r) {
            if (!m_instrumentsTable->item(r, 0))
                setInstrumentCell(r, 0, QString("%1").arg(r, 2, 16, QLatin1Char('0')).toUpper());

            if (!m_instrumentsTable->item(r, 5) || m_instrumentsTable->item(r, 5)->text().trimmed().isEmpty())
                setInstrumentCell(r, 5, "00");

            if (!m_instrumentsTable->item(r, 6) || m_instrumentsTable->item(r, 6)->text().trimmed().isEmpty())
                setInstrumentCell(r, 6, "50");

            if (!m_instrumentsTable->item(r, 7) || m_instrumentsTable->item(r, 7)->text().trimmed().isEmpty())
                setInstrumentCell(r, 7, "50");
        }
    }


    QString instrumentName(int instrumentId) const
    {
        if (!m_instrumentsTable)
            return QString();

        const int row = qBound(0, instrumentId, m_instrumentsTable->rowCount() - 1);
        QTableWidgetItem* item = m_instrumentsTable->item(row, 1);
        return item ? item->text().trimmed() : QString();
    }

    void selectInstrumentFromTable(int row)
    {
        if (!m_instrumentsTable || !m_defaultInstrumentSpin)
            return;

        row = qBound(0, row, m_instrumentsTable->rowCount() - 1);

        QTableWidgetItem* idItem = m_instrumentsTable->item(row, 0);

        bool ok = false;
        int instrumentId = idItem ? idItem->text().toInt(&ok, 16) : row;
        if (!ok)
            instrumentId = row;

        instrumentId = qBound(0, instrumentId, 15);

        m_defaultInstrumentSpin->setValue(instrumentId);

        if (m_volumeSpin)
            m_volumeSpin->setValue(instrumentVolume(instrumentId));

        m_instrumentsTable->selectRow(row);
        loadInstrumentIntoEditor(row);

        if (m_noteInfoLabel) {
            const QString name = instrumentName(instrumentId);
            m_noteInfoLabel->setText(QString("Selected instrument %1%2")
                                         .arg(instrumentId, 2, 16, QLatin1Char('0')).toUpper()
                                         .arg(name.isEmpty() ? QString() : QString(" - %1").arg(name)));
        }
    }

    int activeSoundChannel() const
    {
        if (!m_activeChannelCombo)
            return 0;

        return qBound(0, m_activeChannelCombo->currentData().toInt(), 3);
    }

    QString activeSoundChannelName() const
    {
        switch (activeSoundChannel()) {
        case 0: return "CH1";
        case 1: return "CH2";
        case 2: return "CH3";
        default: return "NOISE";
        }
    }

    int activeChannelBaseColumn() const
    {
        // Pattern table layout:
        // 0 = ROW
        // CH1   = columns 1..4
        // CH2   = columns 5..8
        // CH3   = columns 9..12
        // NOISE = columns 13..16
        return 1 + activeSoundChannel() * 4;
    }

    int currentPatternRow() const
    {
        if (!m_patternTable)
            return 0;

        int row = m_patternTable->currentRow();
        if (row < 0 && m_rowSpin)
            row = m_rowSpin->value();

        return qBound(0, row, m_patternTable->rowCount() - 1);
    }

    QString noteFromKeyboardKey(const QString& keyName, int* semitoneOut = nullptr) const
    {
        static const QStringList notes = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };

        QString key = keyName.trimmed().toUpper();

        int semitone = -1;

        // Bottom row sends direct note names: C, C#, D, ...
        for (int i = 0; i < notes.size(); ++i) {
            if (key == notes[i]) {
                semitone = i;
                break;
            }
        }

        // Top row still sends "Top 1".."Top 20".
        // Map these to chromatic notes, continuing into the next octave.
        if (semitone < 0 && key.startsWith("TOP ")) {
            bool ok = false;
            const int topIndex = key.mid(4).toInt(&ok) - 1;
            if (ok && topIndex >= 0) {
                semitone = topIndex % 12;
                if (m_octaveSpin)
                    m_octaveSpin->setValue(qBound(1, m_octaveSpin->value() + (topIndex / 12), 8));
            }
        }

        if (semitone < 0)
            semitone = 0;

        if (semitoneOut)
            *semitoneOut = semitone;

        const int octave = m_octaveSpin ? m_octaveSpin->value() : 4;
        return QString("%1-%2").arg(notes[semitone].leftJustified(2, '-')).arg(octave);
    }

    double noteFrequency(int semitone, int octave) const
    {
        // MIDI: C4 = 60, A4 = 69 = 440 Hz.
        const int midi = (octave + 1) * 12 + semitone;
        return 440.0 * qPow(2.0, (midi - 69) / 12.0);
    }

    int psgPeriodFromFrequency(double frequency) const
    {
        if (frequency <= 0.0)
            return 0;

        // SN76489/TMS9919 tone period:
        // period = clock / 32 / frequency
        // Coleco clock approx. 3.579545 MHz.
        return qBound(1, qRound(3579545.0 / 32.0 / frequency), 1023);
    }

    void setPatternCell(int row, int col, const QString& text)
    {
        if (!m_patternTable)
            return;

        if (row < 0 || row >= m_patternTable->rowCount() || col < 0 || col >= m_patternTable->columnCount())
            return;

        QTableWidgetItem* item = m_patternTable->item(row, col);
        if (!item) {
            item = new QTableWidgetItem();
            item->setTextAlignment(Qt::AlignCenter);
            m_patternTable->setItem(row, col, item);
        }

        item->setText(text);
    }

    void copyCurrentPattern()
    {
        if (!m_patternTable)
            return;

        saveCurrentPatternToMemory();
        m_patternClipboardJson = tableToJson(m_patternTable);

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Copied Pattern %1")
                                         .arg(m_currentPatternIndex, 2, 16, QLatin1Char('0')).toUpper());
    }

    void pastePatternToCurrent()
    {
        if (!m_patternTable)
            return;

        if (m_patternClipboardJson.isEmpty()) {
            if (m_noteInfoLabel)
                m_noteInfoLabel->setText("Paste Pattern failed: clipboard empty");
            return;
        }

        pushSoundUndoState();

        m_loadingSoundPattern = true;
        jsonToTable(m_patternTable, m_patternClipboardJson);
        renumberPatternRows();
        m_loadingSoundPattern = false;

        saveCurrentPatternToMemory();
        autoRebuildSoundOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Pasted to Pattern %1")
                                         .arg(m_currentPatternIndex, 2, 16, QLatin1Char('0')).toUpper());
    }

    void clearCurrentPattern()
    {
        if (!m_patternTable)
            return;

        pushSoundUndoState();

        for (int r = 0; r < m_patternTable->rowCount(); ++r) {
            setPatternCell(r, 0, QString("%1").arg(r, 2, 16, QLatin1Char('0')).toUpper());
            clearPatternRow(r);
        }

        saveCurrentPatternToMemory();
        autoRebuildSoundOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Cleared Pattern %1")
                                         .arg(m_currentPatternIndex, 2, 16, QLatin1Char('0')).toUpper());
    }

    void duplicateCurrentPatternToNext()
    {
        if (!m_patternTable || !m_patternSpin)
            return;

        pushSoundUndoState();

        saveCurrentPatternToMemory();

        const int sourcePattern = m_currentPatternIndex;
        const int targetPattern = qBound(0, sourcePattern + 1, 255);

        m_soundPatterns[targetPattern] = tableToJson(m_patternTable);

        m_patternSpin->setValue(targetPattern);

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Duplicated Pattern %1 → %2")
                                         .arg(sourcePattern, 2, 16, QLatin1Char('0')).toUpper()
                                         .arg(targetPattern, 2, 16, QLatin1Char('0')).toUpper());
    }

    QString defaultPatternValueForColumn(int col) const
    {
        if (col <= 0)
            return QString();

        const int sub = (col - 1) % 4;

        switch (sub) {
        case 0: return "---"; // Note
        case 1: return "--";  // Inst
        case 2: return "---"; // Vol
        default: return "---"; // Fx
        }
    }

    void clearPatternRow(int row)
    {
        if (!m_patternTable)
            return;

        if (row < 0 || row >= m_patternTable->rowCount())
            return;

        for (int col = 1; col < m_patternTable->columnCount(); ++col)
            setPatternCell(row, col, defaultPatternValueForColumn(col));
    }

    void renumberPatternRows()
    {
        if (!m_patternTable)
            return;

        for (int r = 0; r < m_patternTable->rowCount(); ++r)
            setPatternCell(r, 0, QString("%1").arg(r, 2, 16, QLatin1Char('0')).toUpper());
    }

    QStringList emptyPatternRowValues() const
    {
        QStringList values;

        if (!m_patternTable)
            return values;

        for (int col = 1; col < m_patternTable->columnCount(); ++col)
            values << defaultPatternValueForColumn(col);

        return values;
    }

    void insertEmptyPatternRow()
    {
        pushSoundUndoState();
        if (!m_patternTable)
            return;

        const int row = currentPatternRow();
        m_patternTable->insertRow(row);

        writePatternRow(row, emptyPatternRowValues());
        renumberPatternRows();

        if (m_rowsSpin)
            m_rowsSpin->setValue(m_patternTable->rowCount());

        if (m_rowSpin) {
            m_rowSpin->setRange(0, qMax(0, m_patternTable->rowCount() - 1));
            m_rowSpin->setValue(row);
        }

        m_patternTable->selectRow(row);

        if (m_autoUpdateCheck && m_autoUpdateCheck->isChecked())
            rebuildOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Inserted empty row at %1")
                                         .arg(row, 2, 16, QLatin1Char('0')).toUpper());
    }

    void deleteCurrentPatternRow()
    {
        pushSoundUndoState();
        if (!m_patternTable)
            return;

        if (m_patternTable->rowCount() <= 1) {
            clearCurrentPatternRow();
            return;
        }

        const int row = currentPatternRow();
        m_patternTable->removeRow(row);
        renumberPatternRows();

        const int newRow = qBound(0, row, m_patternTable->rowCount() - 1);

        if (m_rowsSpin)
            m_rowsSpin->setValue(m_patternTable->rowCount());

        if (m_rowSpin) {
            m_rowSpin->setRange(0, qMax(0, m_patternTable->rowCount() - 1));
            m_rowSpin->setValue(newRow);
        }

        m_patternTable->selectRow(newRow);

        if (m_autoUpdateCheck && m_autoUpdateCheck->isChecked())
            rebuildOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Deleted row %1")
                                         .arg(row, 2, 16, QLatin1Char('0')).toUpper());
    }

    QStringList readPatternRow(int row) const
    {
        QStringList values;

        if (!m_patternTable)
            return values;

        if (row < 0 || row >= m_patternTable->rowCount())
            return values;

        for (int col = 1; col < m_patternTable->columnCount(); ++col) {
            QTableWidgetItem* it = m_patternTable->item(row, col);
            values << (it ? it->text() : defaultPatternValueForColumn(col));
        }

        return values;
    }

    void writePatternRow(int row, const QStringList& values)
    {
        if (!m_patternTable)
            return;

        if (row < 0 || row >= m_patternTable->rowCount())
            return;

        for (int col = 1; col < m_patternTable->columnCount(); ++col) {
            const int idx = col - 1;
            const QString value = (idx < values.size())
                                      ? values[idx]
                                      : defaultPatternValueForColumn(col);
            setPatternCell(row, col, value);
        }
    }

    void copyCurrentPatternRow()
    {
        const int row = currentPatternRow();
        m_patternClipboard = readPatternRow(row);

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Copied row %1")
                                         .arg(row, 2, 16, QLatin1Char('0')).toUpper());
    }

    void cutCurrentPatternRow()
    {
        pushSoundUndoState();
        const int row = currentPatternRow();
        m_patternClipboard = readPatternRow(row);
        clearPatternRow(row);

        if (m_autoUpdateCheck && m_autoUpdateCheck->isChecked())
            rebuildOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Cut row %1")
                                         .arg(row, 2, 16, QLatin1Char('0')).toUpper());
    }

    void pastePatternRow()
    {
        pushSoundUndoState();
        if (m_patternClipboard.isEmpty()) {
            if (m_noteInfoLabel)
                m_noteInfoLabel->setText("Paste failed: clipboard empty");
            return;
        }

        const int row = currentPatternRow();
        writePatternRow(row, m_patternClipboard);

        if (m_autoUpdateCheck && m_autoUpdateCheck->isChecked())
            rebuildOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Pasted to row %1")
                                         .arg(row, 2, 16, QLatin1Char('0')).toUpper());
    }

    void clearCurrentPatternRow()
    {
        pushSoundUndoState();
        const int row = currentPatternRow();
        clearPatternRow(row);

        if (m_autoUpdateCheck && m_autoUpdateCheck->isChecked())
            rebuildOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Cleared row %1")
                                         .arg(row, 2, 16, QLatin1Char('0')).toUpper());
    }

    QString sn76489ToneWriteText(int channel, int period, int volume) const
    {
        channel = qBound(0, channel, 2);
        period = qBound(1, period, 1023);
        volume = qBound(0, volume, 15);

        const int toneRegister = channel * 2;
        const int volumeRegister = toneRegister + 1;
        const int attenuation = 15 - volume; // CV volume: 15=loud, SN attenuation: 0=loud

        const int toneLatch = 0x80 | (toneRegister << 4) | (period & 0x0F);
        const int toneData  = (period >> 4) & 0x3F;
        const int volLatch  = 0x80 | (volumeRegister << 4) | attenuation;

        return QString("SN: $%1,$%2,$%3")
            .arg(toneLatch, 2, 16, QLatin1Char('0')).toUpper()
            .arg(toneData, 2, 16, QLatin1Char('0')).toUpper()
            .arg(volLatch, 2, 16, QLatin1Char('0')).toUpper();
    }

    QString sn76489NoiseWriteText(int noiseCode, int volume) const
    {
        noiseCode = qBound(0, noiseCode, 7);
        volume = qBound(0, volume, 15);

        const int attenuation = 15 - volume;
        const int noiseLatch = 0x80 | (6 << 4) | (noiseCode & 0x07);
        const int volLatch = 0x80 | (7 << 4) | attenuation;

        return QString("SN: $%1,$%2")
            .arg(noiseLatch, 2, 16, QLatin1Char('0')).toUpper()
            .arg(volLatch, 2, 16, QLatin1Char('0')).toUpper();
    }

    int previewDurationMs() const
    {
        // Voorlopig vaste preview-lengte per muisklik.
        // Later kan dit gekoppeld worden aan tempo/speed of toets ingedrukt houden.
        return 320;
    }

    int effectivePreviewVolumeForInstrument(int rowVolume, int instrumentId) const
    {
        rowVolume = qBound(0, rowVolume, 15);
        instrumentId = qBound(0, instrumentId, 15);

        const int instVolume = instrumentVolume(instrumentId);
        const double instrFactor = 0.75 + (static_cast<double>(instVolume) / 60.0);

        int effectiveVolume = qRound(rowVolume * instrFactor);

        // Zelfde gedrag als de song-stream: hoge expliciete Vxx niet onnodig stiller maken.
        if (rowVolume >= 12)
            effectiveVolume = qMax(effectiveVolume, rowVolume);

        return qBound(0, effectiveVolume, 15);
    }

    void requestPreviewTone(int channel, int psgPeriod, int volume)
    {
        channel = qBound(0, channel, 3);
        psgPeriod = qBound(0, psgPeriod, 1023);
        volume = qBound(0, volume, 15);

        if (!m_channelAudible[channel])
            volume = 0;

        const int inst = m_defaultInstrumentSpin ? m_defaultInstrumentSpin->value() : 1;
        const int env = instrumentEnvelope(inst);
        const int wx = instrumentWaveX(inst);
        const int wy = instrumentWaveY(inst);
        const int effectiveVolume = (psgPeriod > 0) ? effectivePreviewVolumeForInstrument(volume, inst) : 0;

        // Direct visuele feedback in de Sound Editor zelf.
        // De SoundManager stuurt later ook nog decay-levels terug via de bridge.
        setSoundChannelVuLevel(channel, (effectiveVolume > 0 && psgPeriod > 0) ? effectiveVolume : 0);

        if (onPreviewNoteRequested)
            onPreviewNoteRequested(channel, psgPeriod, effectiveVolume, env, wx, wy);
    }


    void previewSelectedInstrumentKey()
    {
        if (!m_instrumentsTable)
            return;

        const int row = currentInstrumentRow();
        if (row <= 0 || row >= m_instrumentsTable->rowCount())
            return;

        // Zorg dat requestPreviewTone exact het geselecteerde instrument gebruikt.
        if (m_defaultInstrumentSpin && m_defaultInstrumentSpin->value() != row)
            m_defaultInstrumentSpin->setValue(row);

        const int type = instrumentTypeIndex(row);
        const int vol = instrumentVolume(row);

        int channel = 0;
        int psg = 0;
        QString label;

        if (type == 1) {
            // Noise instrument: gebruik een geldige noise-code 1..7.
            channel = 3;
            psg = qBound(1, 1 + (instrumentWaveX(row) / 17), 7);
            label = QString("NOISE N%1").arg(psg, 2, 16, QLatin1Char('0')).toUpper();
        } else {
            // Tone instrument: vaste C-4 testnoot.
            channel = 0;
            const double freq = noteFrequency(0, 4); // C-4
            psg = psgPeriodFromFrequency(freq);
            label = QString("C-4");
        }

        requestPreviewTone(channel, psg, vol);

        const int previewChannel = channel;
        QTimer::singleShot(previewDurationMs(), this, [this, previewChannel]() {
            stopPreviewChannel(previewChannel);
        });

        if (m_noteInfoLabel) {
            m_noteInfoLabel->setText(QString("Instrument test: %1  Inst:%2  Vol:%3  Env:%4  WX:%5 WY:%6")
                                         .arg(label)
                                         .arg(row, 2, 16, QLatin1Char('0')).toUpper()
                                         .arg(vol, 2, 16, QLatin1Char('0')).toUpper()
                                         .arg(instrumentEnvelope(row), 2, 16, QLatin1Char('0')).toUpper()
                                         .arg(instrumentWaveX(row))
                                         .arg(instrumentWaveY(row)));
        }
    }

    void stopInstrumentTestKey()
    {
        if (onStopAllPreviewRequested)
            onStopAllPreviewRequested();

        setSoundPreviewVuLevels(0, 0, 0, 0);

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText("Instrument test stopped");
    }


    void stopPreviewChannel(int channel)
    {
        channel = qBound(0, channel, 3);

        // volume 0 = stil vanuit onze editor-logica.
        // De audio-koppeling kan dit later vertalen naar SN76489 attenuation 15.
        requestPreviewTone(channel, 0, 0);
    }

    void stopActivePreviewChannel()
    {
        const int ch = activeSoundChannel();
        stopAndClearPreviewChannel(ch);

        // Manual keyboard note-off must hard-clear the preview buffer.
        // Otherwise DirectSound can keep looping the last waveform and you hear tremolo.
        if (onStopAllPreviewRequested)
            onStopAllPreviewRequested();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Active: %1   Note Off").arg(activeSoundChannelName()));
    }

    void insertKeyboardKeyIntoPattern(const QString& keyName)
    {
        if (!m_patternTable)
            return;

        if (!(m_keyboardTestOnlyCheck && m_keyboardTestOnlyCheck->isChecked()))
            pushSoundUndoState();

        int semitone = 0;
        const QString noteName = noteFromKeyboardKey(keyName, &semitone);
        const int octave = m_octaveSpin ? m_octaveSpin->value() : 4;
        const double freq = noteFrequency(semitone, octave);
        const int psg = psgPeriodFromFrequency(freq);

        const int row = currentPatternRow();
        const int ch = activeSoundChannel();
        const int baseCol = activeChannelBaseColumn();
        const int inst = m_defaultInstrumentSpin ? m_defaultInstrumentSpin->value() : 1;
        const QString instName = instrumentName(inst);
        const int vol = m_volumeSpin ? m_volumeSpin->value() : 15;

        QString noteText = noteName;
        QString fxText = "---";

        if (ch == 3) {
            // Noise kanaal gebruikt geen gewone toonhoogte.
            // Voorlopig zetten we een noise-code op basis van de aangeklikte toets.
            noteText = QString("N%1").arg(semitone & 0x07, 2, 16, QLatin1Char('0')).toUpper();
            fxText = QString("S%1").arg(semitone & 0x07, 2, 16, QLatin1Char('0')).toUpper();
        }

        const bool testOnly = (m_keyboardTestOnlyCheck && m_keyboardTestOnlyCheck->isChecked());

        if (testOnly) {
            requestPreviewTone(ch, psg, vol);

            const int previewChannel = ch;
            QTimer::singleShot(previewDurationMs(), this, [this, previewChannel]() {
                stopPreviewChannel(previewChannel);
            });

            if (m_noteInfoLabel) {
                if (ch == 3) {
                    m_noteInfoLabel->setText(QString("TEST ONLY: %1   Noise: %2   Inst: %3%4   Vol: V%5")
                                                 .arg(activeSoundChannelName())
                                                 .arg(noteText)
                                                 .arg(inst, 2, 16, QLatin1Char('0')).toUpper()
                                                 .arg(instName.isEmpty() ? QString() : QString(" %1").arg(instName))
                                                 .arg(vol, 2, 16, QLatin1Char('0')).toUpper());
                } else {
                    m_noteInfoLabel->setText(QString("TEST ONLY: %1   Note: %2   Inst: %3%4   PSG: %5   Hz: %6")
                                                 .arg(activeSoundChannelName())
                                                 .arg(noteText)
                                                 .arg(inst, 2, 16, QLatin1Char('0')).toUpper()
                                                 .arg(instName.isEmpty() ? QString() : QString(" %1").arg(instName))
                                                 .arg(instrumentEnvelope(inst), 2, 16, QLatin1Char('0')).toUpper()
                                                 .arg(psg)
                                                 .arg(freq, 0, 'f', 2));
                }
            }

            return;
        }

        setPatternCell(row, baseCol + 0, noteText);
        setPatternCell(row, baseCol + 1, QString("%1").arg(inst, 2, 16, QLatin1Char('0')).toUpper());
        setPatternCell(row, baseCol + 2, QString("V%1").arg(vol, 2, 16, QLatin1Char('0')).toUpper());
        setPatternCell(row, baseCol + 3, fxText);

        m_patternTable->selectRow(row);

        if (m_noteInfoLabel) {
            if (ch == 3) {
                const QString snText = sn76489NoiseWriteText(semitone & 0x07, vol);
                m_noteInfoLabel->setText(QString("Active: %1   Noise: %2   Inst: %3%4   Vol: V%5   %6")
                                             .arg(activeSoundChannelName())
                                             .arg(noteText)
                                             .arg(inst, 2, 16, QLatin1Char('0')).toUpper()
                                             .arg(instName.isEmpty() ? QString() : QString(" %1").arg(instName))
                                             .arg(vol, 2, 16, QLatin1Char('0')).toUpper()
                                             .arg(snText));
            } else {
                const QString snText = sn76489ToneWriteText(ch, psg, vol);
                m_noteInfoLabel->setText(QString("Active: %1   Note: %2   Inst: %3%4   Env:%5   PSG: %6   Hz: %7   %8")
                                             .arg(activeSoundChannelName())
                                             .arg(noteText)
                                             .arg(inst, 2, 16, QLatin1Char('0')).toUpper()
                                             .arg(instName.isEmpty() ? QString() : QString(" %1").arg(instName))
                                             .arg(instrumentEnvelope(inst), 2, 16, QLatin1Char('0')).toUpper()
                                             .arg(psg)
                                             .arg(freq, 0, 'f', 2)
                                             .arg(snText));
            }
        }

        requestPreviewTone(ch, psg, vol);

        const int previewChannel = ch;
        QTimer::singleShot(previewDurationMs(), this, [this, previewChannel]() {
            stopPreviewChannel(previewChannel);
        });

        if (m_autoUpdateCheck && m_autoUpdateCheck->isChecked())
            rebuildOutput();

        const int step = m_stepSpin ? m_stepSpin->value() : 1;
        const int nextRow = qMin(row + step, m_patternTable->rowCount() - 1);

        if (m_rowSpin)
            m_rowSpin->setValue(nextRow);

        m_patternTable->selectRow(nextRow);
    }

    QString patternText(int row, int col) const
    {
        if (!m_patternTable)
            return QString("---");

        if (row < 0 || row >= m_patternTable->rowCount() || col < 0 || col >= m_patternTable->columnCount())
            return QString("---");

        QTableWidgetItem* item = m_patternTable->item(row, col);
        return item ? item->text().trimmed().toUpper() : QString("---");
    }

    int volumeFromPatternText(QString volText) const
    {
        volText = volText.trimmed().toUpper();

        if (volText.startsWith("V"))
            volText.remove(0, 1);

        bool ok = false;
        int value = volText.toInt(&ok, 16);
        if (!ok)
            value = 0;

        return qBound(0, value, 15);
    }

    int semitoneFromNoteName(const QString& noteText) const
    {
        QString n = noteText.trimmed().toUpper();
        if (n.size() < 1)
            return 0;

        // Accept: C-4, C#4, C#-4, D-5, ...
        const QChar base = n.at(0);
        int semitone = 0;

        switch (base.toLatin1()) {
        case 'C': semitone = 0; break;
        case 'D': semitone = 2; break;
        case 'E': semitone = 4; break;
        case 'F': semitone = 5; break;
        case 'G': semitone = 7; break;
        case 'A': semitone = 9; break;
        case 'B': semitone = 11; break;
        default:  semitone = 0; break;
        }

        if (n.contains("#"))
            semitone += 1;

        return qBound(0, semitone, 11);
    }

    int octaveFromNoteName(const QString& noteText) const
    {
        QString n = noteText.trimmed().toUpper();

        for (int i = n.size() - 1; i >= 0; --i) {
            if (n.at(i).isDigit())
                return qBound(1, n.mid(i, 1).toInt(), 8);
        }

        return 4;
    }

    int psgPeriodFromNoteName(const QString& noteText) const
    {
        const QString n = noteText.trimmed().toUpper();

        if (n.isEmpty() || n == "---" || n == "===")
            return 0;

        if (n.startsWith("N"))
            return 0;

        const int semitone = semitoneFromNoteName(n);
        const int octave = octaveFromNoteName(n);
        return psgPeriodFromFrequency(noteFrequency(semitone, octave));
    }

    int noiseValueFromNoteName(const QString& noteText, const QString& fxText) const
    {
        QString src = fxText.trimmed().toUpper();

        if (src.startsWith("S"))
            src.remove(0, 1);
        else {
            src = noteText.trimmed().toUpper();
            if (src.startsWith("N"))
                src.remove(0, 1);
        }

        bool ok = false;
        int v = src.toInt(&ok, 16);
        if (!ok)
            v = 0;

        return qBound(0, v, 7);
    }

    int playbackRowDurationMs() const
    {
        const int tempo = m_tempoSpin ? m_tempoSpin->value() : 125;
        const int speed = m_speedSpin ? m_speedSpin->value() : 6;

        // Tracker-achtige benadering:
        // meer speed = langere row, hoger tempo = kortere row.
        const int ms = qRound((60000.0 / qMax(1, tempo)) * (speed / 4.0));
        return qBound(60, ms, 900);
    }

    void ensurePlayTimer()
    {
        if (m_playTimer)
            return;

        m_playTimer = new QTimer(this);
        m_playTimer->setSingleShot(true);
        m_playTimer->setTimerType(Qt::PreciseTimer);

        connect(m_playTimer, &QTimer::timeout, this, [this]() {
            playCurrentPatternRowAndAdvance();
        });
    }

    int orderPatternAtColumn(int col) const
    {
        if (!m_orderTable)
            return 255;

        if (col < 0 || col >= m_orderTable->columnCount())
            return 255;

        QTableWidgetItem* item = m_orderTable->item(0, col);
        QString v = item ? item->text().trimmed().toUpper() : QString("FF");

        bool ok = false;
        int pattern = v.toInt(&ok, 16);

        if (!ok)
            pattern = 255;

        return qBound(0, pattern, 255);
    }

    int firstPlayableOrderColumnFrom(int startCol) const
    {
        if (!m_orderTable)
            return -1;

        startCol = qBound(0, startCol, m_orderTable->columnCount() - 1);

        for (int c = startCol; c < m_orderTable->columnCount(); ++c) {
            const int pattern = orderPatternAtColumn(c);
            if (pattern != 255)
                return c;
        }

        return -1;
    }

    int nextPlayableOrderColumn(int afterCol) const
    {
        if (!m_orderTable)
            return -1;

        for (int c = afterCol + 1; c < m_orderTable->columnCount(); ++c) {
            const int pattern = orderPatternAtColumn(c);
            if (pattern != 255)
                return c;
        }

        return -1;
    }

    void setCurrentPatternWithoutUndo(int patternIndex)
    {
        patternIndex = qBound(0, patternIndex, 255);

        if (patternIndex == m_currentPatternIndex)
            return;

        saveCurrentPatternToMemory();

        m_currentPatternIndex = patternIndex;

        if (m_patternSpin) {
            QSignalBlocker blocker(m_patternSpin);
            m_patternSpin->setValue(patternIndex);
        }

        loadPatternFromMemory(patternIndex);
    }

    void setPlaybackStatusText(const QString& text)
    {
        if (m_playbackStatusLabel)
            m_playbackStatusLabel->setText(text);
    }

    void setPlaybackUiPlaying(bool playing)
    {
        if (m_playbackPlayBtn)
            m_playbackPlayBtn->setChecked(playing);

        if (m_playbackStopBtn)
            m_playbackStopBtn->setChecked(!playing);
    }

    void resetPlaybackHeldChannels()
    {
        for (int ch = 0; ch < 4; ++ch) {
            m_playbackHeldActive[ch] = false;
            m_playbackHeldPeriod[ch] = 0;
            m_playbackHeldVolume[ch] = 0;
        }
    }

    void stopAndClearPreviewChannel(int channel)
    {
        channel = qBound(0, channel, 3);
        m_playbackHeldActive[channel] = false;
        m_playbackHeldPeriod[channel] = 0;
        m_playbackHeldVolume[channel] = 0;
        stopPreviewChannel(channel);
    }


    QVariantList buildSoundEditorStreamRows() const
    {
        QVariantList streamRows;

        QMap<int, QJsonArray> patterns = m_soundPatterns;

        // Belangrijk: gebruik de zichtbaar geselecteerde pattern-index.
        // Log toonde een mismatch: applySoundSongJson currentPattern=6,
        // maar bij Play currentPattern=0. Als we dan de zichtbare tabel onder
        // de verkeerde index wegschrijven, wordt pattern 0 met een andere pattern vervuild.
        const int visiblePatternIndex = m_patternSpin ? m_patternSpin->value() : m_currentPatternIndex;

        if (m_patternTable && visiblePatternIndex >= 0)
            patterns[visiblePatternIndex] = tableToJson(m_patternTable);

        if (!m_orderTable)
            return streamRows;

        for (int orderCol = m_playFromSpin ? m_playFromSpin->value() : 0;
             orderCol < m_orderTable->columnCount();
             ++orderCol) {

            const int patternIndex = orderPatternAtColumn(orderCol);
            if (patternIndex == 255)
                break;

            const QJsonArray rows = patterns.value(patternIndex);
            if (rows.isEmpty())
                continue;

            for (int r = 0; r < rows.size(); ++r) {
                const QJsonArray row = rows.at(r).toArray();

                QVariantList outRow;
                for (int ch = 0; ch < 4; ++ch) {
                    if (!m_channelAudible[ch]) {
                        outRow << 0 << 0 << 3 << 50 << 50;
                        continue;
                    }

                    const int base = 1 + ch * 4;
                    const QString note = row.at(base + 0).toString("---").trimmed().toUpper();
                    const QString instText = row.at(base + 1).toString("--").trimmed().toUpper();
                    const QString volText = row.at(base + 2).toString("---").trimmed().toUpper();
                    const QString fx = row.at(base + 3).toString("---").trimmed().toUpper();

                    bool instOk = false;
                    int inst = instText.toInt(&instOk, 16);
                    if (!instOk)
                        inst = m_defaultInstrumentSpin ? m_defaultInstrumentSpin->value() : 1;
                    inst = qBound(0, inst, 15);

                    const int instVolume = instrumentVolume(inst);

                    // Belangrijk: instrument volume mag niet altijd vermenigvuldigd worden
                    // met row volume, want dan worden bestaande songs veel te stil/anders.
                    // Als de row expliciet Vxx bevat, respecteren we die.
                    // Als volume leeg/--- is, gebruiken we het instrument-volume.
                    int effectiveVolume = 0;
                    if (volText.startsWith("V")) {
                        const int rowVol = volumeFromPatternText(volText);

                        // V10: instrument volume mag de row niet kapot maken,
                        // maar mag wel licht meebepalen zodat instrumenten consistenter zijn.
                        const double instrFactor = 0.75 + (static_cast<double>(instVolume) / 60.0);
                        effectiveVolume = qRound(rowVol * instrFactor);

                        // Expliciete hoge row-volume nooit onnodig zachter maken.
                        if (rowVol >= 12)
                            effectiveVolume = qMax(effectiveVolume, rowVol);
                    } else if (volText == "---" || volText == "--" || volText.isEmpty()) {
                        effectiveVolume = instVolume;
                    } else {
                        effectiveVolume = volumeFromPatternText(volText);
                    }

                    effectiveVolume = qBound(0, effectiveVolume, 15);
                    const int env = instrumentEnvelope(inst);
                    const int wx = instrumentWaveX(inst);
                    const int wy = instrumentWaveY(inst);

                    if (note == "---") {
                        outRow << -1 << -1 << -1 << wx << wy; // HOLD
                    } else if (note == "===" || effectiveVolume <= 0) {
                        outRow << 0 << 0 << env << wx << wy;   // explicit release
                    } else if (ch < 3) {
                        outRow << psgPeriodFromNoteName(note) << effectiveVolume << env << wx << wy;
                    } else {
                        outRow << noiseValueFromNoteName(note, fx) << effectiveVolume << env << wx << wy;
                    }
                }

                streamRows << QVariant(outRow);
            }
        }

        qDebug().noquote() << "[ADAMP SOUND] buildSoundEditorStreamRows rows=" << streamRows.size()
                           << "playFrom=" << (m_playFromSpin ? m_playFromSpin->value() : -1)
                           << "currentPattern=" << m_currentPatternIndex
                           << "visiblePattern=" << (m_patternSpin ? m_patternSpin->value() : -1);

        for (int i = 0; i < qMin(12, streamRows.size()); ++i)
            qDebug().noquote() << "[ADAMP SOUND] STREAMROW" << i << streamRows.at(i).toList();

        return streamRows;
    }



    void restartCurrentStreamPlayback()
    {
        if (!m_isPatternPlaying)
            return;

        saveCurrentPatternToMemory();
        const QVariantList rows = buildSoundEditorStreamRows();
        if (rows.isEmpty())
            return;

        const bool loopEnabled = (m_loopSongCheck && m_loopSongCheck->isChecked());

        debugDumpSoundState("BEFORE startPatternPlayback stream", 4);

        qDebug().noquote() << "[ADAMP SOUND] startPatternPlayback"
                           << "rows=" << rows.size()
                           << "rowMs=" << playbackRowDurationMs()
                           << "loop=" << loopEnabled
                           << "tempo=" << (m_tempoSpin ? m_tempoSpin->value() : -1)
                           << "speed=" << (m_speedSpin ? m_speedSpin->value() : -1);

        if (onStreamPlayRequested)
            onStreamPlayRequested(rows, playbackRowDurationMs(), loopEnabled);

        if (m_playTimer) {
            m_playTimer->stop();
            m_playTimer->start(playbackRowDurationMs());
        }

        if (m_noteInfoLabel) {
            m_noteInfoLabel->setText(QString("Playback timing updated: Tempo %1 / Speed %2")
                                         .arg(m_tempoSpin ? m_tempoSpin->value() : 0)
                                         .arg(m_speedSpin ? m_speedSpin->value() : 0));
        }
    }


    void startPatternPlayback()
    {
        if (!m_patternTable)
            return;

        ensurePlayTimer();

        if (!m_isPatternPlaying) {
            // Play From betekent nu: order-column startpositie.
            const int requestedOrder = m_playFromSpin ? m_playFromSpin->value() : 0;
            m_playingOrderColumn = firstPlayableOrderColumnFrom(requestedOrder);

            if (m_playingOrderColumn < 0) {
                setPlaybackUiPlaying(false);
                setPlaybackStatusText("No order");

                if (m_noteInfoLabel)
                    m_noteInfoLabel->setText("Playback: no playable order entries");
                return;
            }

            m_playingPatternIndex = orderPatternAtColumn(m_playingOrderColumn);
            setCurrentPatternWithoutUndo(m_playingPatternIndex);

            m_playingRow = 0;
            resetPlaybackHeldChannels();
        }

        m_isPatternPlaying = true;
        setPlaybackUiPlaying(true);
        setPlaybackStatusText("Streaming");

        const QVariantList rows = buildSoundEditorStreamRows();
        if (rows.isEmpty()) {
            stopPatternPlayback();
            return;
        }

        const bool loopEnabled = (m_loopSongCheck && m_loopSongCheck->isChecked());

        if (onStreamPlayRequested)
            onStreamPlayRequested(rows, playbackRowDurationMs(), loopEnabled);

        // Audio loopt nu in SoundManager via de stream-player.
        // Deze timer dient alleen nog voor UI-follow: Order List + Pattern row.
        m_streamPlayerUiFollow = true;

        if (m_playTimer)
            m_playTimer->start(playbackRowDurationMs());

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Streaming %1 tracker row(s) to real PSG audio engine").arg(rows.size()));
    }

    void stopPatternPlayback()
    {
        qDebug().noquote() << "[ADAMP SOUND] stopPatternPlayback";

        if (m_liveInstrumentRestartTimer)
            m_liveInstrumentRestartTimer->stop();

        m_isPatternPlaying = false;
        m_streamPlayerUiFollow = false;

        if (m_playTimer)
            m_playTimer->stop();

        for (int ch = 0; ch < 4; ++ch)
            stopAndClearPreviewChannel(ch);

        if (onStreamStopRequested)
            onStreamStopRequested();

        // Hard audio stop: volume 0 alone is not enough when the DirectSound
        // ringbuffer still contains old waveform data.
        if (onStopAllPreviewRequested)
            onStopAllPreviewRequested();

        setPlaybackUiPlaying(false);
        setPlaybackStatusText("Stopped");

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText("Playback stopped");
    }

    void rewindPatternPlayback()
    {
        const int requestedOrder = m_playFromSpin ? m_playFromSpin->value() : 0;
        m_playingOrderColumn = firstPlayableOrderColumnFrom(requestedOrder);

        if (m_playingOrderColumn < 0) {
            m_playingOrderColumn = 0;
            m_playingPatternIndex = 0;
            m_playingRow = 0;
        } else {
            m_playingPatternIndex = orderPatternAtColumn(m_playingOrderColumn);
            setCurrentPatternWithoutUndo(m_playingPatternIndex);
            m_playingRow = 0;
        }

        resetPlaybackHeldChannels();

        if (m_rowSpin)
            m_rowSpin->setValue(0);

        if (m_patternTable)
            m_patternTable->selectRow(0);

        if (m_orderTable && m_playingOrderColumn >= 0)
            m_orderTable->setCurrentCell(0, m_playingOrderColumn);

        setPlaybackStatusText(QString("O:%1  P:%2  R:00")
                                  .arg(m_playingOrderColumn, 2, 10, QLatin1Char('0'))
                                  .arg(m_playingPatternIndex, 2, 16, QLatin1Char('0')).toUpper());

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Playback rewound: Order %1 Pattern %2")
                                         .arg(m_playingOrderColumn, 2, 10, QLatin1Char('0'))
                                         .arg(m_playingPatternIndex, 2, 16, QLatin1Char('0')).toUpper());
    }

    bool advanceToNextOrderPattern()
    {
        int nextOrder = nextPlayableOrderColumn(m_playingOrderColumn);

        if (nextOrder < 0) {
            if (m_loopSongCheck && m_loopSongCheck->isChecked())
                nextOrder = firstPlayableOrderColumnFrom(0);
        }

        if (nextOrder < 0)
            return false;

        m_playingOrderColumn = nextOrder;
        m_playingPatternIndex = orderPatternAtColumn(m_playingOrderColumn);
        setCurrentPatternWithoutUndo(m_playingPatternIndex);
        m_playingRow = 0;
        return true;
    }

    void playCurrentPatternRowAndAdvance()
    {
        if (!m_isPatternPlaying || !m_patternTable)
            return;

        if (m_playingRow < 0)
            m_playingRow = 0;

        if (m_playingRow >= m_patternTable->rowCount()) {
            if (m_streamPlayerUiFollow) {
                // De stream-player werd als één lineaire lijst gestart.
                // Voor UI-follow gaan we NIET via advanceToNextOrderPattern(),
                // want die kan loop-song opnieuw laten springen terwijl de audio-stream
                // al aan zijn einde zit. Dus hier gewoon de volgende order-column zoeken.
                int nextOrder = nextPlayableOrderColumn(m_playingOrderColumn);

                if (nextOrder < 0) {
                    if (m_loopSongCheck && m_loopSongCheck->isChecked())
                        nextOrder = firstPlayableOrderColumnFrom(0);
                }

                if (nextOrder < 0) {
                    stopPatternPlayback();
                    return;
                }

                m_playingOrderColumn = nextOrder;
                m_playingPatternIndex = orderPatternAtColumn(m_playingOrderColumn);
                setCurrentPatternWithoutUndo(m_playingPatternIndex);
                m_playingRow = 0;
            } else {
                if (!advanceToNextOrderPattern()) {
                    stopPatternPlayback();
                    return;
                }
            }
        }

        if (!m_streamPlayerUiFollow) {
            // Oude fallback-preview playback.
            playTrackerRow(m_playingRow);
        } else {
            // Alleen UI volgen, audio wordt door SoundManager::startSoundEditorStream()
            // sample/buffer-based afgespeeld.
            const QString rowName = QString("%1").arg(m_playingRow, 2, 16, QLatin1Char('0')).toUpper();
            const QString patName = QString("%1").arg(m_currentPatternIndex, 2, 16, QLatin1Char('0')).toUpper();

            setPlaybackStatusText(QString("O:%1  P:%2  R:%3")
                                      .arg(m_playingOrderColumn, 2, 10, QLatin1Char('0'))
                                      .arg(patName)
                                      .arg(rowName));

            if (m_noteInfoLabel) {
                m_noteInfoLabel->setText(QString("Streaming Order %1 Pattern %2 Row %3")
                                             .arg(m_playingOrderColumn, 2, 10, QLatin1Char('0'))
                                             .arg(patName)
                                             .arg(rowName));
            }
        }

        if (m_followPlayCheck && m_followPlayCheck->isChecked()) {
            if (m_orderTable && m_playingOrderColumn >= 0)
                m_orderTable->setCurrentCell(0, m_playingOrderColumn);

            m_patternTable->selectRow(m_playingRow);

            if (m_rowSpin)
                m_rowSpin->setValue(m_playingRow);
        }

        ++m_playingRow;

        if (m_playTimer)
            m_playTimer->start(playbackRowDurationMs());
    }

    void playTrackerRow(int row)
    {
        if (!m_patternTable)
            return;

        QStringList played;

        for (int ch = 0; ch < 4; ++ch) {
            const int base = 1 + ch * 4;
            const QString note = patternText(row, base + 0).trimmed().toUpper();
            const QString volText = patternText(row, base + 2).trimmed().toUpper();
            const QString fx = patternText(row, base + 3).trimmed().toUpper();
            const int volume = volumeFromPatternText(volText);

            // "---" = HOLD.
            // Niet opnieuw triggeren! De vorige versie deed dat wel en dat gaf
            // vooral op Windows/DirectSound een hoorbare herstart per row.
            // CVBasic doet ook geen nieuwe SOUND op een lege row; hij wacht gewoon.
            if (note == "---") {
                if (m_playbackHeldActive[ch]) {
                    if (ch < 3)
                        played << QString("CH%1 hold").arg(ch + 1);
                    else
                        played << QString("NOISE hold");
                }
                continue;
            }

            // "===" of V00 = expliciete release/stop.
            if (note == "===" || volume <= 0) {
                stopAndClearPreviewChannel(ch);
                continue;
            }

            if (ch < 3) {
                const int period = psgPeriodFromNoteName(note);
                if (period > 0) {
                    // Alleen nieuwe/gewijzigde noten naar de preview sturen.
                    // Zelfde period+volume opnieuw sturen is overbodig en kan stotteren.
                    const bool changed =
                        !m_playbackHeldActive[ch] ||
                        m_playbackHeldPeriod[ch] != period ||
                        m_playbackHeldVolume[ch] != volume;

                    m_playbackHeldActive[ch] = true;
                    m_playbackHeldPeriod[ch] = period;
                    m_playbackHeldVolume[ch] = volume;

                    if (changed)
                        requestPreviewTone(ch, period, volume);

                    played << QString("CH%1 %2").arg(ch + 1).arg(note);
                }
            } else {
                const int noise = noiseValueFromNoteName(note, fx);

                const bool changed =
                    !m_playbackHeldActive[ch] ||
                    m_playbackHeldPeriod[ch] != noise ||
                    m_playbackHeldVolume[ch] != volume;

                m_playbackHeldActive[ch] = true;
                m_playbackHeldPeriod[ch] = noise;
                m_playbackHeldVolume[ch] = volume;

                if (changed)
                    requestPreviewTone(3, noise, volume);

                played << QString("NOISE N%1").arg(noise, 2, 16, QLatin1Char('0')).toUpper();
            }
        }

        const QString rowName = QString("%1").arg(row, 2, 16, QLatin1Char('0')).toUpper();
        const QString patName = QString("%1").arg(m_currentPatternIndex, 2, 16, QLatin1Char('0')).toUpper();

        setPlaybackStatusText(QString("O:%1  P:%2  R:%3")
                                  .arg(m_playingOrderColumn, 2, 10, QLatin1Char('0'))
                                  .arg(patName)
                                  .arg(rowName));

        if (m_noteInfoLabel) {
            m_noteInfoLabel->setText(QString("Playing Order %1 Pattern %2 Row %3   %4")
                                         .arg(m_playingOrderColumn, 2, 10, QLatin1Char('0'))
                                         .arg(patName)
                                         .arg(rowName)
                                         .arg(played.isEmpty() ? QString("hold/silence") : played.join(" | ")));
        }
    }


    QJsonArray createEmptyPatternJson(int rows) const
    {
        QJsonArray arr;
        rows = qBound(1, rows, 256);

        for (int r = 0; r < rows; ++r) {
            QJsonArray row;
            row.append(QString("%1").arg(r, 2, 16, QLatin1Char('0')).toUpper());

            for (int col = 1; col < 17; ++col)
                row.append(defaultPatternValueForColumn(col));

            arr.append(row);
        }

        return arr;
    }

    void saveCurrentPatternToMemory()
    {
        if (!m_patternTable || m_loadingSoundPattern)
            return;

        m_soundPatterns[m_currentPatternIndex] = tableToJson(m_patternTable);
    }

    void loadPatternFromMemory(int patternIndex)
    {
        if (!m_patternTable)
            return;

        m_loadingSoundPattern = true;

        QJsonArray patternRows = m_soundPatterns.value(patternIndex);

        if (patternRows.isEmpty()) {
            const int rows = m_rowsSpin ? m_rowsSpin->value() : 16;
            patternRows = createEmptyPatternJson(rows);
            m_soundPatterns[patternIndex] = patternRows;
        }

        jsonToTable(m_patternTable, patternRows);
        renumberPatternRows();

        if (m_rowSpin) {
            m_rowSpin->setRange(0, qMax(0, m_patternTable->rowCount() - 1));
            m_rowSpin->setValue(0);
        }

        m_patternTable->selectRow(0);

        m_loadingSoundPattern = false;
        autoRebuildSoundOutput();
    }

    void switchSoundPattern(int patternIndex)
    {
        patternIndex = qBound(0, patternIndex, 255);

        if (m_loadingSoundPattern)
            return;

        if (patternIndex == m_currentPatternIndex)
            return;

        pushSoundUndoState();
        saveCurrentPatternToMemory();

        m_currentPatternIndex = patternIndex;
        loadPatternFromMemory(m_currentPatternIndex);

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Pattern %1 loaded")
                                         .arg(m_currentPatternIndex, 2, 16, QLatin1Char('0')).toUpper());
    }

    QJsonObject soundPatternsToJson() const
    {
        QJsonObject obj;

        for (auto it = m_soundPatterns.constBegin(); it != m_soundPatterns.constEnd(); ++it)
            obj[QString("%1").arg(it.key(), 2, 16, QLatin1Char('0')).toUpper()] = it.value();

        return obj;
    }

    void soundPatternsFromJson(const QJsonObject& obj)
    {
        m_soundPatterns.clear();

        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            bool ok = false;
            int index = it.key().toInt(&ok, 16);

            if (ok)
                m_soundPatterns[qBound(0, index, 255)] = it.value().toArray();
        }
    }

    int currentOrderColumn() const
    {
        if (!m_orderTable)
            return 0;

        int col = m_orderTable->currentColumn();
        if (col < 0)
            col = 0;

        return qBound(0, col, m_orderTable->columnCount() - 1);
    }

    void renumberOrderColumns()
    {
        if (!m_orderTable)
            return;

        m_orderTable->setRowCount(1);
        m_orderTable->setVerticalHeaderLabels({ "Pat" });

        QStringList headers;

        for (int c = 0; c < m_orderTable->columnCount(); ++c) {
            headers << QString("%1").arg(c, 2, 10, QLatin1Char('0'));

            QTableWidgetItem* pat = m_orderTable->item(0, c);
            if (!pat) {
                pat = new QTableWidgetItem("FF");
                pat->setTextAlignment(Qt::AlignCenter);
                m_orderTable->setItem(0, c, pat);
            }
        }

        m_orderTable->setHorizontalHeaderLabels(headers);
    }

    void normalizeOrderTableRows()
    {
        if (!m_orderTable)
            return;

        QVector<QString> values;
        values.reserve(m_orderTable->columnCount());

        const int sourceRow = (m_orderTable->rowCount() > 1) ? 1 : 0;

        for (int c = 0; c < m_orderTable->columnCount(); ++c) {
            QTableWidgetItem* item = m_orderTable->item(sourceRow, c);
            values.append(item ? item->text().trimmed().toUpper() : QStringLiteral("FF"));
        }

        m_orderTable->setRowCount(1);
        m_orderTable->setVerticalHeaderLabels({ "Pat" });

        for (int c = 0; c < m_orderTable->columnCount(); ++c) {
            QTableWidgetItem* item = m_orderTable->item(0, c);
            if (!item) {
                item = new QTableWidgetItem();
                item->setTextAlignment(Qt::AlignCenter);
                m_orderTable->setItem(0, c, item);
            }
            item->setText(values.value(c, QStringLiteral("FF")));
        }

        renumberOrderColumns();
    }

    void setOrderPatternValue(int col, const QString& value)
    {
        if (!m_orderTable)
            return;

        if (col < 0 || col >= m_orderTable->columnCount())
            return;

        QTableWidgetItem* item = m_orderTable->item(0, col);
        if (!item) {
            item = new QTableWidgetItem();
            item->setTextAlignment(Qt::AlignCenter);
            m_orderTable->setItem(0, col, item);
        }

        item->setText(value.toUpper());
    }

    void addOrderColumn()
    {
        if (!m_orderTable)
            return;

        pushSoundUndoState();

        const int col = m_orderTable->columnCount();
        m_orderTable->insertColumn(col);
        renumberOrderColumns();
        setOrderPatternValue(col, "FF");
        m_orderTable->setCurrentCell(0, col);
        autoRebuildSoundOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Added order column %1").arg(col));
    }

    void insertOrderColumn()
    {
        if (!m_orderTable)
            return;

        pushSoundUndoState();

        const int col = currentOrderColumn();
        m_orderTable->insertColumn(col);
        renumberOrderColumns();
        setOrderPatternValue(col, "FF");
        m_orderTable->setCurrentCell(0, col);
        autoRebuildSoundOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Inserted order column %1").arg(col));
    }

    void deleteOrderColumn()
    {
        if (!m_orderTable)
            return;

        if (m_orderTable->columnCount() <= 1) {
            clearOrderColumn();
            return;
        }

        pushSoundUndoState();

        const int col = currentOrderColumn();
        m_orderTable->removeColumn(col);
        renumberOrderColumns();
        m_orderTable->setCurrentCell(0, qBound(0, col, m_orderTable->columnCount() - 1));
        autoRebuildSoundOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Deleted order column %1").arg(col));
    }

    void clearOrderColumn()
    {
        if (!m_orderTable)
            return;

        pushSoundUndoState();

        const int col = currentOrderColumn();
        setOrderPatternValue(col, "FF");
        autoRebuildSoundOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Cleared order column %1").arg(col));
    }

    void expandOrderList()
    {
        if (!m_orderTable)
            return;

        pushSoundUndoState();

        const int oldCount = m_orderTable->columnCount();
        const int addCount = 8;

        for (int i = 0; i < addCount; ++i)
            m_orderTable->insertColumn(m_orderTable->columnCount());

        renumberOrderColumns();

        for (int c = oldCount; c < m_orderTable->columnCount(); ++c)
            setOrderPatternValue(c, "FF");

        autoRebuildSoundOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Expanded order list to %1 slots").arg(m_orderTable->columnCount()));
    }

    void shrinkOrderList()
    {
        if (!m_orderTable)
            return;

        if (m_orderTable->columnCount() <= 1)
            return;

        pushSoundUndoState();

        const int removeCount = qMin(8, m_orderTable->columnCount() - 1);

        for (int i = 0; i < removeCount; ++i)
            m_orderTable->removeColumn(m_orderTable->columnCount() - 1);

        renumberOrderColumns();
        autoRebuildSoundOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Shrunk order list to %1 slots").arg(m_orderTable->columnCount()));
    }

    QString buildOrderListData() const
    {
        QString out;
        out += "SOUND_ORDER:\n";

        if (!m_orderTable) {
            out += "DATA 1025\n\n";
            return out;
        }

        QStringList values;
        for (int c = 0; c < m_orderTable->columnCount(); ++c) {
            QTableWidgetItem* item = m_orderTable->item(0, c);
            QString v = item ? item->text().trimmed().toUpper() : QString("FF");

            bool ok = false;
            int n = v.toInt(&ok, 16);
            if (!ok)
                n = 255;

            values << QString::number(qBound(0, n, 255));
        }

        values << "255";
        out += "DATA " + values.join(",") + "\n\n";
        return out;
    }

    int cvBasicWaitFramesForOneRow() const
    {
        // De Sound Editor gebruikt playbackRowDurationMs().
        // CVBasic WAIT is één video frame (~1/60 sec op NTSC).
        // De compacte player doet deze timing centraal na elke tracker-row.
        const int ms = playbackRowDurationMs();
        return qMax(1, qRound(ms * 60.0 / 1000.0));
    }

    bool isPatternRowsEmpty(const QJsonArray& rows) const
    {
        for (int r = 0; r < rows.size(); ++r) {
            const QJsonArray row = rows.at(r).toArray();

            for (int ch = 0; ch < 4; ++ch) {
                const int base = 1 + ch * 4;
                const QString note = row.at(base + 0).toString("---").trimmed().toUpper();
                const QString volText = row.at(base + 2).toString("---").trimmed().toUpper();
                const QString fx = row.at(base + 3).toString("---").trimmed().toUpper();

                if (note != "---" && note != "===")
                    return false;

                if (volumeFromPatternText(volText) > 0 && (fx != "---" && fx != "==="))
                    return false;
            }
        }

        return true;
    }

    QSet<int> usedPatternsFromOrder() const
    {
        QSet<int> used;

        if (!m_orderTable)
            return used;

        for (int c = 0; c < m_orderTable->columnCount(); ++c) {
            const int patternIndex = orderPatternAtColumn(c);

            if (patternIndex == 255)
                break;

            used.insert(patternIndex);
        }

        return used;
    }

    QMap<int, QJsonArray> soundPatternsForExport() const
    {
        QMap<int, QJsonArray> patterns = m_soundPatterns;

        // Zorg dat de huidige zichtbare pattern altijd up-to-date is.
        if (m_patternTable)
            patterns[m_currentPatternIndex] = tableToJson(m_patternTable);

        return patterns;
    }

    QSet<int> nonEmptyUsedPatternsForExport(const QMap<int, QJsonArray>& patterns) const
    {
        QSet<int> patternsToExport = usedPatternsFromOrder();

        // Als er geen order is, exporteer minstens de huidige pattern.
        if (patternsToExport.isEmpty())
            patternsToExport.insert(m_currentPatternIndex);

        QSet<int> result;
        for (int patternIndex : std::as_const(patternsToExport)) {
            const QJsonArray rows = patterns.value(patternIndex);
            if (!rows.isEmpty() && !isPatternRowsEmpty(rows))
                result.insert(patternIndex);
        }

        if (result.isEmpty())
            result.insert(m_currentPatternIndex);

        return result;
    }

    QStringList compactPatternRowValues(const QJsonArray& row) const
    {
        QStringList values;
        values.reserve(8);

        for (int ch = 0; ch < 4; ++ch) {
            const int base = 1 + ch * 4;
            const QString note = row.at(base + 0).toString("---").trimmed().toUpper();
            const QString volText = row.at(base + 2).toString("---").trimmed().toUpper();
            const QString fx = row.at(base + 3).toString("---").trimmed().toUpper();
            const int volume = volumeFromPatternText(volText);

            // 1024,1024 = HOLD: niets opnieuw triggeren, alleen wachten.
            //    0,   0 = RELEASE/STOP voor dit kanaal.
            // 1025     = END marker voor pattern einde.
            // We vermijden -1 omdat CVBasic standaard unsigned werkt.
            if (note == "---") {
                values << "1024" << "1024";
                continue;
            }

            if (note == "===" || volume <= 0) {
                values << "0" << "0";
                continue;
            }

            if (ch < 3) {
                values << QString::number(psgPeriodFromNoteName(note));
                values << QString::number(volume);
            } else {
                values << QString::number(noiseValueFromNoteName(note, fx));
                values << QString::number(volume);
            }
        }

        return values;
    }

    QString buildCvBasicPatternData() const
    {
        QString out;

        const QMap<int, QJsonArray> patterns = soundPatternsForExport();
        const QSet<int> patternsToExport = nonEmptyUsedPatternsForExport(patterns);

        bool wroteAnyPattern = false;

        for (auto it = patterns.constBegin(); it != patterns.constEnd(); ++it) {
            const int patternIndex = it.key();
            const QJsonArray rows = it.value();

            if (!patternsToExport.contains(patternIndex))
                continue;

            if (rows.isEmpty() || isPatternRowsEmpty(rows))
                continue;

            wroteAnyPattern = true;

            out += QString("SOUND_PATTERN_%1:\n")
                       .arg(patternIndex, 2, 16, QLatin1Char('0')).toUpper();

            for (int r = 0; r < rows.size(); ++r) {
                const QJsonArray row = rows.at(r).toArray();
                out += "DATA " + compactPatternRowValues(row).join(",") + "\n";
            }

            out += "DATA 1025\n\n";
        }

        if (!wroteAnyPattern) {
            out += QString("SOUND_PATTERN_%1:\n")
                       .arg(m_currentPatternIndex, 2, 16, QLatin1Char('0')).toUpper();
            out += "DATA 1025\n\n";
        }

        return out;
    }

    QString buildCvBasicLinearStreamData() const
    {
        QString out;
        out += "SOUND_STREAM:\n";

        const QMap<int, QJsonArray> patterns = soundPatternsForExport();
        const QSet<int> patternsToExport = nonEmptyUsedPatternsForExport(patterns);

        bool wroteAnyRow = false;

        if (m_orderTable) {
            for (int c = 0; c < m_orderTable->columnCount(); ++c) {
                const int patternIndex = orderPatternAtColumn(c);

                if (patternIndex == 255)
                    break;

                if (!patternsToExport.contains(patternIndex))
                    continue;

                const QJsonArray rows = patterns.value(patternIndex);
                for (int r = 0; r < rows.size(); ++r) {
                    const QJsonArray row = rows.at(r).toArray();
                    out += "DATA " + compactPatternRowValues(row).join(",") + "\n";
                    wroteAnyRow = true;
                }
            }
        }

        if (!wroteAnyRow && patterns.contains(m_currentPatternIndex)) {
            const QJsonArray rows = patterns.value(m_currentPatternIndex);
            for (int r = 0; r < rows.size(); ++r) {
                const QJsonArray row = rows.at(r).toArray();
                out += "DATA " + compactPatternRowValues(row).join(",") + "\n";
                wroteAnyRow = true;
            }
        }

        // 1025 = einde van de volledige stream.
        out += "DATA 1025\n\n";
        return out;
    }

    QString buildCvBasicTickPlayer() const
    {
        const int frames = cvBasicWaitFramesForOneRow();
        QString out;

        out += "REM -------------------------------------\n";
        out += "REM ADAMP non-blocking tick music player\n";
        out += "REM Usage:\n";
        out += "REM   GOSUB ADAMP_MUSIC_INIT\n";
        out += "REM   MAIN_LOOP:\n";
        out += "REM   GOSUB ADAMP_MUSIC_TICK\n";
        out += "REM   REM your game code here\n";
        out += "REM   WAIT\n";
        out += "REM   GOTO MAIN_LOOP\n";
        out += "REM ADAMP_MPLAY = 1 while playing. Stream loops automatically.\n";
        out += "REM Do not use READ/RESTORE elsewhere while this player runs,\n";
        out += "REM because CVBasic has one global DATA pointer.\n";
        out += "REM -------------------------------------\n\n";

        out += "ADAMP_MUSIC_INIT:\n";
        out += "PLAY OFF\n";
        out += QString("ADAMP_WC=%1\n").arg(frames);
        out += "ADAMP_MWAIT=0\n";
        out += "ADAMP_MPLAY=1\n";
        out += "RESTORE SOUND_STREAM\n";
        out += "RETURN\n\n";

        out += "ADAMP_MUSIC_TICK:\n";
        out += "IF ADAMP_MPLAY=0 THEN RETURN\n";
        out += "IF ADAMP_MWAIT>0 THEN GOTO ADAMP_MUSIC_WAITING\n";
        out += "READ #ADAMP_P0\n";
        out += "IF #ADAMP_P0=1025 THEN GOTO ADAMP_MUSIC_FINISHED\n";
        out += "READ ADAMP_V0,#ADAMP_P1,ADAMP_V1,#ADAMP_P2,ADAMP_V2,#ADAMP_P3,ADAMP_V3\n";
        out += "IF #ADAMP_P0<>1024 THEN SOUND 0,#ADAMP_P0,ADAMP_V0\n";
        out += "IF #ADAMP_P1<>1024 THEN SOUND 1,#ADAMP_P1,ADAMP_V1\n";
        out += "IF #ADAMP_P2<>1024 THEN SOUND 2,#ADAMP_P2,ADAMP_V2\n";
        out += "IF #ADAMP_P3<>1024 THEN SOUND 3,#ADAMP_P3,ADAMP_V3\n";
        out += "ADAMP_MWAIT=ADAMP_WC-1\n";
        out += "RETURN\n\n";

        out += "ADAMP_MUSIC_WAITING:\n";
        out += "ADAMP_MWAIT=ADAMP_MWAIT-1\n";
        out += "RETURN\n\n";

        out += "ADAMP_MUSIC_FINISHED:\n";
        out += "RESTORE SOUND_STREAM\n";
        out += "ADAMP_MWAIT=0\n";
        out += "RETURN\n\n";

        return out;
    }

    QString cvBasicStringLiteral(QString text) const
    {
        text = text.trimmed();
        if (text.isEmpty())
            text = QStringLiteral("Untitled Song");

        text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        text.replace(QLatin1Char('"'), QStringLiteral("\\\""));

        // CVBasic text screen is 32 columns wide. Keep the title safe.
        if (text.size() > 32)
            text = text.left(32);

        return text;
    }

    QString buildCvBasicTitleScreen() const
    {
        const QString song = cvBasicStringLiteral(m_songNameEdit ? m_songNameEdit->text() : QStringLiteral("Untitled Song"));
        const QString madeBy = QStringLiteral("made by ADAM+ SoundEditor");

        const int songCol = qBound(0, (32 - song.size()) / 2, 31);
        const int madeByCol = qBound(0, (32 - madeBy.size()) / 2, 31);
        const int songPos = 10 * 32 + songCol;
        const int madeByPos = 12 * 32 + madeByCol;

        QString out;
        out += "MODE 0\n";
        out += "CLS\n";
        out += QString("PRINT AT %1,\"%2\"\n").arg(songPos).arg(song);
        out += QString("PRINT AT %1,\"%2\"\n\n").arg(madeByPos).arg(madeBy);
        return out;
    }

    QString buildCvBasicTickPlayerTestMain() const
    {
        QString out;
        out += buildCvBasicTitleScreen();
        out += "GOSUB ADAMP_MUSIC_INIT\n";
        out += "WAIT_MENU:\n";
        out += "GOSUB ADAMP_MUSIC_TICK\n";
        out += "WAIT\n";
        out += "GOTO WAIT_MENU\n\n";
        return out;
    }

    QString buildCvBasicCompactPlayer() const
    {
        const QMap<int, QJsonArray> patterns = soundPatternsForExport();
        const QSet<int> patternsToExport = nonEmptyUsedPatternsForExport(patterns);
        const int frames = cvBasicWaitFramesForOneRow();

        QList<int> patternList = patternsToExport.values();
        std::sort(patternList.begin(), patternList.end());

        QStringList sequenceCalls;
        if (m_orderTable) {
            for (int c = 0; c < m_orderTable->columnCount(); ++c) {
                const int patternIndex = orderPatternAtColumn(c);

                if (patternIndex == 255)
                    break;

                if (!patternsToExport.contains(patternIndex))
                    continue;

                sequenceCalls << QString("GOSUB ADAMP_PLAY_PAT_%1")
                                     .arg(patternIndex, 2, 16, QLatin1Char('0')).toUpper();
            }
        }

        if (sequenceCalls.isEmpty() && patternsToExport.contains(m_currentPatternIndex)) {
            sequenceCalls << QString("GOSUB ADAMP_PLAY_PAT_%1")
                                 .arg(m_currentPatternIndex, 2, 16, QLatin1Char('0')).toUpper();
        }

        QString out;

        // Belangrijk: RESTORE wijzigt de globale CVBasic READ/DATA-pointer.
        // Daarom mag de speler niet eerst SOUND_ORDER lezen en daarna RESTORE
        // naar een pattern doen, want na de pattern staat de pointer niet meer
        // bij de orderlijst. We genereren de order daarom als directe GOSUB-lijst.
        out += "ADAMP_CVPLAYER_PLAY:\n";
        out += "PLAY OFF\n";
        out += QString("ADAMP_WC=%1\n").arg(frames);

        for (const QString& call : std::as_const(sequenceCalls))
            out += call + "\n";

        out += "ADAMP_PLAYER_STOP:\n";
        out += "SOUND 0,0,0\n";
        out += "SOUND 1,0,0\n";
        out += "SOUND 2,0,0\n";
        out += "SOUND 3,0,0\n";
        out += "RETURN\n\n";

        for (int patternIndex : patternList) {
            out += QString("ADAMP_PLAY_PAT_%1:\n")
                       .arg(patternIndex, 2, 16, QLatin1Char('0')).toUpper();
            out += QString("RESTORE SOUND_PATTERN_%1\n")
                       .arg(patternIndex, 2, 16, QLatin1Char('0')).toUpper();
            out += "GOSUB ADAMP_PLAY_PATTERN\n";
            out += "RETURN\n\n";
        }

        out += "ADAMP_PLAY_PATTERN:\n";
        out += "ADAMP_PAT_LOOP:\n";
        out += "READ #ADAMP_P0\n";
        out += "IF #ADAMP_P0=1025 THEN RETURN\n";
        out += "READ ADAMP_V0,#ADAMP_P1,ADAMP_V1,#ADAMP_P2,ADAMP_V2,#ADAMP_P3,ADAMP_V3\n";
        out += "IF #ADAMP_P0<>1024 THEN SOUND 0,#ADAMP_P0,ADAMP_V0\n";
        out += "IF #ADAMP_P1<>1024 THEN SOUND 1,#ADAMP_P1,ADAMP_V1\n";
        out += "IF #ADAMP_P2<>1024 THEN SOUND 2,#ADAMP_P2,ADAMP_V2\n";
        out += "IF #ADAMP_P3<>1024 THEN SOUND 3,#ADAMP_P3,ADAMP_V3\n";
        out += "GOSUB ADAMP_WAIT_ROW\n";
        out += "GOTO ADAMP_PAT_LOOP\n\n";

        out += "ADAMP_WAIT_ROW:\n";
        out += "FOR ADAMP_WI=1 TO ADAMP_WC\n";
        out += "WAIT\n";
        out += "NEXT ADAMP_WI\n";
        out += "RETURN\n\n";

        return out;
    }

    QString soundChipDisplayName() const
    {
        return QStringLiteral("SN76489 / TMS9919 PSG");
    }

    QString buildCompactCvBasicSongBlock(bool includePlayer) const
    {
        const QString song = m_songNameEdit ? m_songNameEdit->text().trimmed() : QString("Untitled");
        const QString author = m_authorEdit ? m_authorEdit->text().trimmed() : QString("Unknown");
        const int tempo = m_tempoSpin ? m_tempoSpin->value() : 125;
        const int speed = m_speedSpin ? m_speedSpin->value() : 6;

        QString out;
        out += "REM =====================================\n";
        out += QString("REM SONG   : %1\n").arg(song.toUpper());
        out += QString("REM AUTHOR : %1\n").arg(author.toUpper());
        out += QString("REM TEMPO  : %1\n").arg(tempo);
        out += QString("REM SPEED  : %1\n").arg(speed);
        out += QString("REM CHIP   : %1\n").arg(soundChipDisplayName());
        out += QString("REM ROWWAIT: %1 FRAME(S)\n").arg(cvBasicWaitFramesForOneRow());
        out += includePlayer ? "REM FORMAT : ADAMP TICK PLAYER STREAM DATA\n" : "REM FORMAT : COMPACT ADAMP SOUND DATA\n";
        out += "REM ROW    : CH1 P,V, CH2 P,V, CH3 P,V, NOISE C,V\n";
        out += "REM HOLD   : 1024,1024    RELEASE: 0,0    END: 1025    ORDER END: 255\n";
        out += "REM =====================================\n\n";

        if (includePlayer) {
            out += buildCvBasicTickPlayerTestMain();
            out += buildCvBasicTickPlayer();
            out += "ADAMP_CVPLAYER_DONE:\n\n";
        }

        out += QString("SOUND_SPEED:\nDATA %1\n\n").arg(speed);
        out += QString("SOUND_TEMPO:\nDATA %1\n\n").arg(tempo);
        out += buildOrderListData();

        if (includePlayer)
            out += buildCvBasicLinearStreamData();
        else
            out += buildCvBasicPatternData();

        return out;
    }

    QString buildTrackerRowsComment() const
    {
        QString out;
        out += "REM --- CURRENT TRACKER ROWS PREVIEW ---\n";

        if (!m_patternTable)
            return out;

        for (int r = 0; r < m_patternTable->rowCount(); ++r) {
            QStringList rowValues;
            for (int c = 0; c < m_patternTable->columnCount(); ++c)
                rowValues << patternText(r, c);

            out += "REM " + rowValues.join(" | ") + "\n";
        }

        return out;
    }

    QString soundSongDefaultDir() const
    {
        QSettings s(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
        s.beginGroup("cvbasic");
        const QString configured = s.value("soundSourceDir").toString().trimmed();
        s.endGroup();

        if (!configured.isEmpty())
            return QDir::cleanPath(configured);

        return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath("media/cvbasic/sound"));
    }

    QString soundBuildDefaultDir() const
    {
        QSettings s(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
        s.beginGroup("cvbasic");
        const QString configured = s.value("soundBuildDir").toString().trimmed();
        s.endGroup();

        if (!configured.isEmpty())
            return QDir::cleanPath(configured);

        return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath("media/cvbasic/build/sound"));
    }


    QJsonObject buildInstrumentBankJson() const
    {
        QJsonObject root;
        root["format"] = "ADAMP_SOUND_INSTRUMENT_BANK";
        root["version"] = 1;
        root["source"] = "ADAMP Sound Editor";
        root["instrumentsTable"] = tableToJson(m_instrumentsTable);
        return root;
    }

    bool applyInstrumentBankJson(const QJsonObject& root)
    {
        if (root.value("format").toString() != "ADAMP_SOUND_INSTRUMENT_BANK") {
            QMessageBox::warning(this, tr("Load Instrument Bank"), tr("Not a valid ADAMP instrument bank file."));
            return false;
        }

        const QJsonArray instruments = root.value("instrumentsTable").toArray();
        if (instruments.isEmpty()) {
            QMessageBox::warning(this, tr("Load Instrument Bank"), tr("Instrument bank is empty."));
            return false;
        }

        pushSoundUndoState();

        jsonToTable(m_instrumentsTable, instruments);
        normalizeInstrumentTable();

        const int row = qBound(0, currentInstrumentRow(), m_instrumentsTable ? m_instrumentsTable->rowCount() - 1 : 0);
        if (m_instrumentsTable)
            m_instrumentsTable->selectRow(row);

        loadInstrumentIntoEditor(row);
        autoRebuildSoundOutput();
        scheduleLiveInstrumentPlaybackRefresh();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText("Loaded instrument bank");

        return true;
    }

    void saveInstrumentBank()
    {
        if (!m_instrumentsTable)
            return;

        const QString filePath = QFileDialog::getSaveFileName(
            this,
            tr("Save Instrument Bank"),
            m_soundSongFilePath.isEmpty()
                ? QString("ADAMP_Instruments.adpinst")
                : QFileInfo(m_soundSongFilePath).absolutePath() + "/ADAMP_Instruments.adpinst",
            tr("ADAMP Instrument Bank (*.adpinst);;JSON Files (*.json);;All Files (*.*)")
        );

        if (filePath.isEmpty())
            return;

        QFile f(filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Save Instrument Bank"), tr("Cannot write file:\n%1").arg(filePath));
            return;
        }

        const QJsonDocument doc(buildInstrumentBankJson());
        f.write(doc.toJson(QJsonDocument::Indented));
        f.close();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Saved instrument bank: %1").arg(QFileInfo(filePath).fileName()));
    }

    void loadInstrumentBank()
    {
        const QString filePath = QFileDialog::getOpenFileName(
            this,
            tr("Load Instrument Bank"),
            m_soundSongFilePath.isEmpty() ? QString() : QFileInfo(m_soundSongFilePath).absolutePath(),
            tr("ADAMP Instrument Bank (*.adpinst);;JSON Files (*.json);;All Files (*.*)")
        );

        if (filePath.isEmpty())
            return;

        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Load Instrument Bank"), tr("Cannot open file:\n%1").arg(filePath));
            return;
        }

        const QByteArray data = f.readAll();
        f.close();

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, tr("Load Instrument Bank"), tr("Invalid JSON:\n%1").arg(err.errorString()));
            return;
        }

        applyInstrumentBankJson(doc.object());
    }


    QJsonArray tableToJson(QTableWidget* table) const
    {
        QJsonArray rows;

        if (!table)
            return rows;

        for (int r = 0; r < table->rowCount(); ++r) {
            QJsonArray row;
            for (int c = 0; c < table->columnCount(); ++c) {
                QTableWidgetItem* item = table->item(r, c);
                row.append(item ? item->text() : QString());
            }
            rows.append(row);
        }

        return rows;
    }

    void jsonToTable(QTableWidget* table, const QJsonArray& rows)
    {
        if (!table)
            return;

        table->setRowCount(rows.size());

        for (int r = 0; r < rows.size(); ++r) {
            const QJsonArray row = rows.at(r).toArray();

            for (int c = 0; c < table->columnCount(); ++c) {
                const QString value = (c < row.size()) ? row.at(c).toString() : QString();
                QTableWidgetItem* item = table->item(r, c);

                if (!item) {
                    item = new QTableWidgetItem();
                    item->setTextAlignment(Qt::AlignCenter);
                    table->setItem(r, c, item);
                }

                item->setText(value);
            }
        }
    }

    void autoRebuildSoundOutput()
    {
        if (m_autoUpdateCheck && m_autoUpdateCheck->isChecked())
            rebuildOutput();
    }

    void resizePatternRows(int rows)
    {
        if (!m_patternTable)
            return;

        rows = qBound(1, rows, 256);

        if (m_patternTable->rowCount() == rows) {
            if (m_rowSpin)
                m_rowSpin->setRange(0, qMax(0, rows - 1));
            return;
        }

        pushSoundUndoState();

        const int oldRows = m_patternTable->rowCount();
        m_patternTable->setRowCount(rows);

        for (int r = oldRows; r < rows; ++r) {
            setPatternCell(r, 0, QString("%1").arg(r, 2, 16, QLatin1Char('0')).toUpper());
            clearPatternRow(r);
        }

        renumberPatternRows();

        if (m_rowSpin) {
            m_rowSpin->setRange(0, qMax(0, rows - 1));
            m_rowSpin->setValue(qBound(0, m_rowSpin->value(), rows - 1));
        }

        saveCurrentPatternToMemory();
        autoRebuildSoundOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Rows/Pattern: %1").arg(rows));
    }

    void pushSoundUndoState()
    {
        if (m_restoringSoundUndo)
            return;

        const QJsonObject state = buildSoundSongJson();

        if (!m_soundUndoStack.isEmpty() &&
            QJsonDocument(m_soundUndoStack.last()).toJson(QJsonDocument::Compact) ==
            QJsonDocument(state).toJson(QJsonDocument::Compact)) {
            return;
        }

        m_soundUndoStack.append(state);

        // Hou het licht genoeg. 64 stappen is ruim voor deze editor.
        if (m_soundUndoStack.size() > 64)
            m_soundUndoStack.removeFirst();

        m_soundRedoStack.clear();
    }

    void restoreSoundState(const QJsonObject& state)
    {
        m_restoringSoundUndo = true;
        applySoundSongJson(state);
        m_restoringSoundUndo = false;
    }

    void undoSoundEdit()
    {
        if (m_soundUndoStack.isEmpty()) {
            if (m_noteInfoLabel)
                m_noteInfoLabel->setText("Undo: nothing to undo");
            return;
        }

        const QJsonObject current = buildSoundSongJson();
        const QJsonObject previous = m_soundUndoStack.takeLast();
        m_soundRedoStack.append(current);

        restoreSoundState(previous);

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText("Undo");
    }

    void redoSoundEdit()
    {
        if (m_soundRedoStack.isEmpty()) {
            if (m_noteInfoLabel)
                m_noteInfoLabel->setText("Redo: nothing to redo");
            return;
        }

        const QJsonObject current = buildSoundSongJson();
        const QJsonObject next = m_soundRedoStack.takeLast();
        m_soundUndoStack.append(current);

        restoreSoundState(next);

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText("Redo");
    }


    struct MidiNoteEvent
    {
        int channel = 0;
        int note = 60;
        int velocity = 0;
        int startTick = 0;
        int endTick = 0;
    };

    static quint16 midiReadU16(const QByteArray& data, int offset)
    {
        if (offset < 0 || offset + 1 >= data.size())
            return 0;

        return static_cast<quint16>(
            (static_cast<unsigned char>(data.at(offset)) << 8) |
             static_cast<unsigned char>(data.at(offset + 1))
        );
    }

    static quint32 midiReadU32(const QByteArray& data, int offset)
    {
        if (offset < 0 || offset + 3 >= data.size())
            return 0;

        return (static_cast<quint32>(static_cast<unsigned char>(data.at(offset))) << 24) |
               (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 1))) << 16) |
               (static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 2))) << 8) |
                static_cast<quint32>(static_cast<unsigned char>(data.at(offset + 3)));
    }

    static bool midiReadVarLen(const QByteArray& data, int& pos, int end, int& value)
    {
        value = 0;

        for (int i = 0; i < 4; ++i) {
            if (pos >= end)
                return false;

            const int b = static_cast<unsigned char>(data.at(pos++));
            value = (value << 7) | (b & 0x7F);

            if ((b & 0x80) == 0)
                return true;
        }

        return true;
    }

    QString midiNoteName(int midiNote) const
    {
        static const char* names[12] = {
            "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"
        };

        midiNote = qBound(0, midiNote, 127);
        const int octave = (midiNote / 12) - 1;
        return QString("%1%2").arg(names[midiNote % 12]).arg(octave);
    }

    QStringList midiEmptyPatternRow(int row) const
    {
        QStringList values;
        values << QString("%1").arg(row, 2, 16, QLatin1Char('0')).toUpper();

        for (int ch = 0; ch < 4; ++ch)
            values << "---" << "--" << "---" << "---";

        return values;
    }

    bool parseMidiFile(const QByteArray& data,
                       QVector<MidiNoteEvent>& notes,
                       int& ticksPerQuarter,
                       QString& errorText) const
    {
        notes.clear();
        ticksPerQuarter = 480;

        if (data.size() < 14 || data.mid(0, 4) != "MThd") {
            errorText = tr("Missing MIDI header MThd.");
            return false;
        }

        const quint32 headerLen = midiReadU32(data, 4);
        if (headerLen < 6 || data.size() < static_cast<int>(8 + headerLen)) {
            errorText = tr("Invalid MIDI header length.");
            return false;
        }

        const int format = midiReadU16(data, 8);
        const int trackCount = midiReadU16(data, 10);
        const int division = midiReadU16(data, 12);

        Q_UNUSED(format);

        if (division & 0x8000) {
            errorText = tr("SMPTE time-division MIDI files are not supported yet.");
            return false;
        }

        ticksPerQuarter = qMax(1, division);

        int pos = 8 + static_cast<int>(headerLen);

        for (int track = 0; track < trackCount && pos + 8 <= data.size(); ++track) {
            if (data.mid(pos, 4) != "MTrk") {
                errorText = tr("Missing MTrk track chunk.");
                return false;
            }

            const int trackLen = static_cast<int>(midiReadU32(data, pos + 4));
            pos += 8;

            const int trackEnd = qMin(data.size(), pos + trackLen);
            int tick = 0;
            int runningStatus = 0;

            struct ActiveNote
            {
                int startTick = 0;
                int velocity = 0;
            };

            QMap<int, QVector<ActiveNote>> activeNotes;

            while (pos < trackEnd) {
                int delta = 0;
                if (!midiReadVarLen(data, pos, trackEnd, delta))
                    break;

                tick += delta;

                if (pos >= trackEnd)
                    break;

                int status = static_cast<unsigned char>(data.at(pos));

                if (status & 0x80) {
                    ++pos;
                    if (status < 0xF0)
                        runningStatus = status;
                } else {
                    if (runningStatus == 0) {
                        errorText = tr("Invalid MIDI running status.");
                        return false;
                    }
                    status = runningStatus;
                }

                if (status == 0xFF) {
                    if (pos >= trackEnd)
                        break;

                    const int metaType = static_cast<unsigned char>(data.at(pos++));
                    int len = 0;
                    if (!midiReadVarLen(data, pos, trackEnd, len))
                        break;

                    if (metaType == 0x2F) {
                        pos += len;
                        break;
                    }

                    pos = qMin(trackEnd, pos + len);
                    continue;
                }

                if (status == 0xF0 || status == 0xF7) {
                    int len = 0;
                    if (!midiReadVarLen(data, pos, trackEnd, len))
                        break;
                    pos = qMin(trackEnd, pos + len);
                    continue;
                }

                const int command = status & 0xF0;
                const int channel = status & 0x0F;

                auto needByte = [&]() -> int {
                    if (pos >= trackEnd)
                        return 0;
                    return static_cast<unsigned char>(data.at(pos++));
                };

                if (command == 0x80 || command == 0x90) {
                    const int note = needByte();
                    const int velocity = needByte();
                    const int key = channel * 128 + note;

                    if (command == 0x90 && velocity > 0) {
                        activeNotes[key].append({ tick, velocity });
                    } else {
                        QVector<ActiveNote>& stack = activeNotes[key];

                        if (!stack.isEmpty()) {
                            const ActiveNote active = stack.takeLast();

                            if (tick > active.startTick) {
                                MidiNoteEvent ev;
                                ev.channel = channel;
                                ev.note = note;
                                ev.velocity = active.velocity;
                                ev.startTick = active.startTick;
                                ev.endTick = tick;
                                notes.append(ev);
                            }
                        }
                    }
                } else if (command == 0xA0 || command == 0xB0 || command == 0xE0) {
                    needByte();
                    needByte();
                } else if (command == 0xC0 || command == 0xD0) {
                    needByte();
                } else {
                    // Unknown system/common event. Stop safely for this track.
                    break;
                }
            }

            pos = trackEnd;
        }

        if (notes.isEmpty()) {
            errorText = tr("No MIDI note events found.");
            return false;
        }

        std::sort(notes.begin(), notes.end(), [](const MidiNoteEvent& a, const MidiNoteEvent& b) {
            if (a.startTick != b.startTick)
                return a.startTick < b.startTick;
            return a.note > b.note;
        });

        return true;
    }

    void importMidiFile()
    {
        const QString filePath = QFileDialog::getOpenFileName(
            this,
            tr("Import MIDI"),
            soundSongDefaultDir(),
            tr("MIDI Files (*.mid *.midi);;All Files (*.*)")
        );

        if (filePath.isEmpty())
            return;

        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, tr("Import MIDI"), f.errorString());
            return;
        }

        const QByteArray data = f.readAll();
        f.close();

        QVector<MidiNoteEvent> notes;
        int ticksPerQuarter = 480;
        QString errorText;

        if (!parseMidiFile(data, notes, ticksPerQuarter, errorText)) {
            QMessageBox::warning(this, tr("Import MIDI"), errorText);
            return;
        }

        pushSoundUndoState();

        hardResetLoadedSongState();

        const int rowsPerPattern = 16;
        const int ticksPerRow = qMax(1, ticksPerQuarter / 4); // 16th-note grid
        QMap<int, QVector<MidiNoteEvent>> byRow;

        int maxRow = 0;
        for (const MidiNoteEvent& ev : std::as_const(notes)) {
            const int row = qMax(0, qRound(static_cast<double>(ev.startTick) / ticksPerRow));
            byRow[row].append(ev);
            maxRow = qMax(maxRow, row);
        }

        const int patternCount = qBound(1, (maxRow / rowsPerPattern) + 1, 256);
        m_soundPatterns.clear();

        for (int p = 0; p < patternCount; ++p) {
            QJsonArray patternRows;

            for (int r = 0; r < rowsPerPattern; ++r) {
                QStringList values = midiEmptyPatternRow(r);
                const int absoluteRow = p * rowsPerPattern + r;
                QVector<MidiNoteEvent> events = byRow.value(absoluteRow);

                std::sort(events.begin(), events.end(), [](const MidiNoteEvent& a, const MidiNoteEvent& b) {
                    if (a.channel == 9 && b.channel != 9)
                        return false;
                    if (a.channel != 9 && b.channel == 9)
                        return true;
                    if (a.velocity != b.velocity)
                        return a.velocity > b.velocity;
                    return a.note > b.note;
                });

                QVector<MidiNoteEvent> tonal;
                bool hasDrum = false;

                for (const MidiNoteEvent& ev : std::as_const(events)) {
                    if (ev.channel == 9) {
                        hasDrum = true;
                    } else {
                        tonal.append(ev);
                    }
                }

                std::sort(tonal.begin(), tonal.end(), [](const MidiNoteEvent& a, const MidiNoteEvent& b) {
                    return a.note > b.note; // highest becomes melody
                });

                if (!tonal.isEmpty()) {
                    const MidiNoteEvent mel = tonal.value(0);
                    values[1] = midiNoteName(mel.note);
                    values[2] = "01";
                    values[3] = QString("V%1").arg(qBound(4, mel.velocity / 8, 15), 2, 16, QLatin1Char('0')).toUpper();
                    values[4] = "---";
                }

                if (tonal.size() >= 2) {
                    const MidiNoteEvent harm = tonal.value(1);
                    values[5] = midiNoteName(harm.note);
                    values[6] = "02";
                    values[7] = QString("V%1").arg(qBound(3, harm.velocity / 10, 12), 2, 16, QLatin1Char('0')).toUpper();
                    values[8] = "---";
                }

                if (tonal.size() >= 3) {
                    const MidiNoteEvent bass = tonal.last();
                    values[9] = midiNoteName(bass.note);
                    values[10] = "03";
                    values[11] = QString("V%1").arg(qBound(4, bass.velocity / 9, 13), 2, 16, QLatin1Char('0')).toUpper();
                    values[12] = "---";
                }

                if (hasDrum) {
                    values[13] = "---";
                    values[14] = "08";
                    values[15] = "V0C";
                    values[16] = "S02";
                }

                QJsonArray jsonRow;
                for (const QString& value : std::as_const(values))
                    jsonRow.append(value);
                patternRows.append(jsonRow);
            }

            m_soundPatterns[p] = patternRows;
        }

        m_currentPatternIndex = 0;

        if (m_patternSpin) {
            QSignalBlocker blocker(m_patternSpin);
            m_patternSpin->setValue(0);
        }

        if (m_rowsSpin) {
            QSignalBlocker blocker(m_rowsSpin);
            m_rowsSpin->setValue(rowsPerPattern);
        }

        if (m_rowSpin) {
            m_rowSpin->setRange(0, rowsPerPattern - 1);
            m_rowSpin->setValue(0);
        }

        jsonToTable(m_patternTable, m_soundPatterns.value(0));
        renumberPatternRows();

        if (m_orderTable) {
            const int columns = qMax(16, patternCount + 1);
            m_orderTable->setColumnCount(columns);

            QStringList headers;
            for (int c = 0; c < columns; ++c)
                headers << QString("%1").arg(c, 2, 10, QLatin1Char('0'));
            m_orderTable->setHorizontalHeaderLabels(headers);

            m_orderTable->setRowCount(1);
            m_orderTable->setVerticalHeaderLabels({ "Pat" });

            for (int c = 0; c < columns; ++c) {
                const QString pat = (c < patternCount)
                    ? QString("%1").arg(c, 2, 16, QLatin1Char('0')).toUpper()
                    : QStringLiteral("FF");

                QTableWidgetItem* patItem = new QTableWidgetItem(pat);
                patItem->setTextAlignment(Qt::AlignCenter);
                m_orderTable->setItem(0, c, patItem);
            }
        }

        if (m_songNameEdit)
            m_songNameEdit->setText(QFileInfo(filePath).completeBaseName());

        if (m_authorEdit)
            m_authorEdit->setText("MIDI Import");

        if (m_speedSpin)
            m_speedSpin->setValue(6);

        if (m_tempoSpin)
            m_tempoSpin->setValue(125);

        if (m_defaultInstrumentSpin)
            m_defaultInstrumentSpin->setValue(1);

        if (m_patternTable)
            m_patternTable->selectRow(0);

        autoRebuildSoundOutput();
        rebuildOutput();

        debugDumpSoundState("AFTER importMidiFile", 8);

        if (m_noteInfoLabel) {
            m_noteInfoLabel->setText(QString("Imported MIDI: %1  (%2 notes, %3 pattern(s))")
                                         .arg(QFileInfo(filePath).fileName())
                                         .arg(notes.size())
                                         .arg(patternCount));
        }

        QMessageBox::information(
            this,
            tr("Import MIDI"),
            tr("Imported %1 note(s) into %2 pattern(s).\n\nMapping:\nCH1 = highest/melody\nCH2 = second voice\nCH3 = lowest/bass\nNOISE = MIDI drums when present")
                .arg(notes.size())
                .arg(patternCount)
        );
    }

    QJsonObject buildSoundSongJson() const
    {
        QJsonObject root;
        root["format"] = "ADAMP_CVBASIC_SOUND_SONG";
        root["version"] = 1;

        QJsonObject song;
        song["name"] = m_songNameEdit ? m_songNameEdit->text() : QString();
        song["author"] = m_authorEdit ? m_authorEdit->text() : QString();
        song["tempo"] = m_tempoSpin ? m_tempoSpin->value() : 125;
        song["speed"] = m_speedSpin ? m_speedSpin->value() : 6;
        song["rows"] = m_rowsSpin ? m_rowsSpin->value() : 16;
        song["defaultInstrument"] = m_defaultInstrumentSpin ? m_defaultInstrumentSpin->value() : 1;
        song["octave"] = m_octaveSpin ? m_octaveSpin->value() : 4;
        song["volume"] = m_volumeSpin ? m_volumeSpin->value() : 15;
        song["channel"] = m_activeChannelCombo ? m_activeChannelCombo->currentIndex() : 0;
        song["step"] = m_stepSpin ? m_stepSpin->value() : 1;

        root["song"] = song;
        root["orderTable"] = tableToJson(m_orderTable);

        QMap<int, QJsonArray> patterns = m_soundPatterns;
        patterns[m_currentPatternIndex] = tableToJson(m_patternTable);

        QJsonObject patternsObj;
        for (auto it = patterns.constBegin(); it != patterns.constEnd(); ++it)
            patternsObj[QString("%1").arg(it.key(), 2, 16, QLatin1Char('0')).toUpper()] = it.value();

        root["patterns"] = patternsObj;

        // Backwards-compatible: keep current visible pattern as patternTable too.
        root["patternTable"] = tableToJson(m_patternTable);
        root["currentPattern"] = m_currentPatternIndex;
        root["instrumentsTable"] = tableToJson(m_instrumentsTable);
return root;
    }

    bool applySoundSongJson(const QJsonObject& root)
    {
        if (root.value("format").toString() != "ADAMP_CVBASIC_SOUND_SONG") {
            QMessageBox::warning(this, tr("Open Song"), tr("Not a valid ADAMP sound song file."));
            return false;
        }

        // Belangrijk bij songwissel:
        // eerst alles volledig isoleren, zodat deze song niets erft van de vorige.
        hardResetLoadedSongState();

        const QJsonObject song = root.value("song").toObject();

        if (m_songNameEdit) m_songNameEdit->setText(song.value("name").toString("Untitled"));
        if (m_authorEdit) m_authorEdit->setText(song.value("author").toString("CVBasic Dev"));
        if (m_tempoSpin) m_tempoSpin->setValue(song.value("tempo").toInt(125));
        if (m_speedSpin) m_speedSpin->setValue(song.value("speed").toInt(6));
        if (m_rowsSpin) {
            QSignalBlocker blocker(m_rowsSpin);
            m_rowsSpin->setValue(song.value("rows").toInt(16));
        }
        if (m_defaultInstrumentSpin) m_defaultInstrumentSpin->setValue(song.value("defaultInstrument").toInt(1));
        if (m_octaveSpin) m_octaveSpin->setValue(song.value("octave").toInt(4));
        if (m_volumeSpin) m_volumeSpin->setValue(song.value("volume").toInt(15));
        if (m_activeChannelCombo) m_activeChannelCombo->setCurrentIndex(qBound(0, song.value("channel").toInt(0), 3));
        if (m_stepSpin) m_stepSpin->setValue(song.value("step").toInt(1));

        jsonToTable(m_orderTable, root.value("orderTable").toArray());
        normalizeOrderTableRows();

        soundPatternsFromJson(root.value("patterns").toObject());

        // Kies na laden altijd de eerste pattern uit de Order List als zichtbare pattern.
        // currentPattern uit het bestand kan een laatst-bekeken pattern zijn, maar dat mag
        // playback/order-opbouw niet vervuilen.
        int firstPattern = 0;
        if (m_orderTable && m_orderTable->columnCount() > 0) {
            const int p0 = orderPatternAtColumn(0);
            if (p0 >= 0 && p0 < 255)
                firstPattern = p0;
        }

        m_currentPatternIndex = qBound(0, firstPattern, 255);

        if (m_patternSpin) {
            QSignalBlocker blocker(m_patternSpin);
            m_patternSpin->setValue(m_currentPatternIndex);
        }

        if (m_soundPatterns.contains(m_currentPatternIndex))
            jsonToTable(m_patternTable, m_soundPatterns.value(m_currentPatternIndex));
        else
            jsonToTable(m_patternTable, root.value("patternTable").toArray());

        saveCurrentPatternToMemory();

        const QJsonArray instrumentsJson = root.value("instrumentsTable").toArray();
        if (!instrumentsJson.isEmpty()) {
            jsonToTable(m_instrumentsTable, instrumentsJson);
            normalizeInstrumentTable();
            loadInstrumentIntoEditor(qBound(0, m_defaultInstrumentSpin ? m_defaultInstrumentSpin->value() : 1, m_instrumentsTable->rowCount() - 1));
        } else {
            resetDefaultInstrumentsTable();
        }
if (m_patternTable && m_rowSpin) {
            renumberPatternRows();
            m_rowSpin->setRange(0, qMax(0, m_patternTable->rowCount() - 1));
            m_rowSpin->setValue(0);
            m_patternTable->selectRow(0);
        }

        if (m_instrumentsTable && m_defaultInstrumentSpin)
            m_instrumentsTable->selectRow(qBound(0, m_defaultInstrumentSpin->value(), m_instrumentsTable->rowCount() - 1));

        debugDumpSoundState("AFTER applySoundSongJson", 8);

        rebuildOutput();
        return true;
    }

    bool writeSoundSongFile(const QString& filePath)
    {
        QFile f(filePath);

        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Save Song"), f.errorString());
            return false;
        }

        const QJsonDocument doc(buildSoundSongJson());
        f.write(doc.toJson(QJsonDocument::Indented));
        f.close();

        m_soundSongFilePath = filePath;

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText(QString("Saved song: %1").arg(QFileInfo(filePath).fileName()));

        return true;
    }

    void newSoundSong()
    {
        pushSoundUndoState();

        hardResetLoadedSongState();
        createEmptySoundSong();

        m_soundSongFilePath.clear();
        m_soundUndoStack.clear();
        m_soundRedoStack.clear();

        rebuildOutput();

        if (m_noteInfoLabel)
            m_noteInfoLabel->setText("New empty sound song");
    }

    void openSoundSong()
    {
        const QString filePath = QFileDialog::getOpenFileName(
            this,
            tr("Open Sound Song"),
            soundSongDefaultDir(),
            tr("ADAMP Sound Song (*.adpsnd *.json);;All Files (*.*)")
        );

        if (filePath.isEmpty())
            return;

        QFile f(filePath);

        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Open Song"), f.errorString());
            return;
        }

        const QByteArray data = f.readAll();
        f.close();

        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &err);

        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, tr("Open Song"), tr("Invalid JSON: %1").arg(err.errorString()));
            return;
        }

        qDebug().noquote() << "[ADAMP SOUND] openSoundSong file=" << filePath;

        if (applySoundSongJson(doc.object())) {
            m_soundSongFilePath = filePath;
            m_soundUndoStack.clear();
            m_soundRedoStack.clear();

            if (m_noteInfoLabel)
                m_noteInfoLabel->setText(QString("Opened song: %1").arg(QFileInfo(filePath).fileName()));
        }
    }

    void saveSoundSong()
    {
        if (m_soundSongFilePath.isEmpty()) {
            saveSoundSongAs();
            return;
        }

        writeSoundSongFile(m_soundSongFilePath);
    }

    void saveSoundSongAs()
    {
        QDir().mkpath(soundSongDefaultDir());

        const QString filePath = QFileDialog::getSaveFileName(
            this,
            tr("Save Sound Song As"),
            QDir(soundSongDefaultDir()).filePath("song.adpsnd"),
            tr("ADAMP Sound Song (*.adpsnd);;JSON (*.json);;All Files (*.*)")
        );

        if (filePath.isEmpty())
            return;

        QString finalPath = filePath;
        if (!finalPath.endsWith(".adpsnd", Qt::CaseInsensitive) &&
            !finalPath.endsWith(".json", Qt::CaseInsensitive)) {
            finalPath += ".adpsnd";
        }

        writeSoundSongFile(finalPath);
    }

    QString buildCvBasicPlayerInsert() const
    {
        return buildCompactCvBasicSongBlock(true);
    }

    void rebuildOutput()
    {
        if (!m_outputEdit)
            return;

        // Compacte output is nu de standaard: data één keer dumpen,
        // timing/WAIΤ zit centraal in de player wanneer Add Player aan staat.
        m_outputEdit->setPlainText(buildCompactCvBasicSongBlock(false));
    }

private:
    QLineEdit* m_songNameEdit = nullptr;
    QLineEdit* m_authorEdit = nullptr;
    QSpinBox* m_tempoSpin = nullptr;
    QSpinBox* m_speedSpin = nullptr;
    QSpinBox* m_rowsSpin = nullptr;
    QSpinBox* m_defaultInstrumentSpin = nullptr;
    QSpinBox* m_playPatternSpin = nullptr;
    QSpinBox* m_playFromSpin = nullptr;
    QSpinBox* m_patternSpin = nullptr;
    QSpinBox* m_rowSpin = nullptr;
    QSpinBox* m_stepSpin = nullptr;
    QSpinBox* m_octaveSpin = nullptr;
    QSpinBox* m_volumeSpin = nullptr;
    QComboBox* m_activeChannelCombo = nullptr;

    QTimer* m_playTimer = nullptr;
    QTimer* m_liveInstrumentRestartTimer = nullptr;
    QPushButton* m_playbackPlayBtn = nullptr;
    QPushButton* m_playbackStopBtn = nullptr;
    QPushButton* m_playbackRewindBtn = nullptr;
    QPushButton* m_playbackLoopBtn = nullptr;

    int m_playingRow = 0;
    int m_playingOrderColumn = 0;
    int m_playingPatternIndex = 0;
    bool m_isPatternPlaying = false;
    bool m_streamPlayerUiFollow = false;

    bool m_playbackHeldActive[4] = {false, false, false, false};
    int m_playbackHeldPeriod[4] = {0, 0, 0, 0};
    int m_playbackHeldVolume[4] = {0, 0, 0, 0};

    QCheckBox* m_loopSongCheck = nullptr;
    QCheckBox* m_followPlayCheck = nullptr;
    QCheckBox* m_autoUpdateCheck = nullptr;
    QCheckBox* m_addPlayerCheck = nullptr;
    QCheckBox* m_keyboardTestOnlyCheck = nullptr;
    QCheckBox* m_mirrorCheck = nullptr;
    QCheckBox* m_fillDownCheck = nullptr;

    QTableWidget* m_orderTable = nullptr;
    QTabWidget* m_editorTabs = nullptr;
    QTableWidget* m_patternTable = nullptr;
    SoundPatternDelegate* m_patternDelegate = nullptr;
    QTableWidget* m_instrumentsTable = nullptr;

    QLineEdit* m_instNameEdit = nullptr;
    QComboBox* m_instTypeCombo = nullptr;
    QSpinBox* m_instVolumeSpin = nullptr;
    QSlider* m_instVolumeSlider = nullptr;
    QSpinBox* m_instEnvSpin = nullptr;
    QSlider* m_instEnvSlider = nullptr;
    QSpinBox* m_instFadeoutSpin = nullptr;
    QSlider* m_instFadeoutSlider = nullptr;
    QSpinBox* m_instWaveXSpin = nullptr;
    QSlider* m_instWaveXSlider = nullptr;
    QSpinBox* m_instWaveYSpin = nullptr;
    QSlider* m_instWaveYSlider = nullptr;
    SoundInstrumentWavePreviewWidget* m_instWavePreview = nullptr;
    SoundInstrumentEnvelopePreviewWidget* m_instEnvPreview = nullptr;
    bool m_updatingInstrumentEditor = false;

    QPlainTextEdit* m_outputEdit = nullptr;
    QLabel* m_noteInfoLabel = nullptr;
    QLabel* m_playbackStatusLabel = nullptr;
    SoundVuLedBarWidget* m_vuLedBar = nullptr;
    QLabel* m_channelHeaderLabels[4] = {nullptr, nullptr, nullptr, nullptr};
    bool m_channelAudible[4] = {true, true, true, true}; // Runtime editor on/off only. Not saved/exported.
    int m_vuLevels[4] = {0, 0, 0, 0};

    QStringList m_patternClipboard;
    QJsonArray m_patternClipboardJson;
    QString m_soundSongFilePath;

    QMap<int, QJsonArray> m_soundPatterns;
    int m_currentPatternIndex = 0;
    bool m_loadingSoundPattern = false;

    QVector<QJsonObject> m_soundUndoStack;
    QVector<QJsonObject> m_soundRedoStack;
    bool m_restoringSoundUndo = false;
};



CvBasicEditorWindow::CvBasicEditorWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // Geen interne default-paden meer: settings.ini/MainWindow is de enige bron.
    setupUi();
    setupActions();
    setupMenusAndToolbar();
    setupStatusBar();
    loadSettings();
    updateWindowTitle();
    updateSidePanels();

    // Bij eerste openen: cursor naar laatste input / einde tekst.
    if (m_editor) {
        QTextCursor cursor = m_editor->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_editor->setTextCursor(cursor);
        m_editor->ensureCursorVisible();
        m_editor->setFocus(Qt::OtherFocusReason);
        updateCursorStatus();
         }
}

CvBasicEditorWindow::~CvBasicEditorWindow()
{
    saveSettings();
    delete m_printer;
    m_printer = nullptr;
}

void CvBasicEditorWindow::setSoundChannelVuLevel(int channel, int level)
{
    channel = qBound(0, channel, 3);
    level = qBound(0, level, 15);

    if (auto* soundPage = dynamic_cast<CvBasicSoundEditorPage*>(m_soundPage))
        soundPage->setSoundChannelVuLevel(channel, level);
}

void CvBasicEditorWindow::setSoundPreviewVuLevels(int ch1, int ch2, int ch3, int noise)
{
    if (auto* soundPage = dynamic_cast<CvBasicSoundEditorPage*>(m_soundPage))
        soundPage->setSoundPreviewVuLevels(ch1, ch2, ch3, noise);
}

void CvBasicEditorWindow::setToolPaths(const QString& cvbasicExe,
                                       const QString& gasm80Exe,
                                       const QString& buildDir,
                                       const QString& sourceDir)
{
    m_cvbasicExePath = QDir::cleanPath(cvbasicExe.trimmed());
    m_gasm80ExePath  = QDir::cleanPath(gasm80Exe.trimmed());
    m_buildDirPath   = QDir::cleanPath(buildDir.trimmed());
    m_lastOpenDir    = QDir::cleanPath(sourceDir.trimmed());

    qDebug() << "[CVBASIC] Paths from settings.ini/MainWindow:"
             << "cvbasic =" << m_cvbasicExePath
             << "gasm80 =" << m_gasm80ExePath
             << "build =" << m_buildDirPath
             << "source =" << m_lastOpenDir;
}

QPlainTextEdit* CvBasicEditorWindow::activeEditor() const
{
    if (m_codeTabs) {
        if (QPlainTextEdit* ed = qobject_cast<QPlainTextEdit*>(m_codeTabs->currentWidget()))
            return ed;
    }
    return m_editor;
}

QPlainTextEdit* CvBasicEditorWindow::sourceEditorAt(int index) const
{
    if (!m_codeTabs || index < 0 || index >= m_codeTabs->count())
        return nullptr;
    return qobject_cast<QPlainTextEdit*>(m_codeTabs->widget(index));
}

int CvBasicEditorWindow::sourceTabCount() const
{
    return m_codeTabs ? m_codeTabs->count() : (m_editor ? 1 : 0);
}

QString CvBasicEditorWindow::sourceTabName(int index) const
{
    if (!m_codeTabs || index < 0 || index >= m_codeTabs->count())
        return tr("Main");

    QString name = m_codeTabs->tabText(index);
    name.remove('*');
    return name.trimmed().isEmpty() ? QString("Tab%1").arg(index + 1) : name.trimmed();
}

void CvBasicEditorWindow::setSourceTabName(int index, const QString& name)
{
    if (!m_codeTabs || index < 0 || index >= m_codeTabs->count())
        return;

    QString clean = name.trimmed();
    if (clean.isEmpty())
        clean = QString("Tab%1").arg(index + 1);

    m_codeTabs->setTabText(index, clean);
}

void CvBasicEditorWindow::setupSourceEditorContextMenu(QPlainTextEdit* editor)
{
    if (!editor || !m_actFind)
        return;

    editor->setContextMenuPolicy(Qt::CustomContextMenu);
    editor->addAction(m_actFind);
    editor->addAction(m_actFindNext);
    editor->addAction(m_actReplace);
    editor->addAction(m_actViewFoldLines);
    editor->addAction(m_actViewConsole);
    editor->addAction(m_actViewShortcuts);
    editor->addAction(m_actResetConsole);

    connect(editor, &QWidget::customContextMenuRequested, this, [this, editor](const QPoint& pos) {
        if (!editor)
            return;

        if (m_codeTabs) {
            const int idx = m_codeTabs->indexOf(editor);
            if (idx >= 0)
                m_codeTabs->setCurrentIndex(idx);
        }
        m_editor = editor;

        QMenu* menu = editor->createStandardContextMenu();
        menu->addSeparator();
        menu->addAction(m_actNewTab);
        menu->addAction(m_actRenameTab);
        menu->addAction(m_actCloseTab);
        menu->addAction(m_actDeleteTab);
        menu->addSeparator();
        menu->addAction(m_actFind);
        menu->addAction(m_actFindNext);
        menu->addAction(m_actReplace);

        QMenu* basicViewMenu = menu->addMenu(tr("View"));
        basicViewMenu->addAction(m_actViewFoldLines);
        basicViewMenu->addAction(m_actViewConsole);
        basicViewMenu->addAction(m_actViewShortcuts);
        basicViewMenu->addSeparator();
        basicViewMenu->addAction(m_actResetConsole);

        menu->exec(editor->mapToGlobal(pos));
        delete menu;
    });
}

void CvBasicEditorWindow::setupAllSourceEditorContextMenus()
{
    if (!m_codeTabs)
        return;

    for (int i = 0; i < m_codeTabs->count(); ++i)
        setupSourceEditorContextMenu(sourceEditorAt(i));
}


void CvBasicEditorWindow::clearSourceTabs()
{
    if (!m_codeTabs)
        return;

    // Important: removing tabs emits currentChanged(). During a full project reset
    // that signal can make updateSidePanels()/updateCursorStatus() touch an editor
    // that is already being destroyed. Block it and clear our cached pointers first.
    QSignalBlocker blocker(m_codeTabs);

    m_editor = nullptr;
    m_lineNumberAreaWidget = nullptr;

    while (m_codeTabs->count() > 0) {
        QWidget* w = m_codeTabs->widget(0);
        m_codeTabs->removeTab(0);

        if (w) {
            w->removeEventFilter(this);
            w->disconnect(this);
            delete w;
        }
    }
}

void CvBasicEditorWindow::addSourceTab(const QString& name, const QString& text, bool makeCurrent)
{
    if (!m_codeTabs)
        return;

    CodeEditor* editor = new CodeEditor(m_codeTabs);
    editor->setObjectName("cvBasicSourceEditor");
    editor->setLineWrapMode(QPlainTextEdit::NoWrap);

    QFont f = m_editor ? m_editor->font() : font();
    editor->setFont(f);
    editor->setTabStopDistance(QFontMetrics(f).horizontalAdvance(' ') * 4);
    editor->setPlainText(text);

    new CvBasicHighlighter(editor->document());

    connect(editor, &QPlainTextEdit::textChanged,
            this, &CvBasicEditorWindow::onEditorTextChanged);
    connect(editor, &QPlainTextEdit::cursorPositionChanged,
            this, &CvBasicEditorWindow::updateCursorStatus);
    editor->installEventFilter(this);

    setupSourceEditorContextMenu(editor);

    const int index = m_codeTabs->addTab(editor, name.trimmed().isEmpty() ? QString("Tab%1").arg(m_codeTabs->count() + 1) : name.trimmed());
    if (makeCurrent)
        m_codeTabs->setCurrentIndex(index);

    // Force the gutter/layout immediately and once more after Qt has processed
    // the tab insertion. This fixes the first tab after project load; the second
    // tab already looked OK because switching tabs triggers this layout later.
    editor->relayoutLineNumberArea();
    QTimer::singleShot(0, editor, [editor]() { editor->forceLineNumberRefresh(); });
    QTimer::singleShot(50, editor, [editor]() { editor->forceLineNumberRefresh(); });
    QTimer::singleShot(150, editor, [editor]() { editor->forceLineNumberRefresh(); });

    m_editor = activeEditor();
    if (m_editor) {
        if (CodeEditor* codeEditor = dynamic_cast<CodeEditor*>(m_editor))
            m_lineNumberAreaWidget = codeEditor->lineNumberAreaWidget();
    }
}

void CvBasicEditorWindow::newSourceTab()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        tr("New Source Tab"),
        tr("Tab name:"),
        QLineEdit::Normal,
        QString("Tab%1").arg(sourceTabCount() + 1),
        &ok
    ).trimmed();

    if (!ok)
        return;

    addSourceTab(name.isEmpty() ? QString("Tab%1").arg(sourceTabCount() + 1) : name,
                 QString("REM %1\n").arg(name.isEmpty() ? QString("New tab") : name),
                 true);

    m_dirty = true;
    updateWindowTitle();
    updateSidePanels();
    updateStatusText(tr("Source tab added"));
}

void CvBasicEditorWindow::renameCurrentSourceTab()
{
    if (!m_codeTabs || m_codeTabs->count() <= 0)
        return;

    const int index = m_codeTabs->currentIndex();
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        tr("Rename Source Tab"),
        tr("Tab name:"),
        QLineEdit::Normal,
        sourceTabName(index),
        &ok
    ).trimmed();

    if (!ok || name.isEmpty())
        return;

    setSourceTabName(index, name);
    m_dirty = true;
    updateWindowTitle();
    updateStatusText(tr("Source tab renamed"));
}

void CvBasicEditorWindow::closeCurrentSourceTab()
{
    if (!m_codeTabs || m_codeTabs->count() <= 1) {
        QMessageBox::information(this, tr("Close Tab"), tr("At least one source tab must remain."));
        return;
    }

    const int index = m_codeTabs->currentIndex();
    const QString name = sourceTabName(index);

    const QMessageBox::StandardButton ret = QMessageBox::question(
        this,
        tr("Close Tab"),
        tr("Remove source tab '%1' from this project?").arg(name),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (ret != QMessageBox::Yes)
        return;

    QWidget* w = m_codeTabs->widget(index);
    {
        QSignalBlocker blocker(m_codeTabs);
        m_codeTabs->removeTab(index);
    }

    if (w) {
        w->removeEventFilter(this);
        w->disconnect(this);
        delete w;
    }

    if (m_codeTabs->count() > 0)
        m_codeTabs->setCurrentIndex(qMin(index, m_codeTabs->count() - 1));

    m_editor = activeEditor();
    m_dirty = true;
    updateWindowTitle();
    updateSidePanels();
    updateCursorStatus();
    updateStatusText(tr("Source tab removed"));
}

void CvBasicEditorWindow::deleteCurrentSourceTab()
{
    closeCurrentSourceTab();
}

QString CvBasicEditorWindow::buildCombinedSource()
{
    QString out;
    m_buildLineMap.clear();

    auto appendMappedLine = [&](const QString& line, int tabIndex, int localLine, const QString& tabName) {
        out += line;
        out += '\n';

        BuildLineInfo info;
        info.tabIndex = tabIndex;
        info.localLine = localLine;
        info.tabName = tabName;
        m_buildLineMap.append(info);
    };

    appendMappedLine("REM ==================================================", -1, -1, QString());
    appendMappedLine("REM ADAMP CVBasic project build", -1, -1, QString());
    appendMappedLine("REM Generated from source tabs, left to right", -1, -1, QString());
    appendMappedLine("REM ==================================================", -1, -1, QString());
    appendMappedLine(QString(), -1, -1, QString());

    if (m_codeTabs) {
        for (int i = 0; i < m_codeTabs->count(); ++i) {
            QPlainTextEdit* ed = sourceEditorAt(i);
            if (!ed)
                continue;

            const QString tabName = sourceTabName(i);
            appendMappedLine(QString("REM ===== TAB: %1 =====").arg(tabName), -1, -1, QString());

            const QString text = ed->toPlainText();
            const QStringList lines = text.split('\n', Qt::KeepEmptyParts);

            for (int local = 0; local < lines.size(); ++local) {
                QString line = lines.at(local);
                if (line.endsWith('\r'))
                    line.chop(1);
                appendMappedLine(line, i, local + 1, tabName);
            }

            appendMappedLine(QString(), -1, -1, QString());
        }
    } else if (m_editor) {
        const QString tabName = tr("Main");
        const QStringList lines = m_editor->toPlainText().split('\n', Qt::KeepEmptyParts);
        for (int local = 0; local < lines.size(); ++local) {
            QString line = lines.at(local);
            if (line.endsWith('\r'))
                line.chop(1);
            appendMappedLine(line, 0, local + 1, tabName);
        }
    }

    return out;
}

CvBasicEditorWindow::BuildLineInfo CvBasicEditorWindow::sourceLineForCombinedLine(int combinedLine) const
{
    if (combinedLine <= 0 || combinedLine > m_buildLineMap.size())
        return BuildLineInfo();

    return m_buildLineMap.at(combinedLine - 1);
}

bool CvBasicEditorWindow::writeCombinedSourceForBuild()
{
    if (m_buildSourcePath.isEmpty())
        return false;

    QFile file(m_buildSourcePath);
    QDir().mkpath(QFileInfo(m_buildSourcePath).absolutePath());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Build failed"), file.errorString());
        return false;
    }

    file.write(buildCombinedSource().toUtf8());
    file.close();
    return true;
}


void CvBasicEditorWindow::setupUi()
{
    setWindowTitle("CVBasic Editor");

    // Fixed window size for the CVBasic Editor.
    // Pas deze twee waarden aan als je later toch groter/kleiner wil.
    const int fixedWindowWidth = 1350;
    const int fixedWindowHeight = 950;
    setFixedSize(fixedWindowWidth, fixedWindowHeight);

    // Zelfde font als de emulator-dialogs: resource font luculent.ttf.
    // De menubalk laten we ongemoeid; die behoudt zijn bestaande font.
    QString emulatorFontFamily = "Roboto";
    const int luculentFontId = QFontDatabase::addApplicationFont(":/fonts/fonts/luculent.ttf");
    if (luculentFontId != -1) {
        const QStringList families = QFontDatabase::applicationFontFamilies(luculentFontId);
        if (!families.isEmpty())
            emulatorFontFamily = families.first();
    }

    QFont uiFont(emulatorFontFamily, 10);
    uiFont.setBold(false);

    // Source/console now also use Luculent, as requested.
    QFont codeFont(emulatorFontFamily, 10);
    codeFont.setBold(false);

    m_basicPage = new QWidget(this);
    QWidget* central = m_basicPage;
    QVBoxLayout* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(6);

    QSplitter* verticalSplitter = new QSplitter(Qt::Vertical, central);
    verticalSplitter->setChildrenCollapsible(false);

    QSplitter* topSplitter = new QSplitter(Qt::Horizontal, verticalSplitter);
    topSplitter->setChildrenCollapsible(false);

    // Links: project source-tabs + help.
    m_tabs = new QTabWidget(topSplitter);
    m_tabs->setDocumentMode(false);
    m_tabs->setMovable(false);

    m_codeTabs = new QTabWidget(m_tabs);
    m_codeTabs->setObjectName("cvBasicSourceTabs");
    m_codeTabs->setDocumentMode(false);
    m_codeTabs->setMovable(true);
    m_codeTabs->setTabsClosable(true);

    connect(m_codeTabs, &QTabWidget::currentChanged, this, [this](int) {
        m_editor = activeEditor();
        if (m_editor) {
            if (CodeEditor* codeEditor = dynamic_cast<CodeEditor*>(m_editor)) {
                m_lineNumberAreaWidget = codeEditor->lineNumberAreaWidget();
                codeEditor->forceLineNumberRefresh();
                QTimer::singleShot(0, codeEditor, [codeEditor]() { codeEditor->forceLineNumberRefresh(); });
            }
            m_editor->setFocus(Qt::OtherFocusReason);
        }
        updateSidePanels();
        updateCursorStatus();
    });

    connect(m_codeTabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (!m_codeTabs || index < 0 || index >= m_codeTabs->count())
            return;
        m_codeTabs->setCurrentIndex(index);
        closeCurrentSourceTab();
    });

    addSourceTab(tr("Main"),
        "'CVBasic test program for ADAM+ / ColecoVision\n",
        true
    );

    if (m_editor) {
        QTextCursor cursor = m_editor->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_editor->setTextCursor(cursor);
        m_editor->ensureCursorVisible();
    }

    m_helpText = new QPlainTextEdit(m_tabs);
    m_helpText->setObjectName("cvBasicHelpText");
    m_helpText->setReadOnly(true);
    m_helpText->setFont(codeFont);

#if defined(Q_OS_WIN)
    m_helpText->setPlainText(
        "CVBasic Editor shortcuts\n"
        "\n"
        "F7  = Compile project\n"
        "F5  = Compile project && Run\n"
        "Ctrl+S = Save project\n"
        "Ctrl+O = Open project\n"
        "\n"
        "Project source-tabs are compiled from left to right into one temporary .bas file.\n"
        "Keep MODE/CLS/main loop normally in the Main tab, and use other tabs for DATA/procedures.\n"
        "\n"
        "Tools expected next to the emulator executable:\n"
        "tools/cvbasic/cvbasic.exe\n"
        "tools/cvbasic/gasm80.exe\n"
    );
#endif
#if defined(Q_OS_LINUX)
    m_helpText->setPlainText(
        "CVBasic Editor shortcuts\n"
        "\n"
        "F7  = Compile project\n"
        "F5  = Compile project && Run\n"
        "Ctrl+S = Save project\n"
        "Ctrl+O = Open project\n"
        "\n"
        "Project source-tabs are compiled from left to right into one temporary .bas file.\n"
        "Keep MODE/CLS/main loop normally in the Main tab, and use other tabs for DATA/procedures.\n"
        "\n"
        "Tools expected next to the emulator executable:\n"
        "tools/cvbasic/cvbasic_linux\n"
        "tools/cvbasic/gasm80_linux\n"
    );
#endif

    m_tabs->addTab(m_codeTabs, tr("Untitled.adpcvb"));
    m_tabs->addTab(m_helpText, tr("Help"));

    // Rechts: Labels + Procedures.
    m_sidePanel = new QWidget(topSplitter);
    m_sidePanel->setObjectName("cvBasicSidePanel");
    QVBoxLayout* sideLayout = new QVBoxLayout(m_sidePanel);
    sideLayout->setContentsMargins(4, 4, 4, 4);
    sideLayout->setSpacing(6);

    QLabel* lblLabels = new QLabel(tr("Labels"), m_sidePanel);
    lblLabels->setObjectName("panelTitle");
    m_labelsList = new QListWidget(m_sidePanel);
    m_labelsList->setObjectName("cvBasicLabelsList");

    QLabel* lblProcedures = new QLabel(tr("Procedures"), m_sidePanel);
    lblProcedures->setObjectName("panelTitle");
    m_proceduresList = new QListWidget(m_sidePanel);
    m_proceduresList->setObjectName("cvBasicProceduresList");

    sideLayout->addWidget(lblLabels);
    sideLayout->addWidget(m_labelsList, 1);
    sideLayout->addWidget(lblProcedures);
    sideLayout->addWidget(m_proceduresList, 1);

    topSplitter->addWidget(m_tabs);
    topSplitter->addWidget(m_sidePanel);
    topSplitter->setStretchFactor(0, 1);
    topSplitter->setStretchFactor(1, 0);
    topSplitter->setSizes({920, 230});

    // Onder: Console + Errors.
    m_bottomPanel = new QWidget(verticalSplitter);
    m_bottomPanel->setObjectName("cvBasicBottomPanel");
    QVBoxLayout* bottomLayout = new QVBoxLayout(m_bottomPanel);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(4);

    QLabel* consoleTitle = new QLabel(tr("Console"), m_bottomPanel);
    consoleTitle->setObjectName("panelTitle");

    QSplitter* bottomSplitter = new QSplitter(Qt::Horizontal, m_bottomPanel);
    bottomSplitter->setChildrenCollapsible(false);

    m_output = new QPlainTextEdit(bottomSplitter);
    m_output->setObjectName("cvBasicBuildOutput");
    m_output->setReadOnly(true);
    m_output->setFont(codeFont);

    QWidget* errorBox = new QWidget(bottomSplitter);
    errorBox->setObjectName("cvBasicErrorBox");
    QVBoxLayout* errorLayout = new QVBoxLayout(errorBox);
    errorLayout->setContentsMargins(4, 4, 4, 4);
    errorLayout->setSpacing(4);

    QHBoxLayout* countLayout = new QHBoxLayout();
    countLayout->setContentsMargins(0, 0, 0, 0);
    m_errorsLabel = new QLabel(tr("● 0 Errors"), errorBox);
    m_errorsLabel->setObjectName("errorCounter");
    m_warningsLabel = new QLabel(tr("● 0 Warnings"), errorBox);
    m_warningsLabel->setObjectName("warningCounter");
    countLayout->addWidget(m_errorsLabel);
    countLayout->addWidget(m_warningsLabel);
    countLayout->addStretch(1);

    m_errorTable = new QTableWidget(errorBox);
    m_errorTable->setObjectName("cvBasicErrorTable");
    m_errorTable->setColumnCount(3);
    m_errorTable->setHorizontalHeaderLabels({tr("Description"), tr("Tab"), tr("Line")});
    m_errorTable->horizontalHeader()->setStretchLastSection(false);
    m_errorTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_errorTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_errorTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_errorTable->verticalHeader()->hide();
    m_errorTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_errorTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_errorTable->setAlternatingRowColors(true);

    connect(m_errorTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (!m_errorTable || row < 0 || row >= m_errorTable->rowCount())
            return;

        QTableWidgetItem* lineItem = m_errorTable->item(row, 2);
        if (!lineItem)
            return;

        const int tabIndex = lineItem->data(Qt::UserRole + 1).toInt();
        const int localLine = lineItem->data(Qt::UserRole + 2).toInt();

        if (m_codeTabs && tabIndex >= 0 && tabIndex < m_codeTabs->count())
            m_codeTabs->setCurrentIndex(tabIndex);

        gotoSourceLine(localLine);
    });

    errorLayout->addLayout(countLayout);
    errorLayout->addWidget(m_errorTable, 1);

    bottomSplitter->addWidget(m_output);
    bottomSplitter->addWidget(errorBox);
    bottomSplitter->setSizes({560, 440});

    bottomLayout->addWidget(consoleTitle);
    bottomLayout->addWidget(bottomSplitter, 1);

    verticalSplitter->addWidget(topSplitter);
    verticalSplitter->addWidget(m_bottomPanel);
    verticalSplitter->setStretchFactor(0, 1);
    verticalSplitter->setStretchFactor(1, 0);
    verticalSplitter->setSizes({560, 180});

    rootLayout->addWidget(verticalSplitter, 1);
    m_mainPages = new QTabWidget(this);
    m_mainPages->tabBar()->hide();
    m_mainPages->setDocumentMode(true);
    m_mainPages->addTab(m_basicPage, tr("BASIC"));

    QSettings pluginSettings(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
    pluginSettings.beginGroup("cvbasic");
    QString spriteSourceDir = pluginSettings.value("spriteSourceDir").toString().trimmed();
    pluginSettings.endGroup();
    if (spriteSourceDir.isEmpty())
        spriteSourceDir = QDir(QCoreApplication::applicationDirPath()).filePath("media/cvbasic/source");
    spriteSourceDir = QDir::cleanPath(spriteSourceDir);

    CvBasicSpriteDialog* spriteWidget = new CvBasicSpriteDialog(spriteSourceDir, m_mainPages);
    spriteWidget->onInsertRequested = [this](const QString& text) {
        const QString data = text.trimmed();
        QPlainTextEdit* ed = activeEditor();
        if (!data.isEmpty() && ed) {
            QTextCursor cursor = ed->textCursor();

            if (!cursor.atBlockStart())
                cursor.insertText("\n");

            cursor.insertText(data + "\n");
            ed->setTextCursor(cursor);

            refreshBasicEditorLayout();

            ed->setFocus(Qt::OtherFocusReason);

            m_dirty = true;
            updateWindowTitle();
            updateSidePanels();
            updateStatusText(tr("Sprite DATA inserted"));
        }

        if (m_mainPages && m_basicPage)
            m_mainPages->setCurrentWidget(m_basicPage);

        refreshBasicEditorLayout();
    };

    m_spritePage = spriteWidget;
    m_mainPages->addTab(m_spritePage, tr("SPRITES"));

    CvBasicSoundEditorPage* soundWidget = new CvBasicSoundEditorPage(m_mainPages);
    soundWidget->onInsertRequested = [this](const QString& text) {
        const QString data = text.trimmed();
        QPlainTextEdit* ed = activeEditor();
        if (!data.isEmpty() && ed) {
            QTextCursor cursor = ed->textCursor();

            if (!cursor.atBlockStart())
                cursor.insertText("\n");

            cursor.insertText(data + "\n");
            ed->setTextCursor(cursor);

            refreshBasicEditorLayout();

            ed->setFocus(Qt::OtherFocusReason);

            m_dirty = true;
            updateWindowTitle();
            updateSidePanels();
            updateStatusText(tr("Sound DATA inserted"));
        }

        if (m_mainPages && m_basicPage)
            m_mainPages->setCurrentWidget(m_basicPage);

        refreshBasicEditorLayout();
    };

    soundWidget->onPreviewNoteRequested = [this](int channel, int psgPeriod, int volume, int instrumentEnv, int waveX, int waveY) {
        emit soundPreviewRequested(channel, psgPeriod, volume, instrumentEnv, waveX, waveY);

        const QString chName = (channel == 3)
            ? QStringLiteral("Noise")
            : QString("CH%1").arg(channel + 1);

        updateStatusText(tr("Sound preview %1 PSG=%2 VOL=%3 ENV=%4 WX=%5 WY=%6")
                             .arg(chName)
                             .arg(psgPeriod)
                             .arg(volume)
                             .arg(instrumentEnv)
                             .arg(waveX)
                             .arg(waveY));
    };

    soundWidget->onStopAllPreviewRequested = [this]() {
        emit soundPreviewStopAllRequested();
        updateStatusText(tr("Sound preview stopped"));
    };

    soundWidget->onStreamPlayRequested = [this](const QVariantList& rows, int rowMs, bool loop) {
        emit soundEditorStreamPlayRequested(rows, rowMs, loop);
        updateStatusText(tr("Sound stream started: %1 rows%2")
                             .arg(rows.size())
                             .arg(loop ? tr(" (loop)") : QString()));
    };

    soundWidget->onStreamStopRequested = [this]() {
        emit soundEditorStreamStopRequested();
        updateStatusText(tr("Sound stream stopped"));
    };

    m_soundPage = soundWidget;
    m_mainPages->addTab(m_soundPage, tr("SOUND"));

    CvBasicPaintEditorPage* paintWidget = new CvBasicPaintEditorPage(m_mainPages);
    paintWidget->onInsertRequested = [this](const QString& text) {
        const QString data = text.trimmed();
        QPlainTextEdit* ed = activeEditor();
        if (!data.isEmpty() && ed) {
            QTextCursor cursor = ed->textCursor();

            if (!cursor.atBlockStart())
                cursor.insertText("\n");

            cursor.insertText(data + "\n");
            ed->setTextCursor(cursor);

            refreshBasicEditorLayout();
            ed->setFocus(Qt::OtherFocusReason);

            m_dirty = true;
            updateWindowTitle();
            updateSidePanels();
            updateStatusText(tr("Paint DATA inserted"));
        }

        if (m_mainPages && m_basicPage)
            m_mainPages->setCurrentWidget(m_basicPage);

        refreshBasicEditorLayout();
    };
    paintWidget->onStatusRequested = [this](const QString& text) {
        updateStatusText(text);
    };
    paintWidget->onTitleChanged = [this, paintWidget](const QString& title) {
        const int index = m_mainPages ? m_mainPages->indexOf(paintWidget) : -1;
        if (index >= 0)
            m_mainPages->setTabText(index, tr("PAINT - %1").arg(title));
    };

    m_paintPage = paintWidget;
    m_mainPages->addTab(m_paintPage, tr("PAINT"));
    paintWidget->refreshPaintProjectTitle();

    setCentralWidget(m_mainPages);

    m_highlighter = nullptr; // Source tabs create their own highlighter per document.

    // Algemene editor-window widgets in emulator font.
    // Niet de menubalk aanpassen: die moet zijn eigen bestaande font behouden.
    central->setFont(uiFont);
    m_tabs->setFont(uiFont);
    if (m_codeTabs)
        m_codeTabs->setFont(uiFont);
    m_labelsList->setFont(uiFont);
    m_proceduresList->setFont(uiFont);
    m_errorTable->setFont(uiFont);
    m_errorsLabel->setFont(uiFont);
    m_warningsLabel->setFont(uiFont);

    // Source/console/help krijgen ook luculent.
    if (m_codeTabs) {
        for (int i = 0; i < m_codeTabs->count(); ++i) {
            if (QPlainTextEdit* ed = sourceEditorAt(i)) {
                ed->setFont(codeFont);
                ed->setTabStopDistance(QFontMetrics(codeFont).horizontalAdvance(' ') * 4);
            }
        }
    }
    if (m_editor)
        m_editor->setFont(codeFont);
    m_helpText->setFont(codeFont);
    m_output->setFont(codeFont);

    // Emulator/settings look:
    // - Panels/tabs: #3A3A3A
    // - Alle "zwarte" inhoud: #242424
    setStyleSheet(
        "QMainWindow { background-color: #3A3A3A; color: #FFFFFF; }"
        "QWidget { background-color: #3A3A3A; color: #FFFFFF; }"

        "QMenuBar { background-color: #3A3A3A; color: #FFFFFF; border-bottom: 1px solid #555555; }"
        "QMenuBar::item { background: transparent; padding: 4px 8px; }"
        "QMenuBar::item:selected { background-color: #4A4A4A; }"
        "QMenu { background-color: #3A3A3A; color: #FFFFFF; border: 1px solid #1E1E1E; }"
        "QMenu::item { padding: 5px 24px 5px 20px; }"
        "QMenu::item:selected { background-color: #4A4A4A; }"

        "QToolBar { background-color: #3A3A3A; border: 1px solid #555555; spacing: 4px; padding: 4px; }"
        "QToolBar#cvBasicPageToolbar { background-color: #3A3A3A; border: 1px solid #555555; spacing: 4px; padding: 4px; }"
        "QToolButton { background-color: #242424; color: #FFFFFF; border: 1px solid #5C5C5C; padding: 4px 8px; }"
        "QToolButton:hover { background-color: #4A4A4A; }"
        "QToolButton:pressed { background-color: #5A5A5A; padding-top: 5px; padding-left: 9px; }"

        "QTabWidget::pane { border: 1px solid #555555; background-color: #3A3A3A; top: -1px; }"
        "QTabBar::tab {"
        "  background-color: #242424;"
        "  color: #FFFFFF;"
        "  border: 1px solid #6A6A6A;"
        "  border-bottom: none;"
        "  padding: 5px 12px;"
        "  min-width: 70px;"
        "  margin-right: 2px;"
        "}"
        "QTabBar::tab:selected { background-color: #3A3A3A; color: #FFFFFF; }"
        "QTabBar::tab:!selected { margin-top: 2px; }"

        "QSplitter::handle { background-color: #242424; border: 1px solid #555555; }"

        "QPlainTextEdit#cvBasicSourceEditor {"
        "  background-color: #242424;"
        "  color: #F0F0F0;"
        "  border: 1px solid #5C5C5C;"
        "  selection-background-color: #6C63FF;"
        "  selection-color: #FFFFFF;"
        "}"
        "QPlainTextEdit#cvBasicHelpText,"
        "QPlainTextEdit#cvBasicBuildOutput {"
        "  background-color: #242424;"
        "  color: #E0E0E0;"
        "  border: 1px solid #5C5C5C;"
        "  selection-background-color: #6C63FF;"
        "  selection-color: #FFFFFF;"
        "}"

        "QWidget#cvBasicSidePanel, QWidget#cvBasicBottomPanel, QWidget#cvBasicErrorBox {"
        "  background-color: #3A3A3A;"
        "}"

        "QLabel#panelTitle {"
        "  background-color: #3A3A3A;"
        "  color: #FFFFFF;"
        "  border: 1px solid #555555;"
        "  padding: 3px 6px;"
        "}"

        "QLabel#errorCounter {"
        "  background-color: #242424;"
        "  color: #FF5C5C;"
        "  border: 1px solid #5C5C5C;"
        "  padding: 2px 8px;"
        "}"
        "QLabel#warningCounter {"
        "  background-color: #242424;"
        "  color: #FFE082;"
        "  border: 1px solid #5C5C5C;"
        "  padding: 2px 8px;"
        "}"

        "QListWidget, QTableWidget {"
        "  background-color: #242424;"
        "  color: #FFFFFF;"
        "  border: 1px solid #5C5C5C;"
        "  alternate-background-color: #2C2C2C;"
        "  gridline-color: #555555;"
        "  selection-background-color: #6C63FF;"
        "  selection-color: #FFFFFF;"
        "}"
        "QHeaderView::section {"
        "  background-color: #3A3A3A;"
        "  color: #FFFFFF;"
        "  border: 1px solid #555555;"
        "  padding: 3px;"
        "}"

        "QLabel { background: transparent; color: #FFFFFF; }"
        "QLineEdit { background-color: #242424; color: #FFFFFF; border: 1px solid #5C5C5C; padding: 3px 6px; }"

        "QScrollBar:vertical { background-color: #242424; width: 14px; margin: 0px; }"
        "QScrollBar::handle:vertical { background-color: #5A5A5A; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { background: none; border: none; height: 0px; }"
        "QScrollBar:horizontal { background-color: #242424; height: 14px; margin: 0px; }"
        "QScrollBar::handle:horizontal { background-color: #5A5A5A; min-width: 20px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { background: none; border: none; width: 0px; }"

        "QStatusBar { background-color: #3A3A3A; color: #FFFFFF; border-top: 1px solid #555555; }"
    );

    connect(m_labelsList, &QListWidget::itemDoubleClicked,
            this, &CvBasicEditorWindow::onLabelItemDoubleClicked);

    connect(m_proceduresList, &QListWidget::itemDoubleClicked,
            this, &CvBasicEditorWindow::onProcedureItemDoubleClicked);
}

void CvBasicEditorWindow::setupActions()
{
    m_actNew = new QAction(tr("New"), this);
    m_actNew->setShortcut(QKeySequence::New);
    connect(m_actNew, &QAction::triggered, this, &CvBasicEditorWindow::newFile);

    m_actOpen = new QAction(tr("Open..."), this);
    m_actOpen->setShortcut(QKeySequence::Open);
    connect(m_actOpen, &QAction::triggered, this, &CvBasicEditorWindow::openFile);

    m_actSave = new QAction(tr("Save"), this);
    m_actSave->setShortcut(QKeySequence::Save);
    connect(m_actSave, &QAction::triggered, this, &CvBasicEditorWindow::saveFile);

    m_actSaveAs = new QAction(tr("Save As..."), this);
    m_actSaveAs->setShortcut(QKeySequence::SaveAs);
    connect(m_actSaveAs, &QAction::triggered, this, &CvBasicEditorWindow::saveFileAs);

    m_actNewTab = new QAction(tr("New Tab"), this);
    connect(m_actNewTab, &QAction::triggered, this, &CvBasicEditorWindow::newSourceTab);

    m_actRenameTab = new QAction(tr("Rename Tab"), this);
    connect(m_actRenameTab, &QAction::triggered, this, &CvBasicEditorWindow::renameCurrentSourceTab);

    m_actCloseTab = new QAction(tr("Close Tab"), this);
    connect(m_actCloseTab, &QAction::triggered, this, &CvBasicEditorWindow::closeCurrentSourceTab);

    m_actDeleteTab = new QAction(tr("Delete Tab"), this);
    connect(m_actDeleteTab, &QAction::triggered, this, &CvBasicEditorWindow::deleteCurrentSourceTab);


    m_actPrint = new QAction(tr("Print..."), this);
    m_actPrint->setShortcut(QKeySequence::Print);
    connect(m_actPrint, &QAction::triggered, this, &CvBasicEditorWindow::printSource);



    m_actUndo = new QAction(tr("Undo"), this);
    m_actUndo->setShortcut(QKeySequence::Undo);
    connect(m_actUndo, &QAction::triggered, this, &CvBasicEditorWindow::editUndo);

    m_actRedo = new QAction(tr("Redo"), this);
    m_actRedo->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Z));
    connect(m_actRedo, &QAction::triggered, this, &CvBasicEditorWindow::editRedo);

    m_actCut = new QAction(tr("Cut"), this);
    m_actCut->setShortcut(QKeySequence::Cut);
    connect(m_actCut, &QAction::triggered, this, &CvBasicEditorWindow::editCut);

    m_actCopy = new QAction(tr("Copy"), this);
    m_actCopy->setShortcut(QKeySequence::Copy);
    connect(m_actCopy, &QAction::triggered, this, &CvBasicEditorWindow::editCopy);

    m_actPaste = new QAction(tr("Paste"), this);
    m_actPaste->setShortcut(QKeySequence::Paste);
    connect(m_actPaste, &QAction::triggered, this, &CvBasicEditorWindow::editPaste);

    m_actFind = new QAction(tr("Find"), this);
    m_actFind->setShortcut(QKeySequence::Find);
    connect(m_actFind, &QAction::triggered, this, &CvBasicEditorWindow::findText);

    m_actFindNext = new QAction(tr("Find Next"), this);
    m_actFindNext->setShortcut(QKeySequence(Qt::Key_F3));
    connect(m_actFindNext, &QAction::triggered, this, &CvBasicEditorWindow::findNext);

    m_actReplace = new QAction(tr("Replace..."), this);
    m_actReplace->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    connect(m_actReplace, &QAction::triggered, this, &CvBasicEditorWindow::replaceText);

    m_actViewFoldLines = new QAction(tr("Fold Lines"), this);
    m_actViewFoldLines->setCheckable(true);
    m_actViewFoldLines->setChecked(true);
    connect(m_actViewFoldLines, &QAction::toggled, this, &CvBasicEditorWindow::toggleFoldLines);

    m_actViewConsole = new QAction(tr("Console"), this);
    m_actViewConsole->setCheckable(true);
    m_actViewConsole->setChecked(true);
    connect(m_actViewConsole, &QAction::toggled, this, &CvBasicEditorWindow::toggleConsole);

    m_actViewShortcuts = new QAction(tr("Shortcuts"), this);
    m_actViewShortcuts->setCheckable(true);
    m_actViewShortcuts->setChecked(true);
    connect(m_actViewShortcuts, &QAction::toggled, this, &CvBasicEditorWindow::toggleShortcuts);

    m_actResetConsole = new QAction(tr("Reset Console"), this);
    connect(m_actResetConsole, &QAction::triggered, this, &CvBasicEditorWindow::resetConsole);

    m_actCompile = new QAction(tr("Compile"), this);
    m_actCompile->setShortcut(QKeySequence(Qt::Key_F7));
    connect(m_actCompile, &QAction::triggered, this, &CvBasicEditorWindow::compileOnly);

    m_actCompileRun = new QAction(tr("Compile && Run"), this);
    m_actCompileRun->setShortcut(QKeySequence(Qt::Key_F5));
    connect(m_actCompileRun, &QAction::triggered, this, &CvBasicEditorWindow::compileAndRun);

    m_actBasicEditor = new QAction(tr("Basic Editor"), this);
    connect(m_actBasicEditor, &QAction::triggered, this, &CvBasicEditorWindow::showBasicEditor);

    m_actSpriteEditor = new QAction(tr("Sprite Editor"), this);
    connect(m_actSpriteEditor, &QAction::triggered, this, &CvBasicEditorWindow::showSpriteEditor);

    m_actSoundEditor = new QAction(tr("Sound Editor"), this);
    connect(m_actSoundEditor, &QAction::triggered, this, &CvBasicEditorWindow::showSoundEditor);

    m_actPaintEditor = new QAction(tr("Graphics Editor"), this);
    connect(m_actPaintEditor, &QAction::triggered, this, &CvBasicEditorWindow::showPaintEditor);

    #if defined(Q_OS_WIN)
    m_actChooseCvBasic = new QAction(tr("Set cvbasic.exe..."), this);
#else
    m_actChooseCvBasic = new QAction(tr("Set cvbasic_linux..."), this);
#endif
    connect(m_actChooseCvBasic, &QAction::triggered, this, &CvBasicEditorWindow::chooseCvBasicExe);

    #if defined(Q_OS_WIN)
    m_actChooseGasm80 = new QAction(tr("Set gasm80.exe..."), this);
#else
    m_actChooseGasm80 = new QAction(tr("Set gasm80_linux..."), this);
#endif
    connect(m_actChooseGasm80, &QAction::triggered, this, &CvBasicEditorWindow::chooseGasm80Exe);

    m_actOpenBuildFolder = new QAction(tr("Open Build Folder"), this);
    connect(m_actOpenBuildFolder, &QAction::triggered, this, &CvBasicEditorWindow::openBuildFolder);

    m_actAbout = new QAction(tr("About"), this);
    connect(m_actAbout, &QAction::triggered, this, &CvBasicEditorWindow::showAboutDialog);
}

void CvBasicEditorWindow::setupMenusAndToolbar()
{
    // Geen global Edit/View menu: BASIC edit + view opties zitten in de editor-popup.
    setupAllSourceEditorContextMenus();

    QMenu* toolsMenu = menuBar()->addMenu(tr("PLUG-INS"));
    toolsMenu->addAction(m_actBasicEditor);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_actSpriteEditor);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_actSoundEditor);
    toolsMenu->addSeparator();
    toolsMenu->addAction(m_actPaintEditor);

    QMenu* helpMenu = menuBar()->addMenu(tr("Help"));
    helpMenu->addAction(m_actAbout);

    // CVBasic project-toolbar op de BASIC page zelf.
    QToolBar* tb = new QToolBar(tr("CVBasic"), m_basicPage ? m_basicPage : this);
    tb->setObjectName("cvBasicPageToolbar");
    tb->setMovable(false);
    tb->setIconSize(QSize(20, 20));
    tb->setFloatable(false);

    tb->addAction(m_actNew);
    tb->addAction(m_actOpen);
    tb->addAction(m_actSave);
    tb->addAction(m_actSaveAs);
    tb->addSeparator();

    tb->addAction(m_actNewTab);
    tb->addAction(m_actRenameTab);
    tb->addAction(m_actCloseTab);
    tb->addSeparator();

    tb->addAction(m_actPrint);
    tb->addSeparator();

    tb->addAction(m_actCompile);
    tb->addAction(m_actCompileRun);

    QWidget* tbSpacer = new QWidget(tb);
    tbSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(tbSpacer);

    tb->addAction(m_actUndo);
    tb->addAction(m_actRedo);

    if (m_basicPage && m_basicPage->layout()) {
        if (QVBoxLayout* basicLayout = qobject_cast<QVBoxLayout*>(m_basicPage->layout())) {
            basicLayout->insertWidget(0, tb);
        } else {
            tb->setParent(this);
            addToolBar(tb);
        }
    } else {
        addToolBar(tb);
    }
}


void CvBasicEditorWindow::setupStatusBar()
{
    m_cursorStatusLabel = new QLabel(tr("Ln 1, Col 1"), this);
    m_cursorStatusLabel->setObjectName("cursorStatusLabel");
    m_cursorStatusLabel->setMinimumWidth(120);

    m_statusLabel = new QLabel(tr("Ready"), this);
    m_statusLabel->setObjectName("editorStatusLabel");

    m_capsLabel = new QLabel(tr("CAPS"), this);
    m_numLabel  = new QLabel(tr("NUM"), this);
    m_insLabel  = new QLabel(tr("INS"), this);
    m_scrlLabel = new QLabel(tr("SCRL"), this);

    QLabel* sep1 = new QLabel("|", this);
    QLabel* sep2 = new QLabel("|", this);
    QLabel* sep3 = new QLabel("|", this);

    statusBar()->addWidget(m_cursorStatusLabel, 0);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_capsLabel);
    statusBar()->addPermanentWidget(sep1);
    statusBar()->addPermanentWidget(m_numLabel);
    statusBar()->addPermanentWidget(sep2);
    statusBar()->addPermanentWidget(m_insLabel);
    statusBar()->addPermanentWidget(sep3);
    statusBar()->addPermanentWidget(m_scrlLabel);

    updateStatusText("Ready");
    updateCursorStatus();
    updateKeyStatus();
}

void CvBasicEditorWindow::loadSettings()
{
    QSettings s(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
    s.beginGroup("cvbasic");

    // Alleen settings.ini gebruiken. Geen tools/cvbasic fallback meer.
    // Linux kan eigen keys hebben. Als die ontbreken, nemen we enkel de
    // algemene keys uit settings.ini, niet een intern opgebouwd pad.
#if defined(Q_OS_WIN)
    // Windows: enkel de Windows executables uit settings.ini.
    m_cvbasicExePath = QDir::cleanPath(s.value("cvbasicExe").toString().trimmed());
    m_gasm80ExePath  = QDir::cleanPath(s.value("gasm80Exe").toString().trimmed());
#else
    // Linux: enkel de Linux executables uit settings.ini.
    // Geen fallback naar cvbasic.exe/gasm80.exe en geen interne default-paden.
    m_cvbasicExePath = QDir::cleanPath(s.value("cvbasicLinuxExe").toString().trimmed());
    m_gasm80ExePath  = QDir::cleanPath(s.value("gasm80LinuxExe").toString().trimmed());
#endif

    m_buildDirPath = QDir::cleanPath(s.value("buildDir").toString().trimmed());
    m_lastOpenDir  = QDir::cleanPath(s.value("lastOpenDir").toString().trimmed());

    restoreGeometry(s.value("geometry").toByteArray());
    s.endGroup();

    qDebug() << "[CVBASIC] Loaded paths from settings.ini only:"
             << "cvbasic =" << m_cvbasicExePath
             << "gasm80 =" << m_gasm80ExePath
             << "build =" << m_buildDirPath
             << "source =" << m_lastOpenDir;
}

void CvBasicEditorWindow::saveSettings()
{
    // De compiler/source/build-paden worden NIET meer door deze editor bewaard.
    // MainWindow + SettingsWindow beheren settings.ini. Zo kan de editor de
    // ingevulde paden niet meer overschrijven met oude/initiële waarden.
    QSettings s(QCoreApplication::applicationDirPath() + "/settings.ini", QSettings::IniFormat);
    s.beginGroup("cvbasic");
    s.setValue("geometry", saveGeometry());
    s.endGroup();
    s.sync();
}

void CvBasicEditorWindow::newFile()
{
    if (!maybeSaveBeforeDestructiveAction())
        return;

    clearSourceTabs();

    addSourceTab(tr("Main"), "REM New CVBasic project\n", true);

    if (m_editor) {
        QTextCursor cursor = m_editor->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_editor->setTextCursor(cursor);
        m_editor->ensureCursorVisible();
        m_editor->setFocus(Qt::OtherFocusReason);
    }

    setCurrentFile(QString());
    m_dirty = false;
    updateWindowTitle();
    updateStatusText("New CVBasic project");
    updateSidePanels();
    updateCursorStatus();
}

void CvBasicEditorWindow::openFile()
{
    if (!maybeSaveBeforeDestructiveAction())
        return;

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open CVBasic project"),
        m_lastOpenDir,
        tr("ADAMP CVBasic Project (*.adpcvb);;Legacy CVBasic source (*.bas *.cvb *.txt);;All files (*.*)")
    );

    if (filePath.isEmpty())
        return;

    if (readFile(filePath)) {
        m_lastOpenDir = QFileInfo(filePath).absolutePath();
        setCurrentFile(filePath.endsWith(".adpcvb", Qt::CaseInsensitive) ? filePath : QString());
        m_dirty = false;
        updateWindowTitle();
        updateStatusText("Opened " + QFileInfo(filePath).fileName());
        updateSidePanels();
        updateCursorStatus();
    }
}

bool CvBasicEditorWindow::saveFile()
{
    if (m_currentFile.isEmpty())
        return saveFileAs();

    if (!writeCurrentFile(m_currentFile))
        return false;

    m_dirty = false;
    updateWindowTitle();
    updateStatusText("Saved " + QFileInfo(m_currentFile).fileName());
    updateSidePanels();
    return true;
}

bool CvBasicEditorWindow::saveFileAs()
{
    QString startDir = m_lastOpenDir;
    if (!m_currentFile.isEmpty())
        startDir = QFileInfo(m_currentFile).absolutePath();

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Save CVBasic project"),
        startDir,
        tr("ADAMP CVBasic Project (*.adpcvb);;All files (*.*)")
    );

    if (filePath.isEmpty())
        return false;

    QString finalPath = filePath;
    if (!finalPath.endsWith(".adpcvb", Qt::CaseInsensitive))
        finalPath += ".adpcvb";

    if (!writeCurrentFile(finalPath))
        return false;

    m_lastOpenDir = QFileInfo(finalPath).absolutePath();
    setCurrentFile(finalPath);
    m_dirty = false;
    updateWindowTitle();
    updateStatusText("Saved " + QFileInfo(finalPath).fileName());
    updateSidePanels();
    return true;
}


QPrinter* CvBasicEditorWindow::printer()
{
    if (!m_printer) {
        m_printer = new QPrinter(QPrinter::HighResolution);
        m_printer->setDocName(tr("CVBasic Source"));
    }

    return m_printer;
}

void CvBasicEditorWindow::printSource()
{
    printSourceOnPrinter(printer(), true);
}


void CvBasicEditorWindow::printSourceOnPrinter(QPrinter* p, bool showPrintDialog)
{
    QPlainTextEdit* ed = activeEditor();
    if (!ed || !p)
        return;

    QTextCursor cursor = ed->textCursor();
    const bool hasSelection = cursor.hasSelection();

    QString tabName = tr("CVBasic Source");
    if (m_codeTabs) {
        const int idx = m_codeTabs->currentIndex();
        if (idx >= 0)
            tabName = sourceTabName(idx);
    }

    QString projectName = tr("Untitled project");
    QString fileDateText = tr("Unsaved");

    if (!m_currentFile.isEmpty()) {
        const QFileInfo projectInfo(m_currentFile);
        projectName = projectInfo.completeBaseName();

        const QDateTime fileDate = projectInfo.lastModified();
        if (fileDate.isValid())
            fileDateText = fileDate.toString(QStringLiteral("dd/MM/yyyy HH:mm"));
    }

    p->setDocName(hasSelection ? tr("CVBasic Selection") : tabName);

    if (showPrintDialog) {
        QPrintDialog dialog(p, this);
        dialog.setWindowTitle(hasSelection ? tr("Print Selected") : tr("Print"));

        if (hasSelection) {
            dialog.setOption(QAbstractPrintDialog::PrintSelection, true);
            dialog.setPrintRange(QAbstractPrintDialog::Selection);
        }

        if (dialog.exec() != QDialog::Accepted)
            return;
    }

    const QString textToPrint = hasSelection
        ? cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'))
        : ed->toPlainText();

    QPainter painter;
    if (!painter.begin(p)) {
        if (showPrintDialog)
            QMessageBox::warning(this, tr("Print"), tr("Could not start the printer."));
        return;
    }

    // Gebruik nooit de volledige fysieke pagina als tekengebied.
    // De meeste printers kunnen niet tot aan de rand printen en de driver
    // geeft via pageRect/paintRect de echte printbare zone terug.
    p->setFullPage(false);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QRectF printableRect = p->pageLayout().paintRectPixels(p->resolution());
#else
    const QRectF printableRect = p->pageRect(QPrinter::DevicePixel);
#endif

    const qreal dpiScale = qMax<qreal>(1.0, p->resolution() / 96.0);
    const qreal pxPerMm = static_cast<qreal>(p->resolution()) / 25.4;

    // Extra veiligheidsmarge BINNEN de printbare zone.
    // Zo blijft header, footer en code weg van de papierrand, ook bij printers
    // die hun marge wat agressief rapporteren.
    const qreal paperMargin = 8.0 * pxPerMm;
    const QRectF pageRect = printableRect.adjusted(paperMargin,
                                                   paperMargin,
                                                   -paperMargin,
                                                   -paperMargin);

    const qreal headerHeight = 34.0 * dpiScale;
    const qreal footerHeight = 30.0 * dpiScale;
    const qreal innerMargin = 3.0 * pxPerMm;

    // Content blijft tussen header en footer, en houdt ook links/rechts marge.
    const QRectF contentRect = pageRect.adjusted(innerMargin,
                                                headerHeight + innerMargin,
                                                -innerMargin,
                                                -(footerHeight + innerMargin));

    if (pageRect.width() <= 0 || pageRect.height() <= 0 ||
        contentRect.width() <= 0 || contentRect.height() <= 0) {
        painter.end();
        if (showPrintDialog)
            QMessageBox::warning(this, tr("Print"), tr("The printable page area is too small."));
        return;
    }

    QFont bodyFont = ed->font();
    painter.setFont(bodyFont);

    // Belangrijk: gebruik printer/painter metrics, NIET scherm-metrics.
    // Anders denkt Qt dat er veel meer regels op één printerpagina passen
    // en worden meerdere pagina's over elkaar getekend.
    const QFontMetricsF bodyFm = painter.fontMetrics();
    const qreal lineHeight = qMax<qreal>(1.0, bodyFm.lineSpacing());
    const int linesPerPage = qMax(1, static_cast<int>(contentRect.height() / lineHeight));

    auto wrapOneLine = [&](QString line) -> QStringList {
        // CVBasic gebruikt soms lange DATA-regels. Daarom WrapAnywhere:
        // breek ook midden in een lange token indien nodig.
        line.replace(QLatin1Char('\t'), QStringLiteral("    "));

        QStringList out;
        if (line.isEmpty()) {
            out << QString();
            return out;
        }

        QString current;
        for (const QChar ch : std::as_const(line)) {
            const QString test = current + ch;
            if (!current.isEmpty() && bodyFm.horizontalAdvance(test) > contentRect.width()) {
                out << current;
                current = QString(ch);
            } else {
                current = test;
            }
        }

        out << current;
        return out;
    };

    QStringList wrappedLines;
    const QStringList rawLines = textToPrint.split(QLatin1Char('\n'));
    for (const QString& rawLine : rawLines)
        wrappedLines.append(wrapOneLine(rawLine));

    if (wrappedLines.isEmpty())
        wrappedLines << QString();

    const int pageCount = qMax(1, (wrappedLines.size() + linesPerPage - 1) / linesPerPage);

    QFont headerFont = bodyFont;
    headerFont.setBold(true);
    headerFont.setPointSize(qMax(8, headerFont.pointSize() + 1));

    QFont footerFont = bodyFont;
    footerFont.setPointSize(qMax(7, footerFont.pointSize() - 1));

    auto drawHeaderFooter = [&](int pageNumber) {
        painter.save();

        painter.setPen(QPen(Qt::black, qMax<qreal>(1.0, dpiScale)));
        painter.setFont(headerFont);

        const QString leftHeaderText = QStringLiteral("%1   -   %2").arg(projectName, tabName);

        const QRectF headerTextRect(pageRect.left(), pageRect.top(),
                                    pageRect.width(), headerHeight - 8.0 * dpiScale);

        painter.drawText(headerTextRect,
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                         leftHeaderText);

        painter.drawText(headerTextRect,
                         Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine,
                         fileDateText);

        const qreal headerLineY = pageRect.top() + headerHeight;
        painter.drawLine(QPointF(pageRect.left(), headerLineY),
                         QPointF(pageRect.right(), headerLineY));

        const qreal footerLineY = pageRect.bottom() - footerHeight;
        painter.drawLine(QPointF(pageRect.left(), footerLineY),
                         QPointF(pageRect.right(), footerLineY));

        painter.setFont(footerFont);
        const QString pageText = tr("Page %1 / %2").arg(pageNumber).arg(pageCount);
        const QRectF footerTextRect(pageRect.left(), footerLineY,
                                    pageRect.width(), footerHeight);
        painter.drawText(footerTextRect,
                         Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine,
                         pageText);

        painter.restore();
    };

    for (int page = 0; page < pageCount; ++page) {
        if (page > 0)
            p->newPage();

        drawHeaderFooter(page + 1);

        painter.save();
        painter.setFont(bodyFont);
        painter.setPen(Qt::black);
        painter.setClipRect(contentRect);

        const int firstLine = page * linesPerPage;
        const int lastLine = qMin(wrappedLines.size(), firstLine + linesPerPage);

        qreal y = contentRect.top() + bodyFm.ascent();
        for (int i = firstLine; i < lastLine; ++i) {
            painter.drawText(QPointF(contentRect.left(), y), wrappedLines.at(i));
            y += lineHeight;
        }

        painter.restore();
    }

    painter.end();

    if (showPrintDialog) {
        updateStatusText(hasSelection ? tr("Printed selected CVBasic code")
                                      : tr("Printed CVBasic code"));

        // Force een echte layout-refresh van de CodeEditor na de native printdialog.
        // Alleen viewport()->update() is niet genoeg: soms blijft de line-number area
        // visueel over de tekst liggen tot je scrollt.
        refreshBasicEditorLayout();
        QTimer::singleShot(0, this, [this]() { refreshBasicEditorLayout(); });
        QTimer::singleShot(100, this, [this]() { refreshBasicEditorLayout(); });
    }
}

void CvBasicEditorWindow::compileOnly()
{
    startBuild(false);
}

void CvBasicEditorWindow::compileAndRun()
{
    startBuild(true);
}

void CvBasicEditorWindow::chooseCvBasicExe()
{
#if defined(Q_OS_WIN)
    const QString title = tr("Select cvbasic.exe");
    const QString filter = tr("Executable (*.exe);;All files (*.*)");
#else
    const QString title = tr("Select cvbasic_linux");
    const QString filter = tr("Linux executable (cvbasic_linux);;All files (*)");
#endif

    const QString path = QFileDialog::getOpenFileName(
        this,
        title,
        QFileInfo(m_cvbasicExePath).absolutePath(),
        filter
    );
    if (!path.isEmpty()) {
        m_cvbasicExePath = QDir::cleanPath(path);
        saveSettings();
        updateStatusText(QFileInfo(m_cvbasicExePath).fileName() + " set");
    }
}

void CvBasicEditorWindow::chooseGasm80Exe()
{
#if defined(Q_OS_WIN)
    const QString title = tr("Select gasm80.exe");
    const QString filter = tr("Executable (*.exe);;All files (*.*)");
#else
    const QString title = tr("Select gasm80_linux");
    const QString filter = tr("Linux executable (gasm80_linux);;All files (*)");
#endif

    const QString path = QFileDialog::getOpenFileName(
        this,
        title,
        QFileInfo(m_gasm80ExePath).absolutePath(),
        filter
    );
    if (!path.isEmpty()) {
        m_gasm80ExePath = QDir::cleanPath(path);
        saveSettings();
        updateStatusText(QFileInfo(m_gasm80ExePath).fileName() + " set");
    }
}

void CvBasicEditorWindow::openBuildFolder()
{
    QDir dir(m_buildDirPath);
    if (!dir.exists())
        dir.mkpath(".");

    QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}




void CvBasicEditorWindow::refreshBasicEditorLayout()
{
    m_editor = activeEditor();
    if (!m_editor)
        return;

    auto refreshOneEditor = [this](QPlainTextEdit* ed) {
        if (!ed)
            return;

        if (CodeEditor* codeEditor = dynamic_cast<CodeEditor*>(ed)) {
            const bool lineNumbersVisible = !m_actViewFoldLines || m_actViewFoldLines->isChecked();
            codeEditor->setLineNumberAreaVisible(lineNumbersVisible);
            codeEditor->forceLineNumberRefresh();
        }

        // Force the document and viewport to repaint. This replaces the old
        // workaround where scrolling accidentally fixed the text position.
        if (ed->document())
            ed->document()->markContentsDirty(0, ed->document()->characterCount());

        ed->viewport()->update();
        ed->updateGeometry();
        ed->update();
    };

    auto refreshNow = [this, refreshOneEditor]() {
        if (m_codeTabs) {
            for (int i = 0; i < m_codeTabs->count(); ++i)
                refreshOneEditor(sourceEditorAt(i));
        } else {
            refreshOneEditor(m_editor);
        }
    };

    refreshNow();
    QTimer::singleShot(0, this, refreshNow);
    QTimer::singleShot(50, this, refreshNow);
    QTimer::singleShot(150, this, refreshNow);
}


void CvBasicEditorWindow::showBasicEditor()
{
    if (m_mainPages && m_basicPage)
        m_mainPages->setCurrentWidget(m_basicPage);

    refreshBasicEditorLayout();
    updateStatusText(tr("CVBASIC IDE editor"));
}

void CvBasicEditorWindow::showSpriteEditor()
{
    if (m_mainPages && m_spritePage)
        m_mainPages->setCurrentWidget(m_spritePage);

    updateStatusText(tr("Sprite editor"));
}

void CvBasicEditorWindow::showSoundEditor()
{
    if (m_mainPages && m_soundPage)
        m_mainPages->setCurrentWidget(m_soundPage);

    updateStatusText(tr("Sound editor"));
}

void CvBasicEditorWindow::showPaintEditor()
{
    if (m_mainPages && m_paintPage)
        m_mainPages->setCurrentWidget(m_paintPage);

    updateStatusText(tr("Paint editor"));
}


void CvBasicEditorWindow::toggleFoldLines(bool checked)
{
    if (CodeEditor* codeEditor = dynamic_cast<CodeEditor*>(m_editor)) {
        codeEditor->setLineNumberAreaVisible(checked);
        codeEditor->viewport()->update();
    } else if (m_lineNumberAreaWidget) {
        m_lineNumberAreaWidget->setVisible(checked);
    }

    refreshBasicEditorLayout();
    updateStatusText(checked ? tr("Fold lines visible") : tr("Fold lines hidden"));
}

void CvBasicEditorWindow::toggleConsole(bool checked)
{
    if (m_bottomPanel)
        m_bottomPanel->setVisible(checked);

    updateStatusText(checked ? tr("Console visible") : tr("Console hidden"));
}

void CvBasicEditorWindow::toggleShortcuts(bool checked)
{
    if (m_sidePanel)
        m_sidePanel->setVisible(checked);

    updateStatusText(checked ? tr("Shortcuts visible") : tr("Shortcuts hidden"));
}

void CvBasicEditorWindow::resetConsole()
{
    if (m_output)
        m_output->clear();

    if (m_errorTable)
        m_errorTable->setRowCount(0);

    m_errorCount = 0;
    m_warningCount = 0;

    if (m_errorsLabel)
        m_errorsLabel->setText(tr("● 0 Errors"));
    if (m_warningsLabel)
        m_warningsLabel->setText(tr("● 0 Warnings"));

    updateStatusText(tr("Console reset"));
}


void CvBasicEditorWindow::editUndo()
{
    if (m_mainPages && m_spritePage && m_mainPages->currentWidget() == m_spritePage) {
        if (CvBasicSpriteDialog* spriteDialog = dynamic_cast<CvBasicSpriteDialog*>(m_spritePage)) {
            spriteDialog->undoSpriteEdit();
            updateStatusText(tr("Sprite undo"));
            return;
        }
    }

    if (m_editor)
        m_editor->undo();
}

void CvBasicEditorWindow::editRedo()
{
    if (m_mainPages && m_spritePage && m_mainPages->currentWidget() == m_spritePage) {
        if (CvBasicSpriteDialog* spriteDialog = dynamic_cast<CvBasicSpriteDialog*>(m_spritePage)) {
            spriteDialog->redoSpriteEdit();
            updateStatusText(tr("Sprite redo"));
            return;
        }
    }

    if (m_editor)
        m_editor->redo();
}

void CvBasicEditorWindow::editCut()
{
    if (m_editor)
        m_editor->cut();
}

void CvBasicEditorWindow::editCopy()
{
    if (m_editor)
        m_editor->copy();
}

void CvBasicEditorWindow::editPaste()
{
    if (m_editor)
        m_editor->paste();
}

void CvBasicEditorWindow::findText()
{
    QPlainTextEdit* ed = activeEditor();
    if (!ed)
        return;

    bool ok = false;
    const QString text = QInputDialog::getText(
        this,
        tr("Find"),
        tr("Find text:"),
        QLineEdit::Normal,
        m_lastFindText,
        &ok
    );

    if (!ok)
        return;

    m_lastFindText = text;
    findNext();
}

void CvBasicEditorWindow::findNext()
{
    QPlainTextEdit* ed = activeEditor();
    if (!ed)
        return;

    if (m_lastFindText.isEmpty()) {
        findText();
        return;
    }

    if (ed->find(m_lastFindText)) {
        updateStatusText(tr("Found: %1").arg(m_lastFindText));
        return;
    }

    // Wrap naar begin van document.
    QTextCursor cursor = ed->textCursor();
    cursor.movePosition(QTextCursor::Start);
    ed->setTextCursor(cursor);

    if (ed->find(m_lastFindText)) {
        updateStatusText(tr("Found: %1").arg(m_lastFindText));
        return;
    }

    QMessageBox::information(
        this,
        tr("Find"),
        tr("Text not found:\n%1").arg(m_lastFindText)
    );
    updateStatusText(tr("Text not found"));
}

void CvBasicEditorWindow::replaceText()
{
    QPlainTextEdit* ed = activeEditor();
    if (!ed)
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Replace"));
    dlg.setModal(true);

    QVBoxLayout* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    QFormLayout* form = new QFormLayout();
    QLineEdit* findEdit = new QLineEdit(&dlg);
    QLineEdit* replaceEdit = new QLineEdit(&dlg);

    findEdit->setText(m_lastFindText);

    form->addRow(tr("Find:"), findEdit);
    form->addRow(tr("Replace with:"), replaceEdit);

    QHBoxLayout* buttons = new QHBoxLayout();
    QPushButton* replaceButton = new QPushButton(tr("Replace"), &dlg);
    QPushButton* replaceAllButton = new QPushButton(tr("Replace All"), &dlg);
    QPushButton* cancelButton = new QPushButton(tr("Cancel"), &dlg);

    buttons->addStretch(1);
    buttons->addWidget(replaceButton);
    buttons->addWidget(replaceAllButton);
    buttons->addWidget(cancelButton);

    root->addLayout(form);
    root->addLayout(buttons);

    dlg.setStyleSheet(
        "QDialog { background-color: #3A3A3A; color: #FFFFFF; }"
        "QLabel { background: transparent; color: #FFFFFF; }"
        "QLineEdit { background-color: #242424; color: #FFFFFF; border: 1px solid #5C5C5C; padding: 3px 6px; }"
        "QPushButton { background-color: #242424; color: #FFFFFF; border: 1px solid #6A6A6A; padding: 4px 10px; }"
        "QPushButton:hover { background-color: #4A4A4A; }"
        "QPushButton:pressed { background-color: #5A5A5A; padding-top: 5px; padding-left: 11px; }"
    );

    connect(cancelButton, &QPushButton::clicked, &dlg, &QDialog::reject);

    connect(replaceButton, &QPushButton::clicked, &dlg, [&]() {
        const QString find = findEdit->text();
        if (find.isEmpty())
            return;

        m_lastFindText = find;

        QTextCursor cursor = ed->textCursor();
        if (!cursor.hasSelection() || cursor.selectedText() != find) {
            if (!ed->find(find)) {
                QTextCursor startCursor = ed->textCursor();
                startCursor.movePosition(QTextCursor::Start);
                ed->setTextCursor(startCursor);

                if (!ed->find(find)) {
                    QMessageBox::information(&dlg, tr("Replace"), tr("Text not found:\n%1").arg(find));
                    return;
                }
            }
            cursor = ed->textCursor();
        }

        cursor.insertText(replaceEdit->text());
        ed->setTextCursor(cursor);
        updateStatusText(tr("Replaced"));
        ed->find(find);
    });

    connect(replaceAllButton, &QPushButton::clicked, &dlg, [&]() {
        const QString find = findEdit->text();
        const QString repl = replaceEdit->text();

        if (find.isEmpty())
            return;

        m_lastFindText = find;

        QString source = ed->toPlainText();
        const int count = source.count(find, Qt::CaseSensitive);
        if (count <= 0) {
            QMessageBox::information(&dlg, tr("Replace All"), tr("Text not found:\n%1").arg(find));
            return;
        }

        source.replace(find, repl, Qt::CaseSensitive);
        ed->setPlainText(source);
        updateStatusText(tr("Replaced %1 occurrence(s)").arg(count));
    });

    findEdit->setFocus();
    dlg.exec();
}


void CvBasicEditorWindow::showAboutDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("About"));
    dlg.setModal(true);
    dlg.setFixedSize(316, 270);

    QVBoxLayout* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(7, 7, 7, 7);
    root->setSpacing(8);

    QFrame* box = new QFrame(&dlg);
    box->setFrameShape(QFrame::StyledPanel);
    box->setObjectName("aboutBox");

    QVBoxLayout* boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(10, 12, 10, 12);
    boxLayout->setSpacing(4);

    QLabel* text = new QLabel(box);
    text->setAlignment(Qt::AlignCenter);
    text->setTextFormat(Qt::RichText);
    text->setOpenExternalLinks(true);
    text->setText(
        "<div align='center'>"
        "CVBASIC IDE - SPRITE & SOUND-EDITOR<br>"
        "&copy; ADAM+ EMULATOR  PLUG-IN DVdH 2026<br>"
        "<a href='https://github.com/dvdh1961/ADAMP'>https://github.com/dvdh1961/ADAMP</a><br><br>"
        "FREEWARE<br><br>"
        "CVBasic by &Oacute;scar Toledo Guti&eacute;rrez<br>"
        "<a href='https://nanochess.org/cvbasic.html'>https://nanochess.org/cvbasic.html</a><br>"
        "<a href='https://github.com/nanochess/CVBasic'>https://github.com/nanochess/CVBasic</a><br><br>"
        "</div>"
    );

    boxLayout->addWidget(text, 1);

    QPushButton* okButton = new QPushButton(tr("Ok"), &dlg);
    okButton->setObjectName("aboutOkButton");
    okButton->setFixedWidth(72);
    connect(okButton, &QPushButton::clicked, &dlg, &QDialog::accept);

    root->addWidget(box, 1);
    root->addWidget(okButton, 0, Qt::AlignHCenter);

    dlg.setStyleSheet(
        "QDialog { background-color: #3A3A3A; color: #FFFFFF; }"
        "QFrame#aboutBox { background-color: #3A3A3A; border: 1px solid #555555; }"
        "QLabel { background: transparent; color: #FFFFFF; }"
        "QLabel a { color: #66AFFF; }"
        "QPushButton#aboutOkButton {"
        "  background-color: #242424;"
        "  color: #FFFFFF;"
        "  border: 1px solid #6A6A6A;"
        "  padding: 4px 10px;"
        "}"
        "QPushButton#aboutOkButton:hover { background-color: #4A4A4A; }"
        "QPushButton#aboutOkButton:pressed { background-color: #5A5A5A; padding-top: 5px; padding-left: 11px; }"
    );

    dlg.exec();
}

void CvBasicEditorWindow::onEditorTextChanged()
{
    if (!m_dirty) {
        m_dirty = true;
        updateWindowTitle();
    }
    updateSidePanels();
}

bool CvBasicEditorWindow::maybeSaveBeforeDestructiveAction()
{
    if (!m_dirty)
        return true;

    const QMessageBox::StandardButton ret = QMessageBox::question(
        this,
        tr("CVBasic Project"),
        tr("The current CVBasic project has unsaved changes. Save it first?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save
    );

    if (ret == QMessageBox::Save)
        return saveFile();
    if (ret == QMessageBox::Discard)
        return true;
    return false;
}

bool CvBasicEditorWindow::writeCurrentFile(const QString& filePath)
{
    QFile file(filePath);
    QDir().mkpath(QFileInfo(filePath).absolutePath());

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Save failed"), file.errorString());
        return false;
    }

    QJsonObject root;
    root["format"] = "ADAMP_CVBasic_Project";
    root["version"] = 1;
    root["mainTab"] = m_codeTabs ? m_codeTabs->currentIndex() : 0;

    QJsonArray tabs;
    if (m_codeTabs) {
        for (int i = 0; i < m_codeTabs->count(); ++i) {
            QPlainTextEdit* ed = sourceEditorAt(i);
            if (!ed)
                continue;

            QJsonObject tab;
            tab["name"] = sourceTabName(i);
            tab["text"] = ed->toPlainText();
            tabs.append(tab);
        }
    } else if (m_editor) {
        QJsonObject tab;
        tab["name"] = "Main";
        tab["text"] = m_editor->toPlainText();
        tabs.append(tab);
    }

    root["tabs"] = tabs;

    const QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool CvBasicEditorWindow::readFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Open failed"), file.errorString());
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    clearSourceTabs();

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject() && doc.object().value("format").toString() == "ADAMP_CVBasic_Project") {
        const QJsonObject root = doc.object();
        const QJsonArray tabs = root.value("tabs").toArray();

        for (int i = 0; i < tabs.size(); ++i) {
            const QJsonObject tab = tabs.at(i).toObject();
            addSourceTab(tab.value("name").toString(QString("Tab%1").arg(i + 1)),
                         tab.value("text").toString(),
                         false);
        }

        if (sourceTabCount() <= 0)
            addSourceTab(tr("Main"), "REM Empty CVBasic project\n", false);

        // Always show the first source tab when a project is loaded.
        // Do not restore mainTab here: the user wants every loaded project to open
        // on tab 0, and this also guarantees the first editor becomes visible
        // before we force its line-number gutter refresh.
        if (m_tabs && m_codeTabs)
            m_tabs->setCurrentWidget(m_codeTabs);
        if (m_codeTabs && m_codeTabs->count() > 0)
            m_codeTabs->setCurrentIndex(0);

        m_editor = activeEditor();

        // Project loading creates the first source tab before the tab widget has
        // completed its visible layout. Refresh all gutters after the event loop
        // and force the first tab gutter like a tiny internal scroll would do.
        refreshBasicEditorLayout();
        QTimer::singleShot(0, this, [this]() {
            if (m_codeTabs && m_codeTabs->count() > 0)
                m_codeTabs->setCurrentIndex(0);
            refreshBasicEditorLayout();
        });
        QTimer::singleShot(100, this, [this]() {
            if (m_tabs && m_codeTabs)
                m_tabs->setCurrentWidget(m_codeTabs);
            if (m_codeTabs && m_codeTabs->count() > 0)
                m_codeTabs->setCurrentIndex(0);
            refreshBasicEditorLayout();
        });
        QTimer::singleShot(250, this, [this]() { refreshBasicEditorLayout(); });

        return true;
    }

    // Legacy .bas/.cvb/.txt support: open as one Main tab, then save as .adpcvb.
    addSourceTab(tr("Main"), QString::fromUtf8(data), true);
    if (m_tabs && m_codeTabs)
        m_tabs->setCurrentWidget(m_codeTabs);
    if (m_codeTabs && m_codeTabs->count() > 0)
        m_codeTabs->setCurrentIndex(0);
    m_editor = activeEditor();
    refreshBasicEditorLayout();
    QTimer::singleShot(0, this, [this]() { refreshBasicEditorLayout(); });
    QTimer::singleShot(100, this, [this]() { refreshBasicEditorLayout(); });
    QTimer::singleShot(250, this, [this]() { refreshBasicEditorLayout(); });
    return true;
}

bool CvBasicEditorWindow::ensureSourceFileForBuild()
{
    // Save the project first if it has a project path. Untitled projects may compile
    // without saving; they are written to a temporary combined .bas in the build folder.
    if (!m_currentFile.isEmpty() && m_dirty)
        return saveFile();

    return true;
}

bool CvBasicEditorWindow::ensureToolExists(const QString& path, const QString& toolName)
{
    if (QFileInfo::exists(path) && QFileInfo(path).isFile())
        return true;

    QMessageBox::warning(
        this,
        tr("Tool not found"),
        tr("%1 was not found:\n\n%2\n\nUse Tools to select the correct executable.")
            .arg(toolName, QDir::toNativeSeparators(path))
    );
    return false;
}

bool CvBasicEditorWindow::prepareBuildPaths()
{
    QDir buildDir(m_buildDirPath);
    if (!buildDir.exists() && !buildDir.mkpath(".")) {
        QMessageBox::warning(this, tr("Build failed"), tr("Cannot create build folder:\n%1").arg(m_buildDirPath));
        return false;
    }

    QString baseName = m_currentFile.isEmpty()
        ? QStringLiteral("untitled_project")
        : QFileInfo(m_currentFile).completeBaseName();

    baseName.replace(QRegularExpression("[^A-Za-z0-9_\\-]"), "_");
    if (baseName.trimmed().isEmpty())
        baseName = QStringLiteral("untitled_project");

    m_buildSourcePath = QDir::cleanPath(buildDir.filePath(baseName + "_combined.bas"));
    m_asmPath = QDir::cleanPath(buildDir.filePath(baseName + ".asm"));
    m_romPath = QDir::cleanPath(buildDir.filePath(baseName + ".rom"));

    return writeCombinedSourceForBuild();
}

void CvBasicEditorWindow::startBuild(bool runAfterBuild)
{
    if (m_buildStep != BuildStep::Idle) {
        QMessageBox::information(this, tr("Build busy"), tr("A CVBasic build is already running."));
        return;
    }

    if (!ensureSourceFileForBuild())
        return;

#if defined(Q_OS_WIN)
    const QString cvbasicToolName = QStringLiteral("cvbasic.exe");
    const QString gasm80ToolName  = QStringLiteral("gasm80.exe");
#else
    const QString cvbasicToolName = QFileInfo(m_cvbasicExePath).fileName().isEmpty()
        ? QStringLiteral("cvbasic_linux")
        : QFileInfo(m_cvbasicExePath).fileName();
    const QString gasm80ToolName = QFileInfo(m_gasm80ExePath).fileName().isEmpty()
        ? QStringLiteral("gasm80_linux")
        : QFileInfo(m_gasm80ExePath).fileName();
#endif

    if (!ensureToolExists(m_cvbasicExePath, cvbasicToolName))
        return;

    if (!ensureToolExists(m_gasm80ExePath, gasm80ToolName))
        return;

    if (!prepareBuildPaths())
        return;

    m_errorCount = 0;
    m_warningCount = 0;
    if (m_errorTable)
        m_errorTable->setRowCount(0);
    if (m_errorsLabel)
        m_errorsLabel->setText("● 0 Errors");
    if (m_warningsLabel)
        m_warningsLabel->setText("● 0 Warnings");

    m_runAfterBuild = runAfterBuild;
    m_output->clear();
    appendOutput("=== CVBasic build started " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") + " ===\n");
    appendOutput("Source : " + QDir::toNativeSeparators(m_currentFile) + "\n");
    appendOutput("ASM    : " + QDir::toNativeSeparators(m_asmPath) + "\n");
    appendOutput("ROM    : " + QDir::toNativeSeparators(m_romPath) + "\n\n");

    startCvBasic();
}

void CvBasicEditorWindow::startCvBasic()
{
    m_buildStep = BuildStep::CvBasic;
    updateStatusText("Compiling CVBasic...");

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setWorkingDirectory(QFileInfo(m_cvbasicExePath).absolutePath());

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &CvBasicEditorWindow::onProcessReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &CvBasicEditorWindow::onProcessReadyReadStderr);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &CvBasicEditorWindow::onProcessFinished);

    QStringList args;
    args << m_buildSourcePath << m_asmPath;

    appendOutput("> " + quotedNativePath(m_cvbasicExePath) + " --sgm "
                 + quotedNativePath(m_buildSourcePath) + " "
                 + quotedNativePath(m_asmPath) + "\n\n");
    m_process->start(m_cvbasicExePath, args);
}

void CvBasicEditorWindow::startGasm80()
{
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_buildStep = BuildStep::Gasm80;
    updateStatusText("Assembling ROM...");

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setWorkingDirectory(QFileInfo(m_gasm80ExePath).absolutePath());

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &CvBasicEditorWindow::onProcessReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &CvBasicEditorWindow::onProcessReadyReadStderr);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &CvBasicEditorWindow::onProcessFinished);

    QStringList args;
    args << m_asmPath << "-o" << m_romPath;

    appendOutput("\n> " + quotedNativePath(m_gasm80ExePath) + " "
                 + quotedNativePath(m_asmPath) + " -o "
                 + quotedNativePath(m_romPath) + "\n\n");

    m_process->start(m_gasm80ExePath, args);
}

void CvBasicEditorWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_process) {
        onProcessReadyReadStdout();
        onProcessReadyReadStderr();
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        finishBuildFailed(tr("Process failed, exit code %1").arg(exitCode));
        return;
    }

    if (m_buildStep == BuildStep::CvBasic) {
        if (!QFileInfo::exists(m_asmPath)) {
            finishBuildFailed(tr("CVBasic finished but no ASM file was created."));
            return;
        }
        startGasm80();
        return;
    }

    if (m_buildStep == BuildStep::Gasm80) {
        if (!QFileInfo::exists(m_romPath)) {
            finishBuildFailed(tr("gasm80 finished but no ROM file was created."));
            return;
        }
        finishBuildSuccess();
    }
}

void CvBasicEditorWindow::onProcessReadyReadStdout()
{
    if (!m_process)
        return;

    const QString text = QString::fromLocal8Bit(m_process->readAllStandardOutput());
    if (!text.isEmpty())
        appendOutput(text);
}

void CvBasicEditorWindow::onProcessReadyReadStderr()
{
    if (!m_process)
        return;

    const QString text = QString::fromLocal8Bit(m_process->readAllStandardError());
    if (!text.isEmpty())
        appendError(text);
}

void CvBasicEditorWindow::finishBuildSuccess()
{
    appendOutput("\n=== Build OK ===\n");
    appendOutput("ROM created: " + QDir::toNativeSeparators(m_romPath) + "\n");
    updateStatusText("Build OK");

    const QString rom = m_romPath;
    const bool runNow = m_runAfterBuild;

    resetBuildState();

    if (runNow)
        emit romBuilt(rom);
}

void CvBasicEditorWindow::finishBuildFailed(const QString& reason)
{
    appendError("\n=== Build FAILED ===\n" + reason + "\n");
    updateStatusText("Build failed");
    resetBuildState();
}

void CvBasicEditorWindow::resetBuildState()
{
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_buildStep = BuildStep::Idle;
    m_runAfterBuild = false;
}

void CvBasicEditorWindow::appendOutput(const QString& text)
{
    if (!m_output)
        return;

    m_output->moveCursor(QTextCursor::End);
    m_output->insertPlainText(text);
    m_output->moveCursor(QTextCursor::End);
}

void CvBasicEditorWindow::appendError(const QString& text)
{
    appendOutput(text);

    // Simple error/warning detection for the error table.
    const QStringList lines = text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
    const QRegularExpression lineRegex(QStringLiteral("(?:line|Line|LINE)\\s*[:= ]\\s*(\\d+)"));

    for (const QString& line : lines) {
        const bool isWarning = line.contains("warning", Qt::CaseInsensitive);
        const bool isError = line.contains("error", Qt::CaseInsensitive) || line.contains("failed", Qt::CaseInsensitive);

        if (!isWarning && !isError)
            continue;

        int lineNumber = -1;
        const QRegularExpressionMatch match = lineRegex.match(line);
        if (match.hasMatch())
            lineNumber = match.captured(1).toInt();

        if (isWarning) {
            ++m_warningCount;
            if (m_warningsLabel)
                m_warningsLabel->setText(QString("● %1 Warnings").arg(m_warningCount));
        }

        if (isError) {
            ++m_errorCount;
            if (m_errorsLabel)
                m_errorsLabel->setText(QString("● %1 Errors").arg(m_errorCount));
        }

        addErrorRow(line.trimmed(), lineNumber);
    }
}

void CvBasicEditorWindow::addErrorRow(const QString& description, int line)
{
    if (!m_errorTable)
        return;

    BuildLineInfo mapped;
    if (m_buildStep == BuildStep::CvBasic)
        mapped = sourceLineForCombinedLine(line);

    const int row = m_errorTable->rowCount();
    m_errorTable->insertRow(row);

    QString desc = description;
    if (mapped.tabIndex >= 0 && mapped.localLine > 0) {
        desc += tr("  [tab: %1, line: %2]").arg(mapped.tabName).arg(mapped.localLine);
    }

    QTableWidgetItem* descItem = new QTableWidgetItem(desc);
    QTableWidgetItem* tabItem = new QTableWidgetItem(mapped.tabIndex >= 0 ? mapped.tabName : QString());
    QTableWidgetItem* lineItem = new QTableWidgetItem(mapped.localLine > 0 ? QString::number(mapped.localLine) : QString());

    descItem->setData(Qt::UserRole + 1, mapped.tabIndex);
    descItem->setData(Qt::UserRole + 2, mapped.localLine);
    tabItem->setData(Qt::UserRole + 1, mapped.tabIndex);
    tabItem->setData(Qt::UserRole + 2, mapped.localLine);
    lineItem->setData(Qt::UserRole + 1, mapped.tabIndex);
    lineItem->setData(Qt::UserRole + 2, mapped.localLine);
    lineItem->setData(Qt::UserRole + 3, line); // original combined compiler line

    m_errorTable->setItem(row, 0, descItem);
    m_errorTable->setItem(row, 1, tabItem);
    m_errorTable->setItem(row, 2, lineItem);
}

void CvBasicEditorWindow::setCurrentFile(const QString& filePath)
{
    m_currentFile = filePath.isEmpty() ? QString() : QDir::cleanPath(filePath);
}

void CvBasicEditorWindow::updateWindowTitle()
{
    const QString name = m_currentFile.isEmpty() ? tr("Untitled.adpcvb") : QFileInfo(m_currentFile).fileName();
    QString titleName = name;
    if (m_dirty)
        titleName += " *";

    setWindowTitle(titleName + " - CVBasic Editor");

    if (m_tabs)
        m_tabs->setTabText(0, titleName);
}

void CvBasicEditorWindow::updateStatusText(const QString& text)
{
    if (m_statusLabel)
        m_statusLabel->setText(text);
}

void CvBasicEditorWindow::updateCursorStatus()
{
    QPlainTextEdit* ed = activeEditor();
    if (!ed || !m_cursorStatusLabel)
        return;

    const QTextCursor cursor = ed->textCursor();
    const int line = cursor.blockNumber() + 1;
    const int col = cursor.positionInBlock() + 1;

    m_cursorStatusLabel->setText(QString("Ln %1, Col %2").arg(line).arg(col));
}

void CvBasicEditorWindow::updateKeyStatus()
{
    bool capsOn = m_capsLockOn;
    bool numOn = m_numLockOn;
    bool scrlOn = m_scrollLockOn;

#if defined(Q_OS_WIN)
    capsOn = lockKeyActive(VK_CAPITAL);
    numOn  = lockKeyActive(VK_NUMLOCK);
    scrlOn = lockKeyActive(VK_SCROLL);
#elif defined(Q_OS_LINUX)
    capsOn = linuxLockKeyActive("capslock", m_capsLockOn);
    numOn  = linuxLockKeyActive("numlock", m_numLockOn);
    scrlOn = linuxLockKeyActive("scrolllock", m_scrollLockOn);
#endif

    QPlainTextEdit* activeEd = activeEditor();
    const bool insOn = activeEd ? !activeEd->overwriteMode() : true;

    auto setLed = [](QLabel* label, bool on) {
        if (!label)
            return;

        label->setStyleSheet(on
            ? "QLabel { color: #00C853; background: transparent; font-weight: bold; }"
            : "QLabel { color: #777777; background: transparent; font-weight: normal; }");
    };

    setLed(m_capsLabel, capsOn);
    setLed(m_numLabel,  numOn);
    setLed(m_insLabel,  insOn);
    setLed(m_scrlLabel, scrlOn);
}

bool CvBasicEditorWindow::eventFilter(QObject* watched, QEvent* event)
{
    QPlainTextEdit* watchedEditor = qobject_cast<QPlainTextEdit*>(watched);
    if (watchedEditor && (!m_codeTabs || m_codeTabs->indexOf(watchedEditor) >= 0 || watchedEditor == m_editor)) {
        m_editor = watchedEditor;
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

            if (keyEvent->key() == Qt::Key_CapsLock) {
#if !defined(Q_OS_WIN)
                m_capsLockOn = !m_capsLockOn;
#endif
                QTimer::singleShot(0, this, [this]() { updateKeyStatus(); });
                return false;
            }

            if (keyEvent->key() == Qt::Key_NumLock) {
#if !defined(Q_OS_WIN)
                m_numLockOn = !m_numLockOn;
#endif
                QTimer::singleShot(0, this, [this]() { updateKeyStatus(); });
                return false;
            }

            if (keyEvent->key() == Qt::Key_ScrollLock) {
#if !defined(Q_OS_WIN)
                m_scrollLockOn = !m_scrollLockOn;
#endif
                QTimer::singleShot(0, this, [this]() { updateKeyStatus(); });
                return false;
            }

            if (keyEvent->key() == Qt::Key_Insert && m_editor) {
                m_editor->setOverwriteMode(!m_editor->overwriteMode());
                updateKeyStatus();
                return true;
            }

            QTimer::singleShot(0, this, [this]() {
                updateCursorStatus();
                updateKeyStatus();
            });
        } else if (event->type() == QEvent::MouseButtonRelease ||
                   event->type() == QEvent::MouseButtonPress ||
                   event->type() == QEvent::FocusIn) {
            QTimer::singleShot(0, this, [this]() {
                updateCursorStatus();
                updateKeyStatus();
            });
        }
    }

    return QMainWindow::eventFilter(watched, event);
}


void CvBasicEditorWindow::updateSidePanels()
{
    QPlainTextEdit* ed = activeEditor();
    if (!m_labelsList || !m_proceduresList || !ed)
        return;

    m_editor = ed;
    m_labelsList->clear();
    m_proceduresList->clear();

    const QStringList lines = ed->toPlainText().split('\n');

    // CVBasic:
    //   #LABEL:      = label
    //   PROCEDURE:   = procedure
    //   #LABEL,      = label, komma niet tonen
    //   PROCEDURE,   = procedure, komma niet tonen
    //   DIM var      = variabele tonen bij labels
    //
    // Internally everything is stored without #, : or , so double-click remains simple.
    const QRegularExpression targetRegex(
        QStringLiteral("^\\s*(#?)([A-Za-z_][A-Za-z0-9_]*)(?:\\s*[: ,])"),
        QRegularExpression::CaseInsensitiveOption
    );

    const QRegularExpression dimRegex(
        QStringLiteral("\\bDIM\\s+([^'\\r\\n]+)"),
        QRegularExpression::CaseInsensitiveOption
    );

    const QRegularExpression dimVarRegex(
        QStringLiteral("([A-Za-z_][A-Za-z0-9_]*)(?:\\s*\\([^)]*\\))?"),
        QRegularExpression::CaseInsensitiveOption
    );

    const QRegularExpression gosubRegex(
        QStringLiteral("\\bGOSUB\\s+#?([A-Za-z_][A-Za-z0-9_]*)(?:\\s*[:,])?"),
        QRegularExpression::CaseInsensitiveOption
    );

    const QRegularExpression gotoRegex(
        QStringLiteral("\\bGOTO\\s+#?([A-Za-z_][A-Za-z0-9_]*)(?:\\s*[:,])?"),
        QRegularExpression::CaseInsensitiveOption
    );

    QMap<QString, int> labelLines;
    QMap<QString, int> procedureLines;

    auto addUnique = [](QMap<QString, int>& map, const QString& name, int lineNumber) {
        const QString cleaned = name.trimmed();
        if (cleaned.isEmpty())
            return;
        if (!map.contains(cleaned))
            map.insert(cleaned, lineNumber);
    };

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);

        // Strip comments before parsing DIM / labels.
        const int apostrophe = line.indexOf('\'');
        if (apostrophe >= 0)
            line = line.left(apostrophe);

        const QRegularExpression remRegex(QStringLiteral("\\bREM\\b"), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch remMatch = remRegex.match(line);
        if (remMatch.hasMatch())
            line = line.left(remMatch.capturedStart());

        const int lineNumber = i + 1;

        // #NAME: of #NAME, => label
        // NAME: of NAME,   => procedure
        const QRegularExpressionMatch targetMatch = targetRegex.match(line);
        if (targetMatch.hasMatch()) {
            const bool isLabel = (targetMatch.captured(1) == "#");
            const QString name = targetMatch.captured(2);

            if (isLabel)
                addUnique(labelLines, name, lineNumber);
            else
                addUnique(procedureLines, name, lineNumber);
        }

        // DIM variables are labels.
        const QRegularExpressionMatch dimMatch = dimRegex.match(line);
        if (dimMatch.hasMatch()) {
            QString vars = dimMatch.captured(1);

            // Do not include anything after THEN/GOTO/GOSUB/REM on the same line.
            const QRegularExpression stopRegex(QStringLiteral("\\b(THEN|GOTO|GOSUB|REM)\\b"), QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch stopMatch = stopRegex.match(vars);
            if (stopMatch.hasMatch())
                vars = vars.left(stopMatch.capturedStart());

            const QStringList parts = vars.split(',', Qt::SkipEmptyParts);
            for (QString part : parts) {
                part = part.trimmed();

                const QRegularExpressionMatch varMatch = dimVarRegex.match(part);
                if (!varMatch.hasMatch())
                    continue;

                const QString varName = varMatch.captured(1);
                addUnique(labelLines, varName, lineNumber);
            }
        }
    }

    // Show labels with a leading #.
    for (auto it = labelLines.constBegin(); it != labelLines.constEnd(); ++it) {
        QListWidgetItem* item = new QListWidgetItem(
            QString("#%1  (Ln %2)").arg(it.key()).arg(it.value()),
            m_labelsList
        );
        item->setData(Qt::UserRole, it.value());
        item->setToolTip(QString("Go to line %1").arg(it.value()));
    }

    // Show procedures with a trailing colon.
    for (auto it = procedureLines.constBegin(); it != procedureLines.constEnd(); ++it) {
        QListWidgetItem* item = new QListWidgetItem(
            QString("%1:  (Ln %2)").arg(it.key()).arg(it.value()),
            m_proceduresList
        );
        item->setData(Qt::UserRole, it.value());
        item->setToolTip(QString("Go to line %1").arg(it.value()));
    }

    // Extra: procedures referenced by GOTO/GOSUB but not found yet,
    // show them with (?) so you immediately see that a target is missing.
    QSet<QString> referencedProcedures;

    for (const QString& rawLine : lines) {
        QString line = rawLine;

        const int apostrophe = line.indexOf('\'');
        if (apostrophe >= 0)
            line = line.left(apostrophe);

        QRegularExpressionMatchIterator gosubIt = gosubRegex.globalMatch(line);
        while (gosubIt.hasNext())
            referencedProcedures.insert(gosubIt.next().captured(1));

        QRegularExpressionMatchIterator gotoIt = gotoRegex.globalMatch(line);
        while (gotoIt.hasNext())
            referencedProcedures.insert(gotoIt.next().captured(1));
    }

    QStringList missingProcedures = QStringList(referencedProcedures.begin(), referencedProcedures.end());
    missingProcedures.sort(Qt::CaseInsensitive);

    for (const QString& proc : missingProcedures) {
        if (procedureLines.contains(proc) || labelLines.contains(proc))
            continue;

        QListWidgetItem* item = new QListWidgetItem(
            QString("%1:  (?)").arg(proc),
            m_proceduresList
        );
        item->setData(Qt::UserRole, -1);
        item->setToolTip(QString("Procedure not found in source"));
    }
}

void CvBasicEditorWindow::onLabelItemDoubleClicked(QListWidgetItem* item)
{
    if (!item)
        return;

    gotoSourceLine(item->data(Qt::UserRole).toInt());
}

void CvBasicEditorWindow::onProcedureItemDoubleClicked(QListWidgetItem* item)
{
    if (!item)
        return;

    gotoSourceLine(item->data(Qt::UserRole).toInt());
}

void CvBasicEditorWindow::gotoSourceLine(int lineNumber)
{
    QPlainTextEdit* ed = activeEditor();
    if (!ed || lineNumber <= 0)
        return;

    QTextBlock block = ed->document()->findBlockByNumber(lineNumber - 1);
    if (!block.isValid())
        return;

    QTextCursor cursor(block);
    ed->setTextCursor(cursor);
    ed->centerCursor();
    ed->setFocus(Qt::OtherFocusReason);

    updateStatusText(QString("Ln %1").arg(lineNumber));
    updateCursorStatus();
}


QString CvBasicEditorWindow::appRelativePath(const QString& relative) const
{
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(relative));
}

QString CvBasicEditorWindow::quotedNativePath(const QString& path) const
{
    return QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(path));
}

void CvBasicEditorWindow::closeEvent(QCloseEvent* event)
{
    if (maybeSaveBeforeDestructiveAction()) {
        saveSettings();
        event->accept();
    } else {
        event->ignore();
    }
}
