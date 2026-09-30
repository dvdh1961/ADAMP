#include "mainwindow.h"
#include "customfiledialog.h"
#include "colecocontroller.h"
#include "screenwidget.h"
#include "inputwidget.h"
#include "logwindow.h"
#include "debuggerwindow.h"
#include "debugterminalwidget.h"
#include "commandprocessor.h"
#include "disasm_bridge.h"
#include "cartridgeinfowindow.h"
#include "ntablewindow.h"
#include "patternwindow.h"
#include "spritewindow.h"
#include "settingswindow.h"
#include "hardwarewindow.h"
#include "CORE/cv.h"
#include "GRAPH/f18a.h"
#include "6801/adnet_core.h"
#include "joypadwindow.h"
#include "printwindow.h"
#include "simplejoystick.h"
#include "soundpreviewbridge.h"
#include "mcu2_gateway.h"

// Qt includes
#include <QMenuBar>
#include <QSplitter>
#include <QTextEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QFileInfo>
#include <QDebug>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QSettings>
#include <QStyle>
#include <QLayout>
#include <QStatusBar>
#include <QLabel>
#include <QTimer>
#include <QSizePolicy>
#include <QThread>
#include "6801/adnet_mcu2.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWindow>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QFont>
#include <QMap>
#include <QStringList>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QProgressDialog>
#include <QCryptographicHash>
#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QPainter>
#include <QPaintEvent>
#include <QtMath>
#include <QPainterPath>
#include <QRegion>
#include "6801/adnet_core.h"

namespace {
class DayNightToggleButton final : public QPushButton
{
public:
    explicit DayNightToggleButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(116, 30);
        setToolTip(tr("Switch day/night mode"));
    }

    void setDarkMode(bool dark)
    {
        if (m_dark == dark)
            return;
        m_dark = dark;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF pill = QRectF(rect()).adjusted(1.0, 2.0, -1.0, -2.0);
        painter.setPen(m_dark ? QPen(QColor("#101010"), 1.2) : Qt::NoPen);
        painter.setBrush(m_dark ? QColor("#050505") : QColor("#E6E6E6"));
        painter.drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);

        const qreal diameter = 27.0;
        const qreal circleX = m_dark ? pill.left() + 1.0
                                     : pill.right() - diameter - 1.0;
        const QRectF circle(circleX, (height() - diameter) / 2.0, diameter, diameter);
        painter.setPen(QPen(m_dark ? QColor("#111111") : QColor("#D0D0D0"), 1.0));
        painter.setBrush(Qt::white);
        painter.drawEllipse(circle);

        painter.setPen(m_dark ? Qt::white : QColor("#202020"));
        QFont labelFont = font();
        labelFont.setFamily(QStringLiteral("Segoe UI"));
        labelFont.setPixelSize(9);
        labelFont.setBold(true);
        painter.setFont(labelFont);
        const QRectF textRect = m_dark
            ? QRectF(circle.right() + 3.0, 0.0, pill.right() - circle.right() - 6.0, height())
            : QRectF(pill.left() + 7.0, 0.0, circle.left() - pill.left() - 9.0, height());
        painter.drawText(textRect, Qt::AlignCenter,
                         m_dark ? QStringLiteral("NIGHT MODE")
                                : QStringLiteral("DAY MODE"));

        painter.save();
        painter.setClipRect(circle.adjusted(3.0, 3.0, -3.0, -3.0));
        painter.setPen(QPen(QColor("#333333"), 1.1));
        painter.setBrush(Qt::NoBrush);
        const QPointF c = circle.center();
        if (m_dark) {
            painter.setBrush(QColor("#333333"));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(c, 7.0, 7.0);
            painter.setBrush(Qt::white);
            painter.drawEllipse(c + QPointF(3.5, -2.5), 6.5, 6.5);
            painter.setBrush(QColor("#333333"));
            painter.drawEllipse(c + QPointF(5.5, -7.0), 1.0, 1.0);
        } else {
            painter.drawEllipse(c, 4.5, 4.5);
            for (int i = 0; i < 8; ++i) {
                const qreal angle = (3.14159265358979323846 / 4.0) * i;
                const QPointF inner(c.x() + qCos(angle) * 7.0,
                                    c.y() + qSin(angle) * 7.0);
                const QPointF outer(c.x() + qCos(angle) * 9.5,
                                    c.y() + qSin(angle) * 9.5);
                painter.drawLine(inner, outer);
            }
        }
        painter.restore();
    }

private:
    bool m_dark = false;
};

class WindowDragBar final : public QWidget
{
public:
    explicit WindowDragBar(QWidget *parent = nullptr) : QWidget(parent) {}

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && window() && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }
};

class StayOpenMenu final : public QMenu
{
public:
    using QMenu::QMenu;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QAction *action = actionAt(event->pos());
        if (action && action->isEnabled() && !action->menu()) {
            action->trigger();
            setActiveAction(action);
            event->accept();
            return;
        }
        QMenu::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
                || event->key() == Qt::Key_Space) {
            QAction *action = activeAction();
            if (action && action->isEnabled() && !action->menu()) {
                action->trigger();
                setActiveAction(action);
                event->accept();
                return;
            }
        }
        QMenu::keyPressEvent(event);
    }
};
}

//---------------------------------------------------------------------------------------------
// GUI implementations (menus, widgets, media, settings, input, etc.)
//---------------------------------------------------------------------------------------------
// Declaratie bovenaan je bestand
extern "C" void adamnet_set_game_mode(bool enabled);
extern "C" void PutKBD(unsigned int Key);


static bool isDka2018RomName(const QString& name)
{
    const QString n = name.toLower();
    return n.contains("donkey kong arcade") ||
           n.contains("dka") ||
           n.contains("45345709");
}

enum HardwareMediaProfile {
    HardwareUnknown = -1,
    HardwareEos = 0,
    HardwareCpm = 1,
    HardwareTdos = 2,
    HardwareColecoRom = 3,
    HardwareAdamRom = 4
};

static HardwareMediaProfile classifyHardwareHeader(const QByteArray &bytes)
{
    /* FujiNet's Coleco launcher exposes a small boot disk.  The actual ROM
     * begins at block 2 and retains its normal 55AA/AA55 cartridge header. */
    const int romOffsets[] = { 0, 1024, 2048 };
    for (int offset : romOffsets) {
        if (bytes.size() < offset + 2)
            continue;
        const quint8 a = quint8(bytes[offset]);
        const quint8 b = quint8(bytes[offset + 1]);
        if ((a == 0xAA && b == 0x55) || (a == 0x55 && b == 0xAA))
            return HardwareColecoRom;
    }

    const QByteArray upper = bytes.toUpper();
    if (upper.contains("CP/M") || upper.contains("CPM")
        || upper.contains("CP-M"))
        return HardwareCpm;
    /* CP/M tool disks can contain one TDOS utility filename. One loose TDOS
     * occurrence is therefore not sufficient to classify the boot system. */
    if (upper.contains("T-DOS") || upper.count("TDOS") >= 2)
        return HardwareTdos;
    if ((upper.contains("FIRST DIR") && upper.contains("DIRECTORY"))
        || (upper.contains("DIRECTORY") && upper.contains("BLOCKS LEFT"))
        || upper.contains("EOS"))
        return HardwareEos;
    return HardwareUnknown;
}

static bool isFujiNetStartupHeader(const QByteArray &bytes)
{
    /* Physical FujiNet startup D5 observed on hardware. Use three independent
     * loader signatures, not a generic EOS marker, to avoid classifying an
     * ordinary EOS application disk as the FujiNet menu. */
    if (bytes.size() < 3 * 1024)
        return false;
    return bytes.mid(0, 8) == QByteArray::fromHex("78326ffd3e01d37f")
        && bytes.mid(1024, 8) == QByteArray::fromHex("c369000000000000")
        && bytes.mid(2048, 8) == QByteArray::fromHex("e5cddc19c1210000");
}

static QString fujiMediaFingerprint(const QByteArray &bytes)
{
    if (bytes.size() < 1024 + 32)
        return QString();
    /* Do not hash complete directory/allocation blocks: a successful SAVE
     * legitimately changes them and must not make the remembered title vanish.
     * The boot block plus the fixed volume-header prefix forms a stable ID. */
    QByteArray identity = bytes.left(1024);
    identity.append(bytes.mid(1024, 32));
    return QString::fromLatin1(QCryptographicHash::hash(
        identity, QCryptographicHash::Sha256).toHex());
}

static QString cachedFujiMediaPath(const QByteArray &headerBlocks,
                                   const QString &reportedPath)
{
    const QString fingerprint = fujiMediaFingerprint(headerBlocks);
    if (fingerprint.isEmpty())
        return reportedPath;

    const QString iniPath = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("settings.ini"));
    QSettings settings(iniPath, QSettings::IniFormat);
    const QString key = QStringLiteral("fujinet/media_names/%1").arg(fingerprint);
    if (!reportedPath.isEmpty()) {
        settings.setValue(key, reportedPath);
        settings.setValue(QStringLiteral("fujinet/last_mounted_path"), reportedPath);
        settings.setValue(QStringLiteral("fujinet/last_media_fingerprint"), fingerprint);
        settings.sync();
        qDebug() << "[UI][BOOT] cached FujiNet media title for fingerprint="
                 << fingerprint.left(12) << QFileInfo(reportedPath).fileName();
        return reportedPath;
    }

    const QString remembered = settings.value(key).toString();
    if (!remembered.isEmpty()) {
        qDebug() << "[UI][BOOT] restored FujiNet media title from fingerprint="
                 << fingerprint.left(12) << QFileInfo(remembered).fileName();
        return remembered;
    }

    /* Writable media and some FujiNet loaders can alter the inspected header
     * between runs. E2 is authoritative whenever a new image is selected, so
     * its last path is the best available fallback when startup sends no E2. */
    QString lastKnown = settings.value(
        QStringLiteral("fujinet/last_mounted_path")).toString();
    if (lastKnown.isEmpty()) {
        settings.beginGroup(QStringLiteral("fujinet/media_names"));
        const QStringList knownFingerprints = settings.childKeys();
        if (knownFingerprints.size() == 1)
            lastKnown = settings.value(knownFingerprints.constFirst()).toString();
        settings.endGroup();
        if (!lastKnown.isEmpty()) {
            settings.setValue(QStringLiteral("fujinet/last_mounted_path"), lastKnown);
            settings.sync();
        }
    }
    if (!lastKnown.isEmpty())
        qDebug() << "[UI][BOOT] restored last known FujiNet media title; fingerprint changed from="
                 << settings.value(QStringLiteral("fujinet/last_media_fingerprint"))
                        .toString().left(12)
                 << "to=" << fingerprint.left(12)
                 << QFileInfo(lastKnown).fileName();
    return lastKnown;
}

static void updateRomLabelForStatusBar(QStatusBar* bar, QLabel* sepLabel4,
                                       QLabel* romLabel, const QString& fullText);
static QString hardwareProfileName(HardwareMediaProfile profile);

int MainWindow::handleFujiNetBootIntercept(
    const QByteArray &headerBlocks, const QString &mountedPath)
{
    HardwareMediaProfile detected = classifyHardwareHeader(headerBlocks);
    const QString effectivePath = cachedFujiMediaPath(headerBlocks, mountedPath);
    const QByteArray upperHeader = headerBlocks.toUpper();
    const bool resetPreflight = mountedPath.isEmpty();
    const bool fujiStartMedium = resetPreflight
        && detected != HardwareColecoRom
        && (isFujiNetStartupHeader(headerBlocks)
            || upperHeader.contains("FUJINET")
            || upperHeader.contains("FUJI NET"));
    if (fujiStartMedium)
        qDebug() << "[UI][BOOT] FujiNet start medium recognised; showing boot menu";
    const QString lowerPath = effectivePath.toLower();
    if (detected != HardwareColecoRom) {
        const bool cpmName = lowerPath.contains("cp/m")
            || lowerPath.contains("cpm") || lowerPath.contains("cp-m");
        const bool tdosName = lowerPath.contains("t-dos")
            || lowerPath.contains("tdos");
        if (cpmName && !tdosName)
            detected = HardwareCpm;
        else if (tdosName && !cpmName)
            detected = HardwareTdos;
        else if (lowerPath.endsWith(".dsk") || lowerPath.endsWith(".ddp"))
            detected = HardwareEos;
    }

    const QString mediaName = effectivePath.isEmpty()
        ? QStringLiteral("FujiNet mounted media")
        : QFileInfo(effectivePath).fileName();
    const bool detectedRom = detected == HardwareColecoRom;
    QStringList choices;
    if (detectedRom) {
        choices << tr("ROM via ADAM D5 loader (Coleco keypad)")
                << tr("ROM as native Coleco cartridge (mapper/SGM)")
                << tr("EOS disk on D5") << tr("CP/M disk on D5")
                << tr("T-DOS disk on D5")
                << tr("EOS tape on D1") << tr("CP/M tape on D1")
                << tr("T-DOS tape on D1") << tr("Cancel boot");
    } else {
        choices << tr("EOS disk on D5") << tr("CP/M disk on D5")
                << tr("T-DOS disk on D5")
                << tr("EOS tape on D1") << tr("CP/M tape on D1")
                << tr("T-DOS tape on D1")
                << tr("ROM via ADAM D5 loader (Coleco keypad)")
                << tr("ROM as native Coleco cartridge (mapper/SGM)")
                << tr("Cancel boot");
    }
    if (resetPreflight)
        choices.insert(0, tr("FujiNet start medium (continue normally)"));

    /* An unknown retained D5 may simply be FujiNet's own startup disk. Keep
     * that as the safe default; recognised application formats override it. */
    int suggested = detectedRom && resetPreflight ? 1 : 0;
    if (!detectedRom) {
        const int resetOffset = resetPreflight ? 1 : 0;
        if (fujiStartMedium) suggested = 0;
        else if (detected == HardwareEos) suggested = resetOffset;
        else if (detected == HardwareCpm) suggested = resetOffset + 1;
        else if (detected == HardwareTdos) suggested = resetOffset + 2;
        else if (detected == HardwareColecoRom) suggested = resetOffset + 3;
    }
    const QString detectedText = fujiStartMedium
        ? tr("FujiNet startup medium")
        : (detected == HardwareUnknown
            ? tr("not recognised") : hardwareProfileName(detected));
    bool accepted = false;
    const QString choice = QInputDialog::getItem(
        this, resetPreflight ? tr("Mounted FujiNet medium found")
                             : tr("FujiNet BOOT intercepted"),
        tr("Medium: %1\nDetected: %2\n\nChoose how ADAMP must boot it:")
            .arg(mediaName, detectedText),
        choices, suggested, false, &accepted);
    if (!accepted || choice == tr("Cancel boot"))
        return -1;
    if (choice.startsWith("FujiNet"))
        return 4;

    const bool tapeD1 = choice == tr("EOS tape on D1")
        || choice == tr("CP/M tape on D1")
        || choice == tr("T-DOS tape on D1");
    int profile = 0;
    if (choice == tr("CP/M disk on D5") || choice == tr("CP/M tape on D1"))
        profile = 1;
    else if (choice == tr("T-DOS disk on D5")
             || choice == tr("T-DOS tape on D1"))
        profile = 2;
    const bool adamRomLoader =
        choice == tr("ROM via ADAM D5 loader (Coleco keypad)");
    const bool nativeColeco =
        choice == tr("ROM as native Coleco cartridge (mapper/SGM)");
    const bool gameInput = adamRomLoader || nativeColeco;
    /* 0..2 retain the established D5 profile values, 3 remains Coleco and
     * 5..7 are the matching EOS/CP-M/T-DOS profiles for physical tape D1.
     * Value 4 is reserved for "continue FujiNet startup normally". */
    const int decision = nativeColeco ? 8
        : (adamRomLoader ? 3 : (tapeD1 ? 5 + profile : profile));

    if (tapeD1) {
        mcu2_block_set_device_enabled(0x04, false);
        mcu2_block_set_device_enabled(0x08, true);
        m_hardwareDiskMedia[0] = false;
        m_loadedDiskNames[0].clear();
        m_hardwareTapeMedia[0] = true;
        m_loadedTapeNames[0] = QStringLiteral("[HW] %1").arg(mediaName);
        m_AdamTMedia_insert = true;
        onTapeStatusChanged(0, m_loadedTapeNames[0]);
    } else {
        mcu2_block_set_device_enabled(0x08, false);
        mcu2_block_set_device_enabled(0x04, true);
        m_hardwareTapeMedia[0] = false;
        m_loadedTapeNames[0].clear();
        m_hardwareDiskMedia[0] = true;
        m_loadedDiskNames[0] = QStringLiteral("[HW] %1").arg(mediaName);
        m_AdamDMedia_insert = true;
        onDiskStatusChanged(0, m_loadedDiskNames[0]);
    }
    m_resetAdamLocked = true;

    m_adamGameMode = adamRomLoader;

    /* The physical FujiNet transfer must continue through the ADAM D5
     * loader, so m_machineType and the core may not be changed here.  The
     * selected payload is nevertheless a Coleco cartridge: show that final
     * machine identity in the status bar instead of leaving the misleading
     * ADAM label visible throughout gameplay. */
    if (m_sysLabel)
        m_sysLabel->setText(gameInput ? QStringLiteral("COLECO")
                                      : QStringLiteral("ADAM"));
    if (m_actAdamGameOn && m_actAdamGameOff)
        (adamRomLoader ? m_actAdamGameOn : m_actAdamGameOff)->setChecked(true);
    if (m_inputWidget)
        m_inputWidget->setAdamGameMode(adamRomLoader);
    adamnet_set_game_mode(adamRomLoader);
    m_cpm_status = profile == 1 || profile == 2;

    if (gameInput) {
        updateRomLabelForStatusBar(
            statusBar(), m_sepLabel4, m_romLabel,
            QStringLiteral("[HW Coleco] %1").arg(mediaName));
    } else {
        updateRomLabelForStatusBar(statusBar(), m_sepLabel4, m_romLabel,
                                   QStringLiteral("No cart"));
    }
    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();

    qDebug() << "[UI][BOOT] FujiNet decision=" << decision
             << "target=" << (tapeD1 ? "Tape D1" : "Disk D5")
             << "name=" << mediaName
             << "route=" << (nativeColeco ? "native Coleco" :
                                adamRomLoader ? "ADAM D5 loader" : "ADAM media")
             << "game-input=" << adamRomLoader;
    if (adamRomLoader)
        qDebug() << "[UI][BOOT] ROM route keeps ADAM core and enables Coleco keypad";
    else if (nativeColeco)
        qDebug() << "[UI][BOOT] ROM route="
                 << (m_fujiNetDirectRom
                         ? "direct PC network (automatic physical D5 fallback)"
                         : "physical D5 capture")
                 << "-> save FujiNet.rom -> native Coleco";
    return decision;
}

static QString hardwareProfileName(HardwareMediaProfile profile)
{
    switch (profile) {
    case HardwareEos: return QStringLiteral("EOS");
    case HardwareCpm: return QStringLiteral("CP/M");
    case HardwareTdos: return QStringLiteral("T-DOS");
    case HardwareColecoRom: return QStringLiteral("Coleco ROM");
    case HardwareAdamRom: return QStringLiteral("ADAM ROM");
    default: return QStringLiteral("Unknown");
    }
}


