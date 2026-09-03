#include "screenwidget.h"
#include <QMutexLocker>
#include <cstring>
#include <QtGlobal>
#include <QtDebug>
#include <QPainterPath>
#include <QDateTime>
#include <QTimer>
#include <QPixmap>
#include <QDialog>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLayout>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QEvent>
#include <QStringList>
#include "CORE/cv.h"
#include "GRAPH/f18a.h"
#include "GRAPH/f18a_term80.h"
#include "GRAPH/f18a_term80_cpm.h"
#include "GRAPH/f18a_term80_tdos.h"

bool m_80colEnabled = false;

extern "C" {
    #include "CORE/cv.h"
    #include "GRAPH/tms9928a.h"
    void PutKBD(unsigned int Key);
}

extern bool m_cpm_enabled;
extern bool m_tdos_enabled;
extern bool m_cpm_selected;

// TMS9928A color palette
const QColor ScreenWidget::TMS_COLORS[16] = {
    QColor(0, 0, 0),         // 0: Transparent (Black)
    QColor(0, 0, 0),         // 1: Black
    QColor(33, 200, 66),     // 2: Medium Green
    QColor(94, 220, 120),    // 3: Light Green
    QColor(84, 85, 237),     // 4: Dark Blue
    QColor(125, 118, 252),   // 5: Light Blue
    QColor(212, 82, 77),     // 6: Dark Red
    QColor(66, 235, 245),    // 7: Cyan
    QColor(252, 85, 84),     // 8: Medium Red
    QColor(255, 121, 120),   // 9: Light Red
    QColor(212, 193, 84),    // A: Dark Yellow
    QColor(230, 206, 128),   // B: Light Yellow
    QColor(33, 176, 59),     // C: Dark Green
    QColor(201, 91, 186),    // D: Magenta
    QColor(204, 204, 204),   // E: Gray
    QColor(255, 255, 255)    // F: White
};

// ============================================================================
// 80-COLUMN MODE HELPERS / GLOBAL STATE
// ============================================================================

static bool auto80 = true;
static bool ones = false;

// Handmatige CP/M80-kleuren via rechtsklik.
// Als dit actief is, gebruiken we deze kleuren i.p.v. de automatische sampling.
static bool g_cpm80ManualColors = false;
static int  g_cpm80ManualBgIdx  = 1;   // Coleco/TMS black
static int  g_cpm80ManualFgIdx  = 15;  // Coleco/TMS white


ScreenWidget::ScreenWidget(QWidget *parent)
    : QWidget(parent),
    m_frame(COLECO_WIDTH, COLECO_HEIGHT, QImage::Format_RGB32),
    m_backgroundColor(QColor("#323232")),
    m_smoothScaling(true),
    m_isFullScreen(false),
    m_scalingMode(ModeSmooth),
    m_epxBuffer()
    //m_80colEnabled(false)
{
    // Begin met een zwart scherm
    m_frame.fill(Qt::black);
    
    // Setup 80-column font
    m_80colFont = QFont("Consolas", 9);
    m_80colFont.setStyleHint(QFont::Monospace);
    m_80colFont.setFixedPitch(true);
    m_80colFont.setBold(true);

    // Rechtsklik-menu voor CP/M80: kleuren, copy/paste en clear screen.
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested,
            this, &ScreenWidget::showCpm80ContextMenu);

    // Nodig voor CP/M80 paste-editing: Backspace past de geplakte regel aan,
    // Enter commit de regel naar CP/M.
    if (qApp)
        qApp->installEventFilter(this);
}

ScreenWidget::~ScreenWidget()
{
    if (qApp)
        qApp->removeEventFilter(this);
}


