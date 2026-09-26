@file:JvmName("NativeVtCore")

package com.apex.agent.vtnative

// ═══════════════════════════════════════════════════════════════════════════
// NativeVtCore — JNI wrapper over libvt_native.so (apex-vt-native engine).
//
// REFERENCE IMPLEMENTATION — this file is vendored into the Android-Guru-Agent
// `:terminal-native` Gradle module, where the types below resolve:
//   com.apex.agent.terminalemulator.TerminalEngine      (interface)
//   com.apex.agent.terminalemulator.RenderCell / TerminalRenderSnapshot
//   com.apex.agent.terminalemulator.ScreenMutation / MutationType
//   com.apex.agent.terminalemulator.CursorStyle
//
// v0.2 foundation additions (native-only; the JVM TerminalCore fallback
// reports them as unsupported via TerminalEngine's default methods):
//   * search / selection / hyperlinks (OSC 8) — global-row addressing
//   * session persistence (save/restore across process death)
//   * input encoders — key (DECCKM/DECKPAM/modifyOtherKeys/Kitty), paste
//     (anti-injection brackets), mouse (4 modes × 4 encodings), focus, wheel
//
// Design:
//  * feed() is a single JNI call per PTY read (critical array pinning).
//  * renderSnapshot() is ONE JNI call returning a flat IntArray — the object
//    model is built on the JVM side (bounded by rows*cols per 33ms frame).
//    v0.2 header: 32 ints; cell tuple: cp, fg, bg, flags, link, nComb, combs.
//  * responseSink: the native engine buffers DA/DSR/DECRQM self-generated
//    responses; the wrapper polls them after every feed()/flush().
//  * Lifecycle: create() → handle; destroy() from finalize() AND close().
// ═══════════════════════════════════════════════════════════════════════════

import com.apex.agent.terminalemulator.CursorStyle
import com.apex.agent.terminalemulator.MutationType
import com.apex.agent.terminalemulator.RenderCell
import com.apex.agent.terminalemulator.ScreenMutation
import com.apex.agent.terminalemulator.TerminalEngine
import com.apex.agent.terminalemulator.TerminalRenderSnapshot

/** A search hit in GLOBAL row coordinates (0 == oldest retained row). */
data class NativeSearchMatch(
    val startRow: Long,
    val startCol: Int,
    val endRow: Long,
    val endCol: Int
)

/** Mouse tracking modes negotiated by the application (DECSET 9/1000-1003). */
enum class NativeMouseMode(val id: Int) {
    OFF(0), X10(1), NORMAL(2), BUTTON(3), ANY(4);

    companion object {
        fun of(id: Int): NativeMouseMode = entries.firstOrNull { it.id == id } ?: OFF
    }
}

/** Mouse wire encodings (DECSET 1005 / 1006 / 1015). */
enum class NativeMouseEncoding(val id: Int) {
    X11(0), UTF8(1), SGR(2), URXVT(3);

    companion object {
        fun of(id: Int): NativeMouseEncoding = entries.firstOrNull { it.id == id } ?: X11
    }
}

/** Key identities with dedicated terminal encodings (ordinal == native id). */
enum class NativeKey(val nativeId: Int) {
    UP(1), DOWN(2), RIGHT(3), LEFT(4),
    HOME(5), END(6), PAGE_UP(7), PAGE_DOWN(8), INSERT(9), DELETE(10),
    ENTER(11), TAB(12), BACKSPACE(13), ESCAPE(14), SPACE(15),
    F1(16), F2(17), F3(18), F4(19), F5(20), F6(21), F7(22), F8(23),
    F9(24), F10(25), F11(26), F12(27),
    NUMPAD_0(28), NUMPAD_1(29), NUMPAD_2(30), NUMPAD_3(31), NUMPAD_4(32),
    NUMPAD_5(33), NUMPAD_6(34), NUMPAD_7(35), NUMPAD_8(36), NUMPAD_9(37),
    NUMPAD_DECIMAL(38), NUMPAD_DIVIDE(39), NUMPAD_MULTIPLY(40),
    NUMPAD_SUBTRACT(41), NUMPAD_ADD(42), NUMPAD_ENTER(43), NUMPAD_SEPARATOR(44);
}

/** Modifier bit mask for [NativeVtCore.encodeKey] / [encodeMouseEvent]. */
object NativeModifiers {
    const val SHIFT = 1
    const val ALT = 2
    const val CTRL = 4
    const val META = 8
}

