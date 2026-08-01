#ifndef CVBASICEDITORWINDOW_H
#define CVBASICEDITORWINDOW_H

#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <QProcess>
#include <QVariantList>
#include <QVector>

class QAction;
class QLabel;
class QPlainTextEdit;
class QPrinter;
class QSettings;
class QCloseEvent;
class QSyntaxHighlighter;
class QTextDocument;
class QTabWidget;
class QListWidget;
class QListWidgetItem;
class QTableWidget;
class QSplitter;

class CvBasicEditorWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit CvBasicEditorWindow(QWidget* parent = nullptr);
    ~CvBasicEditorWindow() override;

    // MainWindow is de enige bron voor deze paden.
    // De waarden komen uit settings.ini; de editor maakt zelf geen fallback-paden meer.
    void setToolPaths(const QString& cvbasicExe,
                      const QString& gasm80Exe,
                      const QString& buildDir,
                      const QString& sourceDir);

signals:
    // Wordt verzonden wanneer gasm80 succesvol een .rom heeft gemaakt.
    // MainWindow kan hierop reageren en de ROM automatisch laden/runnen.
    void romBuilt(const QString& romPath);

    // Sound Editor preview request.
    // MainWindow / emulator audio layer can connect this to a PSG preview function.
    // waveX/waveY are the visual instrument shape parameters, 0..100.
    void soundPreviewRequested(int channel, int psgPeriod, int volume, int instrumentEnv, int waveX, int waveY);

    // Hard stop for Sound Editor preview playback.
    // Used when STOP is pressed so SoundManager can clear its audio ringbuffer.
    void soundPreviewStopAllRequested();

    // Real Sound Editor stream player.
    // Rows are QVariantList entries with 12 ints:
    // CH1 period,vol,env | CH2 period,vol,env | CH3 period,vol,env | NOISE code,vol,env
    // period/code -1 means HOLD previous state.
    void soundEditorStreamPlayRequested(const QVariantList& rows, int rowMs, bool loop);
    void soundEditorStreamStopRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void newFile();
    void openFile();
    bool saveFile();
    bool saveFileAs();

    void printSource();

    void compileOnly();
    void compileAndRun();

    void chooseCvBasicExe();
    void chooseGasm80Exe();
    void openBuildFolder();
    void showAboutDialog();

    void editUndo();
    void editRedo();
    void editCut();
    void editCopy();
    void editPaste();
    void findText();
    void findNext();
    void replaceText();
    void toggleFoldLines(bool checked);
    void toggleConsole(bool checked);
    void toggleShortcuts(bool checked);
    void resetConsole();
    void showBasicEditor();
    void showSpriteEditor();
    void showSoundEditor();
    void showPaintEditor();

    void onEditorTextChanged();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessReadyReadStdout();
    void onProcessReadyReadStderr();
    void onLabelItemDoubleClicked(QListWidgetItem* item);
    void onProcedureItemDoubleClicked(QListWidgetItem* item);

public slots:
    // Called by SoundPreviewBridge/SoundManager for visual VU-meter feedback.
    void setSoundChannelVuLevel(int channel, int level);
    void setSoundPreviewVuLevels(int ch1, int ch2, int ch3, int noise);

private:
    enum class BuildStep {
        Idle,
        CvBasic,
        Gasm80
    };

    void setupUi();
    void setupActions();
    void setupMenusAndToolbar();
    void setupStatusBar();
    void loadSettings();
    void saveSettings();

    bool maybeSaveBeforeDestructiveAction();
    bool writeCurrentFile(const QString& filePath);
    bool readFile(const QString& filePath);
    bool ensureSourceFileForBuild();
    bool ensureToolExists(const QString& path, const QString& toolName);
    bool prepareBuildPaths();

    QPlainTextEdit* activeEditor() const;
    QPlainTextEdit* sourceEditorAt(int index) const;
    int sourceTabCount() const;
    QString sourceTabName(int index) const;
    void setSourceTabName(int index, const QString& name);
    void addSourceTab(const QString& name, const QString& text = QString(), bool makeCurrent = true);
    void newSourceTab();
    void renameCurrentSourceTab();
    void closeCurrentSourceTab();
    void deleteCurrentSourceTab();
    void setupSourceEditorContextMenu(QPlainTextEdit* editor);
    void setupAllSourceEditorContextMenus();
    void clearSourceTabs();
    struct BuildLineInfo {
        int tabIndex = -1;
        int localLine = -1;
        QString tabName;
    };

    QString buildCombinedSource();
    BuildLineInfo sourceLineForCombinedLine(int combinedLine) const;
    bool writeCombinedSourceForBuild();

    void startBuild(bool runAfterBuild);
    void startCvBasic();
    void startGasm80();
    void finishBuildSuccess();
    void finishBuildFailed(const QString& reason);
    void resetBuildState();

    void appendOutput(const QString& text);
    void appendError(const QString& text);
    void setCurrentFile(const QString& filePath);
    void updateWindowTitle();
    void updateStatusText(const QString& text);
    void updateCursorStatus();
    void updateKeyStatus();
    void updateSidePanels();
    void addErrorRow(const QString& description, int line);
    void gotoSourceLine(int lineNumber);
    void refreshBasicEditorLayout();
    QString appRelativePath(const QString& relative) const;
    QString quotedNativePath(const QString& path) const;
    QPrinter* printer();
    void printSourceOnPrinter(QPrinter* p, bool showPrintDialog);