bool ScreenWidget::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);

    if (event && event->type() == QEvent::KeyPress && cpm80_is_active() && cpm80_paste_pending()) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);

        if (!keyEvent->isAutoRepeat()) {
            if (keyEvent->key() == Qt::Key_Backspace) {
                if (cpm80_paste_backspace()) {
                    update();
                    return true;
                }
            }

            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                if (cpm80_paste_commit()) {
                    update();

                    // Niet opeten: deze Enter mag de originele keyboard/CONIN
                    // routine wakker maken. cpm80.cpp levert ondertussen de
                    // geplakte regel + CR via CONIN en filtert de dubbele echo.
                    return false;
                }
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

bool ScreenWidget::cpm80CellFromMousePos(const QPoint& pos, int& col, int& row) const
{
    if (m_last80TargetRect.isNull() || !m_last80TargetRect.contains(pos))
        return false;

    const int localX = pos.x() - m_last80TargetRect.left();
    const int localY = pos.y() - m_last80TargetRect.top();

    const int screenCol = qBound(0, (localX * 80) / qMax(1, m_last80TargetRect.width()), 79);
    const int screenRow = qBound(0, (localY * 24) / qMax(1, m_last80TargetRect.height()), 23);

    const bool hasSmartKeys = cpm80_has_smartkeys() != 0;

    // Schermregel 0 is de CP/M80 titlebar.
    // Als smartkeys zichtbaar zijn, zijn schermregels 22 en 23 gereserveerd.
    if (screenRow <= 0)
        return false;
    if (hasSmartKeys && screenRow >= 22)
        return false;

    col = screenCol;
    row = screenRow - 1; // CP/M row 0..22 of 0..20
    return true;
}

bool ScreenWidget::term80CellFromMousePos(const QPoint& pos, int& col, int& row) const
{
    if (m_last80TargetRect.isNull() || !m_last80TargetRect.contains(pos))
        return false;

    const int localX = pos.x() - m_last80TargetRect.left();
    const int localY = pos.y() - m_last80TargetRect.top();

    const int screenCol = qBound(0, (localX * 80) / qMax(1, m_last80TargetRect.width()), 79);
    const int screenRow = qBound(0, (localY * 24) / qMax(1, m_last80TargetRect.height()), 23);

    /* Row 0 is reserved for the blue PNG bar. Rows 1..23 are TERM80 text. */
    if (screenRow <= 0)
        return false;

    col = screenCol;
    row = screenRow;
    return true;
}

QString ScreenWidget::term80SelectedText() const
{
    if (!cpm80HasSelection() || !f18a_term80_is_enabled())
        return QString();

    int startCol = qBound(0, m_cpm80SelectionAnchor.x(), 79);
    int startRow = qBound(1, m_cpm80SelectionAnchor.y(), 23);
    int endCol   = qBound(0, m_cpm80SelectionCurrent.x(), 79);
    int endRow   = qBound(1, m_cpm80SelectionCurrent.y(), 23);

    if (startRow > endRow || (startRow == endRow && startCol > endCol)) {
        qSwap(startRow, endRow);
        qSwap(startCol, endCol);
    }

    QStringList lines;
    for (int r = startRow; r <= endRow; ++r) {
        const int firstCol = (r == startRow) ? startCol : 0;
        const int lastCol  = (r == endRow)   ? endCol   : 79;

        QString line;
        line.reserve(lastCol - firstCol + 1);
        for (int c = firstCol; c <= lastCol; ++c) {
            unsigned char ch = f18a_term80_get_char((unsigned)r, (unsigned)c) & 0x7F;
            if (ch < 32)
                ch = ' ';
            line.append(QChar(ch));
        }

        while (!line.isEmpty() && line.endsWith(QLatin1Char(' ')))
            line.chop(1);

        lines.append(line);
    }

    return lines.join(QLatin1Char('\n'));
}


bool ScreenWidget::cpm80HasSelection() const
{
    return m_cpm80SelectionActive && (m_cpm80SelectionAnchor != m_cpm80SelectionCurrent);
}

void ScreenWidget::cpm80ClearSelection()
{
    m_cpm80Selecting = false;
    m_cpm80SelectionActive = false;
    m_cpm80SelectionAnchor = QPoint();
    m_cpm80SelectionCurrent = QPoint();
}

QString ScreenWidget::cpm80SelectedText() const
{
    if (!cpm80HasSelection() || !cpm80_is_active())
        return QString();

    const int maxCpmRow = cpm80_has_smartkeys() ? 20 : 22;

    int startCol = qBound(0, m_cpm80SelectionAnchor.x(), 79);
    int startRow = qBound(0, m_cpm80SelectionAnchor.y(), maxCpmRow);
    int endCol   = qBound(0, m_cpm80SelectionCurrent.x(), 79);
    int endRow   = qBound(0, m_cpm80SelectionCurrent.y(), maxCpmRow);

    if (startRow > endRow || (startRow == endRow && startCol > endCol)) {
        qSwap(startRow, endRow);
        qSwap(startCol, endCol);
    }

    QStringList lines;
    for (int r = startRow; r <= endRow; ++r) {
        const int firstCol = (r == startRow) ? startCol : 0;
        const int lastCol  = (r == endRow)   ? endCol   : 79;

        QString line;
        line.reserve(lastCol - firstCol + 1);
        for (int c = firstCol; c <= lastCol; ++c) {
            unsigned char ch = cpm80_get_char(r, c) & 0x7F;
            if (ch < 32)
                ch = ' ';
            line.append(QChar(ch));
        }

        while (!line.isEmpty() && line.endsWith(QLatin1Char(' ')))
            line.chop(1);

        lines.append(line);
    }

    return lines.join(QLatin1Char('\n'));
}

void ScreenWidget::mousePressEvent(QMouseEvent *event)
{
    if (event && event->button() == Qt::LeftButton && (cpm80_is_active() || f18a_term80_is_enabled())) {
        int col = 0;
        int row = 0;
        const bool ok = cpm80_is_active()
            ? cpm80CellFromMousePos(event->pos(), col, row)
            : term80CellFromMousePos(event->pos(), col, row);
        if (ok) {
            m_cpm80Selecting = true;
            m_cpm80SelectionActive = true;
            m_cpm80SelectionAnchor = QPoint(col, row);
            m_cpm80SelectionCurrent = QPoint(col, row);
            event->accept();
            update();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void ScreenWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event && m_cpm80Selecting && (cpm80_is_active() || f18a_term80_is_enabled())) {
        int col = 0;
        int row = 0;
        const bool ok = cpm80_is_active()
            ? cpm80CellFromMousePos(event->pos(), col, row)
            : term80CellFromMousePos(event->pos(), col, row);
        if (ok) {
            m_cpm80SelectionCurrent = QPoint(col, row);
            event->accept();
            update();
            return;
        }
    }

    QWidget::mouseMoveEvent(event);
}

void ScreenWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event && event->button() == Qt::LeftButton && m_cpm80Selecting) {
        m_cpm80Selecting = false;
        event->accept();
        update();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void ScreenWidget::showCpm80ContextMenu(const QPoint& pos)
{
    const bool userC80Enabled = (m_80colEnabled && (coleco_80col_enabled != 0));
    const bool term80Active = (f18a_term80_is_enabled() != 0);
    const bool classicCpm80Active = (cpm80_is_active() != 0);

    const bool isTdosMode = (m_cpm_enabled && m_tdos_enabled);
    const bool isCpmMode  = (m_cpm_enabled && !m_tdos_enabled);

    /*
 * F18A T-DOS 80C -> 40C zonder reset.
 */
    const bool f18aTdos80CanSwitchTo40 =
        (coleco_vdp_has_f18a()) &&
        isTdosMode &&
        term80Active;

    if (f18aTdos80CanSwitchTo40) {
        QMenu menu(this);

        QAction* switchTo40Action = menu.addAction(tr("Switch T-DOS back to 40 columns"));

        QAction* chosen = menu.exec(mapToGlobal(pos));

        if (chosen == switchTo40Action) {
            /*
         * T-DOS terug naar gewone F18A 40C graphics/text emulation.
         * Geen reset.
         */
            m_80colEnabled = false;
            coleco_80col_enabled = 0;

            f18a_term80_set_enabled(0);
            f18a_term80_tdos_reset();

            cpm80ClearSelection();

            ones = false;
            update();
        }

        return;
    }

    /*
     * F18A CP/M 40C -> 80C zonder reset.
     */
    const bool f18aCpm40CanSwitchTo80 =
        (coleco_vdp_has_f18a()) &&
        isCpmMode &&
        m_cpm_selected &&
        !term80Active &&
        !classicCpm80Active;

    /*
     * F18A T-DOS 40C -> 80C zonder reset.
     * T-DOS gebruikt jouw bestaande read80ColumnVRAM()
     * -> sync80ColumnVRAMToF18A() bridge.
     */
    const bool f18aTdos40CanSwitchTo80 =
        (coleco_vdp_has_f18a()) &&
        isTdosMode &&
        !term80Active;

    if (f18aCpm40CanSwitchTo80 || f18aTdos40CanSwitchTo80) {
        QMenu menu(this);

        QAction* switchTo80Action = menu.addAction(
            f18aTdos40CanSwitchTo80
                ? tr("Switch T-DOS to 80 columns")
                : tr("Switch CP/M to 80 columns")
            );

        QAction* chosen = menu.exec(mapToGlobal(pos));

        if (chosen == switchTo80Action) {
            set80ColumnMode(true);
            f18a_term80_set_enabled(1);
            f18a_term80_clear();
            cpm80ClearSelection();

            if (f18aTdos40CanSwitchTo80) {
                /*
                 * T-DOS: geen reset, geen fake ENTER nodig.
                 * Gewoon meteen de bestaande T-DOS 80C buffer naar F18A TERM80 syncen.
                 */
                sync80ColumnVRAMToF18A();
            } else {
                /*
                 * CP/M: bestaande gedrag behouden.
                 * Eén ENTER laat CP/M opnieuw een prompt naar TERM80 sturen.
                 */
                PutKBD(0x0D);
            }

            update();
        }

        return;
    }

    if (auto80 || (!classicCpm80Active && !term80Active) || (!userC80Enabled && !term80Active))
        return;

    QMenu menu(this);
    QAction* switchTo40Action = nullptr;
    if (term80Active && !classicCpm80Active) {
        switchTo40Action = menu.addAction(tr("Switch back to 40 columns"));
        menu.addSeparator();
    }

    QAction* changeColorsAction = menu.addAction(tr("Change colors..."));
    menu.addSeparator();
    QAction* copyAction = menu.addAction(cpm80HasSelection()
                                       ? tr("Copy selection to clipboard")
                                       : tr("Copy screen to clipboard"));
    QAction* pasteAction = menu.addAction(tr("Paste from clipboard"));
    menu.addSeparator();
    QAction* clearScreenAction = menu.addAction(tr("Clear screen"));

    const bool hasClipboardText = (QApplication::clipboard() && !QApplication::clipboard()->text().isEmpty());
    pasteAction->setEnabled(hasClipboardText);

    QAction* chosen = menu.exec(mapToGlobal(pos));
    if (chosen == switchTo40Action && switchTo40Action != nullptr) {
        set80ColumnMode(false);
        f18a_term80_set_enabled(0);
        f18a_term80_cpm_reset();
        cpm80_reset();
        cpm80ClearSelection();
        update();
    } else if (chosen == changeColorsAction) {
        showCpm80ColorDialog();
    } else if (chosen == copyAction) {
        if (term80Active && !classicCpm80Active)
            copyTerm80TextToClipboard();
        else
            copyCpm80TextToClipboard();
    } else if (chosen == pasteAction) {
        if (term80Active && !classicCpm80Active)
            pasteClipboardTextToTerm80();
        else
            pasteClipboardTextToEmulator();
    } else if (chosen == clearScreenAction) {
        if (term80Active && !classicCpm80Active) {
            f18a_term80_clear();
            cpm80ClearSelection();
        } else {
            cpm80_clear_screen();
            cpm80ClearSelection();
        }

        // Vraag CP/M om opnieuw een prompt te schrijven na clear.
        PutKBD(0x0D);

        update();
    }
}

void ScreenWidget::showCpm80ColorDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("CP/M 80 column colors"));
    dlg.setModal(true);

    auto* mainLayout = new QVBoxLayout(&dlg);
    auto* infoLabel = new QLabel(tr("Choose foreground and background from the Coleco 16-color palette."), &dlg);
    mainLayout->addWidget(infoLabel);

    auto* chooseLayout = new QHBoxLayout();
    auto* fgRadio = new QRadioButton(tr("Foreground"), &dlg);
    auto* bgRadio = new QRadioButton(tr("Background"), &dlg);
    fgRadio->setChecked(true);
    chooseLayout->addWidget(fgRadio);
    chooseLayout->addWidget(bgRadio);
    chooseLayout->addStretch();
    mainLayout->addLayout(chooseLayout);

    auto* preview = new QLabel(tr(" A> CP/M 80 COLUMN PREVIEW "), &dlg);
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumHeight(36);
    preview->setFrameShape(QFrame::Panel);
    preview->setFrameShadow(QFrame::Sunken);
    preview->setFont(m_80colFont);
    mainLayout->addWidget(preview);

    int selectedFg = g_cpm80ManualColors ? g_cpm80ManualFgIdx : 15;
    int selectedBg = g_cpm80ManualColors ? g_cpm80ManualBgIdx : 1;

    auto updatePreview = [&]() {
        const QColor fg = TMS_COLORS[qBound(0, selectedFg, 15)];
        const QColor bg = TMS_COLORS[qBound(0, selectedBg, 15)];
        preview->setStyleSheet(QString("QLabel { color: %1; background-color: %2; padding: 8px; }")
                               .arg(fg.name(), bg.name()));
    };

    auto contrastTextColor = [](const QColor& c) -> QString {
        const int lum = (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000;
        return (lum > 128) ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
    };

    static const char* colorNames[16] = {
        "0 Transparent", "1 Black", "2 Medium green", "3 Light green",
        "4 Dark blue", "5 Light blue", "6 Dark red", "7 Cyan",
        "8 Medium red", "9 Light red", "A Dark yellow", "B Light yellow",
        "C Dark green", "D Magenta", "E Gray", "F White"
    };

    auto* grid = new QGridLayout();
    for (int i = 0; i < 16; ++i) {
        auto* btn = new QPushButton(QString::fromLatin1(colorNames[i]), &dlg);
        btn->setMinimumSize(115, 30);
        const QColor c = TMS_COLORS[i];
        btn->setStyleSheet(QString("QPushButton { background-color: %1; color: %2; border: 1px solid #555; padding: 4px; }")
                           .arg(c.name(), contrastTextColor(c)));
        grid->addWidget(btn, i / 4, i % 4);

        connect(btn, &QPushButton::clicked, &dlg, [&, i]() {
            if (fgRadio->isChecked())
                selectedFg = i;
            else
                selectedBg = i;
            updatePreview();
        });
    }
    mainLayout->addLayout(grid);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    auto* autoButton = buttonBox->addButton(tr("Auto colors"), QDialogButtonBox::ResetRole);
    mainLayout->addWidget(buttonBox);

    connect(autoButton, &QPushButton::clicked, &dlg, [&]() {
        g_cpm80ManualColors = false;
        dlg.accept();
    });
    connect(buttonBox, &QDialogButtonBox::accepted, &dlg, [&]() {
        g_cpm80ManualColors = true;
        g_cpm80ManualFgIdx = qBound(0, selectedFg, 15);
        g_cpm80ManualBgIdx = qBound(0, selectedBg, 15);
        if (f18a_term80_is_enabled())
            f18a_term80_apply_colors((unsigned char)g_cpm80ManualFgIdx,
                                     (unsigned char)g_cpm80ManualBgIdx);
        dlg.accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.layout()->setSizeConstraint(QLayout::SetFixedSize);
    dlg.setWindowFlags(dlg.windowFlags() & ~Qt::WindowMaximizeButtonHint);

    updatePreview();
    dlg.adjustSize();
    dlg.setFixedSize(dlg.sizeHint());

    if (dlg.exec() == QDialog::Accepted) {
        update();
    }
}

void ScreenWidget::copyCpm80TextToClipboard()
{
    if (!QApplication::clipboard() || !cpm80_is_active())
        return;

    if (cpm80HasSelection()) {
        QApplication::clipboard()->setText(cpm80SelectedText());
        return;
    }

    const int cpmRows = cpm80_has_smartkeys() ? 21 : 23;

    QStringList lines;
    lines.reserve(cpmRows);

    for (int row = 0; row < cpmRows; ++row) {
        QString line;
        line.reserve(80);
        for (int col = 0; col < 80; ++col) {
            unsigned char ch = cpm80_get_char(row, col) & 0x7F;
            if (ch < 32)
                ch = ' ';
            line.append(QChar(ch));
        }

        while (!line.isEmpty() && line.endsWith(QLatin1Char(' ')))
            line.chop(1);

        lines.append(line);
    }

    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

void ScreenWidget::pasteClipboardTextToEmulator()
{
    if (!QApplication::clipboard())
        return;

    QString text = QApplication::clipboard()->text();
    if (text.isEmpty())
        return;

    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    // CP/M80 paste is command-line paste: toon tekst direct, maar stuur geen
    // Enter mee. Enter wordt later door eventFilter/cpm80_paste_commit verwerkt.
    const int firstNewline = text.indexOf(QLatin1Char('\n'));
    if (firstNewline >= 0)
        text = text.left(firstNewline);

    const QByteArray latin = text.toLatin1();
    cpm80_queue_paste_text(latin.constData());
    cpm80ClearSelection();
    update();
}


void ScreenWidget::copyTerm80TextToClipboard()
{
    if (!QApplication::clipboard() || !f18a_term80_is_enabled())
        return;

    if (cpm80HasSelection()) {
        QApplication::clipboard()->setText(term80SelectedText());
        return;
    }

    QStringList lines;
    lines.reserve(23);

    /* Row 0 is reserved for the F18A TERM80 blue bar / logo.
     * Copy only the terminal text area: rows 1..23.
     */
    for (int r = 1; r < 24; ++r) {
        QString line;
        line.reserve(80);

        for (int c = 0; c < 80; ++c) {
            unsigned char ch = f18a_term80_get_char((unsigned)r, (unsigned)c) & 0x7F;
            if (ch < 32)
                ch = ' ';
            line.append(QChar(ch));
        }

        while (!line.isEmpty() && line.endsWith(QLatin1Char(' ')))
            line.chop(1);

        lines.append(line);
    }

    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();

    QApplication::clipboard()->setText(lines.join(QLatin1Char(' ')));
}

void ScreenWidget::pasteClipboardTextToTerm80()
{
    if (!QApplication::clipboard() || !f18a_term80_is_enabled())
        return;

    QString text = QApplication::clipboard()->text();
    if (text.isEmpty())
        return;

    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    /*
     * TERM80 paste mag niet alle tekens in één keer via PutKBD() sturen.
     * De ADAM keyboard-buffer houdt effectief maar één toets tegelijk vast;
     * als we in een snelle lus sturen, blijft meestal alleen de laatste letter
     * over. Daarom voeren we de eerste clipboardregel rustig teken per teken.
     */
    const int firstNewline = text.indexOf(QLatin1Char('\n'));
    if (firstNewline >= 0)
        text = text.left(firstNewline);

    QString filtered;
    filtered.reserve(text.size());
    for (const QChar& qc : text) {
        const ushort u = qc.unicode();
        if (u >= 32 && u < 127)
            filtered.append(qc);
    }

    if (filtered.isEmpty())
        return;

    m_term80PasteQueue = filtered;
    cpm80ClearSelection();
    feedTerm80PasteNextChar();
    update();
}

void ScreenWidget::feedTerm80PasteNextChar()
{
    if (m_term80PasteQueue.isEmpty() || !f18a_term80_is_enabled())
        return;

    QTimer::singleShot(50, this, [this]() {
        if (m_term80PasteQueue.isEmpty() || !f18a_term80_is_enabled())
            return;

        const QChar qc = m_term80PasteQueue.at(0);
        m_term80PasteQueue.remove(0, 1);

        const unsigned char c = static_cast<unsigned char>(qc.toLatin1());
        if (c >= 32 && c < 127)
            PutKBD(static_cast<unsigned int>(c));

        if (!m_term80PasteQueue.isEmpty())
            feedTerm80PasteNextChar();

        update();
    });
}

// CP/M80 kleur-sampling uit het echte 40-kolommen beeld.
// CP/M gebruikt soms niet simpel VR7 foreground/background, maar kleuren die al
// in de normale framebuffer zitten. Daarom nemen we voor CP/M80 de kleuren
// rechtstreeks uit imageToDraw voordat de 80-col overlay wordt getekend.
static QColor g_cpm80SampleBgColor(0, 0, 0);
static QColor g_cpm80SampleFgColor(255, 255, 255);
static bool   g_cpm80HaveSampledColors = false;

static inline int colorDistanceSq(const QColor& a, const QColor& b)
{
    const int dr = a.red()   - b.red();
    const int dg = a.green() - b.green();
    const int db = a.blue()  - b.blue();
    return dr * dr + dg * dg + db * db;
}

static void sampleCPM80ColorsFrom40ColImage(const QImage& src)
{
    if (src.isNull()) {
        g_cpm80HaveSampledColors = false;
        return;
    }

    QImage img = src.convertToFormat(QImage::Format_RGB32);

    struct Bucket {
        int count = 0;
        long long r = 0;
        long long g = 0;
        long long b = 0;
    };

    Bucket buckets[4096];

    // Een kleine rand overslaan; daar zitten soms borders/artefacts die de
    // foreground-keuze kunnen vervuilen.
    const int x0 = qMax(0, img.width()  / 32);
    const int y0 = qMax(0, img.height() / 32);
    const int x1 = qMin(img.width(),  img.width()  - x0);
    const int y1 = qMin(img.height(), img.height() - y0);

    for (int y = y0; y < y1; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = x0; x < x1; ++x) {
            const QRgb px = line[x];
            const int r = qRed(px);
            const int g = qGreen(px);
            const int b = qBlue(px);
            const int key = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
            Bucket& bk = buckets[key];
            bk.count++;
            bk.r += r;
            bk.g += g;
            bk.b += b;
        }
    }

    int bgKey = -1;
    int bgCount = -1;
    for (int i = 0; i < 4096; ++i) {
        if (buckets[i].count > bgCount) {
            bgCount = buckets[i].count;
            bgKey = i;
        }
    }

    if (bgKey < 0 || bgCount <= 0) {
        g_cpm80HaveSampledColors = false;
        return;
    }

    const Bucket& bg = buckets[bgKey];
    g_cpm80SampleBgColor = QColor(0,0,0) ; //QColor(int(bg.r / bg.count), int(bg.g / bg.count), int(bg.b / bg.count));
    //g_cpm80SampleBgColor = QColor(int(bg.r / bg.count), int(bg.g / bg.count), int(bg.b / bg.count));

    int fgKey = -1;
    int fgCount = -1;
    int fallbackFgKey = -1;
    int fallbackFgCount = -1;

    for (int i = 0; i < 4096; ++i) {
        if (i == bgKey || buckets[i].count <= 0)
            continue;

        const Bucket& bk = buckets[i];
        QColor c(int(bk.r / bk.count), int(bk.g / bk.count), int(bk.b / bk.count));

        // Moet duidelijk afwijken van de achtergrond.
        if (colorDistanceSq(c, g_cpm80SampleBgColor) < 900)
            continue;

        // Bewaar altijd een fallback, ook zwart.
        if (bk.count > fallbackFgCount) {
            fallbackFgCount = bk.count;
            fallbackFgKey = i;
        }

        // Zwart is vaak rand/lege pixels. Alleen als er echt geen andere kleur
        // is, gebruiken we zwart als fallback.
        if ((c.red() + c.green() + c.blue()) < 30)
            continue;

        if (bk.count > fgCount) {
            fgCount = bk.count;
            fgKey = i;
        }
    }

    if (fgKey < 0)
        fgKey = fallbackFgKey;

    if (fgKey >= 0) {
        const Bucket& fg = buckets[fgKey];
        g_cpm80SampleFgColor = QColor(255,255,255) ; //QColor(int(fg.r / fg.count), int(fg.g / fg.count), int(fg.b / fg.count));
        g_cpm80HaveSampledColors = true;
    } else {
        // Laatste veiligheid: contrasterende kleur kiezen.
        const int lum = (g_cpm80SampleBgColor.red() * 299 +
                         g_cpm80SampleBgColor.green() * 587 +
                         g_cpm80SampleBgColor.blue() * 114) / 1000;
        g_cpm80SampleFgColor = (lum > 128) ? QColor(0, 0, 0) : QColor(255, 255, 255);
        g_cpm80HaveSampledColors = true;
    }
}

// --- TDOS 80-col detectie (ADAMEm-style) ---
static inline uint8_t z80rb(uint16_t a) {
    return (uint8_t)coleco_ReadByte(a);
}

static inline uint16_t z80rw(uint16_t a) {
    return (uint16_t)z80rb(a) | ((uint16_t)z80rb(a + 1) << 8);
}

// Return: startadres van TDOS 80-col buffer (0 = niet actief)
static uint16_t CheckTDOS80BufferAddr()
{
    uint16_t base = (uint16_t)z80rb(0x01) | ((uint16_t)z80rb(0x02) << 8);

    uint16_t addr = (uint16_t)z80rb(0x01) | ((uint16_t)z80rb(0x02) << 8);
    addr = (uint16_t)(addr + 0x6D - 3);

    uint16_t routinePtr = z80rw(addr);

    // Signature check (exact zoals ADAMEm)
    if ( z80rb(routinePtr + 0)  != 0xF5) return 0;
    if ( z80rb(routinePtr + 1)  != 0xC5) return 0;
    if ( z80rb(routinePtr + 2)  != 0xD5) return 0;
    if ( z80rb(routinePtr + 3)  != 0xCD) return 0;
    if ( z80rb(routinePtr + 6)  != 0x30) return 0;
    if ( z80rb(routinePtr + 8)  != 0xE1) return 0;
    if ( z80rb(routinePtr + 9)  != 0x11) return 0;
    if ( z80rb(routinePtr + 12) != 0x01) return 0;
    if ( z80rb(routinePtr + 13) != 0x00) return 0;
    if ( z80rb(routinePtr + 14) != 0x04) return 0;
    if ( z80rb(routinePtr + 15) != 0xED) return 0;
    if ( z80rb(routinePtr + 16) != 0xB0) return 0;

   // if ( (tms.VR[0] & 0x02) != 0x00 ) return 0;
   // if ( (tms.VR[1] & 0x18) != 0x10 ) return 0;

    uint16_t bufBase = z80rw((uint16_t)(routinePtr + 10));
    uint16_t result  = (uint16_t)(bufBase + 0x400);

    return result;
}

static int GetTDOSNumLines()
{
    uint16_t i = (uint16_t)z80rb(0x01) | ((uint16_t)z80rb(0x02) << 8);
    i = (uint16_t)(i + 0x64 - 3);

    uint16_t p = z80rw(i);
    uint16_t q = z80rw((uint16_t)(p + 3));
    int num = (int)z80rb(q) + 1;
    if (num < 0) num = 0;
    if (num > 24) num = 24;
    return num;
}


void ScreenWidget::setScalingMode(ScreenWidget::ScalingMode mode)
{
    if (m_scalingMode == mode) return;
    m_scalingMode = mode;
    update();
}

// Deze functie implementeert het complete Scale2x/EPX (4-regels) algoritme
void ScreenWidget::applyEPX(const QImage& source)
{
    // Zorg dat buffer de juiste (2x) grootte heeft
    const int w = source.width();
    const int h = source.height();
    const QSize targetSize(w * 2, h * 2);

    if (m_epxBuffer.size() != targetSize) {
        m_epxBuffer = QImage(targetSize, QImage::Format_RGB32);
    }

    // Valideer bronformaat (moet 32-bit zijn voor quint32 pointers)
    if (source.format() != QImage::Format_RGB32 && source.format() != QImage::Format_ARGB32) {
        QImage convertedSource = source.convertToFormat(QImage::Format_RGB32);
        if (convertedSource.isNull()) {
            qWarning() << "EPX: Kan bron-image niet converteren naar 32-bit.";
            m_epxBuffer.fill(Qt::magenta);
            return;
        }
        applyEPX(convertedSource);
        return;
    }

    // Gebruik pointers voor snelheid
    const quint32* src = reinterpret_cast<const quint32*>(source.bits());
    quint32* dst = reinterpret_cast<quint32*>(m_epxBuffer.bits());

    // Pitch in pixels (aantal quint32 per scanline)
    const int srcPitch = source.bytesPerLine() / 4;
    const int dstPitch = m_epxBuffer.bytesPerLine() / 4;

    for (int y = 0; y < h; ++y) {
        const quint32* srcLine = src + y * srcPitch;

        // Lijnen voor buren (met grenscontrole)
        const quint32* lineA = (y > 0)   ? (src + (y - 1) * srcPitch) : srcLine;
        const quint32* lineD = (y < h - 1) ? (src + (y + 1) * srcPitch) : srcLine;

        quint32* dstLine0 = dst + (y * 2) * dstPitch;
        quint32* dstLine1 = dst + (y * 2 + 1) * dstPitch;

        for (int x = 0; x < w; ++x) {
            const quint32 P = srcLine[x];
            const quint32 A = lineA[x];
            const quint32 B = (x < w - 1) ? srcLine[x + 1] : P;
            const quint32 C = (x > 0)   ? srcLine[x - 1] : P;
            const quint32 D = lineD[x];

            quint32 p1 = P, p2 = P, p3 = P, p4 = P;

            // Regel 1: Links & Boven
            if (C == A && C != D && A != B) p1 = A;
            // Regel 2: Boven & Rechts
            if (A == B && A != C && B != D) p2 = B;
            // Regel 3: Links & Onder
            if (C == D && C != A && D != B) p3 = C;
            // Regel 4: Rechts & Onder
            if (B == D && B != C && D != A) p4 = B;

            // 4. Schrijf naar de 2x2 doelbuffer
            dstLine0[x * 2]     = p1;
            dstLine0[x * 2 + 1] = p2;
            dstLine1[x * 2]     = p3;
            dstLine1[x * 2 + 1] = p4;
        }
    }
}

void ScreenWidget::applyLCDizeFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);

            // LCD subpixel pattern effect
            if (x % 3 == 0) {
                r = qBound(0, r + 20, 255);
            } else if (x % 3 == 1) {
                g = qBound(0, g + 20, 255);
            } else {
                b = qBound(0, b + 20, 255);
            }

            line[x] = qRgb(r, g, b);
        }
    }
}

