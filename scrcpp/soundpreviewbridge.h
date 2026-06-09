#ifndef SOUNDPREVIEWBRIDGE_H
#define SOUNDPREVIEWBRIDGE_H

#include <QObject>
#include <QtGlobal>

class CvBasicEditorWindow;

// Creates a SoundManager-backed preview connection for the CVBasic Sound Editor.
// Kept in a separate bridge so maingui.cpp does not need to include windows.h/dsound.h.
QObject* createSoundPreviewBridge(CvBasicEditorWindow* editor, quintptr hostWindowId, QObject* parent);

#endif // SOUNDPREVIEWBRIDGE_H
