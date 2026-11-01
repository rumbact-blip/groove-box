#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>
#include <vector>

// Mönster: k/h: '1' = slag. b/l: siffra = skalsteg. '.' = paus.
static const char* kPatterns[4][4] = {
    { "1000100010001000", "1000100110001010", "1001001010010010", "1010101010101010" },
    { "0010001000100010", "1111111111111111", "0010001100100011", "1011101110111011" },
    { "0...0...3...5...", "0.0.0.0.3.3.5.5.", "0..0..0.3..3..5.", "0.0.3.0.5.3.0.2." },
    { "3...5...4...2...", "5.3.4.2.5.3.1.0.", "....3...5...4...", "3.5.4.3.5.4.2.0." }
};
static const int kScale[6] = { 0, 3, 5, 7, 10, 12 };
static const juce::uint32 kTrackColours[4] = { 0xffe85b5b, 0xfff2c94c, 0xff27ae9a, 0xff7b6cf0 };
static const char* kTrackNames[4] = { "Kick", "Hats", "Bas", "Lead" };
static const int kBaseMidi[4] = { 36, 42, 33, 57 };   // MIDI-not som skickas till instrument-plugin

//==============================================================================
class MainComponent : public juce::AudioAppComponent, private juce::Timer
{
public:
    MainComponent()
    {
        formatManager.addFormat (new juce::VST3PluginFormat());
        loadKnownPlugins();

        lnf.setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff262626));
        lnf.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff313131));
        lnf.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffd4d4d4));
        lnf.setColour (juce::Slider::thumbColourId, juce::Colour (0xffff7a1a));
        lnf.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xffff7a1a));
        lnf.setColour (juce::Slider::trackColourId, juce::Colour (0xffff7a1a));
        setLookAndFeel (&lnf);

        for (int t = 0; t < 4; ++t)
        {
            vol[t].store (0.8f); groove[t].store (0.f); mute[t].store (false);
            active[t].store (-1); queued[t].store (-1);

            auto& n = names.add (new juce::Label ({}, kTrackNames[t]));
            n.setJustificationType (juce::Justification::centred);
            n.setColour (juce::Label::backgroundColourId, juce::Colour (kTrackColours[t]));
            n.setColour (juce::Label::textColourId, juce::Colour (0xff151515));
            addAndMakeVisible (n);

            for (int s = 0; s < 4; ++s)
            {
                auto* b = clips.add (new juce::TextButton (juce::String (s + 1)));
                b->onClick = [this, t, s] { launch (t, s); };
                addAndMakeVisible (b);
            }

            auto* v = volSliders.add (new juce::Slider (juce::Slider::LinearVertical, juce::Slider::NoTextBox));
            v->setRange (0.0, 1.0); v->setValue (0.8);
            v->onValueChange = [this, t, v] { vol[t].store ((float) v->getValue()); };
            addAndMakeVisible (v);

            auto* g = grooveSliders.add (new juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox));
            g->setRange (0.0, 1.0); g->setValue (0.0);
            g->onValueChange = [this, t, g] { groove[t].store ((float) g->getValue()); };
            addAndMakeVisible (g);

            auto* m = mutes.add (new juce::ToggleButton ("M"));
            m->onClick = [this, t, m] { mute[t].store (m->getToggleState()); };
            addAndMakeVisible (m);

            auto* pb = pluginButtons.add (new juce::TextButton ("Inbyggd"));
            pb->onClick = [this, t] { showPluginMenu (t); };
            addAndMakeVisible (pb);
        }

        for (int s = 0; s < 4; ++s)
        {
            auto* b = sceneButtons.add (new juce::TextButton (juce::String::fromUTF8 ("\xe2\x96\xb6")));
            b->onClick = [this, s] { for (int t = 0; t < 4; ++t) launch (t, s); };
            addAndMakeVisible (b);
        }

        playButton.setButtonText ("Spela");  playButton.onClick = [this] { playing.store (true); };
        stopButton.setButtonText ("Stoppa"); stopButton.onClick = [this]
        {
            playing.store (false); stepIndex.store (0);
            for (int t = 0; t < 4; ++t) { active[t].store (-1); queued[t].store (-1); }
        };
        audioButton.setButtonText ("Ljudinställningar");
        audioButton.onClick = [this] { showAudioSettings(); };
        scanButton.setButtonText ("Sök VST3");
        scanButton.onClick = [this] { scanPlugins(); };
        bpmSlider.setRange (60.0, 180.0, 1.0); bpmSlider.setValue (120.0);
        bpmSlider.setTextValueSuffix (" BPM");
        bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 80, 22);
        bpmSlider.onValueChange = [this] { bpm.store ((float) bpmSlider.getValue()); };
        posLabel.setColour (juce::Label::textColourId, juce::Colour (0xffff7a1a));
        posLabel.setText ("1.1", juce::dontSendNotification);

        for (auto* c : { (juce::Component*) &playButton, &stopButton, &audioButton, &scanButton, &bpmSlider, &posLabel })
            addAndMakeVisible (c);

        setSize (800, 600);
        setAudioChannels (0, 2);
        startTimerHz (30);
    }

    ~MainComponent() override
    {
        stopTimer();
        for (auto& w : windows) w.reset();
        setLookAndFeel (nullptr);
        shutdownAudio();
        for (auto& p : plugins) if (p) { p->releaseResources(); p.reset(); }
    }

    //==========================================================================
    void prepareToPlay (int expectedBlock, double sr) override
    {
        sampleRate = sr;
        bufSize = juce::jmax (expectedBlock, 1024);
        stepCounter = 0.0;
        for (auto& v : voices) v = Voice();
        for (int t = 0; t < 4; ++t) mono[t].assign ((size_t) bufSize, 0.f);
        mixL.assign ((size_t) bufSize, 0.f); mixR.assign ((size_t) bufSize, 0.f);
        for (auto& b : pbuf) b.setSize (8, bufSize);
        const juce::ScopedLock sl (pluginLock);
        for (auto& p : plugins)
            if (p) { p->setRateAndBufferSizeDetails (sr, bufSize); p->prepareToPlay (sr, bufSize); }
    }
    void releaseResources() override {}

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        info.clearActiveBufferRegion();
        if (! playing.load())
        {
            if (lastPlaying)
            {
                lastPlaying = false;
                const juce::ScopedTryLock tl (pluginLock);
                for (int t = 0; t < 4; ++t) { lastNote[t] = -1; noteOffIn[t] = 0; if (tl.isLocked() && plugins[t]) plugins[t]->reset(); }
            }
            return;
        }
        if (! lastPlaying) { lastPlaying = true; stepCounter = 1.0e9; stepIndex.store (0); firstStep = true; }

        auto* left  = info.buffer->getWritePointer (0, info.startSample);
        auto* right = info.buffer->getNumChannels() > 1 ? info.buffer->getWritePointer (1, info.startSample) : nullptr;
        int done = 0;
        while (done < info.numSamples)
        {
            const int n = juce::jmin (info.numSamples - done, bufSize);
            processChunk (left + done, right ? right + done : nullptr, n);
            done += n;
        }
    }

    //==========================================================================
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff262626)); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        auto top = r.removeFromTop (34);
        playButton.setBounds (top.removeFromLeft (70)); top.removeFromLeft (6);
        stopButton.setBounds (top.removeFromLeft (70)); top.removeFromLeft (6);
        audioButton.setBounds (top.removeFromLeft (130)); top.removeFromLeft (6);
        scanButton.setBounds (top.removeFromLeft (100)); top.removeFromLeft (10);
        posLabel.setBounds (top.removeFromRight (60));
        bpmSlider.setBounds (top);
        r.removeFromTop (10);

        const int sceneW = 48, gap = 4;
        const int colW = (r.getWidth() - sceneW - gap * 4) / 4;
        for (int t = 0; t < 4; ++t)
        {
            const int x = r.getX() + t * (colW + gap);
            names[t]->setBounds (x, r.getY(), colW, 28);
            for (int s = 0; s < 4; ++s)
                clips[t * 4 + s]->setBounds (x, r.getY() + 32 + s * 44, colW, 40);
            const int my = r.getY() + 32 + 4 * 44 + 6;
            mutes[t]->setBounds (x, my, colW, 24);
            pluginButtons[t]->setBounds (x, my + 28, colW, 26);
            grooveSliders[t]->setBounds (x + colW / 2 - 24, my + 58, 48, 48);
            volSliders[t]->setBounds (x + colW / 2 - 14, my + 110, 28, juce::jmax (40, r.getBottom() - (my + 114)));
        }
        for (int s = 0; s < 4; ++s)
            sceneButtons[s]->setBounds (r.getRight() - sceneW, r.getY() + 32 + s * 44, sceneW, 40);
    }

