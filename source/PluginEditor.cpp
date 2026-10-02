#include "PluginEditor.h"

#include "BinaryData.h"

namespace
{
    juce::String mimeTypeFor (const juce::String& path)
    {
        const auto ext = path.fromLastOccurrenceOf (".", false, false).toLowerCase();

        if (ext == "html") return "text/html";
        if (ext == "js")   return "text/javascript";
        if (ext == "css")  return "text/css";
        if (ext == "json") return "application/json";
        if (ext == "mei")  return "application/xml";
        if (ext == "svg")  return "image/svg+xml";
        if (ext == "wasm") return "application/wasm";
        return "application/octet-stream";
    }

    juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> props)
    {
        auto* o = new juce::DynamicObject();

        for (const auto& [name, value] : props)
            o->setProperty (name, value);

        return juce::var (o);
    }
}

TranscriberEditor::TranscriberEditor (TranscriberProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      browserSupported (juce::WebBrowserComponent::areOptionsSupported (makeBrowserOptions())),
      browser (makeBrowserOptions())
{
    processor.getLog().logEvent ("editorOpened", makeObject ({ { "webView2Supported", browserSupported } }));

    addAndMakeVisible (browser);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (640, 420, 2400, 1800);
    setSize (1000, 720);

    startTimerHz (5);
}

TranscriberEditor::~TranscriberEditor()
{
    processor.getLog().logEvent ("editorClosed");
}

juce::WebBrowserComponent::Options TranscriberEditor::makeBrowserOptions()
{
    using Options = juce::WebBrowserComponent::Options;

    return Options{}
        .withBackend (Options::Backend::webview2)
        .withWinWebView2Options (Options::WinWebView2{}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                              .getChildFile ("Transcriber-WebView2"))
                                     .withStatusBarDisabled()
                                     .withBuiltInErrorPageDisabled())
        .withNativeIntegrationEnabled()
        .withResourceProvider ([] (const auto& url) { return getResource (url); })
        .withInitialisationData ("instance", processor.instanceId)
        .withEventListener ("log", [this] (juce::var data)
        {
            processor.getLog().logEvent ("ui", data);
        })
        .withEventListener ("pageReady", [this] (juce::var)
        {
            lastPreviewRevision = ~(uint64_t) 0;   // the page is (re)loaded: send the picture again
        })
        .withEventListener ("capArm", [this] (juce::var)   { processor.getCapture().arm(); })
        .withEventListener ("capStop", [this] (juce::var)  { processor.getCapture().stop(); })
        .withEventListener ("capMode", [this] (juce::var data)
        {
            processor.getCapture().setMode (data.getProperty ("mode", {}).toString() == "oneLoop" ? trs::ReadingMode::oneLoop
                                                                                                 : trs::ReadingMode::asPlayed);
        })
        .withEventListener ("capLoopBars", [this] (juce::var data)
        {
            processor.getCapture().setLoopBars ((int) data.getProperty ("bars", 0));
        })
        .withEventListener ("capRedetect", [this] (juce::var) { processor.getCapture().redetect(); })
        .withEventListener ("diagnostics", [this] (juce::var data)
        {
            processor.setDiagnosticsEnabled ((bool) data.getProperty ("enabled", false));
        })
        .withEventListener ("compareMidi", [this] (juce::var) { compareWithMidiFile(); })
        .withEventListener ("savePdf", [this] (juce::var data) { savePdf (data); })
        .withEventListener ("verSelect", [this] (juce::var data)    { processor.getCapture().selectVersion (data.getProperty ("id", {}).toString()); })
        .withEventListener ("verRename", [this] (juce::var data)    { processor.getCapture().renameVersion (data.getProperty ("id", {}).toString(), data.getProperty ("name", {}).toString()); })
        .withEventListener ("verDuplicate", [this] (juce::var data) { processor.getCapture().duplicateVersion (data.getProperty ("id", {}).toString()); })
        .withEventListener ("verDelete", [this] (juce::var data)    { processor.getCapture().deleteVersion (data.getProperty ("id", {}).toString()); })
        .withEventListener ("openLogFolder", [this] (juce::var)
        {
            processor.getLog().getFile().revealToUser();
        });
}

std::optional<juce::WebBrowserComponent::Resource> TranscriberEditor::getResource (const juce::String& url)
{
    auto path = url.upToFirstOccurrenceOf ("?", false, false).fromFirstOccurrenceOf ("/", false, false);

    if (path.isEmpty())
        path = "index.html";

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (path != BinaryData::originalFilenames[i])
            continue;

        int size = 0;
        const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);

        if (data == nullptr)
            return std::nullopt;

        juce::WebBrowserComponent::Resource resource;
        const auto* bytes = reinterpret_cast<const std::byte*> (data);
        resource.data.assign (bytes, bytes + size);
        resource.mimeType = mimeTypeFor (path);
        return resource;
    }

    return std::nullopt;
}

void TranscriberEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b2130));

    if (! browserSupported)
    {
        g.setColour (juce::Colours::white);
        g.setFont (16.0f);
        g.drawFittedText ("WebView2 is not available in this host. This result has been logged.",
                          getLocalBounds().reduced (20), juce::Justification::centred, 3);
    }
}

void TranscriberEditor::resized()
{
    browser.setBounds (getLocalBounds());

    processor.getLog().logEvent ("resize", makeObject ({
        { "width", getWidth() },
        { "height", getHeight() },
        { "scale", juce::Component::getApproximateScaleFactorForComponent (this) }
    }));
}

void TranscriberEditor::timerCallback()
{
    const auto b = processor.getLatestBlockInfo();
    using Log = DiagnosticLog;

    auto* o = new juce::DynamicObject();
    o->setProperty ("instance", processor.instanceId);
    o->setProperty ("host", juce::PluginHostType().getHostDescription());
    o->setProperty ("hasPos", (b.flags & Log::hasPosition) != 0);
    o->setProperty ("playing", (b.flags & Log::playing) != 0);
    o->setProperty ("looping", (b.flags & Log::looping) != 0);
    o->setProperty ("offline", (b.flags & Log::nonRealtime) != 0);
    if (b.flags & Log::hasPpq)        o->setProperty ("ppq", b.ppq);
    if (b.flags & Log::hasBpm)        o->setProperty ("bpm", b.bpm);
    if (b.flags & Log::hasTimeSig)    o->setProperty ("ts", juce::String (b.tsNum) + "/" + juce::String (b.tsDen));
    if (b.flags & Log::hasLastBarPpq) o->setProperty ("barPpq", b.lastBarPpq);
    if (b.flags & Log::hasBarCount)   o->setProperty ("bars", (juce::int64) b.barCount);
    if (b.flags & Log::hasLoop)       o->setProperty ("loop", juce::String (b.loopStart, 3) + " - " + juce::String (b.loopEnd, 3));
    o->setProperty ("sampleRate", processor.getSampleRateForDisplay());
    o->setProperty ("diagnostics", processor.getLog().isEnabled());
    o->setProperty ("logFile", processor.getLog().isEnabled() ? processor.getLog().getFile().getFullPathName() : juce::String ("(diagnostic log is off)"));
    o->setProperty ("logLines", (juce::int64) processor.getLog().getLinesWritten());
    o->setProperty ("dropped", processor.getLog().getDroppedCount());
    o->setProperty ("scale", juce::Component::getApproximateScaleFactorForComponent (this));

    browser.emitEventIfBrowserIsVisible ("status", juce::var (o));
    browser.emitEventIfBrowserIsVisible ("capture", processor.getCapture().getStatus());

    // The picture of the notes can be large, so send it only when it has changed, and not too often.
    const auto revision = processor.getCapture().getRevision();
    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (browser.isShowing() && revision != lastPreviewRevision && now - lastPreviewMs > 500.0)
    {
        lastPreviewRevision = revision;
        lastPreviewMs = now;
        browser.emitEventIfBrowserIsVisible ("preview", processor.getCapture().getPreview());
    }
}

void TranscriberEditor::compareWithMidiFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the MIDI clip exported from Live",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.mid;*.midi");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        const auto result = ::compareWithMidiFile (file, processor.getCapture().getResolvedNotes(), 0.01);
        processor.getLog().logEvent ("compare", result.report);
        browser.emitEventIfBrowserIsVisible ("compareResult", result.report);
    });
}

void TranscriberEditor::savePdf (const juce::var& payload)
{
    auto base64 = payload.getProperty ("base64", {}).toString();

    if (base64.startsWith ("data:"))
        base64 = base64.fromFirstOccurrenceOf (",", false, false);

    auto pdf = std::make_shared<juce::MemoryBlock>();
    {
        juce::MemoryOutputStream out (*pdf, false);

        if (! juce::Base64::convertFromBase64 (out, base64))
        {
            browser.emitEventIfBrowserIsVisible ("pdfSaved", makeObject ({ { "error", "The PDF data could not be decoded." } }));
            return;
        }
    }

    chooser = std::make_unique<juce::FileChooser> ("Save test PDF",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                       .getChildFile ("Transcriber phase 1 test.pdf"),
                                                   "*.pdf");

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, pdf] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();

        if (file == juce::File())
            return;

        const auto ok = file.replaceWithData (pdf->getData(), pdf->getSize());
        const auto info = makeObject ({ { "path", file.getFullPathName() },
                                        { "bytes", (juce::int64) pdf->getSize() },
                                        { "ok", ok } });
        processor.getLog().logEvent ("pdfSaved", info);
        browser.emitEventIfBrowserIsVisible ("pdfSaved", info);
    });
}