// Verkort statusbar tekst met drie puntjes afhankelijk van de beschikbare breedte.
static QString statusBarElideText3Dots(const QString& text, const QFont& font, int maxWidth)
{
    const QString dots = "...";
    if (text.isEmpty()) return text;

    QFontMetrics fm(font);
    if (fm.horizontalAdvance(text) <= maxWidth) return text;
    if (maxWidth <= fm.horizontalAdvance(dots)) return dots;

    int low = 0;
    int high = text.length();
    int best = 0;

    while (low <= high) {
        const int mid = (low + high) / 2;
        const QString candidate = text.left(mid) + dots;
        if (fm.horizontalAdvance(candidate) <= maxWidth) {
            best = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    return text.left(best) + dots;
}

// Bereken de vrije statusbar-ruimte na m_sepLabel4 en pas m_romLabel daarop aan.
static void updateRomLabelForStatusBar(QStatusBar* bar, QLabel* sepLabel4, QLabel* romLabel, const QString& fullText)
{
    if (!romLabel) return;

    QString display = fullText.trimmed();
    if (display.isEmpty()) display = "No cart";

    int maxWidth = 480;

    if (bar && sepLabel4) {
        const int rightMargin = 24; // ruimte voor statusbar-rand / sizegrip
        const QPoint sepPos = sepLabel4->mapTo(bar, QPoint(0, 0));
        const int startX = sepPos.x() + sepLabel4->width() + 4;
        maxWidth = bar->width() - startX - rightMargin;
    }

    if (maxWidth < 60) maxWidth = 60;
    if (maxWidth > 480) maxWidth = 480;

    romLabel->setFixedWidth(maxWidth);
    romLabel->setText(statusBarElideText3Dots(display, romLabel->font(), maxWidth - 4));
    romLabel->setToolTip(display);
}



static QString appDefaultPath(const QString& relativePath)
{
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(relativePath));
}

static QString defaultRomPathForPlatform()          { return appDefaultPath("media/roms"); }
static QString defaultAtariRomPathForPlatform()     { return appDefaultPath("media/aroms"); }
static QString defaultDiskPathForPlatform()         { return appDefaultPath("media/disks"); }
static QString defaultTapePathForPlatform()         { return appDefaultPath("media/tapes"); }
static QString defaultStatePathForPlatform()        { return appDefaultPath("media/states"); }
static QString defaultBreakpointPathForPlatform()   { return appDefaultPath("media/breakpoints"); }
static QString defaultScreenshotPathForPlatform()   { return appDefaultPath("media/screenshots"); }
static QString defaultSymbolsPathForPlatform()      { return appDefaultPath("media/symbols"); }
static QString defaultBezelPathForPlatform()        { return appDefaultPath("media/bezels"); }

static QString defaultCvBasicSourcePathForPlatform(){ return appDefaultPath("media/cvbasic/source"); }
static QString defaultCvBasicBuildPathForPlatform() { return appDefaultPath("media/cvbasic/build"); }
static QString defaultSpriteSourcePathForPlatform() { return appDefaultPath("media/cvbasic/source"); }
static QString defaultSpriteBuildPathForPlatform()  { return appDefaultPath("media/cvbasic/build/sprites"); }
static QString defaultSoundSourcePathForPlatform()  { return appDefaultPath("media/cvbasic/sound"); }
static QString defaultSoundBuildPathForPlatform()   { return appDefaultPath("media/cvbasic/build/sound"); }

static QString defaultCvBasicExePathForPlatform()
{
#if defined(Q_OS_WIN)
    return appDefaultPath("tools/cvbasic/cvbasic.exe");
#else
    return appDefaultPath("tools/cvbasic/cvbasic_linux");
#endif
}

static QString defaultGasm80ExePathForPlatform()
{
#if defined(Q_OS_WIN)
    return appDefaultPath("tools/cvbasic/gasm80.exe");
#else
    return appDefaultPath("tools/cvbasic/gasm80_linux");
#endif
}


//---------------------------------------------------------------------------------------------
// STATUSBAR
//---------------------------------------------------------------------------------------------
void MainWindow::forceStatusBarMediaFlag(QLabel* label)
{
    if (!label) return;

    const QString text = label->text().trimmed();

    // Verwacht formaat "D1: ..." / "D5: ..."
    const int idx = text.indexOf(':');
    if (idx < 0) return;

    const QString prefix = text.left(idx + 1); // "D1:"

    // Alles behalve "-" telt als "geladen"
    const bool loaded = !text.endsWith('-');

    label->setText(QString("%1 %2").arg(prefix, loaded ? "X" : "-"));

    // Kleur (status-LED stijl)
    label->setStyleSheet(loaded
                             ? "color: #00C853;"   // groen = geladen
                             : "color: #9E9E9E;"   // grijs = leeg
                         );
}

void MainWindow::forceStatusBarMediaFlags()
{
    auto setMediaLabel = [](QLabel* label, const QString& prefix, bool loaded) {
        if (!label) return;
        label->setText(QString("%1 %2").arg(prefix, loaded ? "X" : "-"));
        label->setStyleSheet(loaded ? "color: #00C853;" : "color: #9E9E9E;");
    };

    setMediaLabel(m_tapeLabelA, "D1:", !m_loadedTapeNames[0].trimmed().isEmpty());
    setMediaLabel(m_tapeLabelB, "D2:", !m_loadedTapeNames[1].trimmed().isEmpty());
    setMediaLabel(m_tapeLabelC, "D3:", !m_loadedTapeNames[2].trimmed().isEmpty());
    setMediaLabel(m_tapeLabelD, "D4:", !m_loadedTapeNames[3].trimmed().isEmpty());

    setMediaLabel(m_diskLabelA, "D5:", !m_loadedDiskNames[0].trimmed().isEmpty());
    setMediaLabel(m_diskLabelB, "D6:", !m_loadedDiskNames[1].trimmed().isEmpty());
    setMediaLabel(m_diskLabelC, "D7:", !m_loadedDiskNames[2].trimmed().isEmpty());
    setMediaLabel(m_diskLabelD, "D8:", !m_loadedDiskNames[3].trimmed().isEmpty());
}

void MainWindow::setStatusBar()
{
    statusBar()->setSizeGripEnabled(true);

    // Window flags
    Qt::WindowFlags flags = windowFlags();
    flags &= ~Qt::WindowMaximizeButtonHint;
    flags |= Qt::FramelessWindowHint;
    setWindowFlags(flags);

    auto mkLabel = [&](const QString& text,
                       const char* objName,
                       int minWidth,
                       Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter,
                       QSizePolicy::Policy hPol = QSizePolicy::Fixed) -> QLabel*
    {
        QLabel* l = new QLabel(text, this);
        if (objName && *objName) l->setObjectName(objName);
        l->setAlignment(align);
        if (minWidth > 0) l->setMinimumWidth(minWidth);
        l->setSizePolicy(hPol, QSizePolicy::Preferred);
        return l;
    };

    auto mkSep = [&]() -> QLabel* {
        return mkLabel("|", "", 0);
    };

    // ---- Core labels ----
    m_sysLabel = mkLabel("COLECO", "sysLabel", 50);

    m_sepLabelSGM = mkSep();
    m_sgmLabel = mkLabel("SGM", "sgmLabel", 40);
    m_sgmLabel->hide();
    m_sepLabelSGM->hide();

    m_stdLabel = mkLabel(QString("%1").arg(m_currentStd), "stdLabel", 30);
    m_fpsLabel = mkLabel("0fps", "fpsLabel", 40);
    m_runLabel = mkLabel("RUN", "runLabel", 30);

    // ---- Media labels (geen filenames meer: enkel X of -) ----
    auto mkMedia = [&](const char* obj, const char* prefix, QLabel*& sep, QLabel*& lab) {
        sep = mkSep();
        lab = mkLabel(QString("%1 -").arg(prefix), obj, 35); // start = "-"
    };

    mkMedia("tapeLabel", "D1:", m_sepLabelMedia2a, m_tapeLabelA);
    mkMedia("tapeLabel", "D2:", m_sepLabelMedia2b, m_tapeLabelB);
    mkMedia("tapeLabel", "D3:", m_sepLabelMedia2c, m_tapeLabelC);
    mkMedia("tapeLabel", "D4:", m_sepLabelMedia2d, m_tapeLabelD);

    mkMedia("diskLabel", "D5:", m_sepLabelMedia1a, m_diskLabelA);
    mkMedia("diskLabel", "D6:", m_sepLabelMedia1b, m_diskLabelB);
    mkMedia("diskLabel", "D7:", m_sepLabelMedia1c, m_diskLabelC);
    mkMedia("diskLabel", "D8:", m_sepLabelMedia1d, m_diskLabelD);

    // Start verborgen (zoals je had)
    QLabel* hideAtStart[] = {
        m_sepLabelMedia2a, m_tapeLabelA,
        m_sepLabelMedia2b, m_tapeLabelB,
        m_sepLabelMedia2c, m_tapeLabelC,
        m_sepLabelMedia2d, m_tapeLabelD,
        m_sepLabelMedia1a, m_diskLabelA,
        m_sepLabelMedia1b, m_diskLabelB,
        m_sepLabelMedia1c, m_diskLabelC,
        m_sepLabelMedia1d, m_diskLabelD
    };
    for (QLabel* w : hideAtStart) if (w) w->hide();

    // ---- ROM + separators ----
    m_romLabel = mkLabel("No cart", "romLabel", 0);
    m_romLabel->setFixedWidth(480);

    m_sepLabel1 = mkSep();
    m_sepLabel2 = mkSep();
    m_sepLabel3 = mkSep();
    m_sepLabel4 = mkSep();

    // ---- Add to statusbar ----
    QWidget* widgets[] = {
        m_sysLabel,
        m_sepLabelSGM, m_sgmLabel,

        m_sepLabelMedia2a, m_tapeLabelA,
        m_sepLabelMedia2b, m_tapeLabelB,
        m_sepLabelMedia2c, m_tapeLabelC,
        m_sepLabelMedia2d, m_tapeLabelD,

        m_sepLabelMedia1a, m_diskLabelA,
        m_sepLabelMedia1b, m_diskLabelB,
        m_sepLabelMedia1c, m_diskLabelC,
        m_sepLabelMedia1d, m_diskLabelD,

        m_sepLabel1, m_stdLabel,
        m_sepLabel2, m_fpsLabel,
        m_sepLabel3, m_runLabel,
        m_sepLabel4, m_romLabel
    };
    for (QWidget* w : widgets) if (w) statusBar()->addWidget(w);

    // ---- set Font ----
    QFont statusFont("Roboto", 9);
    statusFont.setBold(false);

    QWidget* fontWidgets[] = {
        m_sysLabel, m_sepLabelSGM, m_sgmLabel,
        m_sepLabelMedia2a, m_tapeLabelA,
        m_sepLabelMedia2b, m_tapeLabelB,
        m_sepLabelMedia2c, m_tapeLabelC,
        m_sepLabelMedia2d, m_tapeLabelD,
        m_sepLabelMedia1a, m_diskLabelA,
        m_sepLabelMedia1b, m_diskLabelB,
        m_sepLabelMedia1c, m_diskLabelC,
        m_sepLabelMedia1d, m_diskLabelD,
        m_sepLabel1, m_stdLabel,
        m_sepLabel2, m_fpsLabel,
        m_sepLabel3, m_runLabel,
        m_sepLabel4, m_romLabel
    };
    for (QWidget* w : fontWidgets) if (w) w->setFont(statusFont);

    updateRomLabelForStatusBar(statusBar(), m_sepLabel4, m_romLabel, "No cart");
}

//---------------------------------------------------------------------------------------------
// DAY / NIGHT APPEARANCE
//---------------------------------------------------------------------------------------------
void MainWindow::setupThemeButton()
{
    if (m_themeButton)
        return;

    QMenuBar *applicationMenu = menuBar();
    QWidget *topArea = new QWidget(this);
    topArea->setObjectName(QStringLiteral("windowTopArea"));
    QVBoxLayout *topLayout = new QVBoxLayout(topArea);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);

    WindowDragBar *titleBar = new WindowDragBar(topArea);
    titleBar->setObjectName(QStringLiteral("customTitleBar"));
    titleBar->setFixedHeight(32);
    QHBoxLayout *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(10, 0, 0, 0);
    titleLayout->setSpacing(0);

    QLabel *title = new QLabel(windowTitle(), titleBar);
    title->setObjectName(QStringLiteral("customWindowTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    titleLayout->addWidget(title);
    titleLayout->addStretch(1);

    m_themeButton = new DayNightToggleButton(titleBar);
    m_themeButton->setObjectName(QStringLiteral("dayNightButton"));
    m_themeButton->setFlat(true);
    m_themeButton->setFocusPolicy(Qt::NoFocus);
    titleLayout->addWidget(m_themeButton);

    QPushButton *minimizeButton = new QPushButton(QString::fromUtf8("\xE2\x80\x94"), titleBar);
    minimizeButton->setObjectName(QStringLiteral("captionButton"));
    minimizeButton->setToolTip(tr("Minimize"));
    minimizeButton->setFocusPolicy(Qt::NoFocus);
    minimizeButton->setFixedSize(46, 32);
    titleLayout->addWidget(minimizeButton);

    QPushButton *closeButton = new QPushButton(QString::fromUtf8("\xC3\x97"), titleBar);
    closeButton->setObjectName(QStringLiteral("closeCaptionButton"));
    closeButton->setToolTip(tr("Close"));
    closeButton->setFocusPolicy(Qt::NoFocus);
    closeButton->setFixedSize(46, 32);
    titleLayout->addWidget(closeButton);

    topLayout->addWidget(titleBar);
    topLayout->addWidget(applicationMenu);
    setMenuWidget(topArea);

    connect(m_themeButton, &QPushButton::clicked,
            this, &MainWindow::onToggleDayNight);
    connect(minimizeButton, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    applyDayNightTheme(m_darkTheme);
}

void MainWindow::onToggleDayNight()
{
    applyDayNightTheme(!m_darkTheme);
    QSettings().setValue(QStringLiteral("appearance/darkTheme"), m_darkTheme);
}

void MainWindow::applyDayNightTheme(bool dark)
{
    m_darkTheme = dark;

    QPalette palette;
    if (dark) {
        palette.setColor(QPalette::Window, QColor(45, 45, 45));
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, QColor(30, 30, 30));
        palette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
        palette.setColor(QPalette::ToolTipBase, QColor(42, 42, 42));
        palette.setColor(QPalette::ToolTipText, Qt::white);
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor(45, 45, 45));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Highlight, QColor(100, 100, 255));
        palette.setColor(QPalette::HighlightedText, Qt::black);
        palette.setColor(QPalette::Light, QColor(70, 70, 70));
        palette.setColor(QPalette::Midlight, QColor(58, 58, 58));
        palette.setColor(QPalette::Mid, QColor(48, 48, 48));
        palette.setColor(QPalette::Dark, QColor(25, 25, 25));
        palette.setColor(QPalette::Shadow, Qt::black);
        palette.setColor(QPalette::PlaceholderText, QColor(150, 150, 150));
    } else {
        // Do not derive this from the current palette: after night mode that
        // palette is dark as well. Set every visible role explicitly.
        palette.setColor(QPalette::Window, QColor(245, 245, 245));
        palette.setColor(QPalette::WindowText, QColor(25, 25, 25));
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::AlternateBase, QColor(238, 238, 238));
        palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
        palette.setColor(QPalette::ToolTipText, QColor(25, 25, 25));
        palette.setColor(QPalette::Text, QColor(25, 25, 25));
        palette.setColor(QPalette::Button, QColor(240, 240, 240));
        palette.setColor(QPalette::ButtonText, QColor(25, 25, 25));
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Highlight, QColor(0, 120, 215));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::Light, Qt::white);
        palette.setColor(QPalette::Midlight, QColor(248, 248, 248));
        palette.setColor(QPalette::Mid, QColor(190, 190, 190));
        palette.setColor(QPalette::Dark, QColor(130, 130, 130));
        palette.setColor(QPalette::Shadow, QColor(80, 80, 80));
        palette.setColor(QPalette::Link, QColor(0, 102, 204));
        palette.setColor(QPalette::PlaceholderText, QColor(110, 110, 110));
    }
    qApp->setPalette(palette);

    const QString common = QStringLiteral(R"(
        QMenuBar::item { background: transparent; padding: 6px 10px; }
        QMenu::item { padding: 6px 24px; border-radius: 3px; }
        QMenu::separator { height: 1px; margin: 6px 8px; }
        QMenu::indicator { width: 16px; height: 16px; }
        QMenu::indicator:checked {
            background: transparent; border: 0px;
            image: url(:/images/images/Check.png);
        }
        QPushButton#dayNightButton {
            border: 0px; border-radius: 0px; padding: 0px;
            font-family: "Segoe UI Symbol"; font-size: 17px;
        }
        QPushButton#captionButton, QPushButton#closeCaptionButton {
            border: 0px; border-radius: 0px; padding: 0px;
            font-family: "Segoe UI Symbol"; font-size: 16px;
        }
    )");

    const QString darkSheet = QStringLiteral(R"(
        QToolTip { color: #ffffff; background-color: #2a2a2a; border: 1px solid #767676; }
        QMenuBar { background-color: #2b2b2b; color: #eaeaea; }
        QMenuBar::item:selected { background: #3a3a3a; color: #ffffff; }
        QMenuBar::item:pressed, QMenuBar::item:open { background: #4c5a66; color: #ffffff; }
        QMenu { background-color: #2b2b2b; color: #eaeaea; border: 1px solid #555; padding: 4px; }
        QMenu::separator { background: #444; }
        QMenu::item:selected { background: #4c5a66; color: #ffffff; }
        QMenu::item:disabled { color: #777; }
        QMenu::item:selected:disabled { background: #3a3a3a; }
        QPushButton#dayNightButton { color: #ffd75e; background: #353535; }
        QPushButton#dayNightButton:hover { background: #4a4a4a; }
        QPushButton#dayNightButton:pressed { background: #555; }
        QWidget#windowTopArea, QWidget#customTitleBar { background: #202020; }
        QMainWindow#adampMainWindow { border: 1px solid #202020; }
        QLabel#customWindowTitle { color: #eeeeee; }
        QPushButton#captionButton, QPushButton#closeCaptionButton { color: #eeeeee; background: #202020; }
        QPushButton#captionButton:hover { background: #3d3d3d; }
        QPushButton#closeCaptionButton:hover { color: white; background: #c42b1c; }
    )");

    const QString lightSheet = QStringLiteral(R"(
        QMainWindow, QDialog { background-color: #f5f5f5; color: #191919; }
        QStatusBar { background-color: #f0f0f0; color: #191919; border-top: 1px solid #c8c8c8; }
        QLineEdit, QTextEdit, QPlainTextEdit, QListView, QTreeView,
        QSpinBox, QDoubleSpinBox, QComboBox {
            background-color: #ffffff; color: #191919; border: 1px solid #b8b8b8;
            selection-background-color: #0078d7; selection-color: #ffffff;
        }
        QTableView {
            background-color: #696867; alternate-background-color: #696867;
            color: #191919; border: 1px solid #b8b8b8;
            selection-background-color: #0078d7; selection-color: #ffffff;
        }
        QMainWindow QLabel, QDialog QLabel,
        QMainWindow QCheckBox, QDialog QCheckBox,
        QMainWindow QRadioButton, QDialog QRadioButton,
        QMainWindow QGroupBox, QDialog QGroupBox,
        QMainWindow QPushButton, QDialog QPushButton {
            color: #191919;
        }
        QHeaderView::section { background-color: #ededed; color: #191919; border: 1px solid #c8c8c8; }
        QTabWidget::pane { background-color: #f5f5f5; border: 1px solid #c8c8c8; }
        QTabBar::tab { background-color: #e7e7e7; color: #191919; border: 1px solid #c8c8c8; padding: 5px 10px; }
        QTabBar::tab:selected { background-color: #ffffff; }
        QGroupBox { color: #191919; }
        QToolTip { color: #202020; background-color: #ffffdc; border: 1px solid #8a8a8a; }
        QMenuBar { background-color: #f3f3f3; color: #202020; }
        QMenuBar::item:selected { background: #e2e2e2; color: #111111; }
        QMenuBar::item:pressed, QMenuBar::item:open { background: #d7e8f7; color: #111111; }
        QMenu { background-color: #ffffff; color: #202020; border: 1px solid #b8b8b8; padding: 4px; }
        QMenu::separator { background: #d0d0d0; }
        QMenu::item:selected { background: #d7e8f7; color: #111111; }
        QMenu::item:disabled { color: #999; }
        QMenu::item:selected:disabled { background: #eeeeee; }
        QPushButton#dayNightButton { color: #344f73; background: #e7e7e7; }
        QPushButton#dayNightButton:hover { background: #d7d7d7; }
        QPushButton#dayNightButton:pressed { background: #c8c8c8; }
        QWidget#windowTopArea, QWidget#customTitleBar { background: #f3f3f3; }
        QMainWindow#adampMainWindow { border: 1px solid #d0d0d0; }
        QLabel#customWindowTitle { color: #202020; }
        QPushButton#captionButton, QPushButton#closeCaptionButton { color: #202020; background: #f3f3f3; }
        QPushButton#captionButton:hover { background: #dddddd; }
        QPushButton#closeCaptionButton:hover { color: white; background: #c42b1c; }
    )");

    qApp->setStyleSheet(common + (dark ? darkSheet : lightSheet));

    // The narrow strips beside the 4:3 emulation image are painted by
    // ScreenWidget itself, not by the QMainWindow frame.
    if (m_screenWidget) {
        m_screenWidget->setBackgroundColor(dark ? QColor(QStringLiteral("#323232"))
                                                : QColor(QStringLiteral("#F3F3F3")));
    }

    if (m_windowBorderOverlay) {
        const QString borderColor = dark ? QStringLiteral("#161616")
                                         : QStringLiteral("#F3F3F3");
        m_windowBorderOverlay->setStyleSheet(
            QStringLiteral("QFrame#windowBorderOverlay { background: transparent; border: 2px solid %1; border-radius: 10px; }")
                .arg(borderColor));
        m_windowBorderOverlay->raise();
    }

    if (m_themeButton) {
        const char baseStyleProperty[] = "adampMainBaseStyleSheet";
        if (!property(baseStyleProperty).isValid())
            setProperty(baseStyleProperty, styleSheet());
        const QString baseStyle = property(baseStyleProperty).toString();
        const QString edgeColor = dark ? QStringLiteral("#202020")
                                       : QStringLiteral("#f3f3f3");
        setStyleSheet(baseStyle + QStringLiteral(
            "QMainWindow#adampMainWindow {"
            " background-color: %1; border: 1px solid %1;"
            "}").arg(edgeColor));

        if (auto* toggle = dynamic_cast<DayNightToggleButton*>(m_themeButton))
            toggle->setDarkMode(dark);
        m_themeButton->setToolTip(dark ? tr("Switch to day mode")
                                       : tr("Switch to night mode"));
    }

    if (m_cvBasicEditor)
        m_cvBasicEditor->setDarkTheme(dark);
}

void MainWindow::onSgmStatusChanged(bool enabled)
{
    if (!m_sgmLabel || !m_sepLabelSGM) return;

    if (enabled) {
        m_sgmLabel->setText("SGM");
        m_sgmLabel->show();
        m_sepLabelSGM->show();
    } else {
        m_sgmLabel->hide();
        m_sepLabelSGM->hide();
    }
}

void MainWindow::onFpsUpdated(int fps)
{
    m_fpsLabel->setText(QString("%1fps").arg(fps));

}

//---------------------------------------------------------------------------------------------
// GUI INTERFACE
//---------------------------------------------------------------------------------------------

void MainWindow::setupUI()
{
    // 1. Laad het lettertype uit je resources of lokale map
    // Pas het pad aan naar waar jouw .ttf staat (bijv. ":/fonts/mijnfont.ttf")
    int fontId = QFontDatabase::addApplicationFont(":/fonts/fonts/BeautifulPoliceOfficer-rvv8x.ttf");

    QString family;
    if (fontId != -1) {
        family = QFontDatabase::applicationFontFamilies(fontId).at(0);
        //qDebug() << "[UI] Custom font geladen:" << family;
    } else {
        qDebug() << "[UI] Kon custom font niet laden, fallback naar Roboto";
        family = "Roboto";
    }

    QFont menuFont(family, 16);
    menuFont.setBold(false);

    // Pas het toe op de menubalk zelf
    menuBar()->setFont(menuFont);

    // De variabele 'family' bevat de naam van je geladen font
    QString style = QString(
                        "QMenuBar { "
                        "  font-family: '%1';"
                        "  font-size: 16pt;"
                        "}"
                        "QMenu { "
                        "  font-family: '%1';"
                        "  font-size: 16pt;"
                        "}"
                        ).arg(family);

    this->setStyleSheet(style);

    // Optioneel: Pas het toe op alle menu's die worden toegevoegd
    // (In Qt erven acties en submenu's vaak het font van de parent)

    QMenu* fileMenu = menuBar()->addMenu(tr("File"));

    fileMenu->setFont(menuFont); // Specifiek voor het dropdown gedeelte

    // Coleco cartridge
    m_openColecoRomAction = new QAction(tr("Coleco Cartridge"), this);
    connect(m_openColecoRomAction, &QAction::triggered, this, &MainWindow::onOpenColecoRom);
    fileMenu->addAction(m_openColecoRomAction);

    m_openAtari2600RomAction = new QAction(tr("Atari 2600 Cartridge"), this);
    connect(m_openAtari2600RomAction, &QAction::triggered,
            this, &MainWindow::onOpenAtari2600Rom);
    fileMenu->addAction(m_openAtari2600RomAction);

    fileMenu->addSeparator();

    // Adam cartridge
    m_openAdamRomAction = new QAction(tr("Adam Cartridge"), this);
    connect(m_openAdamRomAction, &QAction::triggered, this, &MainWindow::onOpenAdamRom);
    fileMenu->addAction(m_openAdamRomAction);

    // m_prepareHardwareMediaAction = new QAction(tr("Manual MCU2 hardware media..."), this);
    // m_prepareHardwareMediaAction->setToolTip(
    //     tr("Manual fallback for inspecting and preparing physical MCU2 media"));
    // m_prepareHardwareMediaAction->setStatusTip(
    //     tr("Use only when automatic FujiNet media detection cannot be used"));
    // connect(m_prepareHardwareMediaAction, &QAction::triggered,
    //         this, &MainWindow::onPrepareHardwareMedia);
    // fileMenu->addAction(m_prepareHardwareMediaAction);
    // fileMenu->addSeparator();

    // Tape Menu's
    m_tapeMenuA = new QMenu(tr("Tape D1"), this);
    m_loadTapeActionA = new QAction(tr("Load"), this);
    connect(m_loadTapeActionA, &QAction::triggered, this, [this]() { onLoadTape(0); });
    m_tapeMenuA->addAction(m_loadTapeActionA);
    m_ejectTapeActionA = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionA, &QAction::triggered, this, [this]() { onEjectTape(0); });
    m_tapeMenuA->addAction(m_ejectTapeActionA);
    fileMenu->addMenu(m_tapeMenuA);
    m_tapeMenuB = new QMenu(tr("Tape D2"), this);
    m_loadTapeActionB = new QAction(tr("Load"), this);
    connect(m_loadTapeActionB, &QAction::triggered, this, [this]() { onLoadTape(1); });
    m_tapeMenuB->addAction(m_loadTapeActionB);
    m_ejectTapeActionB = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionB, &QAction::triggered, this, [this]() { onEjectTape(1); });
    m_tapeMenuB->addAction(m_ejectTapeActionB);
    fileMenu->addMenu(m_tapeMenuB);
    m_tapeMenuC = new QMenu(tr("Tape D3"), this);
    m_loadTapeActionC = new QAction(tr("Load"), this);
    connect(m_loadTapeActionC, &QAction::triggered, this, [this]() { onLoadTape(2); });
    m_tapeMenuC->addAction(m_loadTapeActionC);
    m_ejectTapeActionC = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionC, &QAction::triggered, this, [this]() { onEjectTape(2); });
    m_tapeMenuC->addAction(m_ejectTapeActionC);
    fileMenu->addMenu(m_tapeMenuC);
    m_tapeMenuD = new QMenu(tr("Tape D4"), this);
    m_loadTapeActionD = new QAction(tr("Load"), this);
    connect(m_loadTapeActionD, &QAction::triggered, this, [this]() { onLoadTape(3); });
    m_tapeMenuD->addAction(m_loadTapeActionD);
    m_ejectTapeActionD = new QAction(tr("Eject/Save"), this);
    connect(m_ejectTapeActionD, &QAction::triggered, this, [this]() { onEjectTape(3); });
    m_tapeMenuD->addAction(m_ejectTapeActionD);
    fileMenu->addMenu(m_tapeMenuD);

    // Disk Menu's
    m_diskMenuA = new QMenu(tr("Disk  D5"), this);
    m_loadDiskActionA = new QAction(tr("Load"), this);
    connect(m_loadDiskActionA, &QAction::triggered, this, [this]() { onLoadDisk(0); });
    m_diskMenuA->addAction(m_loadDiskActionA);
    m_ejectDiskActionA = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionA, &QAction::triggered, this, [this]() { onEjectDisk(0); });
    m_diskMenuA->addAction(m_ejectDiskActionA);
    fileMenu->addMenu(m_diskMenuA);
    m_diskMenuB = new QMenu(tr("Disk  D6"), this);
    m_loadDiskActionB = new QAction(tr("Load"), this);
    connect(m_loadDiskActionB, &QAction::triggered, this, [this]() { onLoadDisk(1); });
    m_diskMenuB->addAction(m_loadDiskActionB);
    m_ejectDiskActionB = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionB, &QAction::triggered, this, [this]() { onEjectDisk(1); });
    m_diskMenuB->addAction(m_ejectDiskActionB);
    fileMenu->addMenu(m_diskMenuB);
    m_diskMenuC = new QMenu(tr("Disk  D7"), this);
    m_loadDiskActionC = new QAction(tr("Load"), this);
    connect(m_loadDiskActionC, &QAction::triggered, this,  [this]() { onLoadDisk(2); });
    m_diskMenuC->addAction(m_loadDiskActionC);
    m_ejectDiskActionC = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionC, &QAction::triggered, this, [this]() { onEjectDisk(2); });
    m_diskMenuC->addAction(m_ejectDiskActionC);
    fileMenu->addMenu(m_diskMenuC);
    m_diskMenuD = new QMenu(tr("Disk  D8"), this);
    m_loadDiskActionD = new QAction(tr("Load"), this);
    connect(m_loadDiskActionD, &QAction::triggered, this,  [this]() { onLoadDisk(3); });
    m_diskMenuD->addAction(m_loadDiskActionD);
    m_ejectDiskActionD = new QAction(tr("Eject/Save"), this);
    connect(m_ejectDiskActionD, &QAction::triggered, this, [this]() { onEjectDisk(3); });
    m_diskMenuD->addAction(m_ejectDiskActionD);
    fileMenu->addMenu(m_diskMenuD);

    // Initial state disabled
    m_diskMenuA->setEnabled(false);
    m_diskMenuB->setEnabled(false);
    m_diskMenuC->setEnabled(false);
    m_diskMenuD->setEnabled(false);
    m_tapeMenuA->setEnabled(false);
    m_tapeMenuB->setEnabled(false);
    m_tapeMenuC->setEnabled(false);
    m_tapeMenuD->setEnabled(false);

    fileMenu->addSeparator();

    m_actReleaseAll = new QAction(tr("Release all media"), this);
    connect(m_actReleaseAll, &QAction::triggered, this, &MainWindow::onReleaseAll);
    fileMenu->addAction(m_actReleaseAll);

    fileMenu->addSeparator();
    m_actSaveState = new QAction(tr("Save State..."), this);
    connect(m_actSaveState, &QAction::triggered, this, &MainWindow::onSaveState);
    fileMenu->addAction(m_actSaveState);
    m_actLoadState = new QAction(tr("Load State..."), this);
    connect(m_actLoadState, &QAction::triggered, this, &MainWindow::onLoadState);
    fileMenu->addAction(m_actLoadState);
    m_actSaveState->setEnabled(false);
    m_actLoadState->setEnabled(false);
    fileMenu->addSeparator();
    m_settingsAction = new QAction(tr("Settings"), this);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);
    fileMenu->addAction(m_settingsAction);
    fileMenu->addSeparator();
    m_quitAction = new QAction(tr("&Exit"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &MainWindow::close);
    fileMenu->addAction(m_quitAction);

    // --- DEBUG MENU ---
    QMenu* debugMenu = menuBar()->addMenu(tr("Debug"));
    m_startAction = new QAction(tr("Run/Stop"), this);
    m_startAction->setShortcut(Qt::Key_F11);
    connect(m_startAction, &QAction::triggered, this, &MainWindow::onRunStop);
    debugMenu->addAction(m_startAction);
    debugMenu->addSeparator();
    m_actShowLog = new QAction(tr("Logger"), this);
    m_actShowLog->setCheckable(true);
    m_actShowLog->setChecked(false);
    debugMenu->addAction(m_actShowLog);

    QAction* actClearLog = new QAction(tr("Clear Logger"), this);
    connect(actClearLog, &QAction::triggered, this, [this]() {
        if (m_logView) {
            m_logView->clear();
        }
    });
    debugMenu->addAction(actClearLog);
    debugMenu->addSeparator();
    m_debuggerAction = new QAction(tr("Debugger"), this);
    debugMenu->addAction(m_debuggerAction);

    m_actShowTerminal = new QAction(tr("Terminal"), this);
    debugMenu->addAction(m_actShowTerminal);

    connect(m_actShowTerminal, &QAction::triggered,
            this, &MainWindow::onShowDebugTerminal);

    // --- TOOLS MENU ---
    m_toolsMenu = menuBar()->addMenu(tr("Tools"));
    m_actShowNameTable = new QAction(tr("Name Table Viewer"), this);
    connect(m_actShowNameTable, &QAction::triggered, this, &MainWindow::onShowNameTable);
    m_toolsMenu->addAction(m_actShowNameTable);
    m_actShowPatternTable = new QAction(tr("Pattern Table Viewer"), this);
    connect(m_actShowPatternTable, &QAction::triggered, this, &MainWindow::onShowPatternTable);
    m_toolsMenu->addAction(m_actShowPatternTable);
    m_actShowSpriteTable = new QAction(tr("Sprite Table Viewer"), this);
    connect(m_actShowSpriteTable, &QAction::triggered, this, &MainWindow::onShowSpriteTable);
    m_toolsMenu->addAction(m_actShowSpriteTable);
    m_toolsMenu->addSeparator();
    m_cartInfoAction = new QAction(tr("Cart profile"), this);
    m_toolsMenu->addAction(m_cartInfoAction);

     m_actImageManager = new QAction(tr("EOS Media Manager"), this);
     connect(m_actImageManager, &QAction::triggered, this, [this]() {
        if (!m_imageManagerDialog)
            return;

        m_imageManagerDialog->setDiskRootPath(m_diskPath);
        m_imageManagerDialog->setTapeRootPath(m_tapePath);

        m_imageManagerDialog->show();
        m_imageManagerDialog->raise();
        m_imageManagerDialog->activateWindow();
    });
    m_toolsMenu->addSeparator();
    m_toolsMenu->addAction(m_actImageManager);
    // --- CVBASIC ---
    m_actCvBasicEditor = new QAction(tr("CVBasic SUITE PLUG-IN"), this);
    connect(m_actCvBasicEditor, &QAction::triggered,
            this, &MainWindow::onShowCvBasicEditor);
    m_toolsMenu->addSeparator();
    m_toolsMenu->addAction(m_actCvBasicEditor);

    // --- SMARTBASIC TEXT INJECTOR ---
    m_toolsMenu->addSeparator();
    m_actStartBasicInject = new QAction(tr("START INJECT"), this);
    connect(m_actStartBasicInject, &QAction::triggered,
            this, &MainWindow::onStartBasicInject);
    m_toolsMenu->addAction(m_actStartBasicInject);

    m_actStopBasicInject = new QAction(tr("STOP INJECT"), this);
    m_actStopBasicInject->setEnabled(false);
    connect(m_actStopBasicInject, &QAction::triggered,
            this, &MainWindow::onStopBasicInject);
    m_toolsMenu->addAction(m_actStopBasicInject);

    m_basicInjectTimer = new QTimer(this);
    m_basicInjectTimer->setSingleShot(true);
    connect(m_basicInjectTimer, &QTimer::timeout,
            this, &MainWindow::injectNextBasicCharacter);

    // --- ATARI 2600 MENU ---
    m_atariInputsMenu = new StayOpenMenu(tr("ATARI"), menuBar());
    menuBar()->addMenu(m_atariInputsMenu);
    m_atariGameSelectAction = m_atariInputsMenu->addAction(tr("GAME PLAY \tTRIG L"));
    m_atariGameSelectAction = m_atariInputsMenu->addAction(tr("GAME SELECT \tTRIG R"));
    m_atariInputsMenu->addSeparator();
    m_atariGameResetAction = m_atariInputsMenu->addAction(tr("GAME RESET\tKEY 4"));
    connect(m_atariGameSelectAction, &QAction::triggered, this, [this]() {
        if (!m_inputWidget) return;
        m_inputWidget->setJoystickSelect(true);
        QTimer::singleShot(150, this, [this]() {
            if (m_inputWidget) m_inputWidget->setJoystickSelect(false);
        });
    });
    connect(m_atariGameResetAction, &QAction::triggered, this, [this]() {
        if (!m_inputWidget) return;
        m_inputWidget->setJoystickStart(true);
        QTimer::singleShot(150, this, [this]() {
            if (m_inputWidget) m_inputWidget->setJoystickStart(false);
        });
    });

    m_atariInputsMenu->addSeparator();
    m_atariColorAction = m_atariInputsMenu->addAction(tr("COLOR-B/W\tKEY 1"));
    m_atariColorAction->setCheckable(true);
    m_atariColorAction->setChecked(m_atariColor);
    m_atariColorAction->setToolTip(tr("Checked = COLOR, unchecked = B/W"));
    connect(m_atariColorAction, &QAction::toggled, this, [this](bool color) {
        m_atariColor = color;
        if (m_inputWidget) m_inputWidget->setAtari2600Color(color);
        saveSettings();
        updateAtariStatusBar();
    });
    m_atariInputsMenu->addSeparator();
    QMenu *leftDifficultyMenu = new StayOpenMenu(tr("LEFT DIFFICULTY\tKEY 2"), m_atariInputsMenu);
    m_atariInputsMenu->addMenu(leftDifficultyMenu);
    QActionGroup *leftDifficultyGroup = new QActionGroup(this);
    leftDifficultyGroup->setExclusive(true);
    QAction *leftDifficultyA = leftDifficultyMenu->addAction(tr("A (Expert)"));
    QAction *leftDifficultyB = leftDifficultyMenu->addAction(tr("B (Novice)"));
    m_atariLeftDifficultyAAction = leftDifficultyA;
    m_atariLeftDifficultyBAction = leftDifficultyB;
    leftDifficultyA->setCheckable(true);
    leftDifficultyB->setCheckable(true);
    leftDifficultyGroup->addAction(leftDifficultyA);
    leftDifficultyGroup->addAction(leftDifficultyB);
    (m_atariLeftDifficultyA ? leftDifficultyA : leftDifficultyB)->setChecked(true);

    QMenu *rightDifficultyMenu = new StayOpenMenu(tr("RIGHT DIFFICULTY\tKEY 3"), m_atariInputsMenu);
    m_atariInputsMenu->addMenu(rightDifficultyMenu);
    QActionGroup *rightDifficultyGroup = new QActionGroup(this);
    rightDifficultyGroup->setExclusive(true);
    QAction *rightDifficultyA = rightDifficultyMenu->addAction(tr("A (Expert)"));
    QAction *rightDifficultyB = rightDifficultyMenu->addAction(tr("B (Novice)"));
    m_atariRightDifficultyAAction = rightDifficultyA;
    m_atariRightDifficultyBAction = rightDifficultyB;
    rightDifficultyA->setCheckable(true);
    rightDifficultyB->setCheckable(true);
    rightDifficultyGroup->addAction(rightDifficultyA);
    rightDifficultyGroup->addAction(rightDifficultyB);
    (m_atariRightDifficultyA ? rightDifficultyA : rightDifficultyB)->setChecked(true);

    connect(leftDifficultyA, &QAction::triggered, this, [this]() {
        m_atariLeftDifficultyA = true;
        if (m_inputWidget) m_inputWidget->setAtari2600LeftDifficultyA(true);
        saveSettings();
        updateAtariStatusBar();
    });
    connect(leftDifficultyB, &QAction::triggered, this, [this]() {
        m_atariLeftDifficultyA = false;
        if (m_inputWidget) m_inputWidget->setAtari2600LeftDifficultyA(false);
        saveSettings();
        updateAtariStatusBar();
    });
    connect(rightDifficultyA, &QAction::triggered, this, [this]() {
        m_atariRightDifficultyA = true;
        if (m_inputWidget) m_inputWidget->setAtari2600RightDifficultyA(true);
        saveSettings();
        updateAtariStatusBar();
    });
    connect(rightDifficultyB, &QAction::triggered, this, [this]() {
        m_atariRightDifficultyA = false;
        if (m_inputWidget) m_inputWidget->setAtari2600RightDifficultyA(false);
        saveSettings();
        updateAtariStatusBar();
    });
    m_atariInputsMenu->addSeparator();
    m_atariPhosphorEffectAction=m_atariInputsMenu->addAction(tr("PHOSPHOR EFFECT"));
    m_atariPhosphorEffectAction->setCheckable(true);
    m_atariPhosphorEffectAction->setChecked(m_atariPhosphorEffect);
    connect(m_atariPhosphorEffectAction,&QAction::toggled,this,[this](bool enabled){
        m_atariPhosphorEffect=enabled;
        if(m_colecoController)
            QMetaObject::invokeMethod(m_colecoController,"setAtari2600PhosphorEffect",
                                      Qt::QueuedConnection,Q_ARG(bool,enabled));
        saveSettings();
    });
    m_atariInputsMenu->menuAction()->setVisible(m_machineType == MACHINE_ATARI2600);

    // --- INPUT MENU ---
    QMenu* inputMenu = menuBar()->addMenu(tr("Input"));
    inputMenu->setToolTipsVisible(true);

    // Keypad (was in Tools)
    m_actToggleKeyboard = new QAction(tr("Keypad"), this);
    m_actToggleKeyboard->setCheckable(true);
    m_actToggleKeyboard->setChecked(false);
    inputMenu->addAction(m_actToggleKeyboard);

    // Keypad mapper (was in Tools)
    m_actJoypadMapper = new QAction(tr("Keypad mapper"), this);
    inputMenu->addAction(m_actJoypadMapper);

    inputMenu->addSeparator();

    m_adamInputMenu = inputMenu->addMenu(tr("ADAM game mode"));
    m_adamInputGroup = new QActionGroup(this);
    m_adamInputGroup->setExclusive(true);

    m_actAdamGameOn = new QAction(tr("ENABLED"), this);
    m_actAdamGameOn->setCheckable(true);
    m_actAdamGameOn->setChecked(false);
    m_adamInputGroup->addAction(m_actAdamGameOn);
    m_adamInputMenu->addAction(m_actAdamGameOn);

    m_actAdamGameOff = new QAction(tr("DISABLED"), this);
    m_actAdamGameOff->setCheckable(true);
    m_actAdamGameOff->setChecked(true);
    m_adamInputGroup->addAction(m_actAdamGameOff);
    m_adamInputMenu->addAction(m_actAdamGameOff);

    m_adamInputMenu->setEnabled(false);

    inputMenu->addSeparator();

    // Joystick Controller Menu (was in Tools)
    QMenu* joystickControllerMenu = inputMenu->addMenu(tr("Joystick Controller")); // Hoofdmenu

    // Actiegroep om ervoor te zorgen dat slechts één type is geselecteerd
    m_joystickGroup = new QActionGroup(this);
    m_joystickGroup->setExclusive(true);

    // 1. Algemene Controller
    m_actJoystickGeneral = new QAction(tr("General"), this);
    m_actJoystickGeneral->setCheckable(true);
    m_actJoystickGeneral->setData(0); // 0 = Generic/Default type
    m_joystickGroup->addAction(m_actJoystickGeneral);
    joystickControllerMenu->addAction(m_actJoystickGeneral);

    // 2. PlayStation Controller
    m_actJoystickPS = new QAction(tr("PlayStation"), this);
    m_actJoystickPS->setCheckable(true);
    m_actJoystickPS->setData(1); // 1 = PlayStation type (voor POV mapping)
    m_joystickGroup->addAction(m_actJoystickPS);
    joystickControllerMenu->addAction(m_actJoystickPS);

    // 3. Xbox Controller
    m_actJoystickXbox = new QAction(tr("Xbox"), this);
    m_actJoystickXbox->setCheckable(true);
    m_actJoystickXbox->setData(2); // 2 = Xbox type (optionele toekomstige mapping)
    m_joystickGroup->addAction(m_actJoystickXbox);
    joystickControllerMenu->addAction(m_actJoystickXbox);

    // Verbinden met de afhandelingsslot
    connect(m_joystickGroup, &QActionGroup::triggered, this, &MainWindow::onJoystickTypeChanged);

    // Synchronisatie van de geladen waarde
    if (m_joystickGroup) {
        const QList<QAction*> actions = m_joystickGroup->actions();
        for (int i = 0; i < actions.size(); ++i) {
            QAction* action = actions.at(i);
            if (action && action->data().toInt() == m_joystickType) {
                action->setChecked(true);
                break;
            }
        }
    }

    inputMenu->addSeparator();
    m_actTogglePaddleMode = new QAction(tr("Paddle Mode"), this);
    m_actTogglePaddleMode->setCheckable(true);
    m_actTogglePaddleMode->setChecked(m_usePaddleMode); // Synchroniseer met instelling
    inputMenu->addAction(m_actTogglePaddleMode);

    // De connectie
    connect(m_actTogglePaddleMode, &QAction::toggled, this, &MainWindow::onTogglePaddleMode);

    m_actPaddleDirectionUpDown = new QAction(tr("INPUT DIRECTION UP/DOWN"), this);
    m_actPaddleDirectionUpDown->setCheckable(true);
    m_actPaddleDirectionUpDown->setChecked(m_paddleDirectionUpDown);
    m_actPaddleDirectionUpDown->setEnabled(m_usePaddleMode);
    inputMenu->addAction(m_actPaddleDirectionUpDown);
    connect(m_actPaddleDirectionUpDown, &QAction::toggled, this, [this](bool upDown) {
        m_paddleDirectionUpDown = upDown;
        if (m_inputWidget) m_inputWidget->setPaddleVertical(upDown);
        saveSettings();
    });

    inputMenu->addSeparator();
    m_actToggleDrivingControllerMode = new QAction(tr("DRIVING CONTROLLER MODE"), this);
    m_actToggleDrivingControllerMode->setCheckable(true);
    m_actToggleDrivingControllerMode->setChecked(m_useDrivingControllerMode);
    m_actToggleDrivingControllerMode->setToolTip(
        tr("DUKES OF HAZZARD\n"
           "STEERING WHEEL LEFT (TRIG L)\n"
           "STEERING WHEEL RIGHT (TRIG R)\n"
           "GASPEDAL (BUT #)\n"
           "GEARING (BUT UP,DOWN,LEFT RIGHT)"));
    inputMenu->addAction(m_actToggleDrivingControllerMode);
    connect(m_actToggleDrivingControllerMode, &QAction::toggled,
            this, &MainWindow::onToggleDrivingControllerMode);

    // --- VIDEO MENU ---
    QMenu* videoMenu = menuBar()->addMenu(tr("Video"));
    QActionGroup* videoGroup = new QActionGroup(this);
    // NTSC & PAL
    m_actToggleNTSC = new QAction(tr("NTSC"), this);
    m_actToggleNTSC->setCheckable(true);
    m_actToggleNTSC->setChecked(true);
    videoGroup->addAction(m_actToggleNTSC);
    videoMenu->addAction(m_actToggleNTSC);
    m_actTogglePAL  = new QAction(tr("PAL "), this);
    m_actTogglePAL->setCheckable(true);
    videoGroup->addAction(m_actTogglePAL);
    videoMenu->addAction(m_actTogglePAL);
    videoMenu->addSeparator();
    m_actResetGeometry = new QAction(tr("RESET GEOMETRY"), this);
    videoMenu->addAction(m_actResetGeometry);
    videoMenu->addSeparator();
    m_scanlinesMenu = videoMenu->addMenu(tr("Simulate Scanlines"));
    m_scanlinesGroup = new QActionGroup(this);
    m_scanlinesGroup->setExclusive(true);
    // Optie 1: Scanlines UIT
    QAction* actScanlinesOff = new QAction(tr("Off"), this);
    actScanlinesOff->setCheckable(true);
    actScanlinesOff->setChecked(m_scanlinesMode == ScanlinesOff);
    actScanlinesOff->setData(ScanlinesOff);
    m_scanlinesGroup->addAction(actScanlinesOff);
    m_scanlinesMenu->addAction(actScanlinesOff);
    m_scanlinesMenu->addSeparator();
    // Optie 2: TV Scanlines (TelevizeImage)
    m_actScanlinesTV = new QAction(tr("Horizontal"), this);
    m_actScanlinesTV->setCheckable(true);
    m_actScanlinesTV->setChecked(m_scanlinesMode == ScanlinesTV);
    m_actScanlinesTV->setData(ScanlinesTV);
    m_scanlinesGroup->addAction(m_actScanlinesTV);
    m_scanlinesMenu->addAction(m_actScanlinesTV);
    // Optie 3: LCD Scanlines (LcdizeImage)
    m_actScanlinesLCD = new QAction(tr("Vertical"), this);
    m_actScanlinesLCD->setCheckable(true);
    m_actScanlinesLCD->setChecked(m_scanlinesMode == ScanlinesLCD);
    m_actScanlinesLCD->setData(ScanlinesLCD);
    m_scanlinesGroup->addAction(m_actScanlinesLCD);
    m_scanlinesMenu->addAction(m_actScanlinesLCD);
    // Optie 4: LCD Raster (RasterizeImage)
    m_actScanlinesRaster = new QAction(tr("Raster"), this);
    m_actScanlinesRaster->setCheckable(true);
    m_actScanlinesRaster->setChecked(m_scanlinesMode == ScanlinesRaster);
    m_actScanlinesRaster->setData(ScanlinesRaster);
    m_scanlinesGroup->addAction(m_actScanlinesRaster);
    m_scanlinesMenu->addAction(m_actScanlinesRaster);

    videoMenu->addSeparator();
    m_colorFilterMenu = videoMenu->addMenu(tr("Color Filter"));
    m_colorFilterGroup = new QActionGroup(this);
    m_colorFilterGroup->setExclusive(true);

    // Off
    QAction *actFilterOff = new QAction(tr("Off"), this);
    actFilterOff->setCheckable(true);
    actFilterOff->setData(ColorFilterOff);
    actFilterOff->setChecked(m_colorFilterMode == ColorFilterOff);
    m_colorFilterGroup->addAction(actFilterOff);
    m_colorFilterMenu->addAction(actFilterOff);
    m_colorFilterMenu->addSeparator();

    // Monochrome
    QAction *actFilterMono = new QAction(tr("Monochrome"), this);
    actFilterMono->setCheckable(true);
    actFilterMono->setData(ColorFilterMonochrome);
    actFilterMono->setChecked(m_colorFilterMode == ColorFilterMonochrome);
    m_colorFilterGroup->addAction(actFilterMono);
    m_colorFilterMenu->addAction(actFilterMono);

    // Sepia Tones
    QAction *actFilterSepia = new QAction(tr("Sepia Tones"), this);
    actFilterSepia->setCheckable(true);
    actFilterSepia->setData(ColorFilterSepia);
    actFilterSepia->setChecked(m_colorFilterMode == ColorFilterSepia);
    m_colorFilterGroup->addAction(actFilterSepia);
    m_colorFilterMenu->addAction(actFilterSepia);

    // Green CRT
    QAction *actFilterGreen = new QAction(tr("Green CRT"), this);
    actFilterGreen->setCheckable(true);
    actFilterGreen->setData(ColorFilterGreenCRT);
    actFilterGreen->setChecked(m_colorFilterMode == ColorFilterGreenCRT);
    m_colorFilterGroup->addAction(actFilterGreen);
    m_colorFilterMenu->addAction(actFilterGreen);

    // Amber CRT
    QAction *actFilterAmber = new QAction(tr("Amber CRT"), this);
    actFilterAmber->setCheckable(true);
    actFilterAmber->setData(ColorFilterAmberCRT);
    actFilterAmber->setChecked(m_colorFilterMode == ColorFilterAmberCRT);
    m_colorFilterGroup->addAction(actFilterAmber);
    m_colorFilterMenu->addAction(actFilterAmber);

    // CMY Raster
    QAction *actFilterCMY = new QAction(tr("CMY Raster"), this);
    actFilterCMY->setCheckable(true);
    actFilterCMY->setData(ColorFilterCMY);
    actFilterCMY->setChecked(m_colorFilterMode == ColorFilterCMY);
    m_colorFilterGroup->addAction(actFilterCMY);
    m_colorFilterMenu->addAction(actFilterCMY);

    // RGB Raster
    QAction *actFilterRGB = new QAction(tr("RGB Raster"), this);
    actFilterRGB->setCheckable(true);
    actFilterRGB->setData(ColorFilterRGB);
    actFilterRGB->setChecked(m_colorFilterMode == ColorFilterRGB);
    m_colorFilterGroup->addAction(actFilterRGB);
    m_colorFilterMenu->addAction(actFilterRGB);

    // React op wijziging
    connect(m_colorFilterGroup, &QActionGroup::triggered,
            this, &MainWindow::onColorFilterModeChanged);



    videoMenu->addSeparator();
    m_scalingMenu = videoMenu->addMenu(tr("Video Mode"));
    m_scalingGroup = new QActionGroup(this);
    m_scalingGroup->setExclusive(true);
    // Scaling Mode 2: Sharp
    m_actScalingSharp = new QAction(tr("Sharp"), this);
    m_actScalingSharp->setCheckable(true);
    // De waarde '0' correspondeert met ModeSharp
    m_actScalingSharp->setData(0);
    m_scalingGroup->addAction(m_actScalingSharp);
    m_scalingMenu->addAction(m_actScalingSharp);
    // Scaling Mode 1: Smooth
    m_actScalingSmooth = new QAction(tr("Smooth"), this);
    m_actScalingSmooth->setCheckable(true);
    // De waarde '1' correspondeert met ModeSmooth
    m_actScalingSmooth->setData(1);
    m_scalingGroup->addAction(m_actScalingSmooth);
    m_scalingMenu->addAction(m_actScalingSmooth);
    // Scaling Mode 3: EPX
    m_actScalingEPX = new QAction(tr("EPX Filter (2x)"), this);
    m_actScalingEPX->setCheckable(true);
    // De waarde '2' correspondeert met ModeEPX
    m_actScalingEPX->setData(2);
    m_scalingGroup->addAction(m_actScalingEPX);
    m_scalingMenu->addAction(m_actScalingEPX);
    connect(m_scalingGroup, &QActionGroup::triggered, this, &MainWindow::onScalingModeChanged);

    if (m_scalingGroup) {
        const QList<QAction*> actions = m_scalingGroup->actions();
        for (int i = 0; i < actions.size(); ++i) {
            QAction* action = actions.at(i);
            if (action && action->data().toInt() == m_scalingMode) {
                action->setChecked(true);
                break;
            }
        }
    }
    videoMenu->addSeparator();

    // Volledig scherm
    m_actFullScreen = new QAction(tr("Full Screen"), this);
    m_actFullScreen->setCheckable(true);
    m_actFullScreen->setChecked(m_startFullScreen);
    m_actFullScreen->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F));
    videoMenu->addAction(m_actFullScreen);
    // Reset Window Size
    m_actResetSize = new QAction(tr("Reset Window Size"), this);
    connect(m_actResetSize, &QAction::triggered, this, &MainWindow::onResetWindowSize);
    videoMenu->addAction(m_actResetSize);
    videoMenu->addSeparator();
    // Show Bezels/Wallpaper
    m_actToggleBezels = new QAction(tr("Show Bezels"), this);
    m_actToggleBezels->setCheckable(true);
    m_actToggleBezels->setChecked(m_useBezels);
    connect(m_actToggleBezels, &QAction::toggled, this, &MainWindow::onToggleBezels);

    videoMenu->addAction(m_actToggleBezels);
    videoMenu->addSeparator();
    // Save Screenshot
    m_actSaveScreenshot = new QAction(tr("Save Screenshot..."), this);
    videoMenu->addAction(m_actSaveScreenshot);

    // --- HARDWARE MENU ---
    QMenu* hardwareMenu = menuBar()->addMenu(tr("Hardware"));

    hardwareMenu->addSeparator();
    m_actHardware = new QAction(tr("Hardware selection"), this);
    hardwareMenu->addAction(m_actHardware);
    connect(m_actHardware, &QAction::triggered, this, &MainWindow::onOpenHardware);
    hardwareMenu->addSeparator();

    m_actDTsound = new QAction(tr("Disc-Tape sounds"), this);
    hardwareMenu->addAction(m_actDTsound);
    m_actDTsound->setCheckable(true);
    m_actDTsound->setChecked(m_useDTsound);
    connect(m_actDTsound, &QAction::toggled, this, &MainWindow::onToggleDTsound);

    hardwareMenu->addSeparator();
    m_actPrinterOutput = hardwareMenu->addAction("Printer Output", this, &MainWindow::onShowPrinterWindow);
    m_actPrinterOutput->setShortcut(QKeySequence("Ctrl+Shift+P"));
    hardwareMenu->addSeparator();

    m_biosSourceMenu = hardwareMenu->addMenu(tr("BIOS Source"));

    m_actColecoBiosSource = m_biosSourceMenu->addAction(tr("Coleco: Internal"));
    m_actEosBiosSource    = m_biosSourceMenu->addAction(tr("EOS: Internal"));
    m_actWriterBiosSource = m_biosSourceMenu->addAction(tr("Writer: Internal"));

    // Maak ze read-only, ze dienen alleen als status-indicator
    m_actColecoBiosSource->setEnabled(false);
    m_actEosBiosSource->setEnabled(false);
    m_actWriterBiosSource->setEnabled(false);

    hardwareMenu->addSeparator();


    // --- OPTIONS MENU ---
    QMenu* optionsMenu = menuBar()->addMenu(tr("Options"));

    m_actToggleSnap = new QAction(tr("Snap to mainwindow"), this);
    m_actToggleSnap->setCheckable(true);
    m_actToggleSnap->setChecked(m_snapWindows);
    connect(m_actToggleSnap, &QAction::toggled, this, &MainWindow::onToggleSnap);
    optionsMenu->addAction(m_actToggleSnap);

    //optionsMenu->addSeparator();

    // --- INFO MENU ---
    QMenu* infoMenu = menuBar()->addMenu(tr("Info"));
    m_actWiki = new QAction(tr("Github page"), this);
    infoMenu->addAction(m_actWiki);
    m_actReport = new QAction(tr("Report a bug"), this);
    infoMenu->addAction(m_actReport);
    infoMenu->addSeparator();
    m_actDonate = new QAction(tr("Donate"), this);
    infoMenu->addAction(m_actDonate);
    infoMenu->addSeparator();
    m_actAbout = new QAction(tr("About"), this);
    infoMenu->addAction(m_actAbout);

    // --- HELP ---
    QMenu* helpMenu = menuBar()->addMenu(tr("Help"));
    m_actHelp = new QAction(tr("Github Help page"), this);
    helpMenu->addAction(m_actHelp);

    // --- CONNECTIES ---
    connect(m_actWiki, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/dvdh1961/ADAMP"));
    });
    connect(m_actReport, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/dvdh1961/ADAMP/issues"));
    });
    connect(m_actDonate, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl("https://www.paypal.com/donate?business=dannyvdh@pandora.be"));
    });
    connect(m_actHelp, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/dvdh1961/ADAMP/blob/main/HELP.md"));
    });

    // Scanlines connectie
    connect(m_scanlinesGroup, &QActionGroup::triggered, this, &MainWindow::onScanlinesModeChanged);
    connect(m_actToggleNTSC, &QAction::triggered, this, &MainWindow::onToggleVideoStandard);
    connect(m_actTogglePAL, &QAction::triggered, this, &MainWindow::onToggleVideoStandard);
    connect(m_actResetGeometry, &QAction::triggered, this, &MainWindow::onResetGeometry);
    connect(m_actToggleKeyboard, &QAction::toggled, this, &MainWindow::onToggleKeyboard);
    connect(m_cartInfoAction, &QAction::triggered, this, &MainWindow::onOpenCartInfo);
    connect(m_actAbout, &QAction::triggered, this, &MainWindow::showAboutDialog);
    connect(m_actFullScreen, &QAction::toggled, this, &MainWindow::onToggleFullScreen);
    connect(m_actSaveScreenshot, &QAction::triggered,this, &MainWindow::onSaveScreenshot);
    connect(m_actJoypadMapper, &QAction::triggered, this, &MainWindow::onOpenJoypadMapper);
    connect(m_actAdamGameOff, &QAction::triggered, this, &MainWindow::onAdamGameMode);
    connect(m_actAdamGameOn, &QAction::triggered, this, &MainWindow::onAdamGameMode);

    connect(m_powerBtn, &QPushButton::clicked, this, &MainWindow::onPowerBtnClicked);
    connect(m_resetAdamBtn, &QPushButton::clicked, this, &MainWindow::onResetAdamBtnClicked);
    connect(m_resetCartBtn, &QPushButton::clicked, this, &MainWindow::onResetCartBtnClicked);
    connect(m_resetCartBtn, &QPushButton::pressed, this, [this]() {
        if (m_machineType != MACHINE_ATARI2600 || !m_resetCartBtn)
            return;

        m_resetCartBtn->setChecked(true);
        m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_on.png"));
        if (m_colecoController) {
            QMetaObject::invokeMethod(m_colecoController, "setAtari2600ResetSwitch",
                                      Qt::QueuedConnection, Q_ARG(bool, true));
        }
    });
    connect(m_resetCartBtn, &QPushButton::released, this, [this]() {
        if (m_machineType != MACHINE_ATARI2600 || !m_resetCartBtn)
            return;

        // The real Atari RESET switch is momentary: release it immediately in
        // the emulated console, but keep the button lit briefly so the user
        // gets the same clear visual feedback as with ADAM Reset.
        if (m_colecoController) {
            QMetaObject::invokeMethod(m_colecoController, "setAtari2600ResetSwitch",
                                      Qt::QueuedConnection, Q_ARG(bool, false));
        }

        QTimer::singleShot(250, this, [this]() {
            if (!m_resetCartBtn || m_machineType != MACHINE_ATARI2600 ||
                m_resetCartBtn->isDown())
                return;

            m_resetCartBtn->setChecked(false);
            m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png"));
        });
    });

    onEmuPausedChanged(false);
}

//---------------------------------------------------------------------------------------------
// PREPARE LOGGER DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::setUpLogWindow()
{
    const int w = 770;
    const int h = 500;
    m_logView = new LogWidget(nullptr);
    LogWidget::bindTo(m_logView);
    m_logView->setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    m_logView->setWindowTitle("ADAM+ Debug logger");
    m_logView->resize(w, h);

    m_logView->setAttribute(Qt::WA_DeleteOnClose, false);
    m_logView->hide();
    m_logView->installEventFilter(this);
}

//---------------------------------------------------------------------------------------------
// OPEN/LOAD & SAVE SETTINGS DIALOGS
//---------------------------------------------------------------------------------------------

void MainWindow::onOpenSettings()
{
    if (!m_settingsWindow)
        return;

    // Helper om defaults/relatief -> absoluut te maken
    const QDir appDir(QCoreApplication::applicationDirPath());
    auto resolvePath = [&](QString input, const QString& mediaSubdir) -> QString {
        input = input.trimmed();

        if (input.isEmpty() || input == ".")
            return QDir::cleanPath(appDir.filePath("media/" + mediaSubdir));

        if (QDir::isRelativePath(input))
            input = QDir::cleanPath(appDir.filePath(input));

        return input;
    };

    // 1) Push huidige settings naar dialog
    struct PathBind {
        void (SettingsWindow::*set)(const QString&);
        QString (SettingsWindow::*get)() const;
        QString* target;
        const char* subdir;
    };

    PathBind paths[] = {
        { &SettingsWindow::setRomPath,         &SettingsWindow::romPath,         &m_romPath,         "roms" },
        { &SettingsWindow::setAtariRomPath,    &SettingsWindow::atariRomPath,    &m_atariRomPath,    "aroms" },
        { &SettingsWindow::setDiskPath,        &SettingsWindow::diskPath,        &m_diskPath,        "disks" },
        { &SettingsWindow::setTapePath,        &SettingsWindow::tapePath,        &m_tapePath,        "tapes" },
        { &SettingsWindow::setStatePath,       &SettingsWindow::statePath,       &m_statePath,       "states" },
        { &SettingsWindow::setBreakpointPath,  &SettingsWindow::breakpointPath,  &m_breakpointPath,  "breakpoints" },
        { &SettingsWindow::setScreenshotPath,  &SettingsWindow::screenshotPath,  &m_screenshotsPath, "screenshots" },
        { &SettingsWindow::setSymbolPath,      &SettingsWindow::symbolPath,      &m_symbolsPath,     "symbols" },
        { &SettingsWindow::setAdamBezelPath,   &SettingsWindow::adamBezelPath,   &m_adamBezelPath,   "bezels" },
        { &SettingsWindow::setCvBezelPath,     &SettingsWindow::cvBezelPath,     &m_cvBezelPath,     "bezels" }
    };

    for (auto& p : paths)
        (m_settingsWindow->*(p.set))(*p.target);

    m_settingsWindow->setColecoBiosPath(m_colecoBiosPath);
    m_settingsWindow->setEosBiosPath(m_eosBiosPath);
    m_settingsWindow->setWriterBiosPath(m_writerBiosPath);
    m_settingsWindow->setAdamStartupPath(m_adamStartupPath);
    m_settingsWindow->setCvBasicSourcePath(m_cvbasicSourcePath);
    m_settingsWindow->setCvBasicBuildPath(m_cvbasicBuildPath);
    m_settingsWindow->setCvBasicExePath(m_cvbasicExePath);
    m_settingsWindow->setGasm80ExePath(m_gasm80ExePath);
    m_settingsWindow->setSpriteSourcePath(m_spriteSourcePath);
    m_settingsWindow->setSpriteBuildPath(m_spriteBuildPath);
    m_settingsWindow->setSoundSourcePath(m_soundSourcePath);
    m_settingsWindow->setSoundBuildPath(m_soundBuildPath);
    m_settingsWindow->setAdamBootMode(m_adamBootMode);

    // 2) Toon dialog
    if (m_settingsWindow->exec() != QDialog::Accepted)
        return;

    // 3) Pull waarden terug uit dialog + resolve
    for (auto& p : paths) {
        const QString v = (m_settingsWindow->*(p.get))();
        *p.target = resolvePath(v, p.subdir);
    }

    if (!m_atariRomPath.trimmed().isEmpty())
        QDir().mkpath(m_atariRomPath);

    m_colecoBiosPath = m_settingsWindow->colecoBiosPath();
    m_eosBiosPath    = m_settingsWindow->eosBiosPath();
    m_writerBiosPath = m_settingsWindow->writerBiosPath();
    m_adamStartupPath = m_settingsWindow->adamStartupPath();
    m_cvbasicSourcePath = resolvePath(m_settingsWindow->cvbasicSourcePath(), "cvbasic/source");
    m_cvbasicBuildPath  = resolvePath(m_settingsWindow->cvbasicBuildPath(),  "cvbasic/build");
    m_cvbasicExePath =  m_settingsWindow->cvbasicExePath();
    m_gasm80ExePath = m_settingsWindow->gasm80ExePath();
    m_spriteSourcePath = resolvePath(m_settingsWindow->spriteSourcePath(), "cvbasic/source");
    m_spriteBuildPath  = resolvePath(m_settingsWindow->spriteBuildPath(),  "cvbasic/build/sprites");
    m_soundSourcePath  = resolvePath(m_settingsWindow->soundSourcePath(),  "cvbasic/sound");
    m_soundBuildPath   = resolvePath(m_settingsWindow->soundBuildPath(),   "cvbasic/build/sound");
    m_adamBootMode = m_settingsWindow->adamBootMode();

    saveSettings();
}

void MainWindow::loadSettings()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString iniPath = appDir.filePath("settings.ini");
    const bool firstRun = !QFileInfo::exists(iniPath);

    QSettings settings(iniPath, QSettings::IniFormat);

    auto makeAbsolutePath = [&](QString value, const QString& defaultAbsolutePath) -> QString {
        value = value.trimmed();

        // Eerste installatie of oude lege/"." waarde: echte default naast de applicatie.
        if (value.isEmpty() || value == ".")
            return QDir::cleanPath(defaultAbsolutePath);

        // Relatief blijft ondersteund, maar altijd t.o.v. applicationDirPath().
        if (QDir::isRelativePath(value))
            value = appDir.filePath(value);

        return QDir::cleanPath(value);
    };

    auto resolvePath = [&](const char *key, const QString& defaultAbsolutePath) -> QString {
        return makeAbsolutePath(settings.value(key).toString(), defaultAbsolutePath);
    };

    auto readEnum = [&](const char* key, int def) -> int {
        return settings.value(key, def).toInt();
    };

    auto checkActionByData = [&](QActionGroup* group, int value) {
        if (!group) return;
        const auto actions = group->actions();
        for (QAction* a : actions) {
            if (!a) continue;
            if (a->data().toInt() == value) {
                a->setChecked(true);
                break;
            }
        }
    };

    // --- Media paths ---
    // Geen settings.ini? Dan starten alle directories vanuit de app-folder:
    // <app>/media/...  en tools vanuit <app>/tools/...
    m_romPath         = resolvePath("romPath",         defaultRomPathForPlatform());
    m_atariRomPath    = resolvePath("aromPath",        defaultAtariRomPathForPlatform());
    m_diskPath        = resolvePath("diskPath",        defaultDiskPathForPlatform());
    m_tapePath        = resolvePath("tapePath",        defaultTapePathForPlatform());
    m_statePath       = resolvePath("statePath",       defaultStatePathForPlatform());
    m_breakpointPath  = resolvePath("breakpointPath",  defaultBreakpointPathForPlatform());
    m_screenshotsPath = resolvePath("screenshotPath",  defaultScreenshotPathForPlatform());
    m_symbolsPath     = resolvePath("symbolsPath",     defaultSymbolsPathForPlatform());

    // Bezel directories krijgen ook een media-default; BIOS-bestanden blijven intern/leeg.
    m_adamBezelPath   = resolvePath("adambezelpath",   defaultBezelPathForPlatform());
    m_cvBezelPath     = resolvePath("cvbezelpath",     defaultBezelPathForPlatform());

    // --- Video/Machine ---
    m_paletteIndex = settings.value("video/palette", 0).toInt();

    m_vdpType = settings.value("video/vdp", 0).toInt();
    if (m_vdpType < VDP_TMS || m_vdpType > VDP_PICO9918)
        m_vdpType = 0;

    m_f18a80SelfTest = settings.value("video/f18a80SelfTest", false).toBool();
    if (!vdpHasF18A(m_vdpType))
        m_f18a80SelfTest = false;

    coleco_set_vdp_type(m_vdpType);
    f18a_set_80col_selftest_enabled(m_f18a80SelfTest ? 1 : 0);

    qDebug() << "[VIDEO] Loaded VDP:"
             << vdpTypeName(m_vdpType)
             << "value =" << m_vdpType;

    m_machineType = settings.value("machine/type", 0).toInt();
    if (m_machineType < MACHINE_COLECO || m_machineType > MACHINE_ATARI2600)
        m_machineType = MACHINE_COLECO;
    m_atariColor = settings.value("Atari/Color", true).toBool();
    m_atariLeftDifficultyA = settings.value("Atari/LeftDifficultyA", false).toBool();
    m_atariRightDifficultyA = settings.value("Atari/RightDifficultyA", false).toBool();
    m_atariPhosphorEffect = settings.value("Atari/PhosphorEffect", true).toBool();
    //m_machineType = 1; // always start in adam

    m_realhardware     = settings.value("machine/realhardware", false).toBool();

    m_scalingMode     = settings.value("video/scalingMode", 1).toInt();

    m_scanlinesMode   = static_cast<ScanlinesMode>(readEnum("video/scanlinesMode", ScanlinesOff));
    m_colorFilterMode = static_cast<ColorFilterMode>(readEnum("video/colorFilterMode", ColorFilterOff));
    checkActionByData(m_scanlinesGroup, int(m_scanlinesMode));

    m_startFullScreen = settings.value("video/fullscreen", false).toBool();
    m_useBezels       = settings.value("video/useBezels", true).toBool();

    // --- Hardware ---
    m_sgmEnabled      = settings.value("hardware/sgm",  false).toBool();
    m_c80Enabled     = settings.value("hardware/c80", false).toBool();
    m_useDTsound      = settings.value("hardware/useDTsound", true).toBool();

    // --- Controller ---
    m_joystickType    = settings.value("controller/joystickType", 0).toInt();
    m_usePaddleMode   = settings.value("controller/usePaddleMode", false).toBool();
    m_useDrivingControllerMode = settings.value("controller/useDrivingControllerMode", false).toBool();
    m_paddleDirectionUpDown = settings.value("controller/paddleDirectionUpDown", false).toBool();

    // Real hardware
    m_ctrlJoys    = settings.value("rhard/rJoys",    false).toBool();
    m_ctrlAdamNet      = settings.value("rhard/rAdamnet",      false).toBool();
    m_fujiNetDirectRom = settings.value("rhard/fujiNetDirectRom", false).toBool();
    m_ctrlCartridge = settings.value("rhard/rCartridge", false).toBool();

    // --- BIOS ---
    m_colecoBiosPath  = settings.value("bios/coleco", "").toString();
    m_eosBiosPath     = settings.value("bios/eos",    "").toString();
    m_writerBiosPath  = settings.value("bios/writer", "").toString();
    m_adamStartupPath = settings.value("adam/startup", "").toString();
    // CVBasic Suite paths: MainWindow/settings.ini blijft de enige bron,
    // maar bij eerste installatie worden die keys meteen met correcte defaults gevuld.
    m_cvbasicSourcePath = resolvePath("cvbasic/lastOpenDir", defaultCvBasicSourcePathForPlatform());
    m_cvbasicBuildPath  = resolvePath("cvbasic/buildDir",    defaultCvBasicBuildPathForPlatform());

#if defined(Q_OS_WIN)
    m_cvbasicExePath = resolvePath("cvbasic/cvbasicExe", defaultCvBasicExePathForPlatform());
    m_gasm80ExePath  = resolvePath("cvbasic/gasm80Exe",  defaultGasm80ExePathForPlatform());
#else
    m_cvbasicExePath = resolvePath("cvbasic/cvbasicLinuxExe", defaultCvBasicExePathForPlatform());
    m_gasm80ExePath  = resolvePath("cvbasic/gasm80LinuxExe",  defaultGasm80ExePathForPlatform());
#endif

    m_spriteSourcePath = resolvePath("cvbasic/spriteSourceDir", defaultSpriteSourcePathForPlatform());
    m_spriteBuildPath  = resolvePath("cvbasic/spriteBuildDir",  defaultSpriteBuildPathForPlatform());
    m_soundSourcePath  = resolvePath("cvbasic/soundSourceDir",  defaultSoundSourcePathForPlatform());
    m_soundBuildPath   = resolvePath("cvbasic/soundBuildDir",   defaultSoundBuildPathForPlatform());
    m_adamBootMode = settings.value("adam/bootMode", AdamBootWriter).toInt();
    if (m_adamBootMode < AdamBootWriter || m_adamBootMode > AdamBootBasicImage)
        m_adamBootMode = AdamBootWriter;

    // --- Window ---
    qDebug() << "[UI] Loaded settings";

    const QByteArray geometry = settings.value("window/geometry").toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    else
        resize(770, 700);

    m_snapWindows = settings.value("window/snap", true).toBool();

    // na m_useDTsound laden:
    if (m_colecoController) {
        QMetaObject::invokeMethod(m_colecoController, "setDTsoundEnabled",
                                  Qt::QueuedConnection, Q_ARG(bool, m_useDTsound));
    }

    if (m_inputWidget) {
        m_inputWidget->setMachineType(m_machineType);
        m_inputWidget->setAdamGameMode(false);  // Standaard keyboard mode
        qDebug() << "[MAINWINDOW] InputWidget configured with loaded settings";
    }

    if (firstRun) {
        qDebug() << "[SETTINGS] First run: writing default media/tools paths to" << iniPath;

        const QStringList defaultDirs = {
            m_romPath, m_atariRomPath, m_diskPath, m_tapePath, m_statePath,
            m_breakpointPath, m_screenshotsPath, m_symbolsPath,
            m_adamBezelPath, m_cvBezelPath,
            m_cvbasicSourcePath, m_cvbasicBuildPath,
            m_spriteSourcePath, m_spriteBuildPath,
            m_soundSourcePath, m_soundBuildPath
        };

        for (const QString& dirPath : defaultDirs) {
            if (!dirPath.trimmed().isEmpty())
                QDir().mkpath(dirPath);
        }

        settings.setValue("romPath",        m_romPath);
        settings.setValue("aromPath",       m_atariRomPath);
        settings.setValue("diskPath",       m_diskPath);
        settings.setValue("tapePath",       m_tapePath);
        settings.setValue("statePath",      m_statePath);
        settings.setValue("breakpointPath", m_breakpointPath);
        settings.setValue("screenshotPath", m_screenshotsPath);
        settings.setValue("symbolsPath",    m_symbolsPath);
        settings.setValue("adambezelpath",  m_adamBezelPath);
        settings.setValue("cvbezelpath",    m_cvBezelPath);

        settings.setValue("cvbasic/lastOpenDir", m_cvbasicSourcePath);
        settings.setValue("cvbasic/buildDir",    m_cvbasicBuildPath);
#if defined(Q_OS_WIN)
        settings.setValue("cvbasic/cvbasicExe",  m_cvbasicExePath);
        settings.setValue("cvbasic/gasm80Exe",   m_gasm80ExePath);
#else
        settings.setValue("cvbasic/cvbasicLinuxExe", m_cvbasicExePath);
        settings.setValue("cvbasic/gasm80LinuxExe",  m_gasm80ExePath);
#endif
        settings.setValue("cvbasic/spriteSourceDir", m_spriteSourcePath);
        settings.setValue("cvbasic/spriteBuildDir",  m_spriteBuildPath);
        settings.setValue("cvbasic/soundSourceDir",  m_soundSourcePath);
        settings.setValue("cvbasic/soundBuildDir",   m_soundBuildPath);
        settings.setValue("adam/bootMode", m_adamBootMode);
        settings.sync();
    }

    m_settingsLoaded = true;
}

void MainWindow::saveSettings()
{
    if (!m_allowSaveSettings)
    {
        qDebug() << "[SETTINGS] saveSettings blocked during startup";
        return;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString iniPath = appDir.filePath("settings.ini");

    QSettings settings(iniPath, QSettings::IniFormat);

    // Kleine helper om setValue wat minder repetitief te maken
    auto put = [&](const char* key, const QVariant& v) {
        settings.setValue(key, v);
    };

    // Paths / media
    put("romPath",        m_romPath);
    put("aromPath",       m_atariRomPath);
    put("diskPath",       m_diskPath);
    put("tapePath",       m_tapePath);
    put("statePath",      m_statePath);
    put("breakpointPath", m_breakpointPath);
    put("screenshotPath", m_screenshotsPath);
    put("symbolsPath",    m_symbolsPath);

    // Machine / UI
    put("machine/type",   m_machineType);
    put("machine/realhardware", m_realhardware);
    put("Atari/Color", m_atariColor);
    put("Atari/LeftDifficultyA", m_atariLeftDifficultyA);
    put("Atari/RightDifficultyA", m_atariRightDifficultyA);
    put("Atari/PhosphorEffect", m_atariPhosphorEffect);
    put("video/palette",  m_paletteIndex);
    put("video/vdp",      m_vdpType);
    put("video/f18a80SelfTest", m_f18a80SelfTest);
    put("adambezelpath",  m_adamBezelPath);
    put("cvbezelpath",    m_cvBezelPath);

    // Hardware
    put("hardware/sgm",   m_sgmEnabled);
    put("hardware/c80",  m_c80Enabled);
    put("hardware/useDTsound", m_useDTsound);

    // Controller
    put("controller/joystickType",   m_joystickType);
    put("controller/usePaddleMode",  m_usePaddleMode);
    put("controller/useDrivingControllerMode", m_useDrivingControllerMode);
    put("controller/paddleDirectionUpDown", m_paddleDirectionUpDown);

    // Real Hardware
    put("rhard/rJoys",       m_ctrlJoys);
    put("rhard/rAdamnet",         m_ctrlAdamNet);
    put("rhard/fujiNetDirectRom", m_fujiNetDirectRom);
    put("rhard/rCartridge",    m_ctrlCartridge);

    // Video
    put("video/scalingMode",     m_scalingMode);
    put("video/scanlinesMode",   int(m_scanlinesMode));
    put("video/colorFilterMode", int(m_colorFilterMode));
    put("video/fullscreen",      m_startFullScreen);
    put("video/useBezels",       m_useBezels);

    // BIOS
    put("bios/coleco", m_colecoBiosPath);
    put("bios/eos",    m_eosBiosPath);
    put("bios/writer", m_writerBiosPath);
    put("adam/startup", m_adamStartupPath);
    put("cvbasic/lastOpenDir", m_cvbasicSourcePath);
    put("cvbasic/buildDir",    m_cvbasicBuildPath);
#if defined(Q_OS_WIN)
    put("cvbasic/cvbasicExe",  m_cvbasicExePath);
    put("cvbasic/gasm80Exe",   m_gasm80ExePath);
#else
    put("cvbasic/cvbasicLinuxExe", m_cvbasicExePath);
    put("cvbasic/gasm80LinuxExe",  m_gasm80ExePath);
#endif
    put("cvbasic/spriteSourceDir", m_spriteSourcePath);
    put("cvbasic/spriteBuildDir",  m_spriteBuildPath);
    put("cvbasic/soundSourceDir",  m_soundSourcePath);
    put("cvbasic/soundBuildDir",   m_soundBuildPath);
    put("adam/bootMode", m_adamBootMode);

    // Window (zet dit vóór sync)
    put("window/geometry", saveGeometry());
    put("window/snap",     m_snapWindows);

    qDebug() << "[VIDEO] saveSettings BEFORE sync:"
             << "m_vdpType =" << m_vdpType
             << vdpTypeName(m_vdpType)
             << "file =" << iniPath;

    settings.sync();

    qDebug() << "[VIDEO] saveSettings AFTER sync:"
             << "stored video/vdp =" << settings.value("video/vdp").toInt()
             << "file =" << iniPath;

    QMetaObject::invokeMethod(
        m_colecoController,
        "setDTsoundEnabled",
        Qt::QueuedConnection,
        Q_ARG(bool, m_useDTsound)
        );
}

//---------------------------------------------------------------------------------------------
// SAVE SCREENSHOT DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onSaveScreenshot()
{
    // Basisdir bepalen op basis van setting + appDir
    QDir appDir(QCoreApplication::applicationDirPath());
    QString basePath = m_screenshotsPath.trimmed();

    // Leeg of "." → media/screenshots naast de exe
    if (basePath.isEmpty() || basePath == ".") {
        basePath = appDir.filePath("media/screenshots");
    }
    // Relatief → maak absoluut t.o.v. appDir
    else if (QDir::isRelativePath(basePath)) {
        basePath = appDir.filePath(basePath);
    }

    basePath = QDir::cleanPath(basePath);

    // Directory aanmaken indien nodig
    QDir dir(basePath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // Basisnaam voor bestand (afgeleid van ROM)
    QString romBaseName = m_currentRomName;
    if (romBaseName.isEmpty() || romBaseName == "No cart") {
        romBaseName = "screenshot";
    } else {
        QFileInfo fi(romBaseName);
        romBaseName = fi.completeBaseName();
    }

    // Start-directory voor de save dialog
    QString initialPath = dir.absolutePath();

    QString fileName = CustomFileDialog::getSaveFileName(
        this,
        tr("Save Screenshot"),
        initialPath,
        tr("PNG Images (*.png);;All Files (*)"),
        nullptr,
        CustomFileDialog::PathScreenshot,
        QFileDialog::Options(),
        romBaseName
        );

    if (fileName.isEmpty()) {
        return;
    }

    QString finalPath = fileName;
    if (!finalPath.endsWith(".png", Qt::CaseInsensitive)) {
        finalPath += ".png";
    }

    QFileInfo fileInfo(finalPath);
    CustomFileDialog::s_lastSaveDir = fileInfo.absolutePath();

    if (m_screenWidget) {
        QImage screenshot = m_screenWidget->grab().toImage();

        if (screenshot.save(finalPath)) {
            qDebug() << "[UI] Screenshot saved to:" << finalPath;
        } else {
            QMessageBox::warning(
                this,
                tr("Error"),
                tr("Failed to save screenshot to %1.").arg(finalPath)
                );
        }
    }
}

//---------------------------------------------------------------------------------------------
// HARDWARE DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onOpenHardware()
{
    HardwareConfig cur;

    cur.machine = static_cast<MachineType>(m_machineType);
    cur.realhardware = m_realhardware;
    cur.palette = m_paletteIndex;
    cur.vdpType = m_vdpType;
    cur.f18a80SelfTest = m_f18a80SelfTest;

    cur.sgmEnabled  = m_sgmEnabled;
    cur.c80Enabled = m_c80Enabled;

    cur.Joys = m_ctrlJoys;
    cur.AdamNet    = m_ctrlAdamNet;
    cur.fujiNetDirectRom = m_fujiNetDirectRom;
    cur.Cartridge   = m_ctrlCartridge;

    const int prevPalette = m_paletteIndex;

    HardwareWindow dlg(cur, this);

    dlg.setLoadedMediaDisplayNames(
        static_cast<MachineType>(m_machineType),
        m_currentRomName,         // CC (Coleco Cartridge)
        m_currentARomName,        // CA (ADAM Cartridge)
        m_currentAtari2600RomName,// VCS (Atari 2600 Cartridge)
        m_loadedTapeNames[0],     // D1
        m_loadedTapeNames[1],     // D2
        m_loadedDiskNames[0],     // D5
        m_loadedDiskNames[1],     // D6
        m_loadedDiskNames[2]      // D7 (Alleen de eerste 3 disks worden in de tabel getoond)
        );

    if (dlg.exec() == QDialog::Accepted) {
        HardwareConfig chosen = dlg.config();

        // Bewaar oude machine voor vergelijking
        const int oldMachine = cur.machine;
        const int newMachine = chosen.machine;

        m_paletteIndex = chosen.palette;
        m_vdpType = chosen.vdpType;
        m_f18a80SelfTest = chosen.f18a80SelfTest;
        coleco_setpalette(m_paletteIndex);
        //m_sgmEnabled = chosen.sgmEnabled;
        m_realhardware = chosen.realhardware;

        if (oldMachine != newMachine) {
            if (newMachine == MACHINE_ADAM) {
                switchToAdamMode();
            } else if (newMachine == MACHINE_ATARI2600) {
                switchToAtari2600Mode();
            } else {
                switchToColecoMode();
            }
        }

        applyHardwareConfig(chosen);

        saveSettings();
    } else {
        m_paletteIndex = prevPalette;
        coleco_setpalette(m_paletteIndex);
    }
}


void MainWindow::updateHardwareWindowMediaDisplay()
{
    if (m_hardwareWindow) {
        m_hardwareWindow->setLoadedMediaDisplayNames(
            static_cast<MachineType>(m_machineType),
            m_currentRomName,          // CC (Coleco Cartridge)
            m_currentARomName,         // CA (ADAM Cartridge)
            m_currentAtari2600RomName, // VCS (Atari 2600 Cartridge)
            m_loadedTapeNames[0],      // D1
            m_loadedTapeNames[1],      // D2
            m_loadedDiskNames[0],      // D5
            m_loadedDiskNames[1],      // D6
            m_loadedDiskNames[2]       // D7 (Disks[2])
            );
    }
}

void MainWindow::onToggleSGM(bool checked)
{
    qDebug() << "[UI] Toggle SGM =" << checked;
    QMetaObject::invokeMethod(m_colecoController, "setSGMEnabled",
                              Qt::QueuedConnection,
                              Q_ARG(bool, checked));
    QMetaObject::invokeMethod(m_colecoController, "resetMachine",
                              Qt::QueuedConnection);
}

void MainWindow::onCartridgeStatusChanged(const QString& colecoName, const QString& adamName)
{
    m_currentRomName = colecoName;  // Coleco Cartridge (CC)
    m_currentARomName = adamName;   // ADAM Cartridge (CA)
    const QString display = (m_machineType == MACHINE_ATARI2600)
                                ? m_currentAtari2600RomName
                                : ((m_machineType == MACHINE_ADAM) ? m_currentARomName : m_currentRomName);
    updateRomLabelForStatusBar(statusBar(), m_sepLabel4, m_romLabel, display);

    updateHardwareWindowMediaDisplay();
}

void MainWindow::onAtari2600CartridgeStatusChanged(const QString& fileName)
{
    m_currentAtari2600RomName = fileName;
    updateAtariStatusBar();
}

void MainWindow::updateAtariStatusBar()
{
    if (m_machineType != MACHINE_ATARI2600 || !m_romLabel)
        return;

    const QString cartName = m_currentAtari2600RomName.isEmpty()
                                 ? QStringLiteral("No cart")
                                 : m_currentAtari2600RomName;
    const QString display = QStringLiteral("C/B=%1  DL=%2  DR=%3 | %4")
                                .arg(m_atariColor ? QStringLiteral("C") : QStringLiteral("B"))
                                .arg(m_atariLeftDifficultyA ? QStringLiteral("A") : QStringLiteral("B"))
                                .arg(m_atariRightDifficultyA ? QStringLiteral("A") : QStringLiteral("B"))
                                .arg(cartName);
    updateRomLabelForStatusBar(statusBar(), m_sepLabel4, m_romLabel, display);
}

//---------------------------------------------------------------------------------------------
// INFO DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::showAboutDialog()
{
    QDialog aboutDialog(this);
    aboutDialog.setWindowTitle("About ADAM+");
    aboutDialog.setFixedSize(620, 560);

    QVBoxLayout *layout = new QVBoxLayout(&aboutDialog);
    layout->setAlignment(Qt::AlignCenter);

    QLabel *logoLabel = new QLabel(&aboutDialog);
    QPixmap logo(":/images/images/ADAMP.png");
    logoLabel->setPixmap(logo.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(logoLabel);

    QLabel *textLabel = new QLabel(&aboutDialog);

    textLabel->setOpenExternalLinks(true);

    textLabel->setText(QString(
                           "Version %1<br>"  // %1 appVersion
                           "©2025-26 DannyVdH<br>"
                           "<a href='https://github.com/dvdh1961/ADAMP'>VDH Productions</a><br><br>"
                           "This software is free to use for personal, educational, and non-profit purposes<br>"
                           "Some software components are subject to licensing agreements held by the rightful owners<br>"
                           "The ADAM+ emulator is built using the latest available techniques and technologies<br>"
                           "obtainable in 2025. Leveraging deep expertise and the assistance of advanced<br>"
                           "Language Models (LLMs), we can achieve the full potential of our programming<br>"
                           "skills with exceptional speed and accuracy.<br><br>"
                           "The goal is to go even deeper into my ADAM+ hardware project<br>"
                           " — this ADAM+ emulator — <br>"
                           "will go much further in integrating specific hardware components.<br><br>"
                           "Credits goes to all the brilliant coders worldwide!<br>"
                           "A lot of interfacing and parts of code based on the EmulTwo project.<br>"
                           "Parts of ADAM emulation code from Marat Fayzullin’s ColEm project.<br>"
                           "Wavemotion-dave, for improving compatibility issues.<br>"
                           "Parts of EightyOne created by Michael D Wynne.<br>"
                           "Z80 core taken from Juergen Buchmueller.<br>"
                           "AY8910 code from Z81 ©1995–2001 Russell Marks.<br>"
                           "PICO9918 integration by visrealm.<br><br>"
                           "And all the ones that were involved and that I forgot to mention.<br><br>"
                           ).arg(appVersion));

    textLabel->setOpenExternalLinks(true);
    textLabel->setWordWrap(true);
    textLabel->setAlignment(Qt::AlignCenter);

    // 1. Laad het lettertype uit je resources of lokale map
    // Pas het pad aan naar waar jouw .ttf staat (bijv. ":/fonts/mijnfont.ttf")
    int fontId = QFontDatabase::addApplicationFont(":/fonts/fonts/luculent.ttf");

    QString family;

    if (fontId != -1) {
        family = QFontDatabase::applicationFontFamilies(fontId).at(0);
        //qDebug() << "[UI] Custom font geladen:" << family;
    } else {
        qDebug() << "[UI] Kon custom font niet laden, fallback naar Roboto";
        family = "Roboto";
    }

    QFont monoFont(family, 10);
    monoFont.setBold(false);

   textLabel->setFont(monoFont);
    layout->addWidget(textLabel);
    layout->addStretch(1);

    QIcon okIcon(":/images/images/OK.png");
    QPixmap okPixmap(":/images/images/OK.png");
    if (okIcon.isNull()) {
        qWarning() << "AboutDialog: Kon OK.png niet laden.";
    }

    QPushButton *okButton = new QPushButton(&aboutDialog);

    okButton->setIcon(okIcon);
    okButton->setIconSize(okPixmap.size());
    okButton->setFixedSize(okPixmap.size());
    okButton->setText("");
    okButton->setFlat(true);
    okButton->setCursor(Qt::PointingHandCursor);
    okButton->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:pressed { padding-top: 2px; padding-left: 2px; }"
        );

    connect(okButton, &QPushButton::clicked, &aboutDialog, &QDialog::accept);
    layout->addWidget(okButton, 0, Qt::AlignCenter);

    aboutDialog.exec();
}

//---------------------------------------------------------------------------------------------
// NAMETABLE DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onShowNameTable()
{
        m_ntableWindow->show();
}

//---------------------------------------------------------------------------------------------
// PATTERNTABLE DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onShowPatternTable()
{
        m_patternWindow->show();
}

//---------------------------------------------------------------------------------------------
// SPRITETABLE DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onShowSpriteTable()
{
        m_spriteWindow->show();
}

void MainWindow::onOpenCartInfo()
{
    if (!m_cartInfoDialog) {
        m_cartInfoDialog = new CartridgeInfoDialog(this);
    }

    m_cartInfoDialog->refreshData();

    m_cartInfoDialog->show();
    m_cartInfoDialog->raise();
    m_cartInfoDialog->activateWindow();
}

//---------------------------------------------------------------------------------------------
// VIDEO OPTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::onToggleVideoStandard()
{
    bool isNTSC = m_actToggleNTSC->isChecked();
    qDebug() << "[UI] Video standard set to" << (isNTSC ? "NTSC" : "PAL");

    QMetaObject::invokeMethod(m_colecoController, "setVideoStandard",
                              Qt::QueuedConnection,
                              Q_ARG(bool, isNTSC));
}

void MainWindow::onResetGeometry()
{
    QScreen *primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen)
        return;

    const QRect desktop = primaryScreen->availableGeometry();

    // Verplaats uitsluitend vensters die momenteel geopend/zichtbaar zijn.
    // Gesloten hulpmiddelen worden door RESET GEOMETRY niet geopend.
    const QWidgetList windows = QApplication::topLevelWidgets();
    for (QWidget *window : windows)
    {
        if (!window || !window->isWindow() || !window->isVisible())
            continue;

        const QRect frame = window->frameGeometry();
        const QPoint centeredPosition(
            desktop.center().x() - frame.width() / 2,
            desktop.center().y() - frame.height() / 2
            );

        window->move(centeredPosition);
    }
}

void MainWindow::onCycleScalingMode()
{
    m_scalingMode = (m_scalingMode + 1) % 3;

    QString scaleText;
    if (m_scalingMode == 0) {
        scaleText = "Scaling: Sharp";
    } else if (m_scalingMode == 1) {
        scaleText = "Scaling: Smooth";
    } else {
        scaleText = "Scaling: EPX";
    }

    m_actToggleSmoothing->setText(scaleText);

    if (m_screenWidget) {
        m_screenWidget->setScalingMode(static_cast<ScreenWidget::ScalingMode>(m_scalingMode));
    }

    saveSettings();
}

void MainWindow::onScanlinesModeChanged(QAction* action)
{
    ScanlinesMode newMode = static_cast<ScanlinesMode>(action->data().toInt());

    if (m_scanlinesMode == newMode) {
        return;
    }

    m_scanlinesMode = newMode;
    qDebug() << "[UI] Scanlines Mode changed to" << newMode;

    saveSettings();

    if (m_screenWidget) {
        m_screenWidget->setScanlinesMode(newMode);
    }
}

void MainWindow::onColorFilterModeChanged(QAction* action)
{
    ColorFilterMode newMode = static_cast<ColorFilterMode>(action->data().toInt());

    if (m_colorFilterMode == newMode) {
        return;
    }

    m_colorFilterMode = newMode;
    qDebug() << "[UI] Color Filter Mode changed to" << newMode;

    saveSettings();

    if (m_screenWidget) {
        m_screenWidget->setColorFilterMode(newMode);
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_logoLabel1 && event->type() == QEvent::MouseButtonPress) {
        onOpenHardware();
        return true;
    }

    if (obj == m_logView && event->type() == QEvent::Close) {
        if (m_actShowLog) {
            m_actShowLog->setChecked(false);
        }
        m_logView->hide();
        event->ignore();
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}

//---------------------------------------------------------------------------------------------
// VIDEO SET GAMESCREEN
//---------------------------------------------------------------------------------------------

void MainWindow::onFrameReceived(const QImage &frame)
{
    if (!m_screenWidget || frame.isNull()) return;

    m_screenWidget->setFrame(frame);
}

//---------------------------------------------------------------------------------------------
// EMULATOR MAIN THREAD
//---------------------------------------------------------------------------------------------

void MainWindow::setupEmulatorThread()
{
    qDebug() << "[UI] setupEmulatorThread()";

    m_emulatorThread = new QThread(this);
    m_colecoController = new ColecoController();

    // === SET 80-COL VOOR THREAD START ===
    coleco_80col_enabled = m_c80Enabled ? 1 : 0;
    qDebug() << "[UI] Setting coleco_80col_enabled =" << coleco_80col_enabled;

    connect(m_colecoController, &ColecoController::onBiosStatusUpdated,
            this, &MainWindow::onBiosStatusUpdated,
            Qt::QueuedConnection);

    m_colecoController->moveToThread(m_emulatorThread);

    connect(m_colecoController, &ColecoController::biosCFramesDone,
            this, &MainWindow::onBiosCFramesDone,
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::biosAFramesDone,
            this, &MainWindow::onBiosAFramesDone,
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::frameReady,
            this,               &MainWindow::onFrameReceived,
            Qt::QueuedConnection);


    connect(m_colecoController, &ColecoController::frameReady,
            this, &MainWindow::onFramePresented,
            Qt::QueuedConnection);

    connect(m_colecoController, SIGNAL(videoStandardChanged(QString)),
            this, SLOT(setVideoStandard(QString)),
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::fpsUpdated,
            this, &MainWindow::onFpsUpdated,
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::sgmStatusChanged,
            this, &MainWindow::onSgmStatusChanged,
            Qt::QueuedConnection);

    connect(m_colecoController, &ColecoController::emuPausedChanged,
            this, &MainWindow::onEmuPausedChanged,
            Qt::QueuedConnection);

    auto isMediaImage = [](const QString& path) -> bool {
        const QString ext = QFileInfo(path).suffix().toLower();
        return ext == "dsk" || ext == "img" || ext == "ddp";
    };

    if (isMediaImage(m_writerBiosPath)) {
        qWarning() << "[UI][BIOSPATH] Invalid writer BIOS path (media image detected):" << m_writerBiosPath;
        m_writerBiosPath.clear();
    }

    connect(m_emulatorThread, &QThread::started, m_colecoController, [this]() {
        m_colecoController->setAtari2600Mode(m_machineType == MACHINE_ATARI2600);
        m_colecoController->startWithBios(m_colecoBiosPath, m_eosBiosPath, m_writerBiosPath);
    });

    connect(m_colecoController, &ColecoController::frameReady,
            m_screenWidget, &ScreenWidget::updateFrame,
            Qt::QueuedConnection);
    connect(m_colecoController, &ColecoController::emulationStopped,
            this, &MainWindow::onThreadFinished,
            Qt::QueuedConnection);
    connect(m_emulatorThread, &QThread::finished,
            m_colecoController, &QObject::deleteLater);


    coleco_set_machine_type(m_machineType == MACHINE_ADAM ? MACHINE_ADAM : MACHINE_COLECO);

    m_emulatorThread->start();

    QMetaObject::invokeMethod(m_colecoController, "setSGMEnabled",
                              Qt::QueuedConnection,
                              Q_ARG(bool, m_sgmEnabled));

    connect(
        m_colecoController, &ColecoController::frameReady,
        this,
        [this](const QImage &) {
            QMetaObject::invokeMethod(
                m_colecoController,
                [this]() { coleco_setpalette(m_paletteIndex); },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection
        );

    connect(m_colecoController, &ColecoController::tapeStatusChanged,
            this, &MainWindow::onTapeStatusChanged,
            Qt::QueuedConnection);
    connect(m_colecoController, &ColecoController::diskStatusChanged,
            this, &MainWindow::onDiskStatusChanged,
            Qt::QueuedConnection);
    connect(m_colecoController, &ColecoController::cartridgeStatusChanged, // NIEUW
            this, &MainWindow::onCartridgeStatusChanged,
            Qt::QueuedConnection);
    connect(m_colecoController, &ColecoController::atari2600CartridgeStatusChanged,
            this, &MainWindow::onAtari2600CartridgeStatusChanged,
            Qt::QueuedConnection);

    qDebug() << "[UI] --- EMULATOR RUNNING ---";

}

void MainWindow::onThreadFinished()
{
    qDebug() << "[UI] MainWindow: 'emulationStopped' received.";
    m_emulatorThread->quit();
}

void MainWindow::onFramePresented()
{
    if (m_debugWin && m_debugWin->isVisible()) {
        m_debugWin->updateAllViews();
    }

    if (m_ntableWindow && m_ntableWindow->isVisible()) {
        m_ntableWindow->doRefresh();
    }
}

void MainWindow::setVideoStandard(const QString& standard)
{
    const QString upper = standard.trimmed().toUpper();
    if (upper == "NTSC" || upper == "PAL") {
        m_currentStd = upper;
        m_stdLabel->setText(QString("%1").arg(m_currentStd));
        // When the Atari core detects the cartridge region from its raster,
        // keep the VIDEO menu in sync with the actual standard in use.
        if(m_machineType==MACHINE_ATARI2600){
            m_actToggleNTSC->setChecked(upper=="NTSC");
            m_actTogglePAL->setChecked(upper=="PAL");
        }
    } else {
        m_currentStd = "NTSC";
        m_stdLabel->setText("NTSC");
    }
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);

    if (isMaximized()) {
        return;
    }

    if (m_snapWindows) {
        positionDebugger();
        positionPrinter();
    }
}


//---------------------------------------------------------------------------------------------
// GUI CLOSING EMULATOR FUNCTION
//---------------------------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *event)
{
    qDebug() << "[UI] CLOSE Application.";

    if (m_debugWin && m_debugWin->isVisible())
    {
        m_debugWin->close();
        m_debugWin = nullptr;
    }
    if (m_logView && m_logView->isVisible())
    {
        m_logView->close();
        m_logView = nullptr;
    }

    saveSettings();

    QProgressDialog dlg(
        "Closing emulator…\nPlease wait...",
        QString(),
        0, 0,
        this
        );
    dlg.setWindowTitle("Closing...");
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setCancelButton(nullptr);
    dlg.setMinimumDuration(0);
    dlg.show();

    // Laat Qt de dialog tekenen
    qApp->processEvents(QEventLoop::AllEvents, 50);

    // Als de emulator-thread draait: vraag hem om te stoppen
    if (m_emulatorThread && m_emulatorThread->isRunning()) {

        if (m_colecoController) {
            m_colecoController->stopEmulation();   // zet m_running = false;
        }

        // Geef de thread een beetje tijd om netjes uit de lus te komen
        QElapsedTimer timer;
        timer.start();
        while (m_emulatorThread->isRunning() && timer.elapsed() < 5000) {
            qApp->processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(10);
        }

        if (m_emulatorThread->isRunning()) {
            qWarning() << "[UI] Thread didn't stop in time, forcing terminate.";
            m_emulatorThread->terminate();
            m_emulatorThread->wait();
        }
    }

    const int minVisibleMs = 1000;
    QElapsedTimer visibleTimer;
    visibleTimer.start();
    while (visibleTimer.elapsed() < minVisibleMs) {
        qApp->processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }

    event->accept();
    fflush(nullptr);

    qDebug() << "[UI] --- CLOSE ---";
}

//---------------------------------------------------------------------------------------------
// GUI SELECTOR & INDICATOR EMULATOR STOP/RUN
//---------------------------------------------------------------------------------------------

void MainWindow::onRunStop()
{
    m_isPaused = !m_isPaused;
    if (m_isPaused) {
        QMetaObject::invokeMethod(m_colecoController, "pauseEmulation",
                                  Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(m_colecoController, "resumeEmulation",
                                  Qt::QueuedConnection);
    }
}

void MainWindow::onEmuPausedChanged(bool paused)
{
    m_isPaused = paused;
    if (m_startAction) {
        if (paused) {
            m_startAction->setText(tr("Run emulation"));
            m_runLabel->setText("STOP");
        } else {
            m_runLabel->setText("RUN");
            m_startAction->setText(tr("Stop emulation"));
        }
    }

    const bool allowState = paused;
    if (m_actSaveState) m_actSaveState->setEnabled(allowState);
    if (m_actLoadState) m_actLoadState->setEnabled(true);

    if (m_debugTerminal)
        m_debugTerminal->setEmulatorPaused(paused);
}

void MainWindow::onStartActionTriggered()
{
    if (!m_colecoController)
        return;

    if (m_isPaused) {
        m_runLabel->setText("STOP");
        QMetaObject::invokeMethod(
            m_colecoController,
            "resumeEmulation",
            Qt::QueuedConnection
            );
    } else {
        m_runLabel->setText("RUN");
        QMetaObject::invokeMethod(
            m_colecoController,
            "pauseEmulation",
            Qt::QueuedConnection
            );
    }
}

//---------------------------------------------------------------------------------------------
// GUI SELECTOR LOADING SYSTEM ROMS,TAPES & DISKS
//---------------------------------------------------------------------------------------------

void MainWindow::onBiosCFramesDone()
{
    if (m_machineType != 0)
        return;

    m_isColecoRomLoaded = true;

    /*
     * Alleen knipperen wanneer er echt een nieuwe pending cartridge wacht.
     * Na Reset Cartridge is m_pendingColecoBoot false en mag de knop NIET
     * opnieuw beginnen pinken.
     */
    if (m_pendingColecoBoot && !m_pendingColecoRomPath.isEmpty())
    {
        if (m_resetCartBlinkTimer && !m_resetCartBlinkTimer->isActive()) {
            onToggleResetCartBlink();
            m_resetCartBlinkTimer->start(300);
        }
    }
    else
    {
        stopResetCartBlinkAndSetFinalIcon();
    }

    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();
}

void MainWindow::onOpenAtari2600Rom()
{
    if (m_machineType != MACHINE_ATARI2600 || !m_colecoController)
        return;

    if (!m_atariRomPath.trimmed().isEmpty())
        QDir().mkpath(m_atariRomPath);

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Open Atari 2600 Cartridge"),
        QDir::cleanPath(m_atariRomPath),
        tr("Atari 2600 ROM files (*.a26 *.bin *.rom);;All files (*.*)"),
        nullptr,
        CustomFileDialog::PathAtariRom,
        QFileDialog::Options()
        );

    if (filePath.isEmpty())
        return;

    QFileInfo fi(filePath);
    CustomFileDialog::s_lastOpenDir = fi.absolutePath();
    m_currentAtari2600RomName = fi.fileName();

    // Een cartridge wordt elektrisch met losgelaten joystickknoppen en
    // momentary console-switches geplaatst. Wis ook de Qt-side status;
    // anders kan een oude fire/start-toestand bij de eerstvolgende richting
    // opnieuw naar de pas geresette Atari-core worden gestuurd.
    if(m_inputWidget)
        m_inputWidget->releaseAtari2600Controls();

    // Stella's database identifies this class of dump as PAL.  For ROM sets
    // that carry the standard region marker in their filename, select the
    // matching Atari video standard before the cartridge starts.
    const QString regionName = fi.completeBaseName().toUpper();
    if (regionName.contains("(PAL)")) {
        m_actTogglePAL->setChecked(true);
        setVideoStandard("PAL");
        QMetaObject::invokeMethod(m_colecoController, "setVideoStandard",
                                  Qt::QueuedConnection, Q_ARG(bool, false));
    } else if (regionName.contains("(NTSC)")) {
        m_actToggleNTSC->setChecked(true);
        setVideoStandard("NTSC");
        QMetaObject::invokeMethod(m_colecoController, "setVideoStandard",
                                  Qt::QueuedConnection, Q_ARG(bool, true));
    }

    QMetaObject::invokeMethod(
        m_colecoController,
        "loadAtari2600Rom",
        Qt::QueuedConnection,
        Q_ARG(QString, filePath)
        );

    updateAtariStatusBar();
}

void MainWindow::onEjectAtari2600Rom()
{
    if (!m_colecoController)
        return;
    QMetaObject::invokeMethod(m_colecoController, "ejectAtari2600Rom",
                              Qt::QueuedConnection);
    m_currentAtari2600RomName.clear();
    updateAtariStatusBar();
}

void MainWindow::onOpenColecoRom()
{
    if (m_machineType != 0)
        return;

    QString absoluteRomPath = QDir::cleanPath(m_romPath);

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Open COLECO Cartridge ROM"),
        absoluteRomPath,
        tr("COLECO ROM files (*.rom *.bin *.col);;All files (*.*)"),
        nullptr,
        CustomFileDialog::PathRom,
        QFileDialog::Options()
        );

    if (filePath.isEmpty())
        return;

    QFileInfo fi(filePath);
    CustomFileDialog::s_lastOpenDir = fi.absolutePath();

    // Alleen pending zetten. GEEN ROM laden.
    m_pendingColecoRomPath = filePath;
    m_pendingColecoBoot = true;

    m_currentRomName = fi.fileName();
    m_currentARomName.clear();

    m_isColecoRomLoaded = true;
    m_isAdamRomLoaded = false;
    m_ColecoMedia_insert = true;
    m_resetColecoLocked = false;

    updateRomLabelForStatusBar(
        statusBar(),
        m_sepLabel4,
        m_romLabel,
        "Pending cart: " + m_currentRomName
        );

    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();

    if (m_resetCartBlinkTimer && !m_resetCartBlinkTimer->isActive()) {
        onToggleResetCartBlink();
        m_resetCartBlinkTimer->start(300);
    }

    qDebug() << "[UI] Coleco ROM armed only, waiting for Reset Cartridge:"
             << m_pendingColecoRomPath;
}

void MainWindow::onEjectColecoRom()
{
    if (!m_isColecoRomLoaded) return;

    QMetaObject::invokeMethod(m_colecoController, "ejectColecoCartridge",
                              Qt::QueuedConnection);

    m_isColecoRomLoaded = false;
    m_currentRomName.clear();
    m_pendingColecoBoot = false;
    m_pendingColecoRomPath.clear();
    updateMediaMenuState();
    updateMediaStatusLabels();
}

void MainWindow::onBiosAFramesDone()
{
    if (m_machineType != 1) return;
    m_isAdamRomLoaded = true;
    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();
}

void MainWindow::onOpenAdamRom()
{
    if (m_machineType != 1) return;

    QString absoluteRomPath = QDir::cleanPath(m_romPath);

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Open ADAM Cartridge ROM"),
        absoluteRomPath,
        tr("ADAM ROM files (*.rom *.bin);;All files (*.*)"),
        nullptr,
        CustomFileDialog::PathRom,
        QFileDialog::Options()
        );

    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);
    CustomFileDialog::s_lastOpenDir = fileInfo.absolutePath();

    m_currentARomName = fileInfo.fileName();
    m_isAdamRomLoaded = true;

    m_currentRomName.clear();
    m_isColecoRomLoaded = false;

    QMetaObject::invokeMethod(m_colecoController, "AdamCartridge",
                              Qt::QueuedConnection,
                              Q_ARG(QString, filePath));

    QMetaObject::invokeMethod(m_colecoController,
                              "prepareForNewARomAndPauseOnBios",
                              Qt::QueuedConnection);

}

void MainWindow::onEjectAdamRom()
{
    if (!m_isAdamRomLoaded) return;

    QMetaObject::invokeMethod(m_colecoController, "ejectAdamCartridge",
                              Qt::QueuedConnection);

    m_isAdamRomLoaded = false;
    m_currentARomName.clear();
    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();
}

void MainWindow::onLoadDisk(int drive)
{
    if (m_machineType != 1) return;

    QString absoluteDiskPath = QDir::cleanPath(m_diskPath);

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Open ADAM Disk Image"),
        absoluteDiskPath,
        tr("ADAM Disk (*.dsk *.img);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathDisk,
        QFileDialog::Options()
        );

    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);
    //QDir appDir(QCoreApplication::applicationDirPath());
    CustomFileDialog::s_lastOpenDir = fileInfo.absolutePath();

    m_hardwareDiskMedia[drive] = false;
    mcu2_block_set_device_enabled(byte(0x04 + drive), false);
    m_loadedDiskNames[drive] = fileInfo.fileName();

    switch (drive) {
    case 0: m_isDiskLoadedA = true; break;
    case 1: m_isDiskLoadedB = true; break;
    case 2: m_isDiskLoadedC = true; break;
    case 3: m_isDiskLoadedD = true; break;
    }

    updateMediaMenuState();
    updateMediaStatusLabels();
    forceStatusBarMediaFlags();
    updateHardwareWindowMediaDisplay();

    QMetaObject::invokeMethod(m_colecoController, "loadDisk",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive),
                              Q_ARG(QString, filePath));

    // Start de ADAM Reset knop te knipperen (NIEUW)
    if (!m_AdamDMedia_insert && !m_AdamTMedia_insert) {
    if (m_resetAdamBlinkTimer && !m_resetAdamBlinkTimer->isActive()) {
        onToggleResetAdamBlink(); // Zet direct de eerste blink-status
        m_resetAdamBlinkTimer->start(300); // Start knipperen (300ms interval)
    }
    }
    m_AdamDMedia_insert = true;
}

void MainWindow::onLoadTape(int drive)
{
    if (m_machineType != 1) return;

    QString absoluteTapePath = QDir::cleanPath(m_tapePath);

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Open ADAM Tape Image"),
        absoluteTapePath,
        tr("ADAM Tape (*.ddp);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathTape,
        QFileDialog::Options()
        );

    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);
    //QDir appDir(QCoreApplication::applicationDirPath());
    CustomFileDialog::s_lastOpenDir = fileInfo.absolutePath();

    m_hardwareTapeMedia[drive] = false;
    const byte tapeDeviceId = drive == 0 ? 0x08 : drive == 1 ? 0x18
                              : drive == 2 ? 0x09 : 0x19;
    mcu2_block_set_device_enabled(tapeDeviceId, false);
    m_loadedTapeNames[drive] = fileInfo.fileName();

    switch (drive) {
    case 0: m_isTapeLoadedA = true; break;
    case 1: m_isTapeLoadedB = true; break;
    case 2: m_isTapeLoadedC = true; break;
    case 3: m_isTapeLoadedD = true; break;
    }

    updateMediaMenuState();
    updateMediaStatusLabels();
    forceStatusBarMediaFlags();
    updateHardwareWindowMediaDisplay();

    QMetaObject::invokeMethod(m_colecoController, "loadTape",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive),
                              Q_ARG(QString, filePath));

    // Start de ADAM Reset knop te knipperen (NIEUW)
    if (!m_AdamDMedia_insert && !m_AdamTMedia_insert) {
    if (m_resetAdamBlinkTimer && !m_resetAdamBlinkTimer->isActive()) {
        onToggleResetAdamBlink(); // Zet direct de eerste blink-status
        m_resetAdamBlinkTimer->start(300); // Start knipperen (300ms interval)
     }
    }
    m_AdamTMedia_insert = true;
}

void MainWindow::onPrepareHardwareMedia()
{
    if (!m_ctrlAdamNet) {
        QMessageBox::information(
            this, tr("Manual MCU2 hardware media"),
            tr("Select 'Disk 1 + FujiNet: MCU2 hardware' in Hardware first."));
        return;
    }

    const QStringList targets = {
        tr("Disk D5 (address 4)"), tr("Disk D6 (address 5)"),
        tr("Disk D7 (address 6)"), tr("Disk D8 (address 7)"),
        tr("Tape D1 (address 8, unit 0)"), tr("Tape D2 (address 8, unit 1)"),
        tr("Tape D3 (address 9, unit 0)"), tr("Tape D4 (address 9, unit 1)"),
        tr("Coleco cartridge ROM (manual file fallback)"),
        tr("ADAM cartridge ROM (manual file fallback)")
    };
    bool accepted = false;
    const QString target = QInputDialog::getItem(
        this, tr("Manual MCU2 hardware media"),
        tr("Manual fallback: which physical medium must be prepared?"),
        targets, 0, false, &accepted);
    if (!accepted)
        return;
    const int targetIndex = targets.indexOf(target);

    /* A physical FujiNet ROM is not exposed as a 1 KiB block drive.  Until
     * the gateway has a ROM download command, use the same proven cartridge
     * loader with a local copy and keep the reset/keypad path identical. */
    if (targetIndex == 8 || targetIndex == 9) {
        const bool colecoRom = targetIndex == 8;
        const QString filePath = CustomFileDialog::getOpenFileName(
            this,
            colecoRom ? tr("Select Coleco ROM") : tr("Select ADAM ROM"),
            QDir::cleanPath(m_romPath),
            tr("ROM files (*.rom *.bin *.col);;All files (*.*)"),
            nullptr, CustomFileDialog::PathRom, QFileDialog::Options());
        if (filePath.isEmpty())
            return;
        if (colecoRom) {
            loadColecoRomFromPath(filePath, false);
        } else {
            if (m_machineType != MACHINE_ADAM) {
                QMessageBox::information(this, tr("ADAM ROM"),
                                         tr("Select ADAM mode before preparing an ADAM cartridge."));
                return;
            }
            QFileInfo info(filePath);
            m_currentARomName = info.fileName();
            m_isAdamRomLoaded = true;
            QMetaObject::invokeMethod(m_colecoController, "AdamCartridge",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, filePath));
            if (m_resetAdamBlinkTimer && !m_resetAdamBlinkTimer->isActive())
                m_resetAdamBlinkTimer->start(300);
        }
        return;
    }

    if (m_machineType != MACHINE_ADAM) {
        QMessageBox::information(
            this, tr("Hardware ADAM media"),
            tr("Select ADAM mode first. Preparing media must not perform an early reset."));
        return;
    }

    const bool tape = targetIndex >= 4;
    const int logicalDrive = tape ? targetIndex - 4 : targetIndex;
    const int address = tape ? (logicalDrive < 2 ? 8 : 9) : 4 + logicalDrive;
    const int deviceNumber = tape ? (logicalDrive & 1) : 0;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QByteArray inspected;
    QString inspectError;
    for (quint32 block = 0; block < 3; ++block) {
        const Mcu2Gateway::DcbResult result =
            Mcu2Gateway::inspectBlock(address, deviceNumber, block);
        if (!result.transportOk || result.masterResult != 0
            || result.data.size() != 1024) {
            inspectError = result.error.isEmpty()
                ? tr("The physical drive did not return a complete block.")
                : result.error;
            break;
        }
        inspected.append(result.data);
    }
    QApplication::restoreOverrideCursor();

    const HardwareMediaProfile detected = classifyHardwareHeader(inspected);
    QStringList profiles = { tr("EOS"), tr("CP/M"), tr("T-DOS") };
    int suggested = detected == HardwareCpm ? 1 : detected == HardwareTdos ? 2 : 0;
    const QString prompt = inspected.isEmpty()
        ? tr("Automatic inspection was unavailable:\n%1\n\nSelect the boot type manually.")
              .arg(inspectError)
        : detected == HardwareUnknown
            ? tr("The header was not recognised with certainty. Select the boot type.")
            : tr("Detected from the first blocks: %1\nConfirm or correct it.")
                  .arg(hardwareProfileName(detected));
    const QString selected = QInputDialog::getItem(
        this, tr("Manual hardware boot type"), prompt,
        profiles, suggested, false, &accepted);
    if (!accepted)
        return;
    const int profile = profiles.indexOf(selected);

    const QString defaultName = QStringLiteral("[HW] %1 (%2)")
        .arg(tape ? QStringLiteral("D%1").arg(logicalDrive + 1)
                  : QStringLiteral("D%1").arg(logicalDrive + 5),
             selected);
    const QString displayName = QInputDialog::getText(
        this, tr("Hardware media name"),
        tr("Name shown in the status bar:"),
        QLineEdit::Normal, defaultName, &accepted).trimmed();
    if (!accepted)
        return;

    /* Set the OS router before Reset ADAM.  The software load routines are
     * deliberately not called: the bytes remain owned by physical FujiNet. */
    QMetaObject::invokeMethod(
        m_colecoController, "prepareHardwareBootProfile",
        Qt::QueuedConnection, Q_ARG(int, profile));
    m_cpm_status = profile != HardwareEos;

    if (tape) {
        const byte deviceId = logicalDrive == 0 ? 0x08 : logicalDrive == 1 ? 0x18
                            : logicalDrive == 2 ? 0x09 : 0x19;
        mcu2_block_set_device_enabled(deviceId, true);
        m_hardwareTapeMedia[logicalDrive] = true;
        m_loadedTapeNames[logicalDrive] = displayName.isEmpty() ? defaultName : displayName;
        m_AdamTMedia_insert = true;
        onTapeStatusChanged(logicalDrive, m_loadedTapeNames[logicalDrive]);
    } else {
        mcu2_block_set_device_enabled(byte(0x04 + logicalDrive), true);
        m_hardwareDiskMedia[logicalDrive] = true;
        m_loadedDiskNames[logicalDrive] = displayName.isEmpty() ? defaultName : displayName;
        m_AdamDMedia_insert = true;
        onDiskStatusChanged(logicalDrive, m_loadedDiskNames[logicalDrive]);
    }

    if (m_inputWidget)
        m_inputWidget->setAdamGameMode(false);
    if (m_actAdamGameOff)
        m_actAdamGameOff->setChecked(true);
    adamnet_set_game_mode(false);

    m_resetAdamLocked = true;
    if (m_resetAdamBlinkTimer && !m_resetAdamBlinkTimer->isActive()) {
        onToggleResetAdamBlink();
        m_resetAdamBlinkTimer->start(300);
    }
    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();

    QMessageBox::information(
        this, tr("Hardware media prepared"),
        tr("%1 is prepared as %2. Press Reset ADAM to boot it.")
            .arg(displayName.isEmpty() ? defaultName : displayName, selected));
}

void MainWindow::onEjectDisk(int drive)
{
    qDebug() << "[UI] Eject/Save Disk " << drive + 5;
    /* Always clear the software core as well. This is harmless for a purely
     * physical drive and prevents a stale software image from surviving when
     * the hardware flag changed since it was mounted. */
    QMetaObject::invokeMethod(m_colecoController, "ejectDisk",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive));
    m_hardwareDiskMedia[drive] = false;
    mcu2_block_set_device_enabled(byte(0x04 + drive), false);

    m_loadedDiskNames[drive].clear();

    switch (drive) {
    case 0: m_isDiskLoadedA = false; break;
    case 1: m_isDiskLoadedB = false; break;
    case 2: m_isDiskLoadedC = false; break;
    case 3: m_isDiskLoadedD = false; break;
    }

    QMenu* diskMenus[] = { m_diskMenuA, m_diskMenuB, m_diskMenuC, m_diskMenuD };
    if (diskMenus[drive])
        diskMenus[drive]->setIcon(QIcon());

    m_AdamDMedia_insert =
        !m_loadedDiskNames[0].isEmpty() ||
        !m_loadedDiskNames[1].isEmpty() ||
        !m_loadedDiskNames[2].isEmpty() ||
        !m_loadedDiskNames[3].isEmpty();

    updateMediaMenuState();
    updateMediaStatusLabels();
    forceStatusBarMediaFlags();
    updateHardwareWindowMediaDisplay();
}

void MainWindow::onEjectTape(int drive)
{
    qDebug() << "[UI] Eject/Save Tape " << drive;
    QMetaObject::invokeMethod(m_colecoController, "ejectTape",
                              Qt::QueuedConnection,
                              Q_ARG(int, drive));
    m_hardwareTapeMedia[drive] = false;
    const byte tapeDeviceId = drive == 0 ? 0x08 : drive == 1 ? 0x18
                              : drive == 2 ? 0x09 : 0x19;
    mcu2_block_set_device_enabled(tapeDeviceId, false);

    m_loadedTapeNames[drive].clear();

    switch (drive) {
    case 0: m_isTapeLoadedA = false; break;
    case 1: m_isTapeLoadedB = false; break;
    case 2: m_isTapeLoadedC = false; break;
    case 3: m_isTapeLoadedD = false; break;
    }

    QMenu* tapeMenus[] = { m_tapeMenuA, m_tapeMenuB, m_tapeMenuC, m_tapeMenuD };
    if (tapeMenus[drive])
        tapeMenus[drive]->setIcon(QIcon());

    m_AdamTMedia_insert =
        !m_loadedTapeNames[0].isEmpty() ||
        !m_loadedTapeNames[1].isEmpty() ||
        !m_loadedTapeNames[2].isEmpty() ||
        !m_loadedTapeNames[3].isEmpty();

    updateMediaMenuState();
    updateMediaStatusLabels();
    forceStatusBarMediaFlags();
    updateHardwareWindowMediaDisplay();
}

void MainWindow::onDiskStatusChanged(int drive, const QString& fileName)
{
    if (drive < 0 || drive >= 4)
        return;

    const QIcon checkIcon = style()->standardIcon(QStyle::SP_DialogApplyButton);

    // Bewaar naam
    m_loadedDiskNames[drive] = fileName;

    // Mapping per drive
    struct DriveUI {
        bool* isLoaded;
        QMenu* menu;
        QLabel* label;
        const char* prefix;   // "D5:", "D6:", ...
    };

    DriveUI ui[4] = {
        { &m_isDiskLoadedA, m_diskMenuA, m_diskLabelA, "D5:" },
        { &m_isDiskLoadedB, m_diskMenuB, m_diskLabelB, "D6:" },
        { &m_isDiskLoadedC, m_diskMenuC, m_diskLabelC, "D7:" },
        { &m_isDiskLoadedD, m_diskMenuD, m_diskLabelD, "D8:" }
    };

    DriveUI& d = ui[drive];

    // Loaded flag + icon
    *d.isLoaded = !fileName.isEmpty();
    if (d.menu)
        d.menu->setIcon(*d.isLoaded ? checkIcon : QIcon());

    // Label met elide
    if (d.label) {
        const QString text = QString("%1 %2").arg(d.prefix, fileName.isEmpty() ? "-" : fileName);
        const QFontMetrics fm(d.label->font());
        d.label->setText(fm.elidedText(text, Qt::ElideRight, d.label->width()));
    }

    updateMediaStatusLabels();
    updateMediaMenuState();
    forceStatusBarMediaFlags();
}

void MainWindow::onTapeStatusChanged(int drive, const QString& fileName)
{
    if (drive < 0 || drive >= 4)
        return;

    const QIcon checkIcon = style()->standardIcon(QStyle::SP_DialogApplyButton);

    // Bewaar naam
    m_loadedTapeNames[drive] = fileName;

    struct DriveUI {
        bool* isLoaded;
        QMenu* menu;
        QLabel* label;
        const char* prefix; // "D1:", "D2:", ...
    };

    DriveUI ui[4] = {
        { &m_isTapeLoadedA, m_tapeMenuA, m_tapeLabelA, "D1:" },
        { &m_isTapeLoadedB, m_tapeMenuB, m_tapeLabelB, "D2:" },
        { &m_isTapeLoadedC, m_tapeMenuC, m_tapeLabelC, "D3:" },
        { &m_isTapeLoadedD, m_tapeMenuD, m_tapeLabelD, "D4:" }
    };

    DriveUI& d = ui[drive];

    // Loaded flag + icon
    *d.isLoaded = !fileName.isEmpty();
    if (d.menu)
        d.menu->setIcon(*d.isLoaded ? checkIcon : QIcon());

    // Label met elide
    if (d.label) {
        const QString text = QString("%1 %2").arg(d.prefix, fileName.isEmpty() ? "-" : fileName);
        const QFontMetrics fm(d.label->font());
        d.label->setText(fm.elidedText(text, Qt::ElideRight, d.label->width()));
    }

    // 1× is genoeg
    updateMediaMenuState();
    updateMediaStatusLabels();
    forceStatusBarMediaFlags();
}

void MainWindow::updateMediaStatusLabels()
{
    const bool isAdam = (m_machineType == MACHINE_ADAM);
    const bool isAtari2600 = (m_machineType == MACHINE_ATARI2600);

    const bool showDisk = isAdam;
    const bool showTape = isAdam;

    QLabel* tapeWidgets[] = {
        m_tapeLabelA, m_sepLabelMedia2a,
        m_tapeLabelB, m_sepLabelMedia2b,
        m_tapeLabelC, m_sepLabelMedia2c,
        m_tapeLabelD, m_sepLabelMedia2d
    };

    QLabel* diskWidgets[] = {
        m_diskLabelA, m_sepLabelMedia1a,
        m_diskLabelB, m_sepLabelMedia1b,
        m_diskLabelC, m_sepLabelMedia1c,
        m_diskLabelD, m_sepLabelMedia1d
    };

    for (QLabel* w : tapeWidgets)
        if (w) w->setVisible(showTape);

    for (QLabel* w : diskWidgets)
        if (w) w->setVisible(showDisk);

    if (isAtari2600) {
        updateAtariStatusBar();
    } else {
        const QString display = isAdam ? m_currentARomName : m_currentRomName;
        updateRomLabelForStatusBar(statusBar(), m_sepLabel4, m_romLabel, display);
    }
}

void MainWindow::updateMediaMenuState()
{
    const bool isAdam = (m_machineType == MACHINE_ADAM);
    const bool isAtari2600 = (m_machineType == MACHINE_ATARI2600);

    auto setEnabledIf = [](QWidget* w, bool en) {
        if (w) w->setEnabled(en);
    };
    auto setActionEnabledIf = [](QAction* a, bool en) {
        if (a) a->setEnabled(en);
    };

    // ROM menus: Coleco vs ADAM
    setActionEnabledIf(m_openAdamRomAction,   isAdam);
    setActionEnabledIf(m_openColecoRomAction, !isAdam && !isAtari2600);
    setActionEnabledIf(m_openAtari2600RomAction, isAtari2600);

    // Als geen ADAM: alles media uit en klaar
    if (!isAdam) {
        QMenu* mediaMenus[] = { m_tapeMenuA, m_tapeMenuB, m_tapeMenuC, m_tapeMenuD,
                               m_diskMenuA, m_diskMenuB, m_diskMenuC, m_diskMenuD };

        QAction* mediaActions[] = {
            m_loadTapeActionA,  m_ejectTapeActionA,
            m_loadTapeActionB,  m_ejectTapeActionB,
            m_loadTapeActionC,  m_ejectTapeActionC,
            m_loadTapeActionD,  m_ejectTapeActionD,
            m_loadDiskActionA,  m_ejectDiskActionA,
            m_loadDiskActionB,  m_ejectDiskActionB,
            m_loadDiskActionC,  m_ejectDiskActionC,
            m_loadDiskActionD,  m_ejectDiskActionD
        };

        for (QMenu* m : mediaMenus)   setEnabledIf(m, false);
        for (QAction* a : mediaActions) setActionEnabledIf(a, false);
        return;
    }

    // ADAM: hier kan je later echte tape<->disk exclusie doen
    const bool canUseTape = true;
    const bool canUseDisk = true;

    // Menus enable/disable
    QMenu* tapeMenus[] = { m_tapeMenuA, m_tapeMenuB, m_tapeMenuC, m_tapeMenuD };
    QMenu* diskMenus[] = { m_diskMenuA, m_diskMenuB, m_diskMenuC, m_diskMenuD };

    for (QMenu* m : tapeMenus) setEnabledIf(m, canUseTape);
    for (QMenu* m : diskMenus) setEnabledIf(m, canUseDisk);

    // Actions + loaded flags in tabellen
    struct SlotActions {
        QAction* load;
        QAction* eject;
        bool*    loaded;
    };

    SlotActions tapeSlots[] = {
        { m_loadTapeActionA, m_ejectTapeActionA, &m_isTapeLoadedA },
        { m_loadTapeActionB, m_ejectTapeActionB, &m_isTapeLoadedB },
        { m_loadTapeActionC, m_ejectTapeActionC, &m_isTapeLoadedC },
        { m_loadTapeActionD, m_ejectTapeActionD, &m_isTapeLoadedD }
    };

    SlotActions diskSlots[] = {
        { m_loadDiskActionA, m_ejectDiskActionA, &m_isDiskLoadedA },
        { m_loadDiskActionB, m_ejectDiskActionB, &m_isDiskLoadedB },
        { m_loadDiskActionC, m_ejectDiskActionC, &m_isDiskLoadedC },
        { m_loadDiskActionD, m_ejectDiskActionD, &m_isDiskLoadedD }
    };

    auto updateSlots = [&](SlotActions* items, int count, bool canUse) {
        for (int i = 0; i < count; ++i) {
            const bool loaded = items[i].loaded ? *items[i].loaded : false;
            setActionEnabledIf(items[i].load,  canUse && !loaded);
            setActionEnabledIf(items[i].eject, canUse && loaded);
        }
    };

    updateSlots(tapeSlots, 4, canUseTape);
    updateSlots(diskSlots, 4, canUseDisk);
}

//---------------------------------------------------------------------------------------------
// PRINTER DIALOG
//---------------------------------------------------------------------------------------------

void MainWindow::onShowPrinterWindow()
{
    PrintWindow* w = PrintWindow::instance();

    if (!w->isVisible()) {
        w->show();
    }
    w->raise();
    w->activateWindow();

    positionPrinter();
}

void MainWindow::positionPrinter()
{
    PrintWindow* w = PrintWindow::instance();
    if (!w->isVisible()) return;

    if (m_snapWindows) {
        // --- SNAP ON: paste at right side ---
        const int gap = 10;
        int newX = this->x() + this->width() + gap;
        int newY = this->y();
        w->move(newX, newY);

    } else {
        // --- SNAP OFF: Center on mainwindow ---
        int newX = this->x() + (this->width() - w->width()) / 2;
        int newY = this->y() + (this->height() - w->height()) / 2;
        w->move(newX, newY);
    }
}

//---------------------------------------------------------------------------------------------
// SNAP TO MAINSCREEN FUNCTION  (DEBUGGGER / PRINTER) DIALOGS
//---------------------------------------------------------------------------------------------

void MainWindow::onToggleSnap(bool checked)
{
    m_snapWindows = checked;

    if (m_snapWindows) {
        positionDebugger();
        positionPrinter();
    }

    saveSettings();
}

//---------------------------------------------------------------------------------------------
// MAIN SCREEN FUNCTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::onScalingModeChanged(QAction* action)
{
    int newMode = action->data().toInt();

    if (m_scalingMode == newMode) {
        return;
    }

    m_scalingMode = newMode;
    qDebug() << "[UI] Scaling Mode changed to" << newMode;

    saveSettings();

    if (m_screenWidget) {
        m_screenWidget->setScalingMode(static_cast<ScreenWidget::ScalingMode>(newMode));
    }
}

void MainWindow::onToggleFullScreen(bool checked)
{
    m_startFullScreen = checked;

    if (checked) {
        this->setMinimumSize(QSize(0, 0));
        this->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        showMaximized();
        updateFullScreenWallpaper();
        m_wallpaperLabel->show();
        m_screenWidget->setFullScreenMode(true);
    } else {
        showNormal();
        m_wallpaperLabel->hide();
        m_screenWidget->setFullScreenMode(false);
        this->setMinimumSize(770, 700);
    }
}

void MainWindow::onResetWindowSize()
{
    if (isFullScreen() || (m_actFullScreen && m_actFullScreen->isChecked())) {
        if (m_actFullScreen) m_actFullScreen->setChecked(false);
        onToggleFullScreen(false);
    }

    this->resize(770, 700);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    if (m_windowBorderOverlay) {
        m_windowBorderOverlay->setGeometry(rect());
        m_windowBorderOverlay->raise();
    }

    // Frameless windows do not receive Windows' automatic rounded corners.
    // Clip the normal window to a rounded shape, but keep maximized mode square.
    if (isMaximized() || isFullScreen()) {
        clearMask();
    } else {
        QPainterPath windowShape;
        windowShape.addRoundedRect(QRectF(rect()), 10.0, 10.0);
        setMask(QRegion(windowShape.toFillPolygon().toPolygon()));
    }

    if (!m_screenWidget || m_screenWidget->height() == 0) return;
    int currentHeight = m_screenWidget->height();
    int gameScreenWidth = (currentHeight * 256) / 192;
    int difference = this->width() - gameScreenWidth;

    bool shouldShow = m_useBezels;// && (isFullScreen() || isMaximized() || (difference > 270));

    if (shouldShow) {
        // --- SHOW BEZELS ---
        if (m_wallpaperLabel) {
            updateFullScreenWallpaper();
            m_wallpaperLabel->setGeometry(this->rect());
            m_wallpaperLabel->show();
            m_wallpaperLabel->lower();
        }
        if (m_screenWidget) {
            m_screenWidget->setFullScreenMode(true);
        }
    } else {
        // --- HIDE BEZELS ---
        if (m_wallpaperLabel) {
            m_wallpaperLabel->hide();
        }
        if (m_screenWidget) {
            m_screenWidget->setFullScreenMode(false);
        }
    }

   centerSplash();

    m_bottomBlackBar->setGeometry(
        0,                  // helemaal links
        this->height() -22,  // onderaan window
        this->width(),      // breedte schaalt mee
        18           // vaste hoogte
        );

    if (m_machineType == MACHINE_ATARI2600) {
        updateAtariStatusBar();
    } else {
        const QString display = (m_machineType == MACHINE_ADAM) ? m_currentARomName : m_currentRomName;
        updateRomLabelForStatusBar(statusBar(), m_sepLabel4, m_romLabel, display);
    }

}

void MainWindow::onToggleBezels(bool checked)
{
    m_useBezels = checked;

    QResizeEvent re(size(), size());
    resizeEvent(&re);

    saveSettings();
}

void MainWindow::updateFullScreenWallpaper()
{
    if (!m_wallpaperLabel)
        return;

    const bool isAdam = (m_machineType == 1);

    // 1) Bepaal welk pad we willen gebruiken
    const QString bezelPath = isAdam ? m_adamBezelPath : m_cvBezelPath;

    auto loadCustom = [&](const QString& path) -> QPixmap {
        QPixmap px;
        if (path.isEmpty() || path.compare("none", Qt::CaseInsensitive) == 0)
            return px;

        QString absPath = path;
        if (QDir::isRelativePath(absPath)) {
            const QDir appDir(QCoreApplication::applicationDirPath());
            absPath = appDir.absoluteFilePath(absPath);
        }

        const QFileInfo fi(absPath);
        if (fi.exists() && fi.isFile())
            px.load(absPath);

        return px;
    };

    // 2) Probeer custom, anders fallback resource
    QPixmap wallpaper = loadCustom(bezelPath);
    if (wallpaper.isNull()) {
        wallpaper.load(isAdam
                           ? QStringLiteral(":/images/images/wallpaper_adam.png")
                           : QStringLiteral(":/images/images/wallpaper_coleco.png"));
    }

    if (wallpaper.isNull())
        return;

    // 3) Schaal en toon
    m_wallpaperLabel->setPixmap(wallpaper.scaled(
        m_wallpaperLabel->size(),
        Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation));

    m_wallpaperLabel->setAlignment(Qt::AlignCenter);
}

//---------------------------------------------------------------------------------------------
// SAVE&LOAD EMULATOR STATES FUNCTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::onSaveState()
{
 QString absoluteStatePath = QDir::cleanPath(m_statePath);

    QDir statesDir(absoluteStatePath);
    if (!statesDir.exists())
        statesDir.mkpath(".");

    QString baseName = m_currentRomName;
    if (baseName.isEmpty() || baseName == "No cart")
        baseName = "state";

    QFileInfo fi(baseName);
    baseName = fi.completeBaseName();

    QString defaultFile = statesDir.filePath(baseName + ".sta");

    const QString filePath = CustomFileDialog::getSaveFileName(
        this,
        tr("Save State"),
        defaultFile,
        tr("State files (*.sta);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathState,
        QFileDialog::Options()
        );

    if (filePath.isEmpty())
        return;

    QString finalPath = filePath;
    if (!finalPath.endsWith(".sta", Qt::CaseInsensitive))
        finalPath += ".sta";

    QFileInfo fileInfo(finalPath);
    CustomFileDialog::s_lastSaveDir = fileInfo.absolutePath();

    QMetaObject::invokeMethod(
        m_colecoController,
        "saveState",
        Qt::QueuedConnection,
        Q_ARG(QString, finalPath)
        );
}

void MainWindow::onLoadState()
{
    QString absoluteStatePath = QDir::cleanPath(m_statePath);

    QDir statesDir(absoluteStatePath);
    if (!statesDir.exists())
        statesDir.mkpath(".");

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Load State"),
        statesDir.absolutePath(),
        tr("State files (*.sta);;All Files (*.*)"),
        nullptr,
        CustomFileDialog::PathState,
        QFileDialog::Options()
        );

    if (filePath.isEmpty())
        return;

    QFileInfo fileInfo(filePath);
    CustomFileDialog::s_lastOpenDir = fileInfo.absolutePath();

    QFileInfo inInfo(filePath);
    QDir appDir(QCoreApplication::applicationDirPath());
    m_statePath = appDir.relativeFilePath(inInfo.absolutePath());
    saveSettings();

    QMetaObject::invokeMethod(
        m_colecoController,
        "loadState",
        Qt::QueuedConnection,
        Q_ARG(QString, filePath)
        );
}

//---------------------------------------------------------------------------------------------
// JOYSTICK/KEYPAD/KEYBOARD FUNCTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::onOpenJoypadMapper()
{
    JoypadWindow dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        if (m_inputWidget) m_inputWidget->reloadMappings();
    }
}

void MainWindow::onJoystickTypeChanged(QAction* action)
{
    int newType = action->data().toInt();

    if (m_joystickType == newType) {
        return;
    }

    m_joystickType = newType;

    saveSettings();

    if (m_joystick) {
        // When you select Joystick it will poll again
        m_joystick->setJoystickType(m_joystickType);
        m_joystick->stopPolling();
        m_joystick->startPolling(0);
    }
}

void MainWindow::onTogglePaddleMode(bool checked)
{
    m_usePaddleMode = checked;
    if (checked && m_actToggleDrivingControllerMode
            && m_actToggleDrivingControllerMode->isChecked()) {
        m_actToggleDrivingControllerMode->setChecked(false);
    }
    if (m_actPaddleDirectionUpDown)
        m_actPaddleDirectionUpDown->setEnabled(checked);
    qDebug() << "[UI] Paddle Mode set to" << (checked ? "ON" : "OFF");

    // Sla de status op (toe te voegen in save/loadSettings later)
    QSettings settings;
    settings.setValue("controller/usePaddleMode", checked);

    // PUSH DE STATUS NAAR DE BRIDGE (CRUCIAAL)
    // De bridge heeft geen setter nodig; we kunnen de volatile variabele direct zetten.
    extern volatile uint8_t ib_paddle_mode; // Moet geëxporteerd worden in coleco.cpp om te kunnen gebruiken
    ib_paddle_mode = checked ? 1 : 0;

    // Stuur de status naar de InputWidget voor lokale filtering
    if (m_inputWidget) {
        m_inputWidget->setPaddleMode(checked);
        m_inputWidget->setPaddleVertical(m_paddleDirectionUpDown);
    }

    // (Optioneel: Herstart de polling als de setJoystickType dit nodig heeft)
}

void MainWindow::onToggleDrivingControllerMode(bool checked)
{
    m_useDrivingControllerMode = checked;

    if (checked && m_actTogglePaddleMode && m_actTogglePaddleMode->isChecked()) {
        m_actTogglePaddleMode->setChecked(false);
    }

    qDebug() << "[UI] Driving Controller Mode set to"
             << (checked ? "ON" : "OFF");

    QSettings settings;
    settings.setValue("controller/useDrivingControllerMode", checked);

    extern volatile uint8_t ib_paddle_mode;
    ib_paddle_mode = m_usePaddleMode ? 1 : 0;

    if (m_inputWidget)
        m_inputWidget->setDrivingControllerMode(checked);
}

void MainWindow::onToggleKeyboard(bool on)
{
    if (on) {
        m_inputWidget->setOverlayVisible(true);
        m_inputWidget->raise();
    } else {
        m_inputWidget->setOverlayVisible(false);
        if (m_screenWidget) m_screenWidget->setFocus(Qt::OtherFocusReason);
    }

    if (m_inputWidget) {
        m_inputWidget->setKeyboardOverlay(on);
    }

}

void MainWindow::onAdamGameMode()
{
    m_adamGameMode = m_actAdamGameOn->isChecked();
    qDebug() << "[UI] ADAM Input Mode:" << (m_adamGameMode ? "Game mode enabled" : "Game mode disabled");
    adamnet_set_game_mode(m_adamGameMode);

    if (m_screenWidget) {
        m_screenWidget->setFocus(Qt::OtherFocusReason);
    }

    if (m_inputWidget) {
        m_inputWidget->setAdamGameMode(m_adamGameMode);
    }

}

void MainWindow::onStartBasicInject()
{
    // Use the configured media root. For example:
    // ROM Path C:/ADAMP_EMU/media/roms -> C:/ADAMP_EMU/media/injected.
    const QString mediaRoot = QFileInfo(QDir::cleanPath(m_romPath)).absoluteDir().absolutePath();
    const QString injectPath = QDir::cleanPath(QDir(mediaRoot).filePath("injected"));

    if (!QDir().mkpath(injectPath)) {
        QMessageBox::warning(this, tr("START INJECT"),
                             tr("The folder media/injected could not be created."));
        return;
    }

    const QString filePath = CustomFileDialog::getOpenFileName(
        this,
        tr("Load SmartBASIC source"),
        injectPath,
        tr("BASIC source (*.txt *.bas);;Text files (*.txt);;BASIC files (*.bas)"),
        nullptr,
        CustomFileDialog::PathInjected);

    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("START INJECT"),
                             tr("The selected file could not be opened."));
        return;
    }

    QByteArray source = file.readAll();
    file.close();

    source.replace("\r\n", "\n");
    source.replace('\r', '\n');

    QByteArray filtered;
    filtered.reserve(source.size());
    for (const char raw : source) {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (ch == '\n') {
            filtered.append('\r');
        } else if (ch == '\t') {
            filtered.append(' ');
        } else if (ch >= 32 && ch <= 126) {
            filtered.append(static_cast<char>(ch));
        }
    }

    if (filtered.isEmpty()) {
        QMessageBox::information(this, tr("START INJECT"),
                                 tr("The selected file contains no injectable BASIC text."));
        return;
    }

    // Also submit the final BASIC line when the source file has no newline at EOF.
    if (!filtered.endsWith('\r'))
        filtered.append('\r');

    onStopBasicInject();
    m_basicInjectData = filtered;
    m_basicInjectPosition = 0;
    m_actStartBasicInject->setEnabled(false);
    m_actStopBasicInject->setEnabled(true);

    qDebug() << "[INJECT] Started:" << QFileInfo(filePath).fileName();
    m_basicInjectTimer->start(1);
}

void MainWindow::onStopBasicInject()
{
    const bool wasActive = m_basicInjectTimer && m_basicInjectTimer->isActive();

    if (m_basicInjectTimer)
        m_basicInjectTimer->stop();

    m_basicInjectData.clear();
    m_basicInjectPosition = 0;

    if (m_actStartBasicInject)
        m_actStartBasicInject->setEnabled(true);
    if (m_actStopBasicInject)
        m_actStopBasicInject->setEnabled(false);

    if (wasActive)
        qDebug() << "[INJECT] Stopped by user.";
}

void MainWindow::injectNextBasicCharacter()
{
    if (m_basicInjectPosition >= m_basicInjectData.size()) {
        m_basicInjectData.clear();
        m_basicInjectPosition = 0;
        m_actStartBasicInject->setEnabled(true);
        m_actStopBasicInject->setEnabled(false);
        qDebug() << "[INJECT] Completed.";
        return;
    }

    // PutKBD gebruikt één LastKey-slot. Wacht tot de ADAM het vorige teken
    // werkelijk heeft opgehaald; anders overschrijven de eerste tekens van
    // een nieuwe BASIC-regel elkaar terwijl de vorige regel wordt getokenized.
    if (LastKey != 0) {
        m_basicInjectTimer->start(10);
        return;
    }

    const unsigned char ch = static_cast<unsigned char>(
        m_basicInjectData.at(m_basicInjectPosition++));
    PutKBD(static_cast<unsigned int>(ch));

    // SmartBASIC needs extra time to accept and tokenize a completed line.
    m_basicInjectTimer->start(ch == '\r' ? 220 : 80);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // Doorsturen naar InputWidget - die regelt alles
    if (m_inputWidget && m_inputWidget->handleKey(event, true)) {
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    const int key = event->key();

    // F11/F12 blijven bij MainWindow voor shortcuts
    if (key >= Qt::Key_F11 && key <= Qt::Key_F12) {
        QMainWindow::keyReleaseEvent(event);
        return;
    }

    // Rest naar InputWidget
    if (m_inputWidget && m_inputWidget->handleKey(event, false)) {
        event->accept();
    } else {
        event->ignore();
    }
}

//---------------------------------------------------------------------------------------------
// RESET FUNCTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::powerOff()
{
    /* Capture the user-visible source machine before this routine clears the
     * cartridge/status state.  The C core's currentMachineType can already
     * say ADAM during startup/config synchronisation while the active session
     * and UI are still Coleco, which incorrectly selected a warm BIOS reset. */
    const bool forceAdamCoreBeforeMenu =
        m_ctrlAdamNet && m_machineType != MACHINE_ADAM;

    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));
    m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png"));
    loadExternalBiosRoms();
   // QMetaObject::invokeMethod(m_colecoController, "resethMachine",
   //                             Qt::QueuedConnection);

    // Cartridge ROM loskoppelen bij PowerOff
    QMetaObject::invokeMethod(m_colecoController, "ejectColecoCartridge",
                              Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_colecoController, "ejectAdamCartridge",
                              Qt::QueuedConnection);

    m_isColecoRomLoaded = false;
    m_isAdamRomLoaded = false;
    m_currentRomName.clear();
    m_currentARomName.clear();

    // BUTTON POWER OFF
    qDebug() << "[UI] Button pressed do power off emulator.";
    QMetaObject::invokeMethod(m_colecoController, "powerOffMachine",
                             Qt::QueuedConnection,
                             Q_ARG(bool, m_ctrlAdamNet),
                             Q_ARG(bool, forceAdamCoreBeforeMenu));


    m_resetAdamLocked  = m_ctrlAdamNet;
    m_AdamDMedia_insert = m_ctrlAdamNet;
    m_AdamTMedia_insert = false;
    forceStatusBarMediaFlags();
    m_ColecoMedia_insert = false;
    m_resetColecoLocked = false;

}