void ScreenWidget::applyTVScanlinesFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        if (y % 2 == 1) {
            QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < w; ++x) {
                QRgb pixel = line[x];
                int r = qRed(pixel) * 0.7;
                int g = qGreen(pixel) * 0.7;
                int b = qBlue(pixel) * 0.7;
                line[x] = qRgb(r, g, b);
            }
        }
    }
}

void ScreenWidget::applyRasterizeFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);

            // Apply raster pattern
            if ((x + y) % 2 == 0) {
                r = qBound(0, r - 30, 255);
                g = qBound(0, g - 30, 255);
                b = qBound(0, b - 30, 255);
            }

            line[x] = qRgb(r, g, b);
        }
    }
}

void ScreenWidget::applyMonochromeFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int gray = (qRed(pixel) + qGreen(pixel) + qBlue(pixel)) / 3;
            line[x] = qRgb(gray, gray, gray);
        }
    }
}

void ScreenWidget::applySepiaFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);

            int tr = qBound(0, (int)(r * 0.393 + g * 0.769 + b * 0.189), 255);
            int tg = qBound(0, (int)(r * 0.349 + g * 0.686 + b * 0.168), 255);
            int tb = qBound(0, (int)(r * 0.272 + g * 0.534 + b * 0.131), 255);

            line[x] = qRgb(tr, tg, tb);
        }
    }
}

