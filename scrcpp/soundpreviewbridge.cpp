#include "soundpreviewbridge.h"

#include "cvbasiceditorwindow.h"
#include "soundmanager.h"
#include "soundeditorplayer.h"

#include <QDebug>

QObject* createSoundPreviewBridge(CvBasicEditorWindow* editor, quintptr hostWindowId, QObject* parent)
{
    if (!editor)
        return nullptr;

    SoundManager* soundManager = new SoundManager(parent);

#if defined(Q_OS_WIN)
    if (!soundManager->initialise(reinterpret_cast<HWND>(hostWindowId), 60)) {
        qWarning() << "[SoundPreviewBridge] SoundManager initialise failed";
    }

    // De preview SoundManager mag niet al loopen na initialise().
    // Anders slaat startDirectSoundPreviewIfNeeded() zijn prefill over.
    soundManager->hardStopPreviewAudio();
#elif defined(Q_OS_LINUX)
    if (!soundManager->initialise(60)) {
        qWarning() << "[SoundPreviewBridge] SoundManager initialise failed";
    }
#endif

    SoundEditorPlayer* soundEditorPlayer = new SoundEditorPlayer(soundManager);

    QObject::connect(editor, &CvBasicEditorWindow::soundPreviewRequested,
                     soundManager, &SoundManager::previewPsgNote,
                     Qt::UniqueConnection);

    QObject::connect(editor, &CvBasicEditorWindow::soundPreviewStopAllRequested,
                     soundManager, &SoundManager::hardStopPreviewAudio,
                     Qt::UniqueConnection);

    QObject::connect(editor, &CvBasicEditorWindow::soundEditorStreamPlayRequested,
                     soundEditorPlayer, &SoundEditorPlayer::startSongStream,
                     Qt::UniqueConnection);

    QObject::connect(editor, &CvBasicEditorWindow::soundEditorStreamStopRequested,
                     soundEditorPlayer, &SoundEditorPlayer::hardReset,
                     Qt::UniqueConnection);

    QObject::connect(soundManager, &SoundManager::previewVuMeterChanged,
                     editor, &CvBasicEditorWindow::setSoundChannelVuLevel,
                     Qt::UniqueConnection);

    QObject::connect(soundManager, &SoundManager::previewVuMetersChanged,
                     editor, &CvBasicEditorWindow::setSoundPreviewVuLevels,
                     Qt::UniqueConnection);

    QObject::connect(soundEditorPlayer, &SoundEditorPlayer::previewVuMeterChanged,
                     editor, &CvBasicEditorWindow::setSoundChannelVuLevel,
                     Qt::UniqueConnection);

    QObject::connect(soundEditorPlayer, &SoundEditorPlayer::previewVuMetersChanged,
                     editor, &CvBasicEditorWindow::setSoundPreviewVuLevels,
                     Qt::UniqueConnection);

    return soundManager;
}