void MainWindow::onResetAdamBtnClicked()
{
    if (!m_resetAdamBtn)
        return;

    if (m_ctrlAdamNet)
        mcu2_arm_fuji_reset_boot_probe();

    // Als er nog een pending Coleco cartridge klaarstond, annuleren.
    m_pendingColecoBoot = false;
    m_pendingColecoRomPath.clear();

    // Stop cartridge blink als die nog actief is.
    if (m_resetCartBlinkTimer && m_resetCartBlinkTimer->isActive()) {
        m_resetCartBlinkTimer->stop();
    }

    if (m_resetCartBtn) {
        m_resetCartBtn->setChecked(false);
        m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png"));
    }

    // Als Coleco cartridge gelocked was, loskoppelen bij ADAM reset.
    if (m_resetColecoLocked || m_ColecoMedia_insert) {
        m_resetColecoLocked  = false;
        m_ColecoMedia_insert = false;

        QMetaObject::invokeMethod(
            m_colecoController,
            "ejectColecoCartridge",
            Qt::QueuedConnection
            );
    }

    // Stop ADAM blink.
    if (m_resetAdamBlinkTimer && m_resetAdamBlinkTimer->isActive()) {
        m_resetAdamBlinkTimer->stop();
    }

    m_resetAdamBtn->setChecked(false);
    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));

    // Als er ADAM media aanwezig is, ADAM reset lock actief zetten.
    if (m_AdamDMedia_insert || m_AdamTMedia_insert
        || !m_currentARomName.isEmpty() || m_ctrlAdamNet)
        m_resetAdamLocked = true;

    // Naar ADAM mode.
    switchToAdamMode();

    // ADAM cartridge opnieuw mappen indien aanwezig.
    if (!m_currentARomName.isEmpty())
    {
        QString absolutePath = QDir::cleanPath(
            CustomFileDialog::s_lastOpenDir + QDir::separator() + m_currentARomName
            );

        QMetaObject::invokeMethod(
            m_colecoController,
            "AdamCartridge",
            Qt::QueuedConnection,
            Q_ARG(QString, absolutePath)
            );
    }

    // Tapes D1-D4 opnieuw koppelen.
    for (int drive = 0; drive < 4; ++drive)
    {
        if (!m_loadedTapeNames[drive].isEmpty() && !m_hardwareTapeMedia[drive])
        {
            QString absolutePath = QDir::cleanPath(
                CustomFileDialog::s_lastOpenDir + QDir::separator() + m_loadedTapeNames[drive]
                );

            QMetaObject::invokeMethod(
                m_colecoController,
                "loadTape",
                Qt::QueuedConnection,
                Q_ARG(int, drive),
                Q_ARG(QString, absolutePath)
                );

            m_cpm_status = m_cpm_enabled;
        }
    }

    // Disks D5-D8 opnieuw koppelen.
    for (int drive = 0; drive < 4; ++drive)
    {
        if (!m_loadedDiskNames[drive].isEmpty() && !m_hardwareDiskMedia[drive])
        {
            QString absolutePath = QDir::cleanPath(
                CustomFileDialog::s_lastOpenDir + QDir::separator() + m_loadedDiskNames[drive]
                );

            QMetaObject::invokeMethod(
                m_colecoController,
                "loadDisk",
                Qt::QueuedConnection,
                Q_ARG(int, drive),
                Q_ARG(QString, absolutePath)
                );

            m_cpm_status = m_cpm_enabled;
        }
    }

    mountAdamStartupImageIfNeeded();

    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();

    // Korte visuele feedback.
    m_resetAdamBtn->setChecked(true);
    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_on.png"));

    QTimer::singleShot(250, this, [this]() {
        if (!m_resetAdamBtn)
            return;

        m_resetAdamBtn->setChecked(false);

        if (m_cpm_enabled)
        {
            if (m_AdamTMedia_insert)
                m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_tcpm_locked_adam_off.png"));
            else if (m_AdamDMedia_insert)
                m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_dcpm_locked_adam_off.png"));
            else
                m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));
        }
        else
        {
            if (m_AdamTMedia_insert)
                m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_t_locked_adam_off.png"));
            else if (m_AdamDMedia_insert)
                m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_d_locked_adam_off.png"));
            else if (!m_currentARomName.isEmpty())
                m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_ca_locked_adam_off.png"));
            else
                m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));
        }
    });
}