void ScreenWidget::applyGreenCRTFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int gray = (qRed(pixel) + qGreen(pixel) + qBlue(pixel)) / 3;
            line[x] = qRgb(0, gray, 0);
        }
    }
}

void ScreenWidget::applyAmberCRTFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int gray = (qRed(pixel) + qGreen(pixel) + qBlue(pixel)) / 3;
            int r = qBound(0, gray, 255);
            int g = qBound(0, (int)(gray * 0.7), 255);
            line[x] = qRgb(r, g, 0);
        }
    }
}

void ScreenWidget::applyCMYRasterFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);

            if (x % 3 == 0) {      // Cyan
                r = qBound(0, r - 50, 255);
            } else if (x % 3 == 1) { // Magenta
                g = qBound(0, g - 50, 255);
            } else {               // Yellow
                b = qBound(0, b - 50, 255);
            }

            line[x] = qRgb(r, g, b);
        }
    }
}

void ScreenWidget::applyRGBRasterFilter(QImage& image)
{
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb pixel = line[x];
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);

            if (x % 3 == 0) {
                g = qBound(0, g - 50, 255);
                b = qBound(0, b - 50, 255);
            } else if (x % 3 == 1) {
                r = qBound(0, r - 50, 255);
                b = qBound(0, b - 50, 255);
            } else {
                r = qBound(0, r - 50, 255);
                g = qBound(0, g - 50, 255);
            }

            line[x] = qRgb(r, g, b);
        }
    }
}

