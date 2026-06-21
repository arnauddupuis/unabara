#ifndef TEMPLATE_UNDO_STACK_H
#define TEMPLATE_UNDO_STACK_H

#include <QObject>
#include <QList>
#include <QTimer>
#include "include/core/overlay_template.h"

class OverlayGenerator;

// Snapshot-based undo/redo for the template editor.
//
// The OverlayGenerator already round-trips all template content through
// exportTemplate()/loadTemplate(), so an undo history is just a stack of
// OverlayTemplate snapshots. This class listens to the generator's
// content-change signals and, after a short debounce, records the *previous*
// state. Debouncing coalesces continuous edits (opacity slider, font-size
// spinbox) into a single undo entry per gesture.
//
// Invariant: m_baseline equals the current generator state whenever the
// coalesce timer is not pending.
class TemplateUndoStack : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY canRedoChanged)

public:
    explicit TemplateUndoStack(OverlayGenerator* generator, QObject* parent = nullptr);

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    // Connect a generator content-change signal to the debounced recorder.
    // Called once per tracked signal from main.cpp.
    void trackSignal(const char* signal);

public slots:
    void undo();
    void redo();

    // Commit any pending change immediately (used internally before undo/redo).
    void flush();

    // Drop all history and re-baseline to the current state. Used at history
    // boundaries (template file load, dive import) so undo never crosses them.
    void resetHistory();

signals:
    void canUndoChanged();
    void canRedoChanged();

private slots:
    void onGeneratorChanged();
    void commit();

private:
    OverlayGenerator* m_generator;
    QList<Unabara::OverlayTemplate> m_undoStack;
    QList<Unabara::OverlayTemplate> m_redoStack;
    Unabara::OverlayTemplate m_baseline;
    QTimer m_coalesceTimer;
    bool m_restoring = false;

    static constexpr int kMaxDepth = 50;
    static constexpr int kCoalesceMs = 300;
};

#endif // TEMPLATE_UNDO_STACK_H