void MainWindow::onResetCartBtnClicked()
{
    if (!m_resetCartBtn)
        return;

    if (m_machineType == MACHINE_ATARI2600) {
        // Atari RESET is a momentary console switch.  The pressed/released
        // handlers above operate it; clicking must not cold-reset the core.
        return;
    }

    if (m_resetAdamLocked)
    {
        m_resetAdamLocked = false;
        m_AdamDMedia_insert = false;
        m_AdamTMedia_insert = false;

        if (m_hardwareWindow)
            m_hardwareWindow->updateAvailability();

        forceStatusBarMediaFlags();

        if (m_resetAdamBtn)
            m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));
    }

    stopResetCartBlinkAndSetFinalIcon();

    // ------------------------------------------------------------
    // Geen pending ROM: gewone reset van reeds geladen cartridge.
    // ------------------------------------------------------------
    if (!m_pendingColecoBoot || m_pendingColecoRomPath.isEmpty())
    {
        qDebug() << "[UI] Cartridge reset without pending ROM";

        if (m_ColecoMedia_insert)
            m_resetColecoLocked = true;

        switchToColecoMode();
        onSgmStatusChanged(m_sgmEnabled);

        const QString absolutePath = !m_currentRomName.isEmpty()
                                         ? QDir::cleanPath(CustomFileDialog::s_lastOpenDir + QDir::separator() + m_currentRomName)
                                         : QString();

        QMetaObject::invokeMethod(
            m_colecoController,
            [ctrl = m_colecoController, absolutePath]() {
                if (!absolutePath.isEmpty())
                    ctrl->ColecoCartridge(absolutePath);

                ctrl->resetMachine();
            },
            Qt::QueuedConnection
            );

        updateMediaMenuState();
        updateMediaStatusLabels();
        updateHardwareWindowMediaDisplay();

        m_resetCartBtn->setChecked(true);
        m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_on.png"));

        QTimer::singleShot(250, this, [this]() {
            stopResetCartBlinkAndSetFinalIcon();
        });

        return;
    }

    // ------------------------------------------------------------
    // Pending ROM: pas NU laden/starten.
    // ------------------------------------------------------------
    const QString romPath = m_pendingColecoRomPath;
    const bool isDka = isDka2018RomName(romPath);

    qDebug() << "[UI] Confirmed pending Coleco ROM boot:"
             << romPath
             << "DKA=" << isDka;

    m_pendingColecoBoot = false;
    m_pendingColecoRomPath.clear();

    m_resetColecoLocked = true;
    m_ColecoMedia_insert = true;

    /*
     * BELANGRIJK:
     * Geen losse QMetaObject-calls meer zoals:
     *   ColecoCartridge()
     *   prepareForNewCRomAndPauseOnBios()
     *
     * Alles gebeurt hieronder in één queued lambda in de controller-thread.
     * Daardoor kan er geen reset vóór/na de ROM-load tussen glippen.
     */
    onSgmStatusChanged(m_sgmEnabled);

    QMetaObject::invokeMethod(
        m_colecoController,
        [ctrl = m_colecoController, romPath, isDka]() {
            qDebug() << "[CTRL] Atomic pending Coleco cartridge boot:"
                     << romPath
                     << "DKA=" << isDka;

            // One central cartridge boot path: load ROM, one reset, run.
            // No DKA-specific reset exception.
            ctrl->bootPreparedColecoCartridge(romPath);
        },
        Qt::QueuedConnection
        );

    updateRomLabelForStatusBar(statusBar(), m_sepLabel4, m_romLabel, m_currentRomName);
    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();

    m_resetCartBtn->setChecked(true);
    m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_on.png"));

    QTimer::singleShot(250, this, [this]() {
        stopResetCartBlinkAndSetFinalIcon();
    });
}