void ScreenWidget::setBackgroundColor(const QColor& color)
{
    m_backgroundColor = color;
    update();
}

QSize ScreenWidget::sizeHint() const
{
    return QSize(COLECO_WIDTH * 2, COLECO_HEIGHT * 2);
}

QSize ScreenWidget::minimumSizeHint() const {
    return QSize(COLECO_WIDTH, COLECO_HEIGHT);
}

void ScreenWidget::setFullScreenMode(bool enabled)
{
    if (m_isFullScreen == enabled) return;
    m_isFullScreen = enabled;
    update();
}

void ScreenWidget::setScanlinesMode(ScanlinesMode mode)
{
    if (m_scanlinesMode == mode) return;
    m_scanlinesMode = mode;
    update();
}

void ScreenWidget::setColorFilterMode(ColorFilterMode mode)
{
    if (m_colorFilterMode == mode) return;
    m_colorFilterMode = mode;
    update();
}

void ScreenWidget::updateFrame(const QImage &frame)
{
    {
        QMutexLocker locker(&m_mutex);
        m_frame = frame.copy();
    }
    update();
}

void ScreenWidget::setFrame(const QImage &img)
{
    QMutexLocker locker(&m_mutex);
    m_frame = img.copy();

    locker.unlock();
    update();
}

void ScreenWidget::setSmoothScaling(bool enabled)
{
    if (m_smoothScaling == enabled) return;

    m_smoothScaling = enabled;
    update();
}

void ScreenWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);

    // 1. Haal de frame-copy op veilige wijze op
    QImage frameCopy;
    {
        QMutexLocker lock(&m_mutex);
        frameCopy = m_frame;
    }

    // Achtergrondkleur bepalen
    QColor bgColor = m_isFullScreen ? Qt::transparent : m_backgroundColor;
    p.fillRect(rect(), bgColor);

    // Controleer of er beelddata is
    if (frameCopy.isNull() || frameCopy.width() == 0 || frameCopy.height() == 0) {
        return;
    }

    // 2. Pas scaling/filters toe
    QImage imageToDraw;
    bool useSmoothFinalScale = (m_scalingMode != ModeSharp);

    if (m_scalingMode == ModeEPX) {
        applyEPX(frameCopy);
        imageToDraw = m_epxBuffer;
    } else {
        imageToDraw = frameCopy;
    }

    // Scanlines / filters toepassen indien nodig
    if (m_scanlinesMode != ScanlinesOff) {
        QImage filteredImage = imageToDraw.copy();

        if (m_scanlinesMode == ScanlinesTV)
            applyTVScanlinesFilter(filteredImage);
        else if (m_scanlinesMode == ScanlinesLCD)
            applyLCDizeFilter(filteredImage);
        else if (m_scanlinesMode == ScanlinesRaster)
            applyRasterizeFilter(filteredImage);

        imageToDraw = filteredImage;
    }

    if (m_colorFilterMode != ColorFilterOff) {
        QImage filteredImage = imageToDraw.copy();

        if (filteredImage.format() != QImage::Format_RGB32)
            filteredImage = filteredImage.convertToFormat(QImage::Format_RGB32);

        if (m_colorFilterMode == ColorFilterMonochrome)
            applyMonochromeFilter(filteredImage);
        else if (m_colorFilterMode == ColorFilterSepia)
            applySepiaFilter(filteredImage);
        else if (m_colorFilterMode == ColorFilterGreenCRT)
            applyGreenCRTFilter(filteredImage);
        else if (m_colorFilterMode == ColorFilterAmberCRT)
            applyAmberCRTFilter(filteredImage);
        else if (m_colorFilterMode == ColorFilterCMY)
            applyCMYRasterFilter(filteredImage);
        else if (m_colorFilterMode == ColorFilterRGB)
            applyRGBRasterFilter(filteredImage);

        imageToDraw = filteredImage;
    }

    // 3. Berekening van de targetRect met padding voor de border
    const int b = 4;
    QRect availableSpace = rect().adjusted(b, b, -b, -b);

    const bool f18aWide80Frame = (frameCopy.width() >= 480 && frameCopy.height() == 192);

    /*
     * F18A TERM80 levert intern 480x192, maar moet visueel dubbelhoog worden getoond
     * als 480x384. Hierdoor krijgt 80-col een mooiere monitorverhouding.
     */
    const double sourceAspect = f18aWide80Frame
                                    ? (480.0 / 384.0)
                                    : ((double)imageToDraw.width() / (double)imageToDraw.height());

    const double targetAspect = (double)availableSpace.width() / (double)availableSpace.height();

    QRect targetRect;

    if (targetAspect > sourceAspect) {
        // Widget is breder dan beeld: gebruik volledige beschikbare hoogte
        int scaledWidth = qRound(availableSpace.height() * sourceAspect);
        int offsetX = availableSpace.left() + (availableSpace.width() - scaledWidth) / 2;
        targetRect = QRect(offsetX, availableSpace.top() + 8, scaledWidth, availableSpace.height() - 8);
    } else {
        // Widget is smaller dan beeld: gebruik volledige beschikbare breedte
        int scaledHeight = qRound(availableSpace.width() / sourceAspect);
        int offsetY = availableSpace.top() + (availableSpace.height() - scaledHeight) / 2;
        targetRect = QRect(availableSpace.left(), offsetY + 8, availableSpace.width(), scaledHeight - 8);
    }

    // 4. Renderen van het beeld
    m_last80TargetRect = targetRect;
    p.setRenderHint(QPainter::SmoothPixmapTransform, useSmoothFinalScale);

    const bool isF18A = (coleco_vdp_has_f18a());

    /*
     * Jouw bestaande conventie:
     *
     * CP/M  = m_cpm_enabled && !m_tdos_enabled
     * T-DOS = m_cpm_enabled &&  m_tdos_enabled
     */
    const bool isTdosMode = (m_cpm_enabled && m_tdos_enabled);
    const bool isCpmMode  = (m_cpm_enabled && !m_tdos_enabled);

    /*
     * auto80 betekent alleen: T-DOS heeft een 80-col buffer klaarstaan.
     * De hardwareknop / user setting blijft de echte enable.
     */
    uint16_t buf = CheckTDOS80BufferAddr();
    auto80 = (buf != 0);

    const bool userC80Enabled = (m_80colEnabled && (coleco_80col_enabled != 0));

    /*
     * F18A + T-DOS 80C:
     * T-DOS gebruikt geen CP/M CONOUT-terminal.
     * We lezen de T-DOS 80C RAM/VDP-buffer en kopiëren die naar F18A TERM80.
     */
    bool f18aTdosTerm80Ready = false;

    if (userC80Enabled &&
        isTdosMode &&
        isF18A)
    {
        sync80ColumnVRAMToF18A();
        f18aTdosTerm80Ready = true;
    }

    const bool f18aTerm80Active = (f18a_term80_is_enabled() != 0);

    /*
     * Voor CP/M80 nemen we de kleuren uit het echte 40-col beeld.
     * T-DOS gebruikt zijn eigen read80ColumnVRAM/sync80ColumnVRAMToF18A-pad.
     */
    if (!f18aTerm80Active && userC80Enabled && !auto80 && cpm80_is_active()) {
        sampleCPM80ColorsFrom40ColImage(imageToDraw);

        // CP/M80 cursor repaint/timer
        static qint64 lastCursorRepaintRequest = 0;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

        if (nowMs - lastCursorRepaintRequest > 200) {
            lastCursorRepaintRequest = nowMs;

            QTimer::singleShot(250, this, [this]() {
                if (cpm80_is_active())
                    this->update();
            });
        }
    } else {
        g_cpm80HaveSampledColors = false;
    }

    /*
     * F18A tekent altijd zijn eigen framebuffer.
     *
     * 40C = gewone F18A graphics/text renderer
     * 80C = F18A TERM80 framebuffer
     *
     * De oude render80ColumnText() mag alleen voor niet-F18A gebruikt worden.
     */
    if (isF18A) {
        p.drawImage(targetRect, imageToDraw);
    }
    else if (userC80Enabled && (auto80 || cpm80_is_active())) {
        render80ColumnText(p, targetRect);
    }
    else {
        p.drawImage(targetRect, imageToDraw);
    }

    /*
     * F18A TERM80 titlebar.
     *
     * BELANGRIJK:
     * Deze titelbalk is ALLEEN voor CP/M.
     * T-DOS krijgt GEEN titelbalk en GEEN PNG.
     */
    if (f18aWide80Frame &&
        f18a_term80_is_enabled() &&
        isCpmMode &&
        f18a_term80_cpm_is_active())
    {
        const int f18a80BlueBarHeightPx = 18;

        const QRect blueBarRect(targetRect.left(),
                                targetRect.top(),
                                targetRect.width(),
                                qMin(f18a80BlueBarHeightPx, targetRect.height()));

        p.fillRect(blueBarRect, TMS_COLORS[4]); // donkerblauw

        static QPixmap f18aCpmLogo;
        static bool f18aCpmLogoTriedLoad = false;

        if (!f18aCpmLogoTriedLoad) {
            f18aCpmLogoTriedLoad = true;

            f18aCpmLogo.load(":/images/images/cpmlogo.png");

            if (f18aCpmLogo.isNull())
                f18aCpmLogo.load(":/images/images/adam_cpm_logo.png");

            if (f18aCpmLogo.isNull())
                f18aCpmLogo.load("cpmlogo.png");
        }

        if (!f18aCpmLogo.isNull() && blueBarRect.height() > 2) {
            const int logoMaxHeight = qMax(1, blueBarRect.height() - 2);
            const QPixmap scaledLogo = f18aCpmLogo.scaledToHeight(logoMaxHeight, Qt::SmoothTransformation);

            const int logoX = blueBarRect.left() + 4;
            const int logoY = blueBarRect.top() + (blueBarRect.height() - scaledLogo.height()) / 2;

            p.drawPixmap(logoX, logoY, scaledLogo);
        }

        {
            QFont titleFont = m_80colFont;
            titleFont.setBold(true);

            p.save();
            p.setFont(titleFont);
            p.setPen(Qt::white);
            p.drawText(blueBarRect, Qt::AlignCenter, tr("Welcome to CP/M 80 columns"));
            p.restore();
        }

        /*
         * CP/M smartkeys blijven ook CP/M-only.
         */
        if (f18a_term80_cpm_has_smartkeys())
        {
            static QPixmap f18aSmartKeyBar;
            static bool f18aSmartKeyBarTriedLoad = false;

            if (!f18aSmartKeyBarTriedLoad) {
                f18aSmartKeyBarTriedLoad = true;

                f18aSmartKeyBar.load(":/images/images/cpm_smartkeys.png");

                if (f18aSmartKeyBar.isNull())
                    f18aSmartKeyBar.load("cpm_smartkeys.png");
            }

            if (!f18aSmartKeyBar.isNull()) {
                const int barX = targetRect.left();
                const int row22Y = targetRect.top() + (22 * targetRect.height()) / 24;
                const int row24Y = targetRect.top() + targetRect.height();

                const int barY = row22Y;
                const int barW = targetRect.width();
                const int barH = qMax(1, row24Y - row22Y);

                p.drawPixmap(barX, barY, barW, barH, f18aSmartKeyBar);

                const int keyCount = 6;
                const int pngLayoutW = 743;
                const int pngLeftOffset = 24;
                const int pngLabelW = 116;
                const double pngScaleX = (double)barW / (double)pngLayoutW;

                const int row23Y = targetRect.top() + (23 * targetRect.height()) / 24;
                const int rowH = qMax(1, targetRect.height() / 24);

                QFont keyFont = m_80colFont;
                keyFont.setBold(true);

                p.save();
                p.setFont(keyFont);
                p.setPen(Qt::white);

                for (int i = 0; i < keyCount; ++i) {
                    QString label = QString::fromLatin1(f18a_term80_cpm_get_smartkey_text(i)).trimmed();

                    if (label.length() > 4)
                        label = label.left(4);

                    const int labelX = barX + (int)((pngLeftOffset + (i * pngLabelW)) * pngScaleX);
                    const int labelW = (int)(pngLabelW * pngScaleX);

                    QRect textRect(labelX, row23Y, labelW, rowH);
                    p.drawText(textRect, Qt::AlignCenter, label);
                }

                p.restore();
            }
        }
    }

    /*
     * F18A TERM80 selection overlay.
     *
     * De eigenlijke TERM80 tekst zit al in het 480x192 frame.
     * Bij CP/M is row 0 de titlebar. Bij T-DOS is er geen titlebar,
     * maar selectie vanaf row 1 houden we voorlopig zo om niets stuk te maken.
     */
    if (f18aWide80Frame && f18a_term80_is_enabled() && cpm80HasSelection()) {
        int startCol = qBound(0, m_cpm80SelectionAnchor.x(), 79);
        int startRow = qBound(1, m_cpm80SelectionAnchor.y(), 23);
        int endCol   = qBound(0, m_cpm80SelectionCurrent.x(), 79);
        int endRow   = qBound(1, m_cpm80SelectionCurrent.y(), 23);

        if (startRow > endRow || (startRow == endRow && startCol > endCol)) {
            qSwap(startRow, endRow);
            qSwap(startCol, endCol);
        }

        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(70, 130, 220, 120));

        for (int row = startRow; row <= endRow; ++row) {
            const int firstCol = (row == startRow) ? startCol : 0;
            const int lastCol  = (row == endRow)   ? endCol   : 79;

            const int x1 = targetRect.left() + (firstCol * targetRect.width()) / 80;
            const int x2 = targetRect.left() + ((lastCol + 1) * targetRect.width()) / 80;
            const int y1 = targetRect.top()  + (row * targetRect.height()) / 24;
            const int y2 = targetRect.top()  + ((row + 1) * targetRect.height()) / 24;

            p.fillRect(QRect(QPoint(x1, y1), QPoint(x2 - 1, y2 - 1)),
                       QColor(70, 130, 220, 120));
        }

        p.restore();
    }

    // 5. Teken het kader precies buiten de targetRect
    QColor kaderColor(22, 22, 22);

    p.fillRect(targetRect.left() - b,
               targetRect.top() - b,
               targetRect.width() + (2 * b),
               b,
               kaderColor); // boven

    p.fillRect(targetRect.left() - b,
               targetRect.bottom() + 1,
               targetRect.width() + (2 * b),
               b,
               kaderColor); // onder

    p.fillRect(targetRect.left() - b,
               targetRect.top() - b,
               b,
               targetRect.height() + (2 * b),
               kaderColor); // links

    p.fillRect(targetRect.right() + 1,
               targetRect.top() - b,
               b,
               targetRect.height() + (2 * b),
               kaderColor); // rechts

    // Debug indien nodig:
    // qDebug() << "TDOS80 ready =" << f18aTdosTerm80Ready
    //          << "term80 =" << f18a_term80_is_enabled()
    //          << "f18a80 =" << f18a_is_80col_enabled();
}

// ============================================================================
// 80-COLUMN MODE FUNCTIONS
// ============================================================================

static unsigned int readCurrentNameTableBase()
{
    const unsigned char vr2 = coleco_vdp_read_register(2);

    return ((unsigned int)(vr2 & 0x0F) << 10) & 0x3FFF;
}

void ScreenWidget::set80ColumnMode(bool enabled)
{
    m_80colEnabled = enabled;
    coleco_80col_enabled = enabled ? 1 : 0;


    if (coleco_vdp_has_f18a()) {
        if (enabled) {
            if (m_tdos_enabled) {
                /*
                 * T-DOS:
                 * Alleen C80 hardware zichtbaar maken.
                 * Nog NIET naar F18A TERM80 schakelen tot de T-DOS buffer bestaat.
                 */
                f18a_term80_tdos_reset();
                f18a_term80_set_enabled(1);
                 f18a_term80_clear();
            } else {
                /*
                 * CP/M:
                 * CP/M gebruikt de echte terminal-output, dus mag direct naar TERM80.
                 */
                f18a_term80_cpm_reset();
                f18a_term80_set_enabled(1);
                f18a_term80_clear();
            }
        } else {
            f18a_term80_set_enabled(0);
            f18a_set_80col_enabled(0);

            f18a_term80_tdos_reset();
            f18a_term80_cpm_reset();
            cpm80_reset();
        }
    }

    ones = false;

    update();
}

