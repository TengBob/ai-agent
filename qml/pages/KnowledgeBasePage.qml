import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtWebEngine
import QtWebChannel
import "../components"

Page {
    id: root
    signal goBack()
    signal openChat(int agentId, string agentName)

    background: Rectangle { color: "#FAFAFA" }

    Component.onCompleted: {
        console.log("[KB] Component.onCompleted start")
        console.log("[KB] connect conversionProgress...")
        pdfConverter.conversionProgress.connect(onConversionProgress)
        console.log("[KB] connect conversionFinished...")
        pdfConverter.conversionFinished.connect(onConversionFinished)
        console.log("[KB] connect documentsChanged -> loadDocuments...")
        pdfConverter.documentsChanged.connect(loadDocuments)
        console.log("[KB] connect documentsChanged -> loadHtmlFiles...")
        pdfConverter.documentsChanged.connect(loadHtmlFiles)
        console.log("[KB] connect knowledgeBaseDirChanged...")
        pdfConverter.knowledgeBaseDirChanged.connect(loadHtmlFiles)
        console.log("[KB] connect htmlPageExtracted...")
        pdfConverter.htmlPageExtracted.connect(onHtmlPageExtracted)
        console.log("[KB] connect htmlFilesScanned...")
        pdfConverter.htmlFilesScanned.connect(onHtmlFilesScanned)
        console.log("[KB] connect pdf2HtmlEXAvailabilityChanged...")
        pdfConverter.pdf2HtmlEXAvailabilityChanged.connect(onPdf2HtmlEXAvailabilityChanged)
        console.log("[KB] connect skeletonReady...")
        pdfConverter.skeletonReady.connect(onSkeletonReady)
        console.log("[KB] connect pageRangeReady...")
        pdfConverter.pageRangeReady.connect(onPageRangeReady)
        console.log("[KB] connect chatEngine messageReceived...")
        chatEngine.messageReceived.connect(onKbChatMessage)
        console.log("[KB] connect chatEngine toolLogReceived...")
        chatEngine.toolLogReceived.connect(onToolLog)
        console.log("[KB] connect agentManager agentsChanged...")
        agentManager.agentsChanged.connect(onAgentsChanged)
        console.log("[KB] all signal connections done, scheduling init...")
        Qt.callLater(function() {
            console.time("[KB] init")
            console.log("[KB] deferred init start")
            checkTool()
            console.log("[KB] after checkTool")
            Qt.callLater(function() {
                loadDocuments()
                console.log("[KB] after loadDocuments")
                Qt.callLater(function() {
                    loadHtmlFiles()
                    console.log("[KB] after loadHtmlFiles")
                    console.timeEnd("[KB] init")
                })
            })
        })
    }

    Component.onDestruction: {
        console.log("[KB] Component.onDestruction start")
        pdfConverter.conversionProgress.disconnect(onConversionProgress)
        pdfConverter.conversionFinished.disconnect(onConversionFinished)
        pdfConverter.documentsChanged.disconnect(loadDocuments)
        pdfConverter.documentsChanged.disconnect(loadHtmlFiles)
        pdfConverter.knowledgeBaseDirChanged.disconnect(loadHtmlFiles)
        pdfConverter.htmlPageExtracted.disconnect(onHtmlPageExtracted)
        pdfConverter.htmlFilesScanned.disconnect(onHtmlFilesScanned)
        pdfConverter.pdf2HtmlEXAvailabilityChanged.disconnect(onPdf2HtmlEXAvailabilityChanged)
        pdfConverter.skeletonReady.disconnect(onSkeletonReady)
        pdfConverter.pageRangeReady.disconnect(onPageRangeReady)
        chatEngine.messageReceived.disconnect(onKbChatMessage)
        chatEngine.toolLogReceived.disconnect(onToolLog)
        agentManager.agentsChanged.disconnect(onAgentsChanged)
        console.log("[KB] Component.onDestruction done")
    }

    function restoreChat(agentId) {
        chatEngine.startConversation(agentId)
        kbChatModel.clear()
        const msgs = chatEngine.currentMessages()
        for (let i = 0; i < msgs.length; i++) {
            const m = msgs[i]
            kbChatModel.append({ role: m.role, content: m.content, timestamp: m.timestamp, selectedForNote: false })
        }
        Qt.callLater(function() { kbChatView.positionViewAtEnd() })
    }

    function loadHtmlFile(path, page) {
        console.log("[KB] loadHtmlFile path:", path, "page:", page, "webViewVisible:", webViewVisible, "timerRunning:", webViewRevealTimer.running)
        // Always keep pending up-to-date so the timer fires with the latest path.
        pendingWebViewLoad = { path: path, page: page }

        if (!webViewVisible) {
            // First call: make the WebEngineView visible and let Chromium start.
            console.log("[KB] loadHtmlFile -> webViewVisible=false, starting reveal timer")
            webViewVisible = true
            webViewRevealTimer.start()
            return
        }

        if (webViewRevealTimer.running) {
            // Timer is still counting down (200ms window).
            // pending is already updated above — just wait for the timer.
            console.log("[KB] loadHtmlFile -> timer still running, will fire later")
            return
        }

        // WebView is visible and timer has already fired: load immediately.
        console.log("[KB] loadHtmlFile -> calling doLoadHtmlFile directly")
        pendingWebViewLoad = null
        doLoadHtmlFile(path, page)
    }

    function doLoadHtmlFile(path, page) {
        console.log("[KB] doLoadHtmlFile path:", path, "page:", page)
        webViewEverLoaded = true
        selectedHtmlPath = path
        const targetPage = Math.max(1, page)
        htmlCurrentPage = targetPage
        htmlCurrentInput = "" + targetPage
        htmlTotalPages = 0

        // Reset progressive loading state for each new file load.
        loadSequence++
        isLargeFile = false
        isProgressiveLoading = false
        loadingProgress = 0.0
        totalPagesInFile = 0
        loadedPagesSet = ({})
        pendingPages = ({})
        skeletonWindowStart = 0
        skeletonWindowEnd = 0
        skeletonReady = false
        backgroundLoadFinished = false
        pendingJumpPage = 0
        pendingInjections = []

        const isMarkdown = /\.md$/i.test(path)
        if (isMarkdown) {
            // Render markdown notes as HTML so they display properly.
            const mdText = pdfConverter.readTextFile(path)
            const html = pdfConverter.markdownToHtml(mdText)
            webView.loadHtml(html, "file:///")
            return
        }

        // Check file size; large HTML files use progressive loading.
        const info = pdfConverter.fileInfo(path)
        if (info.size >= largeFileThreshold) {
            isLargeFile = true
            isProgressiveLoading = true

            // totalPagesInFile is unknown until async completes; use 0 as sentinel.
            totalPagesInFile = 0

            // winEnd uses a safe upper bound; buildSkeletonAsync will not exceed actual total.
            const winStart = Math.max(1, targetPage - 2)
            const winEnd   = targetPage + 7    // C++ clamps to actual page count

            skeletonWindowStart = winStart
            skeletonWindowEnd   = winEnd

            if (targetPage > 1) pendingJumpPage = targetPage

            expectedSkeletonSeq = loadSequence
            pdfConverter.buildSkeletonAsync(path, winStart, winEnd)
            return
        }

        // Small file: direct load.
        webView.url = "file:///" + path.replace(/\\/g, "/")
        if (targetPage > 1) {
            webView.onLoadingChanged.connect(function jumpOnce(req) {
                if (req.status === WebEngineView.LoadSucceededStatus) {
                    webView.onLoadingChanged.disconnect(jumpOnce)
                    webView.jumpToPage(targetPage)
                }
            })
        }
    }

    function installScrollDetector() {
        if (/\.md$/i.test(selectedHtmlPath)) return
        webView.runJavaScript(`
(function() {
    if (window.__kbScrollInstalled) return;
    window.__kbScrollInstalled = true;
    var pc = document.getElementById('page-container');
    if (!pc) return;
    var timer = null;
    pc.addEventListener('scroll', function() {
        if (timer) clearTimeout(timer);
        timer = setTimeout(function() {
            var pfs = pc.querySelectorAll('div.pf');
            if (pfs.length === 0) return;
            var sy = pc.scrollTop;
            for (var i = 0; i < pfs.length; i++) {
                if (pfs[i].offsetTop + pfs[i].offsetHeight * 0.5 > sy) {
                    console.log('__KB_PAGE__:' + (i + 1));
                    break;
                }
            }
        }, 300);
    }, {passive: true});
})();
        `)
    }

    function updateLoadingProgress() {
        if (!isLargeFile || totalPagesInFile <= 0) return
        const loaded = Object.keys(loadedPagesSet).length
        loadingProgress = loaded / totalPagesInFile

        const pendingCount = Object.keys(pendingPages).length
        if (backgroundLoadFinished && pendingCount === 0) {
            isProgressiveLoading = false
            loadingProgress = 1.0
        }
    }

    function doScrollToPage(page) {
        webView.runJavaScript(
            "(function(p){" +
            "var pc=document.getElementById('page-container');" +
            "if(!pc) return;" +
            "var el=pc.querySelector('.pf[data-page-no=\"'+p+'\"]');" +
            "if(el){pc.scrollTop=el.offsetTop;return;}" +
            "var near=pc.querySelector('.pf[data-loaded=\"true\"]');" +
            "if(near)pc.scrollTop=near.offsetTop;" +
            "})(" + page + ");"
        )
    }

    function onSkeletonReady(htmlPath, skeletonPath, totalPages, startPage, endPage) {
        console.log("[KB] onSkeletonReady htmlPath:", htmlPath, "selected:", selectedHtmlPath,
                    "skeletonReady:", skeletonReady, "seq:", loadSequence, "expectedSeq:", expectedSkeletonSeq,
                    "skeletonPath:", skeletonPath)
        if (htmlPath !== selectedHtmlPath) return
        if (skeletonReady) return
        // Reject callbacks from a previous load sequence (two rapid clicks same file).
        if (loadSequence !== expectedSkeletonSeq) return

        totalPagesInFile = totalPages > 0 ? totalPages : 1
        skeletonWindowStart = startPage
        skeletonWindowEnd   = endPage

        if (!webViewVisible) {
            webViewVisible = true
        }

        // Load via url — Chromium reads from disk, zero main-thread string copy.
        lastSkeletonPath = skeletonPath
        webView.url = "file:///" + skeletonPath
        skeletonReady = true

        // Delete leftover skeleton files from previous runs now that Chromium has
        // opened the new file. Done here on the main thread to avoid the race where
        // a concurrent background thread deletes another thread's freshly-written file.
        pdfConverter.deleteOldSkeletons(htmlPath, skeletonPath)

        // Mark the initial window as loaded (C++ already injected data-loaded="true").
        var newSet = ({})
        for (var i = startPage; i <= endPage; i++)
            newSet[i] = true
        loadedPagesSet = newSet

        updateLoadingProgress()
    }

    function onPageRangeReady(htmlPath, startPage, endPage, pages) {
        if (htmlPath !== selectedHtmlPath) return
        console.log("[KB] onPageRangeReady htmlPath:", htmlPath, "pages:", pages.length,
                    "webView.loading:", webView.loading)

        // Always do bookkeeping regardless of loading state,
        // so pending markers are cleared and pages aren't re-requested.
        for (var j = 0; j < pages.length; j++) {
            var pn = pages[j].pageNo
            loadedPagesSet[pn] = true
            delete pendingPages[pn]
        }
        loadedPagesSet = loadedPagesSet  // notify binding
        pendingPages   = pendingPages    // notify binding

        updateLoadingProgress()

        if (pages.length === 0) return

        if (webView.loading) {
            // webView is navigating — defer injection until LoadSucceeded.
            // Concat rather than replace so multiple batches accumulate safely.
            pendingInjections = pendingInjections.concat(pages)
            return
        }

        injectPages(pages)

        if (pendingJumpPage > 0 &&
            pendingJumpPage >= startPage && pendingJumpPage <= endPage) {
            const jumpTarget = pendingJumpPage
            pendingJumpPage = 0
            Qt.callLater(function() { doScrollToPage(jumpTarget) })
        }
    }

    function injectPages(pages) {
        if (pages.length === 0) return
        var jsParts = []
        for (var i = 0; i < pages.length; i++) {
            var p = pages[i]
            jsParts.push('__kbLoadPage(' + p.pageNo + ',' + JSON.stringify(p.html) + ');')
        }
        webView.runJavaScript(jsParts.join('\n'))
    }

    function scheduleBackgroundLoad() {
        if (!isLargeFile) return
        const currentSeq = loadSequence

        var unloaded = []
        for (var i = 1; i <= totalPagesInFile; i++) {
            if (!loadedPagesSet[i] && !pendingPages[i]) unloaded.push(i)
        }
        console.log("[KB] scheduleBackgroundLoad totalPages:", totalPagesInFile,
                    "unloaded:", unloaded.length, "seq:", currentSeq)
        if (unloaded.length === 0) {
            backgroundLoadFinished = true
            updateLoadingProgress()
            return
        }

        const current = htmlCurrentPage
        unloaded.sort(function(a, b) { return Math.abs(a - current) - Math.abs(b - current) })

        isProgressiveLoading = true
        backgroundLoadFinished = false
        processBackgroundBatch(unloaded, 0, currentSeq)
    }

    function processBackgroundBatch(pages, index, seq) {
        if (seq !== loadSequence) return

        if (index >= pages.length) {
            backgroundLoadFinished = true
            updateLoadingProgress()
            return
        }

        const batchSize = 10
        var batch = []
        var i = index
        while (batch.length < batchSize && i < pages.length) {
            if (!pendingPages[pages[i]]) {
                pendingPages[pages[i]] = true
                batch.push(pages[i])
            }
            i++
        }

        if (batch.length === 0) {
            processBackgroundBatch(pages, i, seq)
            return
        }

        pendingPages = pendingPages  // notify binding

        const startPage = batch[0]
        const endPage   = batch[batch.length - 1]
        pdfConverter.extractPageRangeAsync(selectedHtmlPath, startPage, endPage)

        const nextIndex = i
        Qt.callLater(function() { processBackgroundBatch(pages, nextIndex, seq) })
    }

    property string toolLogText: ""
    function onToolLog(toolName, argsJson, resultPreview, timestamp) {
        if (resultPreview.length > 0)
            toolLogText = "✅ " + toolName + " 完成"
        else
            toolLogText = "🔧 调用工具：" + toolName
    }

    function onAgentsChanged() {
        if (chatAgentId < 0) return
        const map = agentManager.getAgent(chatAgentId)
        if (!map || map.id === undefined || map.id <= 0) {
            chatAgentId      = -1
            chatAgentName    = ""
            chatPanelVisible = false
            kbChatModel.clear()
        }
    }

    function onKbChatMessage(role, content, timestamp) {
        if (role === "system" || role === "user") return
        kbChatModel.append({ role: role, content: content, timestamp: timestamp, selectedForNote: false })
        Qt.callLater(function() { kbChatView.positionViewAtEnd() })
    }

    property string searchText: ""
    property int    selectedDocId: -1
    property var    selectedDoc: null
    property bool   pdf2htmlEXAvailable: false
    property bool   sidebarVisible: false
    property string selectedHtmlPath: ""

    property bool   chatPanelVisible: false
    property int    chatAgentId: -1
    property string chatAgentName: ""

    property int    htmlTotalPages:  0
    property int    htmlCurrentPage: 1
    property string htmlCurrentInput: "1"
    property int    savedHtmlPage: 1
    property var    htmlPageMap: ({})

    property string editingNotePath: ""
    property string pendingDeleteNotePath: ""

    // Track an externally-opened document so we can auto-select it once it appears
    // in the file list (PDF after conversion, HTML/MD after scan).
    property int    pendingOpenDocId: -1
    property string pendingOpenPath: ""

    // Defer WebEngineView visibility: the Chromium render process starts when the
    // view first becomes visible and blocks the event loop. Let the page UI paint
    // first, then reveal the view in a subsequent event loop iteration.
    // webViewVisible: controls WebEngineView.visible (职责A - 渲染进程启动)
    // webViewEverLoaded: true once doLoadHtmlFile() has been called at least once (职责B - 首次加载门控)
    property bool   webViewVisible: false
    property bool   webViewEverLoaded: false
    property var    pendingWebViewLoad: null

    // ── Progressive loading state ──────────────────────────────────────────
    property bool   isLargeFile: false
    property bool   isProgressiveLoading: false
    property real   loadingProgress: 0.0
    property int    totalPagesInFile: 0
    property int    largeFileThreshold: 500 * 1024   // 500 KB
    property int    loadSequence: 0
    property int    expectedSkeletonSeq: 0
    property var    loadedPagesSet: ({})
    property var    pendingPages: ({})
    property int    skeletonWindowStart: 0
    property int    skeletonWindowEnd: 0
    property bool   skeletonReady: false
    property bool   backgroundLoadFinished: false
    property int    pendingJumpPage: 0               // 0 = no pending jump
    property string lastSkeletonPath: ""             // temp skeleton file to delete on next load
    property var    pendingInjections: []            // pages queued while webView was loading
    // ───────────────────────────────────────────────────────────────────────

    // Bridge used by the WebEngineView scroll detector to report the current
    // page without polling.
    WebChannel {
        id: kbWebChannel
        registeredObjects: [kbBridge]
    }
    QtObject {
        id: kbBridge
        objectName: "kbBridge"
        function reportPage(page) {
            if (!page || page < 1 || page === htmlCurrentPage) return
            htmlCurrentPage = page
            htmlCurrentInput = "" + page
            if (selectedHtmlPath !== "" && !/\.md$/i.test(selectedHtmlPath)) {
                chatEngine.prefetchPage(selectedHtmlPath, page)
            }
        }
    }

    function onHtmlPageExtracted(htmlPath, page, tmpPath) {
        // Single-page extraction is no longer used for the web view.
        // This handler is kept for compatibility in case it is called elsewhere.
        console.log("[KB] htmlPageExtracted (unused):", htmlPath, "page", page, "tmp", tmpPath)
    }

    function generateStudyNote() {
        if (selectedHtmlPath === "") {
            errorBar.text = "请先打开一个 HTML 文档"
            errorBar.visible = true
            errorTimer.restart()
            return
        }

        let selectedCount = 0
        for (let i = 0; i < kbChatModel.count; i++) {
            if (kbChatModel.get(i).selectedForNote) selectedCount++
        }
        if (selectedCount === 0) {
            errorBar.text = "请勾选要加入笔记的对话"
            errorBar.visible = true
            errorTimer.restart()
            return
        }

        const fileName = selectedHtmlPath.split(/[/\\]/).pop()
        const docName = fileName.replace(/\.html?$/i, "")
        const noteBaseName = docName + "学习笔记"
        const noteFileName = noteBaseName + ".md"
        const timestamp = Qt.formatDateTime(new Date(), "yyyy-MM-dd hh:mm:ss")

        // Compute the target note path safely for both / and \ separators.
        const normalizedPath = selectedHtmlPath.replace(/\\/g, "/")
        const noteDir = normalizedPath.substring(0, normalizedPath.lastIndexOf('/'))
        const notePath = noteDir + "/" + noteFileName
        const fileExists = pdfConverter.fileExists(notePath)

        let content = ""
        if (fileExists) {
            // Append a new page entry to the existing note.
            content += "\n---\n\n"
            content += "## 📍 第 " + htmlCurrentPage + " 页 — " + timestamp + "\n\n"
        } else {
            // Create the note with a full header.
            content += "# " + docName + " 学习笔记\n\n"
            content += "> 📄 文档：" + docName + "\n"
            content += "> 📅 创建时间：" + timestamp + "\n"
            content += "> 📂 来源：" + selectedHtmlPath + "\n\n"
            content += "---\n\n"
            content += "## 📍 第 " + htmlCurrentPage + " 页 — " + timestamp + "\n\n"
        }

        for (let i = 0; i < kbChatModel.count; i++) {
            const m = kbChatModel.get(i)
            if (!m.selectedForNote) continue
            if (m.role === "user") {
                content += "### ❓ 问题\n\n" + m.content + "\n\n"
            } else if (m.role === "assistant") {
                content += "### 💡 回答\n\n" + m.content + "\n\n"
            }
        }

        if (pdfConverter.saveMarkdownNote(selectedHtmlPath, noteBaseName, content, fileExists)) {
            // 清空所有勾选，避免下次生成笔记时重复写入
            for (let i = 0; i < kbChatModel.count; i++) {
                kbChatModel.setProperty(i, "selectedForNote", false)
            }
            loadHtmlFiles()
            errorBar.color = "#E8F5E9"
            errorBar.border.color = "#81C784"
            errorBar.text = fileExists
                ? "笔记已追加到：" + noteFileName + "（第 " + htmlCurrentPage + " 页）"
                : "笔记已创建：" + noteFileName
            errorBar.visible = true
            errorTimer.restart()
        } else {
            errorBar.color = "#FFEBEE"
            errorBar.border.color = "#F44336"
            errorBar.text = "保存笔记失败"
            errorBar.visible = true
            errorTimer.restart()
        }
    }

    function openNoteEditor(path) {
        editingNotePath = path
        noteEditArea.text = pdfConverter.readTextFile(path)
        noteEditDialog.open()
    }

    function saveNoteEdit() {
        if (editingNotePath === "") return
        if (pdfConverter.saveTextFile(editingNotePath, noteEditArea.text)) {
            noteEditDialog.close()
            if (selectedHtmlPath === editingNotePath)
                loadHtmlFile(editingNotePath, 1)
            loadHtmlFiles()
            errorBar.color = "#E8F5E9"
            errorBar.border.color = "#81C784"
            errorBar.text = "笔记已更新"
            errorBar.visible = true
            errorTimer.restart()
        } else {
            errorBar.color = "#FFEBEE"
            errorBar.border.color = "#F44336"
            errorBar.text = "保存笔记失败"
            errorBar.visible = true
            errorTimer.restart()
        }
        editingNotePath = ""
    }

    function confirmDeleteNote(path) {
        pendingDeleteNotePath = path
        noteDeleteConfirmDialog.open()
    }

    function doDeleteNote() {
        if (pendingDeleteNotePath === "") return
        if (pdfConverter.deleteNoteFile(pendingDeleteNotePath)) {
            if (selectedHtmlPath === pendingDeleteNotePath) {
                selectedHtmlPath = ""
            }
            loadHtmlFiles()
            errorBar.color = "#E8F5E9"
            errorBar.border.color = "#81C784"
            errorBar.text = "笔记已删除"
            errorBar.visible = true
            errorTimer.restart()
        } else {
            errorBar.color = "#FFEBEE"
            errorBar.border.color = "#F44336"
            errorBar.text = "删除笔记失败"
            errorBar.visible = true
            errorTimer.restart()
        }
        pendingDeleteNotePath = ""
    }

    function checkTool() {
        pdfConverter.checkPdf2HtmlEXAsync()
    }

    function onPdf2HtmlEXAvailabilityChanged(available) {
        pdf2htmlEXAvailable = available
    }

    function loadDocuments() {
        docModel.clear()
        const all = pdfConverter.documents()
        const lower = searchText.toLowerCase()
        for (const d of all) {
            if (lower.length === 0 || d.title.toLowerCase().includes(lower))
                docModel.append(d)
        }
        updateSelectedDoc()
    }

    function updateSelectedDoc() {
        selectedDoc = null
        for (let i = 0; i < docModel.count; i++) {
            if (docModel.get(i).id === selectedDocId) {
                selectedDoc = docModel.get(i)
                break
            }
        }
    }

    function onConversionProgress(docId, percent, message) {
        for (let i = 0; i < docModel.count; i++) {
            if (docModel.get(i).id === docId) {
                docModel.setProperty(i, "status", "converting")
                docModel.setProperty(i, "progress", percent)
                docModel.setProperty(i, "progressMsg", message)
                break
            }
        }
    }

    function onConversionFinished(docId, success, error) {
        loadDocuments()
        loadHtmlFiles()

        if (pendingOpenDocId >= 0 && docId === pendingOpenDocId) {
            const wasPending = pendingOpenDocId
            pendingOpenDocId = -1
            if (success) {
                let htmlPath = ""
                let outputDir = ""
                for (let i = 0; i < docModel.count; i++) {
                    const d = docModel.get(i)
                    if (d.id === docId) {
                        htmlPath = d.htmlPath || ""
                        outputDir = d.outputDir || ""
                        break
                    }
                }
                const targetPath = htmlPath || (outputDir.replace(/\\$/g, "") + "/index.html")
                if (targetPath !== "/index.html" && pdfConverter.fileExists(targetPath)) {
                    selectedDocId = docId
                    updateSelectedDoc()
                    loadHtmlFile(targetPath, 1)
                }
            } else {
                errorBar.text = error
                errorBar.visible = true
                errorTimer.restart()
            }
            return
        }

        if (!success) {
            errorBar.text = error
            errorBar.visible = true
            errorTimer.restart()
        }
    }

    function loadHtmlFiles() {
        pdfConverter.scanHtmlFilesAsync()
    }

    function onHtmlFilesScanned(files) {
        console.log("[KB] onHtmlFilesScanned count:", files.length)
        htmlFileModel.clear()
        for (const f of files) {
            htmlFileModel.append(f)
        }

        if (pendingOpenPath !== "") {
            const target = pendingOpenPath.replace(/\\/g, "/")
            for (let i = 0; i < htmlFileModel.count; i++) {
                const p = htmlFileModel.get(i).path.replace(/\\/g, "/")
                if (p === target) {
                    pendingOpenPath = ""
                    const f = htmlFileModel.get(i)
                    loadHtmlFile(f.path, 1)
                    break
                }
            }
        }
    }

    FileDialog {
        id: pdfDialog
        title: "选择 PDF 文件"
        nameFilters: ["PDF 文件 (*.pdf)"]
        onAccepted: {
            const path = selectedFile.toString().replace("file:///", "")
            pdfConverter.convertPdf(path)
        }
    }

    FileDialog {
        id: externalDocDialog
        title: "打开外部文档"
        nameFilters: [
            "PDF 文件 (*.pdf)",
            "HTML 文件 (*.html *.htm)",
            "Markdown 文件 (*.md)",
            "所有文件 (*)"
        ]
        onAccepted: {
            const path = selectedFile.toString().replace("file:///", "")
            const res = pdfConverter.openExternalDocument(path)
            const docId = res.docId !== undefined ? res.docId : -1
            const destPath = res.destPath !== undefined ? res.destPath : ""
            pendingOpenDocId = docId
            pendingOpenPath = destPath
            if (docId < 0) {
                errorBar.text = "打开文档失败"
                errorBar.visible = true
                errorTimer.restart()
            } else if (docId === 0) {
                errorBar.text = "正在导入文档，请稍候..."
                errorBar.visible = true
                errorTimer.restart()
            } else {
                errorBar.text = "PDF 已导入，正在转换..."
                errorBar.visible = true
                errorTimer.restart()
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: "选择知识库目录"
        currentFolder: "file:///" + pdfConverter.knowledgeBaseDir
        onAccepted: {
            const path = selectedFolder.toString().replace("file:///", "")
            if (pdfConverter.setKnowledgeBaseDir(path))
                loadDocuments()
        }
    }

    FileDialog {
        id: exeDialog
        title: "选择 pdf2htmlEX 可执行文件"
        nameFilters: ["可执行文件 (*.exe)", "所有文件 (*)"]
        onAccepted: {
            const path = selectedFile.toString().replace("file:///", "")
            if (pdfConverter.setPdf2HtmlEXPath(path)) {
                checkTool()
            } else {
                errorBar.text = "选择的文件无效或无法访问"
                errorBar.visible = true
                errorTimer.restart()
            }
        }
    }

    Dialog {
        id: detectLogDialog
        title: "自动检测详情"
        modal: true
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.8, 640)
        height: Math.min(parent.height * 0.8, 480)
        standardButtons: Dialog.Ok

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Text {
                text: "未找到可用的 pdf2htmlEX。以下路径均已尝试："
                font.pixelSize: 13
                color: "#E65100"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    text: pdfConverter.detectLog
                    readOnly: true
                    selectByMouse: true
                    font.pixelSize: 12
                    font.family: "Courier New"
                    wrapMode: TextArea.Wrap
                    background: Rectangle { color: "#FAFAFA"; border.color: "#E0E0E0"; border.width: 1 }
                }
            }

            Text {
                text: "常见原因：\n1. 可执行文件名不是 pdf2htmlEX.exe\n2. 缺少运行依赖（如 MSYS2/Cygwin DLL）\n3. 放在子目录中未被识别\n4. 文件没有执行权限"
                font.pixelSize: 12
                color: "#616161"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    Dialog {
        id: noteEditDialog
        title: "编辑学习笔记"
        modal: true
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.8, 720)
        height: Math.min(parent.height * 0.8, 540)
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: saveNoteEdit()
        onRejected: editingNotePath = ""

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Text {
                text: editingNotePath
                font.pixelSize: 11
                color: "#616161"
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true

                TextArea {
                    id: noteEditArea
                    wrapMode: TextArea.Wrap
                    font.pixelSize: 13
                    selectByMouse: true
                    background: Rectangle { radius: 6; color: "#F5F5F5"; border.color: "#E0E0E0" }
                }
            }
        }
    }

    Dialog {
        id: noteDeleteConfirmDialog
        title: "删除笔记"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: doDeleteNote()
        onRejected: pendingDeleteNotePath = ""

        Text {
            text: "确定要删除这条学习笔记吗？"
            font.pixelSize: 13
            color: "#212121"
        }
    }

    // ── 选择 Agent 对话框 ──
    Dialog {
        id: createAgentDialog
        title: "选择 Agent"
        modal: true
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.45, 360)
        height: Math.min(parent.height * 0.6, 460)
        standardButtons: Dialog.Cancel

        ListView {
            anchors.fill: parent
            clip: true
            spacing: 6
            model: agentManager.agentListModel

            Text {
                anchors.centerIn: parent
                visible: agentManager.agentListModel.count === 0
                text: "暂无 Agent\n请先在 Agent 页面创建"
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 13; color: "#9E9E9E"
            }

            delegate: Rectangle {
                width: ListView.view.width
                height: 54
                radius: 8
                color: agentPickHover.containsMouse ? "#E3F2FD" : "#F5F5F5"

                ColumnLayout {
                    anchors { left: parent.left; leftMargin: 14; right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    spacing: 2
                    Text {
                        text: model.agentName
                        font.pixelSize: 13; font.bold: true; color: "#212121"
                        Layout.fillWidth: true; elide: Text.ElideRight
                    }
                    Text {
                        text: model.agentModelName
                        font.pixelSize: 11; color: "#9E9E9E"
                        Layout.fillWidth: true; elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: agentPickHover
                    anchors.fill: parent; hoverEnabled: true
                    onClicked: {
                        createAgentDialog.close()
                        chatAgentId   = model.agentId
                        chatAgentName = model.agentName
                        chatEngine.startConversation(model.agentId)
                        kbChatModel.clear()
                        chatPanelVisible = true
                    }
                }
            }
        }
    }

    ListModel { id: kbChatModel }
    ListModel { id: docModel }
    ListModel { id: htmlFileModel }

    header: ToolBar {
        background: Rectangle { color: "white"; border.color: "#E0E0E0"; border.width: 1 }
        RowLayout {
            anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
            Button { text: "← 返回"; flat: true; onClicked: root.goBack() }
            Text {
                text: "知识库"
                font.pixelSize: 18; font.bold: true; color: "#212121"
            }
            Item { Layout.fillWidth: true }
            TextField {
                id: searchField
                placeholderText: "搜索文档..."
                width: 200; height: 36
                font.pixelSize: 13
                background: Rectangle { radius: 6; color: "#F5F5F5"; border.color: "#E0E0E0" }
                onTextChanged: { root.searchText = text; loadDocuments() }
            }
            Button {
                text: "📁 知识库目录"
                flat: true; font.pixelSize: 12
                onClicked: folderDialog.open()
            }
            Button {
                text: "⚙️ pdf2htmlEX"
                flat: true; font.pixelSize: 12
                onClicked: exeDialog.open()
            }
            Button {
                text: "+ 添加 PDF"
                enabled: !pdfConverter.isConverting
                Material.foreground: "white"
                Material.background: enabled ? "#1976D2" : "#BDBDBD"
                onClicked: pdfDialog.open()
            }
            Button {
                text: "📂 打开文档"
                flat: true
                font.pixelSize: 12
                onClicked: externalDocDialog.open()
            }
            Button {
                text: chatAgentId > 0 ? ("🤖 " + chatAgentName) : "🤖 与 Agent 对话"
                Material.foreground: "white"
                Material.background: "#388E3C"
                onClicked: {
                    if (chatAgentId > 0)
                        chatPanelVisible = true
                    else
                        createAgentDialog.open()
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── pdf2htmlEX config banner ──
        Rectangle {
            Layout.fillWidth: true
            height: pdf2htmlEXAvailable ? 0 : 56
            visible: !pdf2htmlEXAvailable
            color: "#FFF3E0"
            border.color: "#FFB74D"; border.width: 1

            RowLayout {
                anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
                spacing: 12

                Text {
                    text: "⚠ 未检测到 pdf2htmlEX，转换功能不可用"
                    font.pixelSize: 13; color: "#E65100"
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "自动检测"
                    flat: true
                    font.pixelSize: 12
                    Material.foreground: "#E65100"
                    onClicked: {
                        if (pdfConverter.findPdf2HtmlEX()) {
                            checkTool()
                        } else {
                            detectLogDialog.open()
                        }
                    }
                }
                Button {
                    text: "手动选择"
                    flat: true
                    font.pixelSize: 12
                    Material.foreground: "#E65100"
                    onClicked: exeDialog.open()
                }
                Button {
                    text: "下载"
                    flat: true
                    font.pixelSize: 12
                    Material.foreground: "#E65100"
                    onClicked: pdfConverter.openDownloadPage()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            clip: true

            // ── document list ──
            Rectangle {
                Layout.preferredWidth: sidebarVisible ? 360 : 0
                Layout.fillHeight: true
                visible: sidebarVisible
                color: "white"
                border.color: "#E0E0E0"; border.width: 1

                ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true; height: 44
                    color: "#FAFAFA"
                    Text {
                        anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                        text: "文档列表"
                        font.pixelSize: 14; font.bold: true; color: "#424242"
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                        text: docModel.count + " 个文档"
                        font.pixelSize: 12; color: "#9E9E9E"
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#E0E0E0" }
                }

                ListView {
                    id: listView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true; spacing: 1
                    model: docModel

                    delegate: Rectangle {
                        id: delegateRoot
                        width: listView.width
                        height: 100
                        color: selectedDocId === model.id ? "#E3F2FD" : (hoverArea.containsMouse ? "#F5F5F5" : "white")

                        // hover + select
                        MouseArea {
                            id: hoverArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                selectedDocId = model.id
                                updateSelectedDoc()
                            }
                        }

                        // PDF icon
                        Rectangle {
                            id: iconRect
                            x: 12; anchors.verticalCenter: parent.verticalCenter
                            width: 40; height: 52
                            color: "#EEEEEE"; radius: 4
                            Text { anchors.centerIn: parent; text: "📄"; font.pixelSize: 20 }
                        }

                        // title + status
                        Column {
                            anchors {
                                left: iconRect.right; leftMargin: 10
                                right: actionCol.left; rightMargin: 8
                                verticalCenter: parent.verticalCenter
                            }
                            spacing: 3

                            Text {
                                width: parent.width
                                text: model.title
                                font.pixelSize: 13; font.bold: true; color: "#212121"
                                elide: Text.ElideRight
                            }
                            Text {
                                text: {
                                    if (model.status === "done")      return "✅ 已转换"
                                    if (model.status === "converting") return "⏳ 转换中..."
                                    if (model.status === "error")     return "❌ 失败"
                                    return "⏸ 待处理"
                                }
                                font.pixelSize: 11
                                color: model.status === "error" ? "#C62828" : "#616161"
                            }
                            Text {
                                visible: model.status === "converting"
                                text: model.progressMsg || "准备中..."
                                font.pixelSize: 10; color: "#1976D2"
                                width: parent.width; elide: Text.ElideRight
                            }
                            Text {
                                visible: model.status === "error"
                                text: model.errorMessage || ""
                                font.pixelSize: 10; color: "#C62828"
                                width: parent.width; elide: Text.ElideRight
                            }
                        }

                        // action buttons
                        Column {
                            id: actionCol
                            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                            spacing: 5

                            Rectangle {
                                visible: model.status === "done"
                                width: 56; height: 26; radius: 4
                                color: openHover.containsMouse ? "#1565C0" : "#1976D2"
                                Text { anchors.centerIn: parent; text: "打开"; color: "white"; font.pixelSize: 11 }
                                MouseArea {
                                    id: openHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: (mouse) => {
                                        mouse.accepted = true
                                        pdfConverter.openInBrowser(model.id)
                                    }
                                }
                            }

                            Rectangle {
                                visible: model.status === "done" || model.status === "error"
                                width: 56; height: 26; radius: 4
                                color: reconvertHover.containsMouse ? "#F57C00" : "#FF9800"
                                Text { anchors.centerIn: parent; text: "重转"; color: "white"; font.pixelSize: 11 }
                                MouseArea {
                                    id: reconvertHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: (mouse) => {
                                        mouse.accepted = true
                                        pdfConverter.reconvertPdf(model.id)
                                    }
                                }
                            }

                            Rectangle {
                                visible: model.status !== "converting"
                                width: 56; height: 26; radius: 4
                                color: delHover.containsMouse ? "#C62828" : "#E53935"
                                Text { anchors.centerIn: parent; text: "删除"; color: "white"; font.pixelSize: 11 }
                                MouseArea {
                                    id: delHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: (mouse) => {
                                        mouse.accepted = true
                                        if (selectedDocId === model.id) {
                                            selectedDocId = -1
                                            updateSelectedDoc()
                                        }
                                        pdfConverter.deleteDocument(model.id)
                                    }
                                }
                            }
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width; height: 1; color: "#EEEEEE"
                        }
                    }

                    // empty state
                    Text {
                        anchors.centerIn: parent
                        visible: docModel.count === 0
                        text: "还没有 PDF 文档\n点击右上角「+ 添加 PDF」开始"
                        horizontalAlignment: Text.AlignHCenter
                        font.pixelSize: 15; color: "#9E9E9E"; lineHeight: 1.6
                    }
                }

                // ── HTML 文件目录 ──
                Rectangle {
                    Layout.fillWidth: true; height: 1; color: "#E0E0E0"
                }

                Rectangle {
                    Layout.fillWidth: true; height: 40
                    color: "#F5F5F5"
                    RowLayout {
                        anchors { fill: parent; leftMargin: 12; rightMargin: 8 }
                        Text {
                            text: "文档详情"
                            font.pixelSize: 13; font.bold: true; color: "#424242"
                            Layout.fillWidth: true
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180

                    Text {
                        anchors.centerIn: parent
                        visible: selectedDoc === null
                        text: "点击上方文档查看详情"
                        font.pixelSize: 12; color: "#BDBDBD"
                    }

                    ScrollView {
                        anchors { fill: parent; margins: 12 }
                        visible: selectedDoc !== null

                        Column {
                            spacing: 6
                            width: parent.width

                            Text {
                                text: selectedDoc ? selectedDoc.title : ""
                                font.pixelSize: 14; font.bold: true; color: "#212121"
                                width: parent.width; elide: Text.ElideRight
                            }

                            Text {
                                text: {
                                    if (!selectedDoc) return ""
                                    if (selectedDoc.status === "done")      return "✅ 已转换"
                                    if (selectedDoc.status === "converting") return "⏳ 转换中"
                                    if (selectedDoc.status === "error")     return "❌ 失败"
                                    return "⏸ 待处理"
                                }
                                font.pixelSize: 12
                                color: selectedDoc && selectedDoc.status === "error" ? "#C62828" : "#616161"
                            }

                            Text {
                                visible: selectedDoc && selectedDoc.status === "error"
                                text: selectedDoc ? (selectedDoc.errorMessage || "") : ""
                                font.pixelSize: 11; color: "#C62828"
                                wrapMode: Text.Wrap; width: parent.width
                            }

                            Text {
                                text: selectedDoc ? (selectedDoc.createdAt || "") : ""
                                font.pixelSize: 11; color: "#9E9E9E"
                            }

                            Rectangle {
                                visible: selectedDoc && selectedDoc.status === "done"
                                width: 100; height: 26; radius: 4
                                color: openDetailHover.containsMouse ? "#1565C0" : "#1976D2"
                                Text { anchors.centerIn: parent; text: "在浏览器打开"; color: "white"; font.pixelSize: 11 }
                                MouseArea {
                                    id: openDetailHover
                                    anchors.fill: parent; hoverEnabled: true
                                    onClicked: pdfConverter.openInBrowser(selectedDoc.id)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── HTML 文件目录 + 预览（右侧主区域，可拖动分界）──
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            // 左侧：HTML 文件列表
            Rectangle {
                SplitView.preferredWidth: 160
                SplitView.minimumWidth: 60
                color: "white"
                border.color: "#E0E0E0"; border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true; height: 44
                        color: "#FAFAFA"
                        RowLayout {
                            anchors { fill: parent; leftMargin: 16; rightMargin: 12 }
                            Rectangle {
                                width: 32; height: 28; radius: 4
                                color: toggleHover.containsMouse ? "#E3F2FD" : "#EEF2FF"
                                border.color: "#90CAF9"; border.width: 1
                                Text { anchors.centerIn: parent; text: sidebarVisible ? "◀" : "▶"; font.pixelSize: 14; color: "#1976D2" }
                                MouseArea {
                                    id: toggleHover
                                    anchors.fill: parent; hoverEnabled: true
                                    onClicked: sidebarVisible = !sidebarVisible
                                }
                            }
                            Text {
                                text: "文件目录"
                                font.pixelSize: 14; font.bold: true; color: "#424242"
                                Layout.fillWidth: true
                            }
                            Text {
                                text: htmlFileModel.count + " 个文件"
                                font.pixelSize: 12; color: "#9E9E9E"
                            }
                            Rectangle {
                                width: 28; height: 28; radius: 4
                                color: refreshHover.containsMouse ? "#E0E0E0" : "transparent"
                                Text { anchors.centerIn: parent; text: "↺"; font.pixelSize: 16; color: "#616161" }
                                MouseArea {
                                    id: refreshHover
                                    anchors.fill: parent; hoverEnabled: true
                                    onClicked: loadHtmlFiles()
                                }
                            }
                        }
                        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#E0E0E0" }
                    }

                    ListView {
                        id: htmlListView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true; spacing: 1
                        model: htmlFileModel

                        delegate: Rectangle {
                            width: htmlListView.width
                            height: 48
                            color: (selectedHtmlPath === model.path)
                                   ? "#E3F2FD"
                                   : (htmlHover.containsMouse ? "#F5F5F5" : "white")

                            Row {
                                anchors { left: parent.left; leftMargin: 12; right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                                spacing: 8
                                Text { text: model.isMarkdown ? "📝" : "🌐"; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
                                Column {
                                    spacing: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - (model.isMarkdown ? 70 : 30)
                                    Text {
                                        text: model.displayName
                                        font.pixelSize: 12; font.bold: true; color: "#212121"
                                        width: parent.width; elide: Text.ElideRight
                                    }
                                    Text {
                                        text: model.relPath
                                        font.pixelSize: 10; color: "#9E9E9E"
                                        width: parent.width; elide: Text.ElideRight
                                    }
                                }
                            }

                            MouseArea {
                                id: htmlHover
                                anchors.fill: parent
                                hoverEnabled: true
                                onDoubleClicked: () => {
                                    const savedPage = htmlPageMap[model.path] || 1
                                    savedHtmlPage = savedPage
                                    loadHtmlFile(model.path, savedPage)

                                    const fileName = model.displayName || model.path.split(/[/\\]/).pop()
                                    if (chatAgentId > 0) {
                                        kbInput.text = "关于文件《" + fileName + "》："
                                        kbInput.forceActiveFocus()
                                        kbInput.cursorPosition = kbInput.text.length
                                    }
                                }
                            }

                            Row {
                                anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                                spacing: 4
                                visible: model.isMarkdown && htmlHover.containsMouse

                                Rectangle {
                                    width: 26; height: 26; radius: 4
                                    color: editNoteHover.containsMouse ? "#E3F2FD" : "transparent"
                                    Text { anchors.centerIn: parent; text: "✎"; font.pixelSize: 14; color: "#1976D2" }
                                    ToolTip.visible: editNoteHover.containsMouse
                                    ToolTip.text: "编辑笔记"
                                    MouseArea {
                                        id: editNoteHover
                                        anchors.fill: parent; hoverEnabled: true
                                        onClicked: openNoteEditor(model.path)
                                    }
                                }
                                Rectangle {
                                    width: 26; height: 26; radius: 4
                                    color: delNoteHover.containsMouse ? "#FFEBEE" : "transparent"
                                    Text { anchors.centerIn: parent; text: "🗑"; font.pixelSize: 14; color: "#E53935" }
                                    ToolTip.visible: delNoteHover.containsMouse
                                    ToolTip.text: "删除笔记"
                                    MouseArea {
                                        id: delNoteHover
                                        anchors.fill: parent; hoverEnabled: true
                                        onClicked: confirmDeleteNote(model.path)
                                    }
                                }
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width; height: 1; color: "#F0F0F0"
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: htmlFileModel.count === 0
                            text: "暂无 HTML 文件\n转换 PDF 后将在此显示"
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: 13; color: "#BDBDBD"; lineHeight: 1.6
                        }
                    }
                }
            }

            // 右侧：WebEngineView 预览
            Item {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 200

                Rectangle {
                    id: previewToolbar
                    anchors { top: parent.top; left: parent.left; right: parent.right }
                    height: 44
                    color: "#FAFAFA"
                    border.color: "#E0E0E0"; border.width: 1
                    RowLayout {
                        anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                        spacing: 8

                        Text {
                            text: selectedHtmlPath !== ""
                                  ? selectedHtmlPath.split(/[/\\]/).pop()
                                  : "单击左侧文件预览"
                            font.pixelSize: 13; color: selectedHtmlPath !== "" ? "#212121" : "#BDBDBD"
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                        }

                        RowLayout {
                            visible: selectedHtmlPath !== "" && (htmlTotalPages > 0 || totalPagesInFile > 0)
                            spacing: 4
                            Rectangle {
                                width: 28; height: 28; radius: 4
                                color: prevPageHov.containsMouse ? "#E3F2FD" : "transparent"
                                Text { anchors.centerIn: parent; text: "◀"; font.pixelSize: 12; color: "#1976D2" }
                                MouseArea {
                                    id: prevPageHov
                                    anchors.fill: parent; hoverEnabled: true
                                    onClicked: webView.jumpToPage(htmlCurrentPage - 1)
                                }
                            }
                            TextField {
                                id: pageInput
                                text: htmlCurrentInput
                                onTextChanged: htmlCurrentInput = text
                                Layout.preferredWidth: 48
                                Layout.preferredHeight: 28
                                horizontalAlignment: TextInput.AlignHCenter
                                font.pixelSize: 12
                                background: Rectangle { radius: 4; color: "white"; border.color: "#E0E0E0" }
                                Keys.onReturnPressed: {
                                    const p = parseInt(htmlCurrentInput, 10)
                                    if (!isNaN(p)) webView.jumpToPage(p)
                                }
                            }
                            Text {
                                text: "/ " + (isLargeFile ? totalPagesInFile : htmlTotalPages)
                                font.pixelSize: 12; color: "#616161"
                            }
                            Rectangle {
                                width: 28; height: 28; radius: 4
                                color: nextPageHov.containsMouse ? "#E3F2FD" : "transparent"
                                Text { anchors.centerIn: parent; text: "▶"; font.pixelSize: 12; color: "#1976D2" }
                                MouseArea {
                                    id: nextPageHov
                                    anchors.fill: parent; hoverEnabled: true
                                    onClicked: webView.jumpToPage(htmlCurrentPage + 1)
                                }
                            }
                        }

                        Rectangle {
                            visible: selectedHtmlPath !== "" && htmlCurrentPage > 0 && !/\.md$/i.test(selectedHtmlPath)
                            width: 76; height: 28; radius: 4
                            color: savePosHov.containsMouse ? "#E3F2FD" : "transparent"
                            border.color: "#1976D2"; border.width: 1
                            Text { anchors.centerIn: parent; text: "保存位置"; font.pixelSize: 11; color: "#1976D2" }
                            ToolTip.visible: savePosHov.containsMouse
                            ToolTip.text: "将当前第 " + htmlCurrentPage + " 页设为上次阅读位置"
                            MouseArea {
                                id: savePosHov
                                anchors.fill: parent; hoverEnabled: true
                                onClicked: {
                                    savedHtmlPage = htmlCurrentPage
                                    htmlPageMap[selectedHtmlPath] = htmlCurrentPage
                                    htmlPageMap = htmlPageMap
                                    window.kbHtmlPath = selectedHtmlPath
                                    window.kbHtmlPage = htmlCurrentPage
                                    window.kbHtmlPageMapJson = JSON.stringify(htmlPageMap)
                                }
                            }
                        }

                        Rectangle {
                            visible: selectedHtmlPath !== "" && savedHtmlPage > 0 && savedHtmlPage !== htmlCurrentPage && savedHtmlPage <= (isLargeFile ? totalPagesInFile : htmlTotalPages)
                            width: 78; height: 28; radius: 4
                            color: lastReadHov.containsMouse ? "#E8F5E9" : "transparent"
                            border.color: "#81C784"; border.width: 1
                            Text { anchors.centerIn: parent; text: "↩ 上次位置"; font.pixelSize: 11; color: "#2E7D32" }
                            ToolTip.visible: lastReadHov.containsMouse
                            ToolTip.text: "跳转到上次阅读位置：第 " + savedHtmlPage + " 页"
                            MouseArea {
                                id: lastReadHov
                                anchors.fill: parent; hoverEnabled: true
                                onClicked: webView.jumpToPage(savedHtmlPage)
                            }
                        }

                        Rectangle {
                            visible: selectedHtmlPath !== ""
                            width: 28; height: 28; radius: 4
                            color: openBrowserHover.containsMouse ? "#E0E0E0" : "transparent"
                            Text { anchors.centerIn: parent; text: "⬡"; font.pixelSize: 15; color: "#616161" }
                            ToolTip.visible: openBrowserHover.containsMouse
                            ToolTip.text: "在浏览器中打开"
                            MouseArea {
                                id: openBrowserHover
                                anchors.fill: parent; hoverEnabled: true
                                onClicked: pdfConverter.openHtmlFile(selectedHtmlPath)
                            }
                        }
                        Rectangle {
                            visible: selectedHtmlPath !== ""
                            width: 28; height: 28; radius: 4
                            color: backHover.containsMouse ? "#E0E0E0" : "transparent"
                            Text { anchors.centerIn: parent; text: "←"; font.pixelSize: 15; color: "#616161" }
                            MouseArea {
                                id: backHover
                                anchors.fill: parent; hoverEnabled: true
                                onClicked: webView.goBack()
                            }
                        }
                    }
                }

                // Progressive loading progress bar
                Rectangle {
                    id: progressBar
                    visible: isProgressiveLoading
                    anchors { top: previewToolbar.bottom; left: parent.left; right: parent.right }
                    height: 3
                    color: "#E3F2FD"
                    z: 10
                    Rectangle {
                        width: parent.width * loadingProgress
                        height: parent.height
                        color: "#1976D2"
                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }

                WebEngineView {
                    id: webView
                    anchors { top: previewToolbar.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
                    visible: webViewVisible
                    webChannel: kbWebChannel
                    settings.javascriptEnabled: true
                    settings.localContentCanAccessFileUrls: true

                    onNavigationRequested: function(req) {
                        console.log("[KB] navReq url:", req.url, "type:", req.navigationType, "mainFrame:", req.isMainFrame, "accepted:", req.accepted)
                        req.accept()
                        console.log("[KB] navReq after accept, accepted:", req.accepted)
                    }

                    onJavaScriptConsoleMessage: function(level, message, lineNumber, sourceID) {
                        if (!message) return
                        if (message === '__KB_SKELETON_INIT__') {
                            console.log("[KB] skeleton JS initialized in page")
                            return
                        }
                        if (message.indexOf('__KB_PAGE__:') === 0) {
                            const page = parseInt(message.substring('__KB_PAGE__:'.length), 10)
                            kbBridge.reportPage(page)
                            return
                        }
                        if (message.indexOf('__KB_REQ__:') === 0 && isLargeFile) {
                            const parts = message.substring('__KB_REQ__:'.length).split(',')
                            if (parts.length !== 2) return
                            const reqStart = parseInt(parts[0], 10)
                            const reqEnd   = parseInt(parts[1], 10)
                            if (isNaN(reqStart) || isNaN(reqEnd)) return
                            // Collect pages in range not already loaded or pending.
                            var needed = []
                            for (var p = reqStart; p <= reqEnd; p++) {
                                if (!loadedPagesSet[p] && !pendingPages[p]) {
                                    pendingPages[p] = true
                                    needed.push(p)
                                }
                            }
                            if (needed.length > 0) {
                                pendingPages = pendingPages  // notify binding
                                pdfConverter.extractPageRangeAsync(selectedHtmlPath, needed[0], needed[needed.length - 1])
                            }
                            return
                        }
                        // Forward all other JS console messages to Qt output for debugging
                        console.log("[JS]", message)
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: "white"
                        visible: webView.loading && selectedHtmlPath !== ""
                        Column {
                            anchors.centerIn: parent
                            spacing: 8
                            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: parent.visible }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "正在加载中..."
                                font.pixelSize: 13; color: "#9E9E9E"
                            }
                        }
                    }

                    function updatePageCount() {
                        runJavaScript("document.querySelectorAll('div.pf').length", function(result) {
                            htmlTotalPages = result || 0
                        })
                    }

                    function jumpToPage(page) {
                        if (page < 1) return
                        if (page > htmlTotalPages && htmlTotalPages > 0 && !isLargeFile) return
                        htmlCurrentPage = page
                        htmlCurrentInput = "" + page
                        if (isLargeFile) {
                            // For large files check if the page is already loaded in DOM.
                            webView.runJavaScript(
                                "(function(p){" +
                                "var el=document.querySelector('.pf[data-page-no=\"'+p+'\"][data-loaded=\"true\"]');" +
                                "return !!el;" +
                                "})(" + page + ");",
                                function(loaded) {
                                    if (loaded) {
                                        doScrollToPage(page)
                                    } else {
                                        pendingJumpPage = page
                                    }
                                }
                            )
                        } else {
                            runJavaScript("var el=document.querySelector('div.pf[data-page-no=\\'" + page + "\\']'); if(el) el.scrollIntoView({behavior:'instant',block:'start'});")
                        }
                    }

                    onLoadingChanged: function(req) {
                        console.log("[KB] webView loadingChanged status:", req.status, "url:", req.url)
                        if (req.status === WebEngineView.LoadSucceededStatus) {
                            runJavaScript(`
(function(){
    var style = document.createElement('style');
    style.textContent = '.p,.pi,.pF,.pN,.pP,.pT,.pB,.pC{display:none !important;}';
    document.head.appendChild(style);
})();
                            `)
                            updatePageCount()
                            installScrollDetector()
                            // Debug: inspect page 45 specifically
                            Qt.callLater(function() {
                                webView.runJavaScript(
                                    "(function(){" +
                                    "var pc=document.getElementById('page-container');" +
                                    "if(!pc) return 'NO_PC';" +
                                    "var p45=pc.querySelector('.pf[data-page-no=\"45\"]');" +
                                    "if(!p45) return 'NO_PF45';" +
                                    "var r=p45.getBoundingClientRect();" +
                                    "var cs=getComputedStyle(p45);" +
                                    "return 'pf45: x='+Math.round(r.x)+' y='+Math.round(r.y)+' w='+Math.round(r.width)+' h='+Math.round(r.height)+' compW='+cs.width+' compH='+cs.height+' loaded='+p45.dataset.loaded+' pcScrollTop='+pc.scrollTop+' pf45.offsetTop='+p45.offsetTop;" +
                                    "})()",
                                    function(result) { console.log("[KB] pf45-inspect:", result) }
                                )
                            })
                            // Flush pages that arrived while the skeleton was loading.
                            if (pendingInjections.length > 0) {
                                const toInject = pendingInjections
                                pendingInjections = []
                                injectPages(toInject)
                            }
                            // Prefetch the landing page once; further pages come
                            // from the WebChannel scroll detector.
                            chatEngine.prefetchPage(selectedHtmlPath, htmlCurrentPage)
                            // Jump to the saved page position (set when targetPage > 1).
                            if (pendingJumpPage > 0) {
                                const jumpTarget = pendingJumpPage
                                pendingJumpPage = 0
                                Qt.callLater(function() { doScrollToPage(jumpTarget) })
                            }
                            // Start background loading of remaining pages only after
                            // the skeleton is fully loaded — runJavaScript is safe now.
                            if (isLargeFile) Qt.callLater(scheduleBackgroundLoad)
                        } else if (req.status === WebEngineView.LoadFailedStatus) {
                            console.warn("[WebView] load failed:", req.errorString)
                            errorBar.text = "页面加载失败：" + (req.errorString || "未知错误")
                            errorBar.visible = true
                            errorTimer.restart()
                        } else if (req.status === WebEngineView.LoadStoppedStatus) {
                            const skelPath = lastSkeletonPath
                            console.log("[KB] LoadStopped: lastSkeletonPath=", skelPath,
                                        "fileExists=", pdfConverter.fileExists(skelPath))
                        }
                    }
                }

                Timer {
                    id: webViewRevealTimer
                    interval: 200
                    repeat: false
                    onTriggered: {
                        console.log("[KB] webViewRevealTimer fired, pendingWebViewLoad:", JSON.stringify(pendingWebViewLoad))
                        if (pendingWebViewLoad) {
                            const info = pendingWebViewLoad
                            pendingWebViewLoad = null
                            doLoadHtmlFile(info.path, info.page)
                        }
                    }
                }

                // Placeholder shown while no file has been selected.
                Rectangle {
                    anchors.fill: parent
                    color: "white"
                    visible: selectedHtmlPath === ""
                    Text {
                        anchors.centerIn: parent
                        text: "双击左侧 HTML 文件开始预览"
                        font.pixelSize: 14
                        color: "#9E9E9E"
                    }
                }
            }

            // ── 右侧聊天面板 ──
            Rectangle {
                id: chatPanel
                SplitView.preferredWidth: chatPanelVisible ? 300 : 0
                SplitView.minimumWidth: chatPanelVisible ? 220 : 0
                SplitView.maximumWidth: chatPanelVisible ? 480 : 0
                clip: true
                color: "white"
                border.color: "#E0E0E0"; border.width: chatPanelVisible ? 1 : 0

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    // 标题栏
                    Rectangle {
                        Layout.fillWidth: true; height: 44
                        color: "#F5F5F5"
                        border.color: "#E0E0E0"; border.width: 1
                        RowLayout {
                            anchors { fill: parent; leftMargin: 12; rightMargin: 8 }
                            Text {
                                text: chatAgentName.length > 0 ? ("🤖 " + chatAgentName) : "知识库对话"
                                font.pixelSize: 13; font.bold: true; color: "#212121"
                                Layout.fillWidth: true; elide: Text.ElideRight
                            }
                            Rectangle {
                                width: 86; height: 26; radius: 4
                                color: noteBtnHov.containsMouse ? "#E3F2FD" : "transparent"
                                border.color: "#1976D2"; border.width: 1
                                enabled: selectedHtmlPath !== "" && kbChatModel.count > 0
                                opacity: enabled ? 1.0 : 0.5
                                Text {
                                    anchors.centerIn: parent
                                    text: "生成笔记"
                                    font.pixelSize: 11; color: "#1976D2"
                                }
                                ToolTip.visible: noteBtnHov.containsMouse
                                ToolTip.text: "将勾选的对话保存为学习笔记"
                                MouseArea {
                                    id: noteBtnHov
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: generateStudyNote()
                                }
                            }
                            Rectangle {
                                width: 28; height: 28; radius: 4
                                color: switchAgentHov.containsMouse ? "#E0E0E0" : "transparent"
                                Text { anchors.centerIn: parent; text: "⇄"; font.pixelSize: 16; color: "#616161" }
                                ToolTip.visible: switchAgentHov.containsMouse; ToolTip.text: "切换 Agent"
                                MouseArea { id: switchAgentHov; anchors.fill: parent; hoverEnabled: true; onClicked: createAgentDialog.open() }
                            }
                            Rectangle {
                                width: 28; height: 28; radius: 4
                                color: closeChatHov.containsMouse ? "#FFEBEE" : "transparent"
                                Text { anchors.centerIn: parent; text: "✕"; font.pixelSize: 14; color: "#E53935" }
                                MouseArea { id: closeChatHov; anchors.fill: parent; hoverEnabled: true; onClicked: chatPanelVisible = false }
                            }
                        }
                    }

                    // 消息列表
                    ListView {
                        id: kbChatView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        topMargin: 8; bottomMargin: 8
                        model: kbChatModel

                        Text {
                            anchors.centerIn: parent
                            visible: kbChatModel.count === 0
                            text: "开始提问，检索知识库内容"
                            font.pixelSize: 13; color: "#BDBDBD"
                        }

                        delegate: Item {
                            width: kbChatView.width
                            height: bubbleCol.implicitHeight + 8

                            readonly property bool isUser: model.role === "user"
                            readonly property bool isError: model.role === "error"
                            readonly property bool isTool: model.role === "tool_call" || model.role === "tool_result"

                            Column {
                                id: bubbleCol
                                anchors {
                                    left:  isUser ? undefined : parent.left
                                    right: isUser ? parent.right : undefined
                                    leftMargin:  isUser ? 0 : 10
                                    rightMargin: isUser ? 10 : 0
                                    top: parent.top; topMargin: 4
                                }
                                width: Math.min(kbChatView.width * 0.82, 360)

                                CheckBox {
                                    anchors.right: parent.right
                                    height: 22
                                    padding: 0
                                    checked: model.selectedForNote
                                    onClicked: kbChatModel.setProperty(index, "selectedForNote", checked)
                                    ToolTip.visible: hovered
                                    ToolTip.text: "加入学习笔记"
                                }

                                Rectangle {
                                    width: parent.width
                                    height: msgText.implicitHeight + 14
                                    radius: 10
                                    color: isUser ? "#1976D2" : (isError ? "#FFEBEE" : (isTool ? "#F3F3F3" : "#F5F5F5"))
                                    border.color: isError ? "#FFCDD2" : "transparent"

                                    TextArea {
                                        id: msgText
                                        anchors { fill: parent; margins: 8 }
                                        text: model.content
                                        font.pixelSize: 12
                                        color: isUser ? "white" : (isError ? "#C62828" : "#212121")
                                        wrapMode: TextArea.Wrap
                                        readOnly: true
                                        background: Item {}
                                        selectByMouse: true
                                        selectionColor: isUser ? "#64B5F6" : "#BBDEFB"
                                        selectedTextColor: "#0D47A1"
                                    }
                                }
                                Text {
                                    text: model.timestamp
                                    font.pixelSize: 10; color: "#BDBDBD"
                                    anchors.right: isUser ? parent.right : undefined
                                }
                            }
                        }
                    }

                    // 加载指示
                    Rectangle {
                        Layout.fillWidth: true; height: 28
                        visible: chatEngine.isLoading && chatAgentId > 0
                        color: "#F5F5F5"
                        RowLayout {
                            anchors { fill: parent; leftMargin: 12 }
                            BusyIndicator { running: parent.visible; implicitWidth: 20; implicitHeight: 20 }
                            Text {
                                text: toolLogText.length > 0 ? toolLogText : "正在思考..."
                                font.pixelSize: 12; color: "#9E9E9E"
                            }
                        }
                    }

                    // 分隔线
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#E0E0E0" }

                    // 输入区
                    Rectangle {
                        Layout.fillWidth: true
                        height: 100
                        color: "white"

                        RowLayout {
                            anchors { fill: parent; margins: 8 }
                            spacing: 6

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                TextArea {
                                    id: kbInput
                                    anchors.fill: parent
                                    wrapMode: TextArea.Wrap
                                    font.pixelSize: 13
                                    leftPadding: 10
                                    rightPadding: 10
                                    topPadding: 8
                                    bottomPadding: 8
                                    background: Rectangle {
                                        radius: 6; color: "#F5F5F5"
                                        border.color: kbInput.activeFocus ? "#1976D2" : "#E0E0E0"
                                    }
                                    Keys.onReturnPressed: (e) => {
                                        if (e.modifiers & Qt.ShiftModifier) { e.accepted = false; return }
                                        kbSendBtn.doSend()
                                        e.accepted = true
                                    }
                                }

                                Text {
                                    anchors { left: parent.left; leftMargin: 12; top: parent.top; topMargin: 10 }
                                    text: "输入问题，Enter 发送..."
                                    font.pixelSize: 13; color: "#BDBDBD"
                                    visible: kbInput.text.length === 0 && !kbInput.activeFocus
                                }
                            }

                            Rectangle {
                                id: kbSendBtn
                                width: 36; height: 36; radius: 8
                                color: sendHov.containsMouse ? (chatEngine.isLoading ? "#C62828" : "#1565C0") : (chatEngine.isLoading ? "#E53935" : "#1976D2")
                                enabled: chatAgentId > 0 && (chatEngine.isLoading || kbInput.text.trim().length > 0)
                                opacity: enabled ? 1.0 : 0.4

                                function doSend() {
                                    const txt = kbInput.text.trim()
                                    if (txt.length === 0 || chatAgentId < 0) return

                                    kbChatModel.append({ role: "user", content: txt, timestamp: Qt.formatTime(new Date(), "hh:mm:ss"), selectedForNote: false })
                                    Qt.callLater(function() { kbChatView.positionViewAtEnd() })

                                    if (selectedHtmlPath !== "" && htmlCurrentPage > 0
                                        && !/\.md$/i.test(selectedHtmlPath)) {
                                        chatEngine.setCurrentFileContext(selectedHtmlPath, htmlCurrentPage)
                                    } else {
                                        chatEngine.setCurrentFileContext("", 0)
                                    }
                                    chatEngine.sendMessage(txt)
                                    kbInput.text = ""
                                }

                                Text { anchors.centerIn: parent; text: chatEngine.isLoading ? "■" : "↑"; font.pixelSize: chatEngine.isLoading ? 14 : 18; color: "white" }
                                MouseArea {
                                    id: sendHov
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if (chatEngine.isLoading)
                                            chatEngine.cancelRequest()
                                        else
                                            kbSendBtn.doSend()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }  // RowLayout
    }  // ColumnLayout


    // error bar
    Rectangle {
        id: errorBar
        property string text: ""
        visible: false
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
        height: 44
        color: "#FFEBEE"; border.color: "#F44336"; border.width: 1
        Text {
            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
            text: "⚠ " + errorBar.text
            color: "#C62828"; font.pixelSize: 13
        }
        Timer { id: errorTimer; interval: 4000; onTriggered: errorBar.visible = false }
    }
}