void MainWindow::stopResetCartBlinkAndSetFinalIcon()
{
    if (m_resetCartBlinkTimer) {
        m_resetCartBlinkTimer->stop();
    }

    if (!m_resetCartBtn)
        return;

    m_resetCartBtn->setChecked(false);

    if (m_ColecoMedia_insert && !m_sgmEnabled) {
        m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_c_locked_cartridge_off.png"));
    }
    else if (m_ColecoMedia_insert && m_sgmEnabled) {
        m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_csgm_locked_cartridge_off.png"));
    }
    else {
        m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png"));
    }
}

void MainWindow::switchToAdamMode()
{
    if (m_toolsMenu) m_toolsMenu->menuAction()->setVisible(true);
    if (m_atariInputsMenu) m_atariInputsMenu->menuAction()->setVisible(false);
    if (!m_colecoController) return;

    const bool changingMachine = (m_machineType != MACHINE_ADAM);
    if (changingMachine) {
        // A real machine change releases both reset buttons and all media.
        // A normal Reset ADAM never enters this block and keeps its media.
        releaseAllMedia(false);
    }

    /* switchToColecoMode()/Release All disconnects logical D5.  Re-arm the
     * physical FujiNet drive whenever ADAMNet is selected again, otherwise
     * CONFIG appears but a newly selected image cannot be read until the
     * whole application is restarted. */
    if (m_ctrlAdamNet) {
        mcu2_disk_set_enabled(true);
        mcu2_block_set_device_enabled(0x04, true);
        qDebug() << "[UI][BOOT] ADAM mode re-armed physical FujiNet D5";
    }


    coleco_hide_current_vdp_sprites();

    QTimer::singleShot(300, this, []() {
        coleco_hide_current_vdp_sprites();
    });

    QTimer::singleShot(1500, this, []() {
    });


    // 1. Maak een NIEUWE configuratie op basis van de HUIDIGE configuratie
    HardwareConfig newCfg;

    // Laad de actuele hardwaresetting van MainWindow in de tijdelijke config
    newCfg.machine = MACHINE_ADAM;
    newCfg.palette = m_paletteIndex;
    newCfg.vdpType = m_vdpType;
    newCfg.f18a80SelfTest = m_f18a80SelfTest;
    newCfg.sgmEnabled = m_sgmEnabled;
    newCfg.c80Enabled = m_c80Enabled;
    newCfg.Joys = m_ctrlJoys;
    newCfg.AdamNet = m_ctrlAdamNet;
    newCfg.fujiNetDirectRom = m_fujiNetDirectRom;
    newCfg.Cartridge = m_ctrlCartridge;

   applyHardwareConfig(newCfg);

    // Alleen een echte powerOff doen als er GEEN ADAM media klaarstaat.
    // Anders wissen we bij de eerste reset m_cpm_enabled / m_tdos_enabled
    // en boot CP/M/TDOS terug naar Writer.
    const bool hasAdamMedia =
        m_AdamDMedia_insert ||
        m_AdamTMedia_insert ||
        !m_currentARomName.isEmpty() ||
        m_ctrlAdamNet;

    if (emutype == false  && !hasAdamMedia)  {
       //powerOff();
        coleco_initialise();
        coleco_reset_and_restart_bios();
    }

    emutype = true;

   // Keyboard input
    if (m_inputWidget) {
         m_inputWidget->setMachineType(1);
         //m_inputWidget->setAdamGameMode(false);  // Standaard keyboard mode
         qDebug() << "[MAINWINDOW] EXTRA: Forced InputWidget to ADAM mode";
     }

     updateMediaMenuState();
     updateMediaStatusLabels();

}

