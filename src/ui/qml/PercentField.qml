import QtQuick
import QtQuick.Controls

// One hardened percent-entry field (0.0–100.0, 1 decimal) for the Overlay
// Positions section. A TextField, NOT a SpinBox: a SpinBox with a
// non-identity value mapping (percent-tenths) is at the mercy of the
// style's own text→value plumbing — the KDE style parses typed text as a
// plain integer, turning a typed "50" into 5.0% (observed live). The
// videoOffsetSpinBox precedent survives only because its mapping is
// identity. Hardening kept from that precedent: unparseable text keeps the
// current value, commits on editingFinished / focus loss, and the display
// is re-synced imperatively after EVERY commit attempt — a Binding element
// alone cannot undo a typed value the clamp rejected back to the unchanged
// model value (documented guardrail). Up/Down keys nudge by 0.1.
TextField {
    id: root

    // The model-derived value in percent. A plain binding: it re-evaluates
    // on the owner's NOTIFY (clip switch, committed layout change) and
    // onModelValueChanged re-displays, wiping half-typed text so it cannot
    // commit onto a different clip.
    property real modelValue: 0
    // Clamp bounds in percent, owner-supplied (e.g. X's max is 100 − W).
    property real minPercent: 0
    property real maxPercent: 100
    // Fires with the new percent ONLY when the quantized (1-decimal) value
    // differs from the quantized model value — tabbing through unchanged
    // fields must never rewrite the stored layout (re-quantization would
    // count as a change and broadcast a subpixel nudge).
    signal committed(real percent)

    property real shownPercent: 0

    horizontalAlignment: TextInput.AlignRight
    validator: DoubleValidator {
        bottom: root.minPercent
        top: root.maxPercent
        decimals: 1
        notation: DoubleValidator.StandardNotation
    }
    inputMethodHints: Qt.ImhFormattedNumbersOnly

    function quantized(p) {
        return Math.round(p * 10) / 10
    }

    function showValue() {
        shownPercent = quantized(root.modelValue)
        text = Number(shownPercent).toLocaleString(locale, 'f', 1)
    }

    function commitPercent(p) {
        if (!isNaN(p)) {
            var q = quantized(Math.max(minPercent, Math.min(maxPercent, p)))
            if (q !== quantized(root.modelValue))
                root.committed(q)
        }
        showValue()
    }

    // Unparseable text ("" or a lone "-") means "keep the current value".
    function commitText() {
        var v = NaN
        try {
            v = Number.fromLocaleString(locale, text)
        } catch (e) {
            v = NaN
        }
        commitPercent(v)
    }

    Component.onCompleted: showValue()
    onModelValueChanged: {
        if (quantized(modelValue) !== shownPercent)
            showValue()
    }

    onEditingFinished: commitText()
    // editingFinished needs acceptable input; this also restores the
    // display after "" or "-". A no-change commit is a no-op, so the pair
    // firing together commits once.
    onActiveFocusChanged: {
        if (!activeFocus)
            commitText()
    }

    Keys.onUpPressed: commitPercent(quantized(root.modelValue) + 0.1)
    Keys.onDownPressed: commitPercent(quantized(root.modelValue) - 0.1)
    Keys.onEscapePressed: {
        showValue()
        focus = false
    }
}