private:
    QTabWidget* m_mainPages = nullptr;
    QWidget* m_basicPage = nullptr;
    QWidget* m_spritePage = nullptr;
    QWidget* m_soundPage = nullptr;
    QWidget* m_paintPage = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTabWidget* m_codeTabs = nullptr;
    QPlainTextEdit* m_editor = nullptr;
    QPlainTextEdit* m_helpText = nullptr;
    QPlainTextEdit* m_output = nullptr;

    QListWidget* m_labelsList = nullptr;
    QListWidget* m_proceduresList = nullptr;
    QTableWidget* m_errorTable = nullptr;
    QWidget* m_bottomPanel = nullptr;
    QWidget* m_sidePanel = nullptr;
    QWidget* m_lineNumberAreaWidget = nullptr;

    QLabel* m_statusLabel = nullptr;
    QLabel* m_cursorStatusLabel = nullptr;
    QLabel* m_capsLabel = nullptr;
    QLabel* m_numLabel = nullptr;
    QLabel* m_insLabel = nullptr;
    QLabel* m_scrlLabel = nullptr;
    QLabel* m_errorsLabel = nullptr;
    QLabel* m_warningsLabel = nullptr;

    QAction* m_actNew = nullptr;
    QAction* m_actOpen = nullptr;
    QAction* m_actSave = nullptr;
    QAction* m_actSaveAs = nullptr;
    QAction* m_actPrint = nullptr;
    QAction* m_actNewTab = nullptr;
    QAction* m_actRenameTab = nullptr;
    QAction* m_actCloseTab = nullptr;
    QAction* m_actDeleteTab = nullptr;
    QAction* m_actUndo = nullptr;
    QAction* m_actRedo = nullptr;
    QAction* m_actCut = nullptr;
    QAction* m_actCopy = nullptr;
    QAction* m_actPaste = nullptr;
    QAction* m_actFind = nullptr;
    QAction* m_actFindNext = nullptr;
    QAction* m_actReplace = nullptr;
    QAction* m_actViewFoldLines = nullptr;
    QAction* m_actViewConsole = nullptr;
    QAction* m_actViewShortcuts = nullptr;
    QAction* m_actResetConsole = nullptr;
    QAction* m_actBasicEditor = nullptr;
    QAction* m_actSpriteEditor = nullptr;
    QAction* m_actSoundEditor = nullptr;
    QAction* m_actPaintEditor = nullptr;
    QAction* m_actCompile = nullptr;
    QAction* m_actCompileRun = nullptr;
    QAction* m_actChooseCvBasic = nullptr;
    QAction* m_actChooseGasm80 = nullptr;
    QAction* m_actOpenBuildFolder = nullptr;
    QAction* m_actAbout = nullptr;

    QSyntaxHighlighter* m_highlighter = nullptr;
    QProcess* m_process = nullptr;
    QPrinter* m_printer = nullptr;

    QString m_currentFile;
    QString m_buildDirPath;
    QString m_cvbasicExePath;
    QString m_gasm80ExePath;
    QString m_lastOpenDir;

    QString m_asmPath;
    QString m_romPath;
    QString m_buildSourcePath;
    QString m_lastFindText;
    QVector<BuildLineInfo> m_buildLineMap;

    int m_errorCount = 0;
    int m_warningCount = 0;

    bool m_capsLockOn = false;
    bool m_numLockOn = false;
    bool m_scrollLockOn = false;

    bool m_dirty = false;
    bool m_runAfterBuild = false;
    BuildStep m_buildStep = BuildStep::Idle;
};

#endif // CVBASICEDITORWINDOW_H