void MainWindow::switchToColecoMode()
{
    if (m_toolsMenu) m_toolsMenu->menuAction()->setVisible(true);
    if (m_atariInputsMenu) m_atariInputsMenu->menuAction()->setVisible(false);
    if (!m_colecoController) return;

    const bool changingMachine = (m_machineType != MACHINE_COLECO);
    emutype=false;

    if (changingMachine) {
        // Only an actual ADAM/Atari -> Coleco transition releases media.
        // Reset Coleco stays in the same machine and must keep its ROM/icon.
        releaseAllMedia(false);
    }

    if (m_c80Enabled) m_c80Enabled = false;

    // 1. Maak een NIEUWE configuratie op basis van de HUIDIGE configuratie
    HardwareConfig newCfg;
    newCfg.machine = MACHINE_COLECO;
    newCfg.palette = m_paletteIndex;
    newCfg.vdpType = m_vdpType;
    newCfg.f18a80SelfTest = m_f18a80SelfTest;
    newCfg.sgmEnabled = m_sgmEnabled;
    newCfg.c80Enabled = m_c80Enabled;
    newCfg.Joys = m_ctrlJoys;
    newCfg.AdamNet = m_ctrlAdamNet;
    newCfg.fujiNetDirectRom = m_fujiNetDirectRom;
    newCfg.Cartridge = m_ctrlCartridge;
    applyHardwareConfig(newCfg);

    if (m_inputWidget) {
        m_inputWidget->setMachineType(0);
        m_inputWidget->setAdamGameMode(false);  // Standaard keyboard mode
        qDebug() << "[MAINWINDOW] EXTRA: Forced InputWidget to Coleco mode";
    }

    QMetaObject::invokeMethod(
        m_colecoController,
        "setSGMEnabled",
        Qt::QueuedConnection,
        Q_ARG(bool, m_sgmEnabled) // Gebruik de actuele, in applyHardwareConfig ingestelde status
        );

    updateMediaMenuState();
    updateMediaStatusLabels();
}