static unsigned char readCurrentVramByte(unsigned int addr)
{
    addr &= 0x3FFF;

    return coleco_vdp_peek_vram(addr);
}

void ScreenWidget::read80ColumnVRAM(char textBuffer[24][80], unsigned char colorBuffer[24][80])
{
    const unsigned char vr7 = coleco_vdp_read_register(7);

    unsigned char fgIdx = (vr7 >> 4) & 0x0F;
    unsigned char bgIdx = vr7 & 0x0F;

    if (fgIdx == 0)
        fgIdx = 15;

    if (bgIdx == 0)
        bgIdx = 1;

    // // 1) Probeer TDOS 80-col buffer in RAM (ADAMEm manier)
     uint16_t tdosAddr = CheckTDOS80BufferAddr();

     if (tdosAddr != 0) {

         int numLines = GetTDOSNumLines();

         if (numLines <= 0)
             numLines = 21;

         if (numLines > 24)
             numLines = 24;

         QString dump;
         for (int i = 0; i < 80; ++i) {
             unsigned char b = z80rb((uint16_t)(tdosAddr + i));
             dump += QString("%1 ").arg(b, 2, 16, QChar('0')).toUpper();
         }

         // Copy offscreen buffer: numLines * 80 bytes uit Z80-RAM
         for (int row = 0; row < numLines; ++row) {
             for (int col = 0; col < 80; ++col) {
                 textBuffer[row][col] = (char)z80rb((uint16_t)(tdosAddr + row * 80 + col));
                 colorBuffer[row][col] = fgIdx;
             }
         }

         unsigned int nameTableBase = readCurrentNameTableBase();

         for (int r = 21; r < 24; ++r) {
             for (int c = 0; c < 40; ++c) {
                 unsigned char raw = readCurrentVramByte((nameTableBase + r * 40 + c) & 0x3FFF);
                 textBuffer[r][c] = (char)raw;
                 colorBuffer[r][c] = fgIdx;
             }

             for (int c = 40; c < 80; ++c) {
                 textBuffer[r][c] =(char)(0x80 | ' ');
                 colorBuffer[r][c] = fgIdx;
             }
         }

        textBuffer[22][67]=' ';
        textBuffer[22][68]='T';
        textBuffer[22][69]='-';
        textBuffer[22][70]='D';
        textBuffer[22][71]='O';
        textBuffer[22][72]='S';
        textBuffer[22][73]=' ';
        textBuffer[22][74]='8';
        textBuffer[22][75]='0';
        textBuffer[22][76]=' ';

        return; // TDOS-pad gebruikt, klaar.
    }

    // 2) CP/M80 terminalbuffer: alleen actief wanneer de hardwareknop C80 aan staat.
    if (cpm80_is_active()) {
        const int cpmRows = cpm80_has_smartkeys() ? 21 : 23;

        // Schermregel 0 is een eigen title bar.
        // De echte CP/M80-terminal gebruikt intern 21 of 23 regels en wordt
        // op schermregels 1..21 of 1..23 getekend.
        for (int row = 0; row < 24; ++row) {
            for (int col = 0; col < 80; ++col) {
                textBuffer[row][col] = ' ';
                colorBuffer[row][col] = 1;
            }
        }

        // CP/M rows 0..20 of 0..22 -> screen rows 1..21 of 1..23
        for (int row = 0; row < cpmRows; ++row) {
            for (int col = 0; col < 80; ++col) {
                textBuffer[row + 1][col] = (char)cpm80_get_char(row, col);
                colorBuffer[row + 1][col] = cpm80_get_color(row, col);
            }
        }

        // Regel 0 blijft leeg/gereserveerd.
        // De blauwe balk + PNG worden nu pixel-perfect in ScreenWidget::paintEvent()
        // als echte QPainter-rechthoek getekend, niet meer als tekst-titlebar.

        return;
    }

    // 3) Veilige fallback: normale TMS nametable is 40 kolommen breed.
    // Niet lineair 24x80 lezen, want dan verschuiven de regels.
    unsigned int nameTableBase = (coleco_vdp_read_register(2) & 0x0F) << 10;
    for (int row = 0; row < 24; ++row) {
        for (int col = 0; col < 80; ++col) {
            textBuffer[row][col] = ' ';
            colorBuffer[row][col] = fgIdx;
        }
        for (int col = 0; col < 40; ++col) {
            unsigned char rawByte = coleco_vdp_peek_vram((nameTableBase + row * 40 + col) & 0x3FFF);
            textBuffer[row][col] = (char)rawByte;
            colorBuffer[row][col] = fgIdx;
        }
    }
}

void ScreenWidget::setText(QPainter& painter, const QRect& targetRect,int charWidth, int charHeight, unsigned char globalBgIdx)
{

    painter.save();
    painter.translate(targetRect.left(), targetRect.top());
    // Schaal naar een virtueel canvas van 80x24
    painter.scale((double)targetRect.width() / (80.0 * charWidth),
                  (double)targetRect.height() / (24.0 * charHeight));

    globalBgIdx = coleco_vdp_read_register(7) & 0x0F;

    // CP/M80 gebruikt de handmatige kleuren of de achtergrondkleur uit het echte 40-col beeld.
    // TDOS gebruikt de normale TMS-kleurindex.
    if (!auto80 && cpm80_is_active() && g_cpm80ManualColors) {
        painter.fillRect(0, 0, 80 * charWidth, 24 * charHeight,
                         TMS_COLORS[qBound(0, g_cpm80ManualBgIdx, 15)]);
    } else if (!auto80 && cpm80_is_active() && g_cpm80HaveSampledColors) {
        painter.fillRect(0, 0, 80 * charWidth, 24 * charHeight, g_cpm80SampleBgColor);
    } else {
        painter.fillRect(0, 0, 80 * charWidth, 24 * charHeight, TMS_COLORS[globalBgIdx]);
    }
}

void ScreenWidget::sync80ColumnVRAMToF18A()
{
    char textBuffer[24][80];
    unsigned char colorBuffer[24][80];

    for (int row = 0; row < 24; ++row) {
        for (int col = 0; col < 80; ++col) {
            textBuffer[row][col] = ' ';
            colorBuffer[row][col] = 15;
        }
    }

    read80ColumnVRAM(textBuffer, colorBuffer);

    f18a_term80_set_enabled(1);

    unsigned char vr7 = coleco_vdp_read_register(7);

    unsigned char fg40 = (vr7 >> 4) & 0x0F;
    unsigned char bg40 = vr7 & 0x0F;

    if (fg40 == 0)
        fg40 = 15;

    if (bg40 == 0)
        bg40 = 1;

    if (fg40 == bg40) {
        fg40 = 15;
        bg40 = 1;
    }

    for (int row = 0; row < 24; ++row) {
        for (int col = 0; col < 80; ++col) {
            unsigned char raw = (unsigned char)textBuffer[row][col];
            unsigned char ch = (unsigned char)(raw & 0x7F);

            if (ch < 32)
                ch = ' ';

            unsigned char fg = colorBuffer[row][col] & 0x0F;
            unsigned char bg = bg40;

            if (fg == 0)
                fg = fg40;

            if (fg == bg) {
                fg = fg40;
                bg = bg40;
            }

            if (fg == bg) {
                fg = 15;
                bg = 1;
            }

            if (raw & 0x80) {
                unsigned char tmp = fg;
                fg = bg;
                bg = tmp;
            }

            f18a_term80_put_cell((unsigned int)row,
                                 (unsigned int)col,
                                 ch,
                                 fg,
                                 bg);
        }
    }

    f18a_term80_show_cursor(0);
}