/** Mouse event types (ordinal == native id 0..6). */
enum class NativeMouseEventType(val nativeId: Int) {
    PRESS(0), RELEASE(1), MOTION(2),
    WHEEL_UP(3), WHEEL_DOWN(4), WHEEL_LEFT(5), WHEEL_RIGHT(6);
}

class NativeVtCore(
    initialRows: Int,
    initialCols: Int,
    maxScrollback: Int = 1000
) : TerminalEngine, AutoCloseable {

    companion object {
        init {
            // Throws UnsatisfiedLinkError when the .so is absent — callers
            // (VtEngineFactory) catch it once and fall back to TerminalCore.
            System.loadLibrary("vt_native")
        }

        /**
         * Rehydrate an engine from a [saveSession] blob. Returns null when
         * the blob fails validation (corruption never crashes).
         */
        fun restoreSession(
            data: ByteArray,
            offset: Int = 0,
            length: Int = data.size,
            maxScrollback: Int = 1000
        ): NativeVtCore? {
            val handle = nativeRestoreSession(data, offset, length, maxScrollback)
            if (handle == 0L) return null
            return NativeVtCore(handle)
        }

        private external fun nativeRestoreSession(
            data: ByteArray, off: Int, len: Int, maxScrollback: Int
        ): Long
    }

    /** Secondary constructor from an already-created native handle. */
    private constructor(handle: Long) : this(1, 1, 0) {
        if (this.handle != 0L) nativeDestroy(this.handle)
        this.handle = handle
        this.closed = false
    }

    private var handle: Long = nativeCreate(initialRows, initialCols, maxScrollback)
    private var closed = false

    @Synchronized
    override fun close() {
        if (!closed && handle != 0L) {
            nativeDestroy(handle)
            handle = 0L
            closed = true
        }
    }

    protected fun finalize() = close()

    // ─── feed / lifecycle ───────────────────────────────────────────────

    override fun feed(bytes: ByteArray, offset: Int, length: Int) {
        checkHandle()
        nativeFeed(handle, bytes, offset, length)
        pollResponsesToSink()
    }

    override fun flush() {
        checkHandle()
        nativeFlush(handle)
        pollResponsesToSink()
    }

    override fun resize(newRows: Int, newCols: Int) {
        checkHandle()
        nativeResize(handle, newRows, newCols)
    }

    override fun reset() {
        checkHandle()
        nativeReset(handle)
    }

    // ─── state accessors ───────────────────────────────────────────────

    override val rows: Int get() = header()[0].toInt()
    override val cols: Int get() = header()[1].toInt()

    private fun header(): LongArray {
        checkHandle()
        return nativeHeader(handle)
    }

    override val cursorVisible: Boolean get() = header()[4] != 0L

    // ─── snapshots ─────────────────────────────────────────────────────

    override fun snapshot(): com.apex.agent.terminalemulator.TerminalScreenSnapshot {
        checkHandle()
        val text = nativeRenderedText(handle) ?: ""
        val h = header()
        return com.apex.agent.terminalemulator.TerminalScreenSnapshot(
            rows = h[0].toInt(),
            cols = h[1].toInt(),
            cursorRow = h[2].toInt(),
            cursorCol = h[3].toInt(),
            alternateScreen = h[6] != 0L,
            cursorVisible = h[4] != 0L,
            title = nativeTitle(handle),
            renderedText = text,
            scrollbackLineCount = h[10].toInt()
        )
    }

    // Latest v0.2 snapshot side-state (filled by [renderSnapshot]).
    private var lastLinks: List<String> = emptyList()
    private var lastSelStartRow: Long = -1
    private var lastSelStartCol: Int = 0
    private var lastSelEndRow: Long = -1
    private var lastSelEndCol: Int = 0
    private var lastSearchHitCount: Int = 0
    private var lastActiveSearchHit: Int = -1
    private var lastMouseMode: NativeMouseMode = NativeMouseMode.OFF
    private var lastMouseEncoding: NativeMouseEncoding = NativeMouseEncoding.X11
    private var lastFocusReport = false
    private var lastAltScroll = false
    private var lastApplicationKeypad = false
    private var lastModifyLevel = 0

    override fun renderSnapshot(maxScrollbackLines: Int): TerminalRenderSnapshot {
        checkHandle()
        val flat = nativeSnapshotCells(handle, maxScrollbackLines) ?: IntArray(32)
        fun u64(hi: Int, lo: Int): Long = (hi.toLong() shl 32) or (lo.toLong() and 0xFFFFFFFFL)
        // v0.2 header (32 ints) — see vt_jni.cpp nativeSnapshotCells docs.
        lastLinks = nativeLinks(handle)?.toList() ?: emptyList()
        lastSelStartRow = u64(flat[16], flat[17])
        lastSelStartCol = flat[18]
        lastSelEndRow = u64(flat[19], flat[20])
        lastSelEndCol = flat[21]
        lastSearchHitCount = flat[22]
        lastActiveSearchHit = flat[23]
        lastMouseMode = NativeMouseMode.of(flat[24])
        lastMouseEncoding = NativeMouseEncoding.of(flat[25])
        lastFocusReport = flat[26] != 0
        lastAltScroll = flat[27] != 0
        lastApplicationKeypad = flat[28] != 0
        lastModifyLevel = flat[29]
        var p = 32  // header size
        fun decodeRow(): List<RenderCell> {
            val n = flat[p++]
            if (n == 0) return emptyList()
            val out = ArrayList<RenderCell>(n)
            repeat(n) {
                val cp = flat[p++]
                val fg = flat[p++].toLong() and 0xFFFFFFFFL
                val bg = flat[p++].toLong() and 0xFFFFFFFFL
                val flags = flat[p++]
                val link = flat[p++]  // 1-based URI table index (0 = none)
                val nComb = flat[p++]
                val text = StringBuilder(1 + nComb)
                text.appendCodePoint(if (cp == 0) ' '.code else cp)
                repeat(nComb) { text.appendCodePoint(flat[p++]) }
                out.add(
                    RenderCell(
                        text = text.toString(),
                        fg = fg,
                        bg = bg,
                        flags = flags,
                        link = link
                    )
                )
            }
            return out
        }
        val nVisible = flat[p++]
        val visible = ArrayList<List<RenderCell>>(nVisible)
        repeat(nVisible) { visible.add(decodeRow()) }
        val nSb = flat[p++]
        val scrollback = ArrayList<List<RenderCell>>(nSb)
        repeat(nSb) { scrollback.add(decodeRow()) }
        return TerminalRenderSnapshot(
            rows = flat[0],
            cols = flat[1],
            cursorRow = flat[2],
            cursorCol = flat[3],
            cursorVisible = flat[4] != 0,
            cursorStyle = when (flat[5]) {
                0 -> CursorStyle.BLOCK
                1 -> CursorStyle.UNDERLINE
                else -> CursorStyle.BAR
            },
            alternateScreen = flat[6] != 0,
            applicationCursor = flat[7] != 0,
            bracketedPaste = flat[8] != 0,
            reverseVideo = flat[9] != 0,
            title = nativeTitle(handle),
            lines = visible,
            scrollback = scrollback,
            scrollbackTotal = flat[10],
            scrollbackBase = u64(flat[12], flat[13]),
            bellSeq = u64(flat[14], flat[15])
        )
    }

    // ─── v0.2: search ──────────────────────────────────────────────────

    /** Run a search; returns the hit count. Hits use global-row addressing. */
    fun search(pattern: String, caseInsensitive: Boolean = true, wholeWord: Boolean = false): Int {
        checkHandle()
        return nativeSearch(handle, pattern, caseInsensitive, wholeWord)
    }

    fun clearSearch() {
        checkHandle()
        nativeClearSearch(handle)
    }

    fun searchHitCount(): Int {
        checkHandle()
        return nativeSearchHitCount(handle)
    }

    /** All hits as flat longs [startRow, startCol, endRow, endCol] per hit. */
    fun searchHits(): List<NativeSearchMatch> {
        checkHandle()
        val flat = nativeSearchHits(handle) ?: return emptyList()
        val out = ArrayList<NativeSearchMatch>(flat.size / 4)
        var i = 0
        while (i + 3 < flat.size) {
            out.add(NativeSearchMatch(flat[i], flat[i + 1].toInt(), flat[i + 2], flat[i + 3].toInt()))
            i += 4
        }
        return out
    }

    fun setActiveSearchHit(index: Int) {
        checkHandle()
        nativeSetActiveSearchHit(handle, index)
    }

    fun activeSearchHit(): Int {
        checkHandle()
        return nativeActiveSearchHit(handle)
    }

    // ─── v0.2: selection (touch copy/paste) ─────────────────────────────

    fun beginSelection(globalRow: Long, col: Int) {
        checkHandle()
        nativeBeginSelection(handle, globalRow, col)
    }

    fun extendSelection(globalRow: Long, col: Int) {
        checkHandle()
        nativeExtendSelection(handle, globalRow, col)
    }

    fun clearSelection() {
        checkHandle()
        nativeClearSelection(handle)
    }

    /** Expand to the word around (globalRow, col) — CJK/emoji count as words. */
    fun expandSelectionWord(globalRow: Long, col: Int) {
        checkHandle()
        nativeExpandSelectionWord(handle, globalRow, col)
    }

    /** Expand to the whole logical (wrapped) line around (globalRow, col). */
    fun expandSelectionLine(globalRow: Long, col: Int) {
        checkHandle()
        nativeExpandSelectionLine(handle, globalRow, col)
    }

    /** Selection text: wrapped rows joined without separator, hard ends '\n'. */
    fun selectionText(): String {
        checkHandle()
        return nativeSelectionText(handle) ?: ""
    }

    // ─── v0.2: hyperlinks (OSC 8) ──────────────────────────────────────

    /** URI at a SCREEN position, or null. */
    fun linkAt(screenRow: Int, col: Int): String? {
        checkHandle()
        return nativeLinkAt(handle, screenRow, col)
    }

    /** Active URI table — RenderCell.link is a 1-based index into this list. */
    fun links(): List<String> {
        checkHandle()
        return nativeLinks(handle)?.toList() ?: emptyList()
    }

    // ─── v0.2: session persistence ─────────────────────────────────────

    /**
     * Serialize the main screen + scrollback + modes. Feed the blob to
     * [restoreSession] after process death / activity recreation.
     */
    fun saveSession(maxScrollbackRows: Int = 1000): ByteArray {
        checkHandle()
        return nativeSaveSession(handle, maxScrollbackRows) ?: ByteArray(0)
    }

    // ─── v0.2: input encoders (mode-aware) ─────────────────────────────

    /**
     * Encode a key event against the CURRENT modes (DECCKM / DECKPAM /
     * modifyOtherKeys / Kitty). Empty result = drop the event or use the
     * text path.
     */
    fun encodeKey(key: NativeKey, mods: Int = 0): ByteArray {
        checkHandle()
        return nativeEncodeKey(handle, key.nativeId, mods) ?: ByteArray(0)
    }

    /**
     * Paste text sanitized (ESC/C0 stripped — anti sequence-injection) and
     * bracket-wrapped per mode 2004.
     */
    fun encodePaste(text: String): ByteArray {
        checkHandle()
        return nativeEncodePaste(handle, text) ?: ByteArray(0)
    }

    /**
     * Encode a mouse event against the negotiated tracking mode + encoding.
     * Empty result = not reportable (the event is dropped).
     */
    fun encodeMouseEvent(
        type: NativeMouseEventType,
        button: Int,
        mods: Int,
        col: Int,
        row: Int
    ): ByteArray {
        checkHandle()
        return nativeEncodeMouseEvent(handle, type.nativeId, button, mods, col, row) ?: ByteArray(0)
    }

    /** Focus change (CSI I / CSI O); empty when mode 1004 is off. */
    fun encodeFocus(focused: Boolean): ByteArray {
        checkHandle()
        return nativeEncodeFocus(handle, focused) ?: ByteArray(0)
    }

    /**
     * Physical wheel: routes to mouse encoding when tracking is on, else to
     * arrow keys when alternate scroll (1007) is active on the alt screen.
     * Empty = scroll the viewport instead.
     */
    fun encodeWheel(dir: Int): ByteArray {
        checkHandle()
        return nativeEncodeWheel(handle, dir) ?: ByteArray(0)
    }

    // ─── v0.2: mode mirrors (latest snapshot values) ───────────────────

    val mouseMode: NativeMouseMode get() = lastMouseMode
    val mouseEncoding: NativeMouseEncoding get() = lastMouseEncoding
    val focusReportEnabled: Boolean get() = lastFocusReport
    val altScrollEnabled: Boolean get() = lastAltScroll
    val applicationKeypadMode: Boolean get() = lastApplicationKeypad
    val modifyOtherKeysLevel: Int get() = lastModifyLevel
    val selectionStartRow: Long get() = lastSelStartRow
    val selectionStartCol: Int get() = lastSelStartCol
    val selectionEndRow: Long get() = lastSelEndRow
    val selectionEndCol: Int get() = lastSelEndCol
    val searchHitCountMirror: Int get() = lastSearchHitCount
    val activeSearchHitMirror: Int get() = lastActiveSearchHit

    // ─── drains ────────────────────────────────────────────────────────

    override fun drainMutations(): List<ScreenMutation> {
        checkHandle()
        val flat = nativeDrainMutations(handle) ?: return emptyList()
        val out = ArrayList<ScreenMutation>(flat.size / 3)
        var i = 0
        while (i + 2 < flat.size) {
            val type = when (flat[i]) {
                0 -> MutationType.CELLS
                1 -> MutationType.SCROLL_UP
                2 -> MutationType.SCROLL_DOWN
                3 -> MutationType.ERASE
                4 -> MutationType.INSERT_LINES
                5 -> MutationType.DELETE_LINES
                6 -> MutationType.RESIZE
                else -> MutationType.FULL
            }
            val first = flat[i + 1].coerceAtLeast(0)
            val last = flat[i + 2].coerceAtLeast(first)
            out.add(ScreenMutation(type, first..last))
            i += 3
        }
        return out
    }

    override fun drainBell(): Long {
        checkHandle()
        return nativeDrainBell(handle)
    }

    override fun drainClipboardRequests(): List<String> {
        checkHandle()
        return nativeDrainClipboardRequests(handle)?.toList() ?: emptyList()
    }

    override fun applicationCursorKeys(): Boolean = header()[7] != 0L

    override fun bracketedPasteMode(): Boolean = header()[8] != 0L

    override fun scrollbackLineCount(): Int = header()[10].toInt()

    override fun scrollbackText(maxLines: Int): List<String> {
        checkHandle()
        return nativeScrollbackText(handle, maxLines)?.toList() ?: emptyList()
    }

    // ─── response sink (DA/DSR/DECRQM write-back) ──────────────────────

    private var responseSinkField: ((ByteArray) -> Unit)? = null

    override var responseSink: ((ByteArray) -> Unit)?
        get() = responseSinkField
        set(value) { responseSinkField = value }

    private fun pollResponsesToSink() {
        val sink = responseSinkField ?: return
        val bytes = nativePollResponses(handle) ?: return
        if (bytes.isNotEmpty()) sink(bytes)
    }

    // ─── JNI surface (symbols must match vt_jni.cpp exactly) ───────────

    private external fun nativeCreate(rows: Int, cols: Int, maxScrollback: Int): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeFeed(handle: Long, bytes: ByteArray, off: Int, len: Int)
    private external fun nativeFlush(handle: Long)
    private external fun nativeResize(handle: Long, rows: Int, cols: Int)
    private external fun nativeReset(handle: Long)
    private external fun nativeHeader(handle: Long): LongArray?
    private external fun nativeTitle(handle: Long): String?
    private external fun nativeRenderedText(handle: Long): String?
    private external fun nativeScrollbackText(handle: Long, maxLines: Int): Array<String>?
    private external fun nativeVisibleCells(handle: Long): IntArray?
    private external fun nativeSnapshotCells(handle: Long, maxScrollbackLines: Int): IntArray?
    private external fun nativeDrainMutations(handle: Long): IntArray?
    private external fun nativeDrainBell(handle: Long): Long
    private external fun nativeDrainClipboardRequests(handle: Long): Array<String>?
    private external fun nativePollResponses(handle: Long): ByteArray?

    // v0.2
    private external fun nativeLinks(handle: Long): Array<String>?
    private external fun nativeLinkAt(handle: Long, screenRow: Int, col: Int): String?
    private external fun nativeSearch(
        handle: Long, pattern: String, caseInsensitive: Boolean, wholeWord: Boolean
    ): Int
    private external fun nativeClearSearch(handle: Long)
    private external fun nativeSearchHits(handle: Long): LongArray?
    private external fun nativeSetActiveSearchHit(handle: Long, index: Int)
    private external fun nativeActiveSearchHit(handle: Long): Int
    private external fun nativeSearchHitCount(handle: Long): Int
    private external fun nativeBeginSelection(handle: Long, globalRow: Long, col: Int)
    private external fun nativeExtendSelection(handle: Long, globalRow: Long, col: Int)
    private external fun nativeClearSelection(handle: Long)
    private external fun nativeExpandSelectionWord(handle: Long, globalRow: Long, col: Int)
    private external fun nativeExpandSelectionLine(handle: Long, globalRow: Long, col: Int)
    private external fun nativeSelectionText(handle: Long): String?
    private external fun nativeSaveSession(handle: Long, maxScrollbackRows: Int): ByteArray?
    private external fun nativeEncodeKey(handle: Long, key: Int, mods: Int): ByteArray?
    private external fun nativeEncodePaste(handle: Long, text: String): ByteArray?
    private external fun nativeEncodeMouseEvent(
        handle: Long, type: Int, button: Int, mods: Int, col: Int, row: Int
    ): ByteArray?
    private external fun nativeEncodeFocus(handle: Long, focused: Boolean): ByteArray?
    private external fun nativeEncodeWheel(handle: Long, dir: Int): ByteArray?

    private fun checkHandle() {
        check(!closed && handle != 0L) { "NativeVtCore already closed" }
    }
}