void MainWindow::switchToAtari2600Mode()
{
    if (!m_colecoController)
        return;

    onReleaseAll();
    emutype = false;
    m_machineType = MACHINE_ATARI2600;
    m_sgmEnabled = false;
    m_c80Enabled = false;
    if (m_toolsMenu) m_toolsMenu->menuAction()->setVisible(false);
    if (m_atariInputsMenu) m_atariInputsMenu->menuAction()->setVisible(true);

    if (m_screenWidget)
        m_screenWidget->set80ColumnMode(false);
    if (m_inputWidget) {
        m_inputWidget->setMachineType(MACHINE_ATARI2600);
        m_inputWidget->setAdamGameMode(false);
    }

    QMetaObject::invokeMethod(
        m_colecoController,
        "setAtari2600Mode",
        Qt::QueuedConnection,
        Q_ARG(bool, true)
        );

    updateMediaMenuState();
    updateMediaStatusLabels();
    updateFullScreenWallpaper();
}

void MainWindow::applyHardwareConfig(const HardwareConfig& cfg)
{

    m_vdpType = (cfg.vdpType >= VDP_TMS && cfg.vdpType <= VDP_PICO9918)
                    ? cfg.vdpType : VDP_TMS;
    m_f18a80SelfTest = vdpHasF18A(m_vdpType) ? cfg.f18a80SelfTest : false;

    coleco_set_vdp_type(m_vdpType);
    f18a_set_80col_selftest_enabled(m_f18a80SelfTest ? 1 : 0);

    qDebug() << "[VIDEO] Selected VDP:"
             << vdpTypeName(m_vdpType)
             << "80col self-test=" << m_f18a80SelfTest;

    // C80 eerst toepassen, vóór resetAdam/resetColeco.
    // Zo ziet TDOS de 80-col hardware alleen als de HardwareWindow-knop echt aan staat.
    m_c80Enabled = cfg.c80Enabled;
    coleco_80col_enabled = cfg.c80Enabled ? 1 : 0;
    if (m_screenWidget) {
        m_screenWidget->set80ColumnMode(cfg.c80Enabled);
    }

    if (cfg.machine == MACHINE_ATARI2600)
    {
        QMetaObject::invokeMethod(
            m_colecoController, "setAtari2600Mode",
            Qt::QueuedConnection, Q_ARG(bool, true)
            );
    }
    else
    {
        QMetaObject::invokeMethod(
            m_colecoController, "setAtari2600Mode",
            Qt::QueuedConnection, Q_ARG(bool, false)
            );
    }

    if  (cfg.machine == MACHINE_ADAM)
    {
            const bool forceAdamCoreBeforeMenu =
                m_machineType != MACHINE_ADAM;
            QMetaObject::invokeMethod(
                m_colecoController, "resetAdam",
                Qt::QueuedConnection,
                Q_ARG(bool, forceAdamCoreBeforeMenu)
                );
   }
    if  (cfg.machine == MACHINE_COLECO)
    {
            // COLECO
            QMetaObject::invokeMethod(
                m_colecoController, "resetColeco",
                Qt::QueuedConnection
                );
    }

    m_machineType = cfg.machine;
    m_realhardware = cfg.realhardware;

    // applyHardwareConfig() wordt ook rechtstreeks tijdens startup gebruikt.
    // Daardoor loopt een opgeslagen Atari-keuze niet via switchToAtari2600Mode().
    // Houd de menuzichtbaarheid daarom centraal gelijk aan de actieve machine.
    if (m_toolsMenu)
        m_toolsMenu->menuAction()->setVisible(m_machineType != MACHINE_ATARI2600);
    if (m_atariInputsMenu)
        m_atariInputsMenu->menuAction()->setVisible(m_machineType == MACHINE_ATARI2600);

    if (m_paletteIndex != cfg.palette) {
        m_paletteIndex = cfg.palette;
        if (m_colecoController) {
            QMetaObject::invokeMethod(
                m_colecoController,
                [this]() { coleco_setpalette(m_paletteIndex); },
                Qt::QueuedConnection
                );
        }
    }

    //const bool desiredSgm = (cfg.machine == MACHINE_ADAM) ? cfg.sgmEnabled : false;
    const bool desiredSgm = cfg.sgmEnabled;

    if (desiredSgm != m_sgmEnabled) {
        m_sgmEnabled = desiredSgm;

        QMetaObject::invokeMethod(
            m_colecoController, "setSGMEnabled",
            Qt::QueuedConnection, Q_ARG(bool, m_sgmEnabled)
            );
    }

    m_ctrlJoys    = cfg.Joys;
    m_ctrlAdamNet      = cfg.AdamNet;
    m_fujiNetDirectRom = cfg.fujiNetDirectRom;
    mcu2_disk_set_enabled(cfg.machine == MACHINE_ADAM && m_ctrlAdamNet);
    mcu2_set_fuji_direct_rom_enabled(
        cfg.machine == MACHINE_ADAM && m_ctrlAdamNet && m_fujiNetDirectRom);
    m_ctrlCartridge = cfg.Cartridge;

    if (m_sysLabel) {
        if (cfg.machine == MACHINE_ATARI2600)
            m_sysLabel->setText("ATARI 2600");
        else
            m_sysLabel->setText(cfg.machine == MACHINE_COLECO ? "COLECO" : "ADAM");
    }

    bool isAdam = (cfg.machine == MACHINE_ADAM);
    if (m_adamInputMenu) m_adamInputMenu->setEnabled(isAdam);

    // C80 is bovenaan al toegepast vóór de reset.

    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();
    updateMediaMenuState();
    updateFullScreenWallpaper();

    if (m_settingsLoaded) {
        saveSettings();
    } else {
        qDebug() << "[VIDEO] skip saveSettings during startup, vdp =" << m_vdpType;
    }
}

