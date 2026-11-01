#include <JuceHeader.h>
#include "MainComponent.h"

class GrooveboxApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Groovebox"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise (const juce::String&) override { window = std::make_unique<MainWindow> (getApplicationName()); }
    void shutdown() override { window = nullptr; }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff262626), allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (GrooveboxApp)
