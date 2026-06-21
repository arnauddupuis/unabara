#include "include/generators/template_undo_stack.h"
#include "include/generators/overlay_gen.h"

TemplateUndoStack::TemplateUndoStack(OverlayGenerator* generator, QObject* parent)
    : QObject(parent)
    , m_generator(generator)
{
    m_coalesceTimer.setSingleShot(true);
    m_coalesceTimer.setInterval(kCoalesceMs);
    connect(&m_coalesceTimer, &QTimer::timeout, this, &TemplateUndoStack::commit);

    // Baseline the current generator state. Tracked signals are connected by
    // main.cpp via trackSignal() after construction, so nothing fires here.
    if (m_generator)
        m_baseline = m_generator->exportTemplate();
}

void TemplateUndoStack::trackSignal(const char* signal)
{
    if (m_generator)
        connect(m_generator, signal, this, SLOT(onGeneratorChanged()));
}

void TemplateUndoStack::onGeneratorChanged()
{
    // Ignore signals emitted by our own loadTemplate() during undo/redo.
    if (m_restoring)
        return;
    // Coalesce rapid changes into a single entry; (re)start the debounce.
    m_coalesceTimer.start();
}

void TemplateUndoStack::commit()
{
    if (!m_generator)
        return;

    m_coalesceTimer.stop();

    const bool hadRedo = !m_redoStack.isEmpty();
    const bool wasEmpty = m_undoStack.isEmpty();

    // Record the prior state and clear any redo branch — a new edit invalidates
    // the redo history.
    m_undoStack.append(m_baseline);
    m_redoStack.clear();
    m_baseline = m_generator->exportTemplate();

    // Bound memory: drop the oldest entries beyond the cap.
    while (m_undoStack.size() > kMaxDepth)
        m_undoStack.removeFirst();

    if (wasEmpty)
        emit canUndoChanged();
    if (hadRedo)
        emit canRedoChanged();
}

void TemplateUndoStack::flush()
{
    if (m_coalesceTimer.isActive())
        commit();
}

void TemplateUndoStack::undo()
{
    if (!m_generator)
        return;

    // Commit any in-progress edit so it becomes the first thing we undo.
    flush();

    if (m_undoStack.isEmpty())
        return;

    const bool redoWasEmpty = m_redoStack.isEmpty();

    const Unabara::OverlayTemplate current = m_generator->exportTemplate();
    const Unabara::OverlayTemplate prev = m_undoStack.takeLast();
    m_redoStack.append(current);

    m_restoring = true;
    m_generator->loadTemplate(prev);
    m_restoring = false;

    m_baseline = prev;

    if (m_undoStack.isEmpty())
        emit canUndoChanged();
    if (redoWasEmpty)
        emit canRedoChanged();
}

void TemplateUndoStack::redo()
{
    if (!m_generator)
        return;

    // A pending edit must win over the redo branch, so commit it first; that
    // clears the redo stack and redo() below becomes a no-op (correct).
    flush();

    if (m_redoStack.isEmpty())
        return;

    const bool undoWasEmpty = m_undoStack.isEmpty();

    const Unabara::OverlayTemplate current = m_generator->exportTemplate();
    const Unabara::OverlayTemplate next = m_redoStack.takeLast();
    m_undoStack.append(current);

    m_restoring = true;
    m_generator->loadTemplate(next);
    m_restoring = false;

    m_baseline = next;

    if (m_redoStack.isEmpty())
        emit canRedoChanged();
    if (undoWasEmpty)
        emit canUndoChanged();
}

void TemplateUndoStack::resetHistory()
{
    if (!m_generator)
        return;

    m_coalesceTimer.stop();

    const bool hadUndo = !m_undoStack.isEmpty();
    const bool hadRedo = !m_redoStack.isEmpty();

    m_undoStack.clear();
    m_redoStack.clear();
    m_baseline = m_generator->exportTemplate();

    if (hadUndo)
        emit canUndoChanged();
    if (hadRedo)
        emit canRedoChanged();
}
