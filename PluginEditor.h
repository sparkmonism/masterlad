#pragma once

#include "MasterProcessor.h"
#include <functional>
#include <memory>

// Songcraft Audio palette for Masterlad: dark purple with a turquoise accent.
namespace ml
{
inline const juce::Colour kBg      { 0xff150f20 };
inline const juce::Colour kPanel   { 0xff1d1530 };
inline const juce::Colour kPanelHi { 0xff2a1f42 };
inline const juce::Colour kLine    { 0xff3d2f5c };
inline const juce::Colour kTrack   { 0xff352a50 };
inline const juce::Colour kText    { 0xffefe9f8 };
inline const juce::Colour kMuted   { 0xff9a8db4 };
inline const juce::Colour kAccent  { 0xff2fe0cc };
inline const juce::Colour kRed     { 0xffff5a6e };
} // namespace ml

using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

//==============================================================================
class MasterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MasterLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
};

//==============================================================================
// Rotary knob with a title above and a value box below.
class KnobCell : public juce::Component
{
public:
    KnobCell (juce::AudioProcessorValueTreeState& state, const juce::String& title,
              const juce::String& suffix, int decimals, bool big = false);

    void bind (const juce::String& paramId);
    void resized() override;

    juce::Slider slider;
    juce::Label label;

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String unit;
    int places;
    std::unique_ptr<SliderAttachment> att;
};

//==============================================================================
// Spectrum analyser with a draggable 6-band EQ curve on top.
class EqGraph : public juce::Component
{
public:
    EqGraph (MasterProcessor&, std::function<void (int)> onSelect, std::function<int()> selected);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void updateSpectrum();

private:
    juce::Rectangle<float> plot() const;
    float freqToX (float f) const;
    float xToFreq (float x) const;
    float gainToY (float g) const;
    float yToGain (float y) const;
    int nodeAt (juce::Point<float> p) const;
    float bandValue (int band, const char* suffix) const;
    void setBandValue (int band, const char* suffix, float value);

    MasterProcessor& proc;
    std::function<void (int)> selectFn;
    std::function<int()> selectedFn;

    juce::dsp::FFT fft { 11 };
    std::array<float, 4096> fftData {};
    std::array<float, 1025> spec {};

    int dragBand = -1;
};

//==============================================================================
class EqPage : public juce::Component
{
public:
    explicit EqPage (MasterProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();

private:
    void selectBand (int b);

    MasterProcessor& proc;
    int selected = 0;

    EqGraph graph;
    KnobCell freqK, gainK, qK;
    juce::ToggleButton bandOn;
    std::array<juce::TextButton, MasterProcessor::kEqBands> bandBtn;
    juce::ComboBox modeBox;

    std::unique_ptr<ButtonAttachment> onAtt;
    std::unique_ptr<ComboAttachment> modeAtt;
};

//==============================================================================
class DynPage : public juce::Component
{
public:
    explicit DynPage (MasterProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();

private:
    MasterProcessor& proc;
    std::array<std::unique_ptr<KnobCell>, 20> knobs;   // 5 per band
    std::array<juce::ToggleButton, MasterProcessor::kDynBands> bypass;
    std::array<std::unique_ptr<ButtonAttachment>, MasterProcessor::kDynBands> bypassAtt;
    std::array<std::unique_ptr<KnobCell>, 3> xover;
    std::array<float, MasterProcessor::kDynBands> grShown {};
};

//==============================================================================
class ExcPage : public juce::Component
{
public:
    explicit ExcPage (MasterProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    MasterProcessor& proc;
    KnobCell driveK, amountK, freqK;
    juce::ComboBox modeBox;
    std::unique_ptr<ComboAttachment> modeAtt;
};

//==============================================================================
class ImgPage : public juce::Component
{
public:
    explicit ImgPage (MasterProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    MasterProcessor& proc;
    std::array<std::unique_ptr<KnobCell>, 4> width;
    std::array<std::unique_ptr<KnobCell>, 3> xover;
};

//==============================================================================
class MaxPage : public juce::Component
{
public:
    explicit MaxPage (MasterProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();

private:
    MasterProcessor& proc;
    KnobCell gainK, ceilK, relK;
    juce::ToggleButton autoBtn;
    std::unique_ptr<ButtonAttachment> autoAtt;
    float grShown = 0.0f;
};

//==============================================================================
class MeterPanel : public juce::Component
{
public:
    explicit MeterPanel (MasterProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    MasterProcessor& proc;
    juce::Slider inSlider;
    std::unique_ptr<SliderAttachment> inAtt;
    juce::TextButton resetBtn { "RESET" };

    float inDb[2] { -100.0f, -100.0f }, outDb[2] { -100.0f, -100.0f };
    float inHold[2] { -100.0f, -100.0f }, outHold[2] { -100.0f, -100.0f };
    int inHoldAge[2] { 0, 0 }, outHoldAge[2] { 0, 0 };
    float mom = -120.0f, st = -120.0f, integ = -120.0f, tp = -120.0f, corr = 0.0f;
};

//==============================================================================
class MasterEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit MasterEditor (MasterProcessor&);
    ~MasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPage (int index);

    MasterProcessor& proc;
    MasterLookAndFeel lnf;

    std::array<juce::TextButton, 5> tabs;
    std::array<juce::ToggleButton, 5> power;
    std::array<std::unique_ptr<ButtonAttachment>, 5> powerAtt;

    juce::ComboBox osBox;
    juce::ToggleButton bypassBtn;
    std::unique_ptr<ComboAttachment> osAtt;
    std::unique_ptr<ButtonAttachment> bypassAtt;

    EqPage eqPage;
    DynPage dynPage;
    ExcPage excPage;
    ImgPage imgPage;
    MaxPage maxPage;
    MeterPanel meters;

    int current = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterEditor)
};