private:
    //==========================================================================
    struct Voice { bool on = false; float phase = 0, freq = 0, vel = 1, noisePrev = 0, lp = 0; double age = 0; };

    struct PluginWindow : juce::DocumentWindow
    {
        PluginWindow (juce::AudioProcessorEditor* ed, std::function<void()> onClose)
            : DocumentWindow (ed->getName(), juce::Colour (0xff262626), closeButton), closeFn (std::move (onClose))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (ed, true);
            setResizable (ed->isResizable(), false);
            centreAroundComponent (nullptr, getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { closeFn(); }
        std::function<void()> closeFn;
    };

    //======================= Spår, launcher och groove ========================
    void launch (int t, int s)
    {
        playing.store (true);
        queued[t].store (active[t].load() == s && queued[t].load() == -1 ? -2 : s);
    }

    void onStep (int step, int i, const bool* inst)
    {
        const int pos = step % 16;
        if (pos == 0)
            for (int t = 0; t < 4; ++t)
            {
                const int q = queued[t].exchange (-1);
                if (q != -1) active[t].store (q == -2 ? -1 : q);
            }
        for (int t = 0; t < 4; ++t)
        {
            const int a = active[t].load();
            pending[t] = -1;
            if (a < 0) continue;
            const char c = kPatterns[t][a][pos];
            if (c == '.' || (t < 2 && c == '0')) continue;
            const float g = groove[t].load();
            pendingNote[t] = (t < 2) ? 0 : kScale[juce::jlimit (0, 5, c - '0')];
            pendingVel[t]  = (pos % 4 == 0) ? 1.f : 1.f - 0.45f * g;
            pending[t]     = (pos % 2 == 1) ? (int) (g * sps * 0.6) : 0;   // groove: förskjuter udda 16-delar
            if (pending[t] == 0) { fire (t, i, inst); pending[t] = -1; }
        }
    }

    void fire (int t, int i, const bool* inst)
    {
        if (inst[t])
        {
            const int note = kBaseMidi[t] + pendingNote[t];
            if (lastNote[t] >= 0) midi[t].addEvent (juce::MidiMessage::noteOff (1, lastNote[t]), i);
            midi[t].addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, (int) (pendingVel[t] * 110.f))), i);
            lastNote[t] = note;
            noteOffIn[t] = juce::jmax (1, (int) (sps * 0.9));
        }
        else trigger (t, pendingNote[t], pendingVel[t]);
    }

    void processChunk (float* L, float* R, int n)
    {
        sps = sampleRate * 60.0 / (double) bpm.load() / 4.0;
        const juce::ScopedTryLock tl (pluginLock);
        bool inst[4] = {}, fx[4] = {};
        for (int t = 0; t < 4; ++t)
        {
            midi[t].clear();
            std::fill (mono[t].begin(), mono[t].begin() + n, 0.f);
            if (tl.isLocked() && plugins[t])
            {
                if (plugins[t]->getPluginDescription().isInstrument) inst[t] = true; else fx[t] = true;
            }
        }

        for (int i = 0; i < n; ++i)
        {
            stepCounter += 1.0;
            if (stepCounter >= sps)
            {
                stepCounter -= sps;
                if (firstStep) { firstStep = false; stepCounter = 0.0; } else stepIndex.fetch_add (1);
                onStep (stepIndex.load(), i, inst);
            }
            for (int t = 0; t < 4; ++t)
            {
                if (pending[t] >= 0 && --pending[t] < 0) fire (t, i, inst);
                if (inst[t] && noteOffIn[t] > 0 && --noteOffIn[t] == 0 && lastNote[t] >= 0)
                { midi[t].addEvent (juce::MidiMessage::noteOff (1, lastNote[t]), i); lastNote[t] = -1; }
                if (! inst[t]) mono[t][(size_t) i] = renderVoice (t);
            }
        }

        std::fill (mixL.begin(), mixL.begin() + n, 0.f);
        std::fill (mixR.begin(), mixR.begin() + n, 0.f);
        for (int t = 0; t < 4; ++t)
        {
            const float gain = vol[t].load() * (mute[t].load() ? 0.f : 1.f);
            if (inst[t] || fx[t])
            {
                auto& b = pbuf[t];
                b.clear();
                if (fx[t])
                    for (int i = 0; i < n; ++i) { b.setSample (0, i, mono[t][(size_t) i]); b.setSample (1, i, mono[t][(size_t) i]); }
                juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), b.getNumChannels(), n);
                plugins[t]->processBlock (view, midi[t]);
                for (int i = 0; i < n; ++i) { mixL[(size_t) i] += b.getSample (0, i) * gain; mixR[(size_t) i] += b.getSample (1, i) * gain; }
            }
            else
                for (int i = 0; i < n; ++i) { mixL[(size_t) i] += mono[t][(size_t) i] * gain; mixR[(size_t) i] += mono[t][(size_t) i] * gain; }
        }
        for (int i = 0; i < n; ++i)
        {
            L[i] = std::tanh (mixL[(size_t) i] * 0.8f);
            if (R) R[i] = std::tanh (mixR[(size_t) i] * 0.8f);
        }
    }

    //========================== Inbyggd synt ==================================
    void trigger (int t, int note, float vel)
    {
        auto& v = voices[t];
        v.on = true; v.age = 0; v.vel = vel; v.phase = 0;
        if (t == 2) v.freq = 55.f  * std::pow (2.f, note / 12.f);
        if (t == 3) v.freq = 220.f * std::pow (2.f, note / 12.f);
    }

    float renderVoice (int t)
    {
        auto& v = voices[t];
        if (! v.on) return 0.f;
        const float ts = (float) (v.age / sampleRate);
        v.age += 1.0;
        float out = 0.f;
        const float twoPi = juce::MathConstants<float>::twoPi;
        switch (t)
        {
            case 0: { const float f = 40.f + 110.f * std::exp (-ts * 35.f);
                      v.phase += twoPi * f / (float) sampleRate;
                      out = std::sin (v.phase) * std::exp (-ts * 8.f); break; }
            case 1: { const float nz = rng.nextFloat() * 2.f - 1.f;
                      out = (nz - v.noisePrev) * 0.5f * std::exp (-ts * 60.f); v.noisePrev = nz; break; }
            default: { v.phase += v.freq / (float) sampleRate;
                       if (v.phase >= 1.f) v.phase -= 1.f;
                       const float raw = (t == 2) ? (v.phase * 2.f - 1.f) : (v.phase < 0.5f ? 1.f : -1.f);
                       const float cutoff = (t == 2 ? 1800.f : 2700.f) * (0.25f + 0.75f * std::exp (-ts * 6.f));
                       const float a = 1.f - std::exp (-twoPi * cutoff / (float) sampleRate);
                       v.lp += a * (raw - v.lp);
                       out = v.lp * std::exp (-ts * (t == 2 ? 4.f : 6.f)) * 0.4f; break; }
        }
        if (ts > 1.f) v.on = false;
        return out * v.vel;
    }

    //========================== VST3-hosting ==================================
    juce::File pluginListFile() const
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Groovebox").getChildFile ("plugins.xml");
    }

    void loadKnownPlugins()
    {
        if (auto xml = juce::parseXML (pluginListFile())) knownPlugins.recreateFromXml (*xml);
    }

    void saveKnownPlugins()
    {
        pluginListFile().getParentDirectory().createDirectory();
        if (auto xml = knownPlugins.createXml()) xml->writeTo (pluginListFile());
    }

    void scanPlugins()
    {
        if (scanning) return;
        scanning = true;
        scanButton.setButtonText ("Söker...");
        juce::Component::SafePointer<MainComponent> safe (this);
        juce::Thread::launch ([safe, this]
        {
            juce::VST3PluginFormat fmt;
            const auto dead = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("groovebox_scan_dead.txt");
            juce::PluginDirectoryScanner sc (knownPlugins, fmt, fmt.getDefaultLocationsToSearch(), true, dead);
            juce::String name;
            while (sc.scanNextFile (true, name)) {}
            juce::MessageManager::callAsync ([safe]
            {
                if (safe == nullptr) return;
                safe->scanning = false;
                safe->scanButton.setButtonText ("Sök VST3");
                safe->saveKnownPlugins();
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Klart",
                    juce::String (safe->knownPlugins.getNumTypes()) + " plugin hittades.");
            });
        });
    }

    void showPluginMenu (int t)
    {
        juce::PopupMenu m;
        m.addItem (1, "Inbyggd synt", true, ! plugins[t]);
        if (plugins[t]) m.addItem (2, "Visa pluginfönster");
        m.addSeparator();
        auto types = knownPlugins.getTypes();
        if (types.isEmpty()) m.addItem (3, "Inga plugin. Tryck på Sök VST3.", false);
        for (int i = 0; i < types.size(); ++i)
            m.addItem (100 + i, types[i].name + (types[i].isInstrument ? "  (instrument)" : "  (effekt)"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pluginButtons[t]),
            [this, t, types, safe = juce::Component::SafePointer<MainComponent> (this)] (int id)
            {
                if (safe == nullptr || id == 0) return;
                if (id == 1) removePlugin (t);
                else if (id == 2) showPluginWindow (t);
                else if (id >= 100) loadPlugin (t, types[id - 100]);
            });
    }

    void loadPlugin (int t, const juce::PluginDescription& desc)
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        formatManager.createPluginInstanceAsync (desc, sampleRate > 0 ? sampleRate : 44100.0, bufSize,
            [safe, t] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err)
            {
                if (safe == nullptr) return;
                if (! inst)
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Kunde inte ladda plugin", err);
                    return;
                }
                safe->installPlugin (t, std::move (inst));
            });
    }

    void installPlugin (int t, std::unique_ptr<juce::AudioPluginInstance> inst)
    {
        windows[t].reset();
        inst->enableAllBuses();
        inst->setRateAndBufferSizeDetails (sampleRate, bufSize);
        inst->prepareToPlay (sampleRate, bufSize);
        pluginButtons[t]->setButtonText (inst->getName());
        std::unique_ptr<juce::AudioPluginInstance> old;
        {
            const juce::ScopedLock sl (pluginLock);
            old = std::move (plugins[t]);
            plugins[t] = std::move (inst);
            lastNote[t] = -1; noteOffIn[t] = 0;
        }
        if (old) old->releaseResources();
        showPluginWindow (t);
    }

    void removePlugin (int t)
    {
        windows[t].reset();
        std::unique_ptr<juce::AudioPluginInstance> old;
        { const juce::ScopedLock sl (pluginLock); old = std::move (plugins[t]); lastNote[t] = -1; noteOffIn[t] = 0; }
        if (old) old->releaseResources();
        pluginButtons[t]->setButtonText ("Inbyggd");
    }

    void showPluginWindow (int t)
    {
        if (! plugins[t]) return;
        if (windows[t]) { windows[t]->toFront (true); return; }
        juce::AudioProcessorEditor* ed = plugins[t]->hasEditor() ? plugins[t]->createEditorIfNeeded() : nullptr;
        if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor (*plugins[t]);
        juce::Component::SafePointer<MainComponent> safe (this);
        windows[t] = std::make_unique<PluginWindow> (ed, [safe, t]
        {
            juce::MessageManager::callAsync ([safe, t] { if (safe != nullptr) safe->windows[t].reset(); });
        });
    }

    void showAudioSettings()
    {
        auto* sel = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 0, 0, 2, false, false, true, false);
        sel->setSize (520, 420);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (sel);
        o.dialogTitle = "Ljudinställningar (välj ASIO-driver)";
        o.componentToCentreAround = this;
        o.dialogBackgroundColour = juce::Colour (0xff262626);
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    void timerCallback() override
    {
        for (int t = 0; t < 4; ++t)
            for (int s = 0; s < 4; ++s)
            {
                auto* b = clips[t * 4 + s];
                juce::Colour c (0xff313131);
                if (active[t].load() == s) c = juce::Colour (kTrackColours[t]);
                else if (queued[t].load() == s) c = juce::Colour (0xff4a4a4a);
                b->setColour (juce::TextButton::buttonColourId, c);
                b->setColour (juce::TextButton::textColourOffId,
                              active[t].load() == s ? juce::Colour (0xff151515) : juce::Colour (0xffd4d4d4));
                b->repaint();
            }
        const int st = stepIndex.load();
        posLabel.setText (juce::String (st / 16 % 4 + 1) + "." + juce::String (st % 16 / 4 + 1), juce::dontSendNotification);
    }

    //==========================================================================
    juce::LookAndFeel_V4 lnf;
    juce::OwnedArray<juce::Label> names;
    juce::OwnedArray<juce::TextButton> clips, sceneButtons, pluginButtons;
    juce::OwnedArray<juce::Slider> volSliders, grooveSliders;
    juce::OwnedArray<juce::ToggleButton> mutes;
    juce::TextButton playButton, stopButton, audioButton, scanButton;
    juce::Slider bpmSlider;
    juce::Label posLabel;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    std::unique_ptr<juce::AudioPluginInstance> plugins[4];
    std::unique_ptr<PluginWindow> windows[4];
    juce::CriticalSection pluginLock;
    bool scanning = false;

    std::atomic<float> vol[4], groove[4], bpm { 120.f };
    std::atomic<bool> mute[4], playing { false };
    std::atomic<int> active[4], queued[4], stepIndex { 0 };

    // Endast ljudtråden:
    Voice voices[4];
    int pending[4] = { -1, -1, -1, -1 }, pendingNote[4] = {}, lastNote[4] = { -1, -1, -1, -1 }, noteOffIn[4] = {};
    float pendingVel[4] = { 1, 1, 1, 1 };
    double sampleRate = 44100.0, stepCounter = 0.0, sps = 11025.0;
    int bufSize = 1024;
    bool lastPlaying = false, firstStep = true;
    juce::Random rng;
    std::vector<float> mono[4], mixL, mixR;
    juce::AudioBuffer<float> pbuf[4];
    juce::MidiBuffer midi[4];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