void MainWindow::onPowerBtnClicked()
{
    if (m_c80Enabled) m_c80Enabled = false;

    // 1. Maak een NIEUWE configuratie op basis van de HUIDIGE configuratie
    HardwareConfig newCfg;
    newCfg.c80Enabled = m_c80Enabled;


    // Stop ADAM Reset knop knipperen
    if (m_resetAdamBlinkTimer && m_resetAdamBlinkTimer->isActive()) {
        m_resetAdamBlinkTimer->stop();
        if (m_resetAdamBtn) {
            m_resetAdamBtn->setChecked(false);
            m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png")); // Zet standaard icoon terug
        }
    }

    // Stop Cartridge Reset knop knipperen
    if (m_resetCartBlinkTimer && m_resetCartBlinkTimer->isActive()) {
        m_resetCartBlinkTimer->stop();
        if (m_resetCartBtn) {
            m_resetCartBtn->setChecked(false);
            m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png")); // Zet standaard icoon terug
        }
    }

    m_powerBtn->setChecked(true);
    m_powerBtn->setIcon(QIcon(":/images/images/adamp_logo_power_adam_on.png"));

    QTimer::singleShot(250, this, [this]() {
        if (m_powerBtn) {
            m_powerBtn->setChecked(false);
            m_powerBtn->setIcon(QIcon(":/images/images/adamp_logo_power_adam_off.png"));
        }
    });

    // Power Off is always a real cold power cycle. In Coleco mode this first
    // ejects and erases the cartridge; only Reset Coleco keeps/restarts it.
    releaseAllMedia(true);
    if (m_ctrlAdamNet)
        mcu2_arm_fuji_reset_boot_probe();
    powerOff();
}

void MainWindow::onReleaseAll()
{
    releaseAllMedia(false);
}

void MainWindow::releaseAllMedia(bool preservePhysicalFujiD5)
{
    // Atari 2600 Cartridge
    onEjectAtari2600Rom();

    // Coleco Cartridge
    onEjectColecoRom();

    // ADAM Cartridge
    onEjectAdamRom();

    m_resetAdamBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_adam_off.png"));
    m_resetCartBtn->setIcon(QIcon(":/images/images/adamp_logo_reset_cartridge_off.png"));

    m_resetAdamLocked  = false;
    m_AdamDMedia_insert = false;
    m_AdamTMedia_insert = false;
    forceStatusBarMediaFlags();
    m_ColecoMedia_insert = false;
    m_resetColecoLocked = false;


    // Tapes (D1-D4)
    for (int drive = 0; drive < 4; ++drive) {
        onEjectTape(drive);
    }

    // Disks (D5-D8)
    for (int drive = 0; drive < 4; ++drive) {
        if (drive == 0 && preservePhysicalFujiD5 && m_ctrlAdamNet) {
            /* D5 is a physical FujiNet device, not an in-memory image. Keep
             * it connected across the emulated power cycle so the ADAM BIOS
             * can boot FujiNet's start disk or its retained mounted medium. */
            m_hardwareDiskMedia[0] = true;
            mcu2_block_set_device_enabled(0x04, true);
            if (m_loadedDiskNames[0].isEmpty())
                m_loadedDiskNames[0] = QStringLiteral("[HW] FujiNet D5");
            m_AdamDMedia_insert = true;
            qDebug() << "[UI][BOOT] preserving physical FujiNet D5 across power-off";
        } else {
            onEjectDisk(drive);
        }
    }

    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();
}

void MainWindow::onToggleResetAdamBlink()
{
    if (!m_resetAdamBtn) return;

    // Toggle de checked-status
    bool isChecked = m_resetAdamBtn->isChecked();
    m_resetAdamBtn->setChecked(!isChecked);

    // Wissel tussen de standaard 'off' icoon en de 'blink' icoon
    m_resetAdamBtn->setIcon(QIcon(isChecked
                                      ? ":/images/images/adamp_logo_reset_adam_off.png"
                                      : ":/images/images/adamp_logo_reset_adam_blink.png"));

}

void MainWindow::onToggleResetCartBlink()
{
   if (!m_resetCartBtn) return;

    // Toggle de checked-status
    bool isChecked = m_resetCartBtn->isChecked();
    m_resetCartBtn->setChecked(!isChecked);

    // Wissel tussen de standaard 'off' icoon en de 'blink' icoon
    m_resetCartBtn->setIcon(QIcon(isChecked
                                      ? ":/images/images/adamp_logo_reset_cartridge_off.png"
                                      : ":/images/images/adamp_logo_reset_cartridge_blink.png"));
}

//---------------------------------------------------------------------------------------------
// BIOS FUNCTIONS
//---------------------------------------------------------------------------------------------

void MainWindow::onBiosStatusUpdated(int colecoExt, int eosExt, int writerExt)
{
    if (!m_actColecoBiosSource || !m_actEosBiosSource || !m_actWriterBiosSource)
        return;

    m_actColecoBiosSource->setText(colecoExt ? "Coleco: External" : "Coleco: Internal");
    m_actEosBiosSource->setText(   eosExt    ? "EOS: External"    : "EOS: Internal");
    m_actWriterBiosSource->setText(writerExt ? "Writer: External" : "Writer: Internal");
}

void MainWindow::mountAdamStartupImageIfNeeded()
{
    if (!m_colecoController)
        return;

    if (m_adamBootMode == AdamBootWriter)
        return;

    const QString startupPath = m_adamStartupPath.trimmed();
    if (startupPath.isEmpty())
        return;

    // Niet over bestaande ADAM media heen gaan. Writer blijft dan netjes Writer,
    // en geladen D1/D5 media blijven de baas. Geen ADAM-chaos-lasagne.
    if (m_ctrlAdamNet || m_AdamTMedia_insert || m_AdamDMedia_insert ||
        !m_loadedTapeNames[0].isEmpty() || !m_loadedDiskNames[0].isEmpty())
        return;

    QFileInfo info(startupPath);
    if (!info.exists()) {
        qWarning() << "[ADAM][BOOT] Startup image not found:" << startupPath;
        return;
    }

    const QString ext = info.suffix().toLower();
    CustomFileDialog::s_lastOpenDir = info.absolutePath();

    if (ext == "ddp") {
        m_loadedTapeNames[0] = info.fileName();
        m_AdamTMedia_insert = true;
        m_resetAdamLocked = true;

        QMetaObject::invokeMethod(
            m_colecoController,
            "loadTape",
            Qt::QueuedConnection,
            Q_ARG(int, 0),
            Q_ARG(QString, info.absoluteFilePath())
            );

        qDebug() << "[ADAM][BOOT]"
                 << (m_adamBootMode == AdamBootBasicImage ? "BASIC DDP" : "Startup DDP")
                 << "mounted on D1:" << info.absoluteFilePath();
    }
    else if (ext == "dsk" || ext == "img") {
        m_loadedDiskNames[0] = info.fileName();
        m_AdamDMedia_insert = true;
        m_resetAdamLocked = true;

        QMetaObject::invokeMethod(
            m_colecoController,
            "loadDisk",
            Qt::QueuedConnection,
            Q_ARG(int, 0),
            Q_ARG(QString, info.absoluteFilePath())
            );

        qDebug() << "[ADAM][BOOT]"
                 << (m_adamBootMode == AdamBootBasicImage ? "BASIC DSK" : "Startup DSK")
                 << "mounted on D5:" << info.absoluteFilePath();
    }
    else {
        qWarning() << "[ADAM][BOOT] Unsupported startup image type:" << startupPath;
    }
}

void MainWindow::loadExternalBiosRoms()
{

    // 2. Stuurt de opgeslagen paden van MainWindow naar de controller thread.
    // De controller roept coleco_initialise() aan en reset de emulator.
    QMetaObject::invokeMethod(
        m_colecoController,
        "loadBiosRoms",
        Qt::QueuedConnection,
        Q_ARG(QString, m_colecoBiosPath),
        Q_ARG(QString, m_eosBiosPath),
        Q_ARG(QString, m_writerBiosPath)
        );
}

void MainWindow::onToggleDTsound(bool checked)
{
    m_useDTsound = checked;

    if (m_colecoController) {
        QMetaObject::invokeMethod(
            m_colecoController,
            "setDTsoundEnabled",
            Qt::QueuedConnection,
            Q_ARG(bool, checked)
            );
    }

    saveSettings();
    qDebug() << "[UI] Disc/Tape sounds =" << (checked ? "ON" : "OFF");
}

//---------------------------------------------------------------------------------------------
// OS SYSTEM
//---------------------------------------------------------------------------------------------

void MainWindow::configurePlatformSettings()
{
    qDebug() << "[UI] --- START EMULATOR ---";

#if defined(Q_OS_WIN)
    qDebug() << "[UI] WINDOWS SYSTEM";
#elif defined(Q_OS_LINUX)
    qDebug() << "[UI] LINUX SYSTEM";
#else
    qDebug() << "[UI] UNKNOW SYSTEM";
#endif
}

//---------------------------------------------------------------------------------------------

void MainWindow::onShowDebugTerminal()
{
    if (!m_debugTerminal)
    {
        m_commandProcessor = new CommandProcessor(this);

        m_commandProcessor->setStartCallback([this]() -> QString {
            onRunStop();
            return "Start/Run command sent.";
        });

        m_commandProcessor->setStopCallback([this]() -> QString {
            onRunStop();
            return "Stop/Pause command sent.";
        });

        m_commandProcessor->setResetAdamCallback([this]() -> QString {
            onResetAdamBtnClicked();
            return "Reset ADAM command sent.";
        });

        m_commandProcessor->setResetColecoCallback([this]() -> QString {
            onResetCartBtnClicked();
            return "Reset Coleco command sent.";
        });

        m_commandProcessor->setPowerOffCallback([this]() -> QString {
            onPowerBtnClicked();
            return "PowerOff command sent.";
        });

        m_commandProcessor->setInjectCallback([this](const QString& text) -> QString {
            QByteArray filtered;
            const QByteArray source = text.toLatin1();
            filtered.reserve(source.size() + 1);

            for (const char raw : source) {
                const unsigned char ch = static_cast<unsigned char>(raw);
                if (ch == '\n' || ch == '\r')
                    filtered.append('\r');
                else if (ch == '\t')
                    filtered.append(' ');
                else if (ch >= 32 && ch <= 126)
                    filtered.append(static_cast<char>(ch));
            }

            if (filtered.isEmpty())
                return "Nothing to inject.";

            if (!filtered.endsWith('\r'))
                filtered.append('\r');

            onStopBasicInject();
            m_basicInjectData = filtered;
            m_basicInjectPosition = 0;

            if (m_actStartBasicInject)
                m_actStartBasicInject->setEnabled(false);
            if (m_actStopBasicInject)
                m_actStopBasicInject->setEnabled(true);

            qDebug() << "[INJECT] Monitor text started:" << text;
            m_basicInjectTimer->start(1);
            return QString("Inject started: \"%1\"").arg(text);
        });

        m_commandProcessor->setMemoryReadCallback([](uint32_t address, uint8_t& value) -> bool {
            value = coleco_ReadByte(static_cast<word>(address & 0xFFFF));
            return true;
        });

        m_commandProcessor->setMemoryReadCallback([](uint32_t address, uint8_t& value) -> bool {
            value = coleco_ReadByte(static_cast<uint16_t>(address & 0xFFFF));
            return true;
        });


        // Terminal command: dasm <address> <length>
        // De tweede parameter is dus AANTAL BYTES, niet langer een eindadres.
        m_commandProcessor->setDisasmCallback([](uint16_t from, uint16_t length) -> QString {
            if (length == 0)
                return "Invalid length. Usage: dasm <address> <length>";

            QString out;

            const uint32_t end32 = static_cast<uint32_t>(from) + static_cast<uint32_t>(length) - 1u;
            const uint16_t to = static_cast<uint16_t>(end32 > 0xFFFFu ? 0xFFFFu : end32);

            uint16_t cur = from;

            while (cur <= to)
            {
                const uint16_t addr = cur;

                int oplen = 0;
                QString instr = disasmOneAt(cur, oplen);
                instr.replace(QRegularExpression("\\$([0-9A-Fa-f]{4})\\$+"), "$\\1");

                // Zelfde veiligheid als in DebuggerWindow
                if (oplen <= 0 || oplen > 4)
                    oplen = 1;

                QString bytesStr;

                for (int b = 0; b < oplen; ++b)
                {
                    const uint16_t byteAddr = static_cast<uint16_t>(addr + b);

                    if (byteAddr > to)
                        break;

                    bytesStr += QString("%1 ")
                                    .arg(coleco_ReadByte(byteAddr), 2, 16, QChar('0'))
                                    .toUpper();
                }

                bytesStr = bytesStr.trimmed();
                bytesStr = bytesStr.leftJustified(12, ' ');

                out += QString("%1: %2 %3\n")
                           .arg(addr, 4, 16, QChar('0'))
                           .arg(bytesStr)
                           .arg(instr)
                           .toUpper();

                if (addr == 0xFFFF)
                    break;

                uint16_t next = static_cast<uint16_t>(addr + oplen);

                if (next <= addr)
                    break;

                cur = next;
            }

            return out.trimmed();
        });


        m_commandProcessor->setCpuRegsCallback([]() -> QString {
            return QString(
                       "PC=%1  SP=%2\n"
                       "AF=%3  BC=%4  DE=%5  HL=%6\n"
                       "IX=%7  IY=%8\n"
                       "AF'=%9  BC'=%10  DE'=%11  HL'=%12\n"
                       "I=%13  R=%14"
                       )
                .arg(Z80.pc.w.l, 4, 16, QChar('0'))
                .arg(Z80.sp.w.l, 4, 16, QChar('0'))
                .arg(Z80.af.w.l, 4, 16, QChar('0'))
                .arg(Z80.bc.w.l, 4, 16, QChar('0'))
                .arg(Z80.de.w.l, 4, 16, QChar('0'))
                .arg(Z80.hl.w.l, 4, 16, QChar('0'))
                .arg(Z80.ix.w.l, 4, 16, QChar('0'))
                .arg(Z80.iy.w.l, 4, 16, QChar('0'))
                .arg(Z80.af2.w.l, 4, 16, QChar('0'))
                .arg(Z80.bc2.w.l, 4, 16, QChar('0'))
                .arg(Z80.de2.w.l, 4, 16, QChar('0'))
                .arg(Z80.hl2.w.l, 4, 16, QChar('0'))
                .arg(Z80.i , 2, 16, QChar('0'))
                .arg(Z80.r , 2, 16, QChar('0'))
                .toUpper();
        });

        m_commandProcessor->setMemoryWriteCallback([](uint32_t address, uint8_t value) -> bool {
            const uint32_t addr = address & 0xFFFF;

            const int page = (addr >> 13) & 0x07;
            const int offs = addr & 0x1FFF;

            if (!MemoryMap[page])
                return false;

            *(MemoryMap[page] + offs) = value;

            qDebug().noquote() << QString("[MONITOR POKE] Z80 addr=%1 page=%2 offs=%3 data=%4 phys=%5")
                                      .arg(addr, 4, 16, QChar('0'))
                                      .arg(page)
                                      .arg(offs, 4, 16, QChar('0'))
                                      .arg(value, 2, 16, QChar('0'))
                                      .arg((quintptr)(MemoryMap[page]), 0, 16)
                                      .toUpper();

            return true;
        });

        m_debugTerminal = new DebugTerminalWidget(m_commandProcessor, nullptr);
        m_debugTerminal->setEmulatorPaused(m_isPaused);

        m_debugTerminal->setWindowFlags(Qt::Window);
        m_debugTerminal->setWindowTitle("Terminal");
        m_debugTerminal->resize(650, 500);
    }

    if (m_debugTerminal->isVisible())
    {
        m_debugTerminal->hide();
    }
    else
    {
        // Center terminal on current main emulator window position
        const QRect mainFrame = frameGeometry();
        const QSize terminalSize = m_debugTerminal->size();

        QPoint centeredPos(
            mainFrame.center().x() - (terminalSize.width() / 2),
            mainFrame.center().y() - (terminalSize.height() / 2)
            );

        m_debugTerminal->move(centeredPos);
        m_debugTerminal->setEmulatorPaused(m_isPaused);

        m_debugTerminal->show();
        m_debugTerminal->raise();
        m_debugTerminal->activateWindow();
    }
}

//---------------------------------------------------------------------------------------------
void MainWindow::onShowCvBasicEditor()
{
    if (!m_cvBasicEditor) {
        m_cvBasicEditor = new CvBasicEditorWindow(this);

        // CVBasicEditorWindow mag zelf geen paden kiezen.
        // Altijd de waarden gebruiken die MainWindow uit settings.ini geladen heeft.
        m_cvBasicEditor->setToolPaths(
            m_cvbasicExePath,
            m_gasm80ExePath,
            m_cvbasicBuildPath,
            m_cvbasicSourcePath
        );

        connect(m_cvBasicEditor, &CvBasicEditorWindow::romBuilt,
                this, &MainWindow::onCvBasicRomBuilt);

        // Sound Editor live preview.
        // De bridge houdt windows.h / dsound.h uit maingui.cpp,
        // zodat er geen PrintWindow/DWORD/ULONG conflicten ontstaan.
        if (!m_soundPreviewBridge) {
            m_soundPreviewBridge = createSoundPreviewBridge(
                m_cvBasicEditor,
                winId(),
                this
            );
        }
    }

    // Ook bij opnieuw openen synchroniseren, voor het geval settings gewijzigd zijn
    // terwijl het editorvenster al bestond.
    m_cvBasicEditor->setToolPaths(
        m_cvbasicExePath,
        m_gasm80ExePath,
        m_cvbasicBuildPath,
        m_cvbasicSourcePath
    );
    m_cvBasicEditor->setDarkTheme(m_darkTheme);

    m_cvBasicEditor->show();
    m_cvBasicEditor->raise();
    m_cvBasicEditor->activateWindow();
}
//---------------------------------------------------------------------------------------------
void MainWindow::loadColecoRomFromPath(const QString& filePath, bool autoRun)
{
    if (filePath.isEmpty())
        return;

    if (m_machineType != 0) {
        /* Reproduce the proven manual sequence exactly.  Switching ADAM to
         * Coleco queues a hard core initialisation; arming/loading the ROM in
         * the same GUI turn races that initialisation and leaves a black
         * screen.  Finish the machine switch first, then perform the normal
         * media-map cartridge flow in a fresh turn. */
        switchToColecoMode();
        qDebug() << "[UI][BOOT] waiting for real Coleco mode before arming ROM:"
                 << filePath;
        QTimer::singleShot(750, this, [this, filePath, autoRun]() {
            loadColecoRomFromPath(filePath, autoRun);
        });
        return;
    }

    QFileInfo fi(filePath);

    /* Reset Cartridge historically reconstructs the active ROM path from
     * s_lastOpenDir + m_currentRomName.  A generated FujiNet ROM did not pass
     * through the file dialog, so retain its actual directory explicitly. */
    CustomFileDialog::s_lastOpenDir = fi.absolutePath();

    m_pendingColecoRomPath = filePath;
    m_pendingColecoBoot = true;

    m_currentRomName = fi.fileName();
    m_currentARomName.clear();

    m_isColecoRomLoaded = true;
    m_isAdamRomLoaded = false;
    m_ColecoMedia_insert = true;
    m_resetColecoLocked = false;

    updateRomLabelForStatusBar(
        statusBar(),
        m_sepLabel4,
        m_romLabel,
        "Pending cart: " + m_currentRomName
        );

    updateMediaMenuState();
    updateMediaStatusLabels();
    updateHardwareWindowMediaDisplay();

    if (autoRun)
        onResetCartBtnClicked();
}
//---------------------------------------------------------------------------------------------
void MainWindow::onCvBasicRomBuilt(const QString& romPath)
{
    loadColecoRomFromPath(romPath, true);
}
//---------------------------------------------------------------------------------------------