void ScreenWidget::render80ColumnText(QPainter& painter, const QRect& targetRect)
{
    m_last80TargetRect = targetRect;

    char textBuffer[24][80];
    unsigned char colorBuffer[24][80];
    read80ColumnVRAM(textBuffer, colorBuffer);

    unsigned char globalBgIdx =  coleco_vdp_read_register(7) & 0x0F;

    // font and metrics
    painter.setFont(m_80colFont);
    painter.setRenderHint(QPainter::TextAntialiasing, false);
    QFontMetrics fm(m_80colFont);
    int charWidth = fm.horizontalAdvance('M');
    int charHeight = fm.height();

    const bool hasSmartKeys = (!auto80 && cpm80_is_active() && cpm80_has_smartkeys());
    const int cpmRows = hasSmartKeys ? 21 : 23;

    bool hasSelection = (!auto80 && cpm80_is_active() && cpm80HasSelection());
    int selStartCol = 0;
    int selStartRow = 0;
    int selEndCol = 0;
    int selEndRow = 0;

    if (hasSelection) {
        selStartCol = qBound(0, m_cpm80SelectionAnchor.x(), 79);
        selStartRow = qBound(0, m_cpm80SelectionAnchor.y(), cpmRows - 1);
        selEndCol   = qBound(0, m_cpm80SelectionCurrent.x(), 79);
        selEndRow   = qBound(0, m_cpm80SelectionCurrent.y(), cpmRows - 1);

        if (selStartRow > selEndRow ||
            (selStartRow == selEndRow && selStartCol > selEndCol)) {
            qSwap(selStartRow, selEndRow);
            qSwap(selStartCol, selEndCol);
        }
    }

    // setText scaling and background
    setText(painter, targetRect, charWidth, charHeight, globalBgIdx);

    // draw lineair 0..79 over 24 rows
    for (int row = 0; row < 24; row++) {
        for (int col = 0; col < 80; col++) {
            unsigned char rawCh = (unsigned char)textBuffer[row][col];
            bool inverted = (rawCh & 0x80);
            char displayCh = (char)(rawCh & 0x7F);

            int x = col * charWidth;
            int y = row * charHeight;

            bool selectedCell = false;
            if (hasSelection && row >= 1) {
                const int cpmRow = row - 1;
                if (cpmRow >= selStartRow && cpmRow <= selEndRow) {
                    const int firstCol = (cpmRow == selStartRow) ? selStartCol : 0;
                    const int lastCol  = (cpmRow == selEndRow)   ? selEndCol   : 79;
                    selectedCell = (col >= firstCol && col <= lastCol);
                }
            }

            if (!auto80 && cpm80_is_active() && g_cpm80ManualColors) {
                const QColor manualFg = TMS_COLORS[qBound(0, g_cpm80ManualFgIdx, 15)];
                const QColor manualBg = TMS_COLORS[qBound(0, g_cpm80ManualBgIdx, 15)];
                if (inverted) {
                    painter.fillRect(x, y, charWidth, charHeight, manualFg);
                    painter.setPen(manualBg);
                } else {
                    painter.setPen(manualFg);
                }
            } else if (!auto80 && cpm80_is_active() && g_cpm80HaveSampledColors) {
                // CP/M80: tekstkleur direct uit het 40-col beeld gebruiken.
                // Dus niet cpm80_get_color(), want die stond zwart te geven.
                if (inverted) {
                    painter.fillRect(x, y, charWidth, charHeight, g_cpm80SampleFgColor);
                    painter.setPen(g_cpm80SampleBgColor);
                } else {
                    painter.setPen(g_cpm80SampleFgColor);
                }
            } else if (inverted) {
                painter.fillRect(x, y, charWidth, charHeight, TMS_COLORS[colorBuffer[row][col]]);
                painter.setPen(TMS_COLORS[globalBgIdx]);
            } else {
                painter.setPen(TMS_COLORS[colorBuffer[row][col]]);
            }

            if (selectedCell) {
                painter.fillRect(x, y, charWidth, charHeight, QColor(70, 130, 220));
                painter.setPen(Qt::white);
            }

            if ((unsigned char)displayCh > 32 || inverted || selectedCell) {
                QString txt = (displayCh <= 32) ? " " : QString(QChar(displayCh));
                painter.drawText(x, y + charHeight - 2, txt);
            }
        }
    }

    // CP/M80 PNG-logo links in de eerste title bar tekenen.
    // Zet het logo in je resources als :/images/images/cpmlogo.png.
    // Fallbacks zijn handig tijdens testen buiten resources.
    if (false && !auto80 && cpm80_is_active()) {
        static QPixmap cpm80Logo;
        static bool cpm80LogoTriedLoad = false;

        if (!cpm80LogoTriedLoad) {
            cpm80LogoTriedLoad = true;
            cpm80Logo.load(":/images/images/cpmlogo.png");
            if (cpm80Logo.isNull())
                cpm80Logo.load(":/images/images/adam_cpm_logo.png");
            if (cpm80Logo.isNull())
                cpm80Logo.load("cpmlogo.png");
        }

        if (!cpm80Logo.isNull()) {
            const int targetLogoH = qMax(1, charHeight - 2);
            QPixmap scaledLogo = cpm80Logo.scaledToHeight(targetLogoH, Qt::SmoothTransformation);

            const int logoX = 2;
            const int logoY = (charHeight - scaledLogo.height()) / 2;

            painter.drawPixmap(logoX, logoY, scaledLogo);
        }
    }


    // CP/M80 smartkey PNG + labels onderaan tekenen.
    // Alleen tonen als er effectief smartkey-labels gedetecteerd zijn.
    // De PNG bestaat uit 2 rijen:
    //   rij 1 = getekende Romeinse cijfers
    //   rij 2 = achtergrond waarop de labels getekend worden.
    // Zet de PNG in je resources als :/images/images/cpm_smartkeys.png.
    if (!auto80 && cpm80_is_active() && cpm80_has_smartkeys()) {
        static QPixmap smartKeyBar;
        static bool smartKeyBarTriedLoad = false;

        if (!smartKeyBarTriedLoad) {
            smartKeyBarTriedLoad = true;
            smartKeyBar.load(":/images/images/cpm_smartkeys.png");

            // Fallback handig tijdens testen buiten resources.
            if (smartKeyBar.isNull())
                smartKeyBar.load("cpm_smartkeys.png");
        }

        if (!smartKeyBar.isNull()) {
            const int barX = 0;
            const int barY = 22 * charHeight;       // schermregel 22 en 23
            const int barW = 80 * charWidth;
            const int barH = 2 * charHeight;

            painter.drawPixmap(barX, barY, barW, barH, smartKeyBar);

            const int keyCount = 6;

            // De PNG-layout is 743 pixels breed:
            // 24 px linker offset + (6 labels * 116 px) + 23 px rechter marge.
            // Omdat de PNG naar barW geschaald wordt, schalen we de labelposities mee.
            const int pngLayoutW = 743;
            const int pngLeftOffset = 24;
            const int pngLabelW = 116;
            const double pngScaleX = (double)barW / (double)pngLayoutW;

            QFont keyFont = m_80colFont;
            keyFont.setBold(true);
            painter.setFont(keyFont);
            painter.setPen(Qt::white);

            for (int i = 0; i < keyCount; ++i) {
                QString label = QString::fromLatin1(cpm80_get_smartkey_text(i)).trimmed();

                // Veiligheid zodat lange labels niet over elkaar lopen.
                if (label.length() > 4)
                    label = label.left(4);

                const int labelX = (int)((pngLeftOffset + (i * pngLabelW)) * pngScaleX);
                const int labelW = (int)(pngLabelW * pngScaleX);

                QRect textRect(
                    labelX,
                    23 * charHeight,       // tweede rij van de PNG
                    labelW,
                    charHeight
                );

                painter.drawText(textRect, Qt::AlignCenter, label);
            }

            // Font terugzetten voor de rest van de CP/M80 renderer.
            painter.setFont(m_80colFont);
        }
    }

    // CP/M80 cursor: de virtuele 80-kolommenkaart moet zelf een cursor tekenen.
    // TDOS gebruikt dit niet; alleen CP/M80 en alleen wanneer de cursor zichtbaar is
    // in de blinkfase. We tekenen een klassieke block cursor door foreground en
    // background om te draaien op de actuele cursorpositie.
    if (!auto80 && cpm80_is_active()) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool cursorVisible = ((nowMs / 500) & 1) == 0;

        if (cursorVisible) {
            const int cursorCol = cpm80_get_cursor_x();
            const int cpmCursorRow = cpm80_get_cursor_y();

            // Schermregel 0 is de title bar, dus de CP/M-cursor moet
            // visueel 1 regel naar beneden.
            const int screenCursorRow = cpmCursorRow + 1;

            if (cursorCol >= 0 && cursorCol < 80 &&
                cpmCursorRow >= 0 && cpmCursorRow < cpmRows &&
                screenCursorRow >= 1 && screenCursorRow < (cpmRows + 1)) {

                const int x = cursorCol * charWidth;
                const int y = screenCursorRow * charHeight;

                QColor cursorFg;
                QColor cursorBg;

                if (g_cpm80ManualColors) {
                    cursorFg = TMS_COLORS[qBound(0, g_cpm80ManualFgIdx, 15)];
                    cursorBg = TMS_COLORS[qBound(0, g_cpm80ManualBgIdx, 15)];
                } else if (g_cpm80HaveSampledColors) {
                    cursorFg = g_cpm80SampleFgColor;
                    cursorBg = g_cpm80SampleBgColor;
                } else {
                    cursorFg = TMS_COLORS[15];
                    cursorBg = TMS_COLORS[1];
                }

                const unsigned char rawCh = cpm80_get_char(cpmCursorRow, cursorCol);
                const char displayCh = (char)(rawCh & 0x7F);

                painter.fillRect(x, y, charWidth, charHeight, cursorFg);
                painter.setPen(cursorBg);

                if ((unsigned char)displayCh > 32) {
                    painter.drawText(x, y + charHeight - 2, QString(QChar(displayCh)));
                }
            }
        }
    }

    painter.restore();
}
