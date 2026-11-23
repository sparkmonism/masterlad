#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

using namespace ml;

namespace
{
juce::Font caps (float size, float kerning = 0.15f)
{
    return juce::Font (juce::FontOptions (size).withStyle ("Bold")).withExtraKerningFactor (kerning);
}

constexpr float kDialStart = juce::MathConstants<float>::pi * 1.2f;
constexpr float kDialEnd   = juce::MathConstants<float>::pi * 2.8f;

juce::String formatHz (float f)
{
    if (f >= 10000.0f) return juce::String (f / 1000.0f, 1) + "k";
    if (f >= 1000.0f)  return juce::String (f / 1000.0f, 2) + "k";
    return juce::String ((int) std::round (f));
}

juce::String formatValue (float v, const juce::String& unit, int places)
{
    if (unit == "Hz")
        return formatHz (v) + " Hz";
    if (unit.isEmpty())
        return juce::String (v, places);
    if (unit == "%" || unit == ":1")
        return juce::String (v, places) + unit;
    return juce::String (v, places) + " " + unit;
}

void fillCombo (juce::ComboBox& box, juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        box.addItemList (c->choices, 1);
}

float getParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    return apvts.getRawParameterValue (id)->load();
}

void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                  juce::Colour c = kMuted, float size = 10.5f,
                  juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (c);
    g.setFont (caps (size, 0.14f));
    g.drawText (text, r, j, false);
}

void drawBox (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, float corner = 10.0f)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, corner);
    g.setColour (kLine);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

void drawVBar (juce::Graphics& g, juce::Rectangle<float> r, float frac, bool fromTop, juce::Colour c)
{
    g.setColour (kTrack);
    g.fillRoundedRectangle (r, r.getWidth() * 0.5f);
    frac = juce::jlimit (0.0f, 1.0f, frac);
    if (frac <= 0.0f) return;
    auto fill = fromTop ? r.withHeight (r.getHeight() * frac)
                        : r.withTop (r.getBottom() - r.getHeight() * frac);
    g.setColour (c);
    g.fillRoundedRectangle (fill, r.getWidth() * 0.5f);
}
} // namespace

//==============================================================================
MasterLookAndFeel::MasterLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, kBg);

    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, kAccent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, kAccent);

    setColour (juce::ComboBox::backgroundColourId, kPanelHi);
    setColour (juce::ComboBox::outlineColourId, kLine);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::arrowColourId, kMuted);

    setColour (juce::PopupMenu::backgroundColourId, kPanelHi);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    setColour (juce::PopupMenu::highlightedTextColourId, kBg);
}

void MasterLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float startAngle, float endAngle,
                                          juce::Slider& slider)
{
    const bool big = (bool) slider.getProperties().getWithDefault ("big", false);

    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                      .reduced (big ? 10.0f : 7.0f);
    const float dia = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto area = bounds.withSizeKeepingCentre (dia, dia);
    const auto c = area.getCentre();

    const float radius = dia * 0.5f;
    const float thick  = big ? 6.0f : 4.0f;
    const float arcR   = radius - thick * 0.5f;
    const float angle  = startAngle + sliderPos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (kTrack);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    if (angle - startAngle > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (slider.isEnabled() ? kAccent : kMuted);
        g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    const float innerR = radius - thick - (big ? 12.0f : 7.0f);
    g.setColour (kPanelHi);
    g.fillEllipse (c.x - innerR, c.y - innerR, innerR * 2.0f, innerR * 2.0f);
    g.setColour (kLine);
    g.drawEllipse (c.x - innerR, c.y - innerR, innerR * 2.0f, innerR * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -innerR * 0.9f, 3.0f, innerR * 0.5f, 1.5f);
    g.setColour (kText);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void MasterLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float, float, juce::Slider::SliderStyle,
                                          juce::Slider& slider)
{
    // Keep the thumb fully inside the component: map the slider's position range
    // onto a track inset by the thumb radius.
    constexpr float inset = 9.0f;
    const float cx = (float) x + (float) width * 0.5f;
    const float top = (float) y + inset, bottom = (float) (y + height) - inset;

    auto toVisual = [&] (float pos)
    {
        const float p = juce::jlimit (0.0f, 1.0f, (pos - (float) y) / (float) height);
        return top + p * (bottom - top);
    };

    const juce::Rectangle<float> track (cx - 2.5f, top, 5.0f, bottom - top);
    g.setColour (kTrack);
    g.fillRoundedRectangle (track, 2.5f);

    const float zeroY = bottom - (float) slider.valueToProportionOfLength (0.0) * (bottom - top);
    g.setColour (kMuted);
    g.fillRect (cx - 7.0f, zeroY - 0.5f, 14.0f, 1.0f);

    const float thumbY = toVisual (sliderPos);
    g.setColour (kAccent);
    const float a = std::min (thumbY, zeroY), b = std::max (thumbY, zeroY);
    g.fillRoundedRectangle (cx - 2.5f, a, 5.0f, std::max (1.0f, b - a), 2.5f);

    g.setColour (kText);
    g.fillEllipse (cx - 8.0f, thumbY - 8.0f, 16.0f, 16.0f);
    g.setColour (kPanel);
    g.fillEllipse (cx - 3.0f, thumbY - 3.0f, 6.0f, 6.0f);
}

void MasterLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                          bool highlighted, bool)
{
    const auto b  = button.getLocalBounds().toFloat();
    const bool on = button.getToggleState();

    if (button.getProperties().contains ("power"))
    {
        const float d = 12.0f;
        const auto r = b.withSizeKeepingCentre (d, d);
        if (on)
        {
            g.setColour (kAccent.withAlpha (0.25f));
            g.fillEllipse (r.expanded (3.0f));
        }
        g.setColour (on ? kAccent : kTrack);
        g.fillEllipse (r);
        g.setColour (highlighted ? kText : kLine);
        g.drawEllipse (r, 1.0f);
        return;
    }

    if (button.getProperties().contains ("pill"))
    {
        const auto r = b.reduced (1.0f);
        const float corner = r.getHeight() * 0.5f;

        g.setColour (on ? kAccent : kPanelHi);
        g.fillRoundedRectangle (r, corner);

        if (highlighted && ! on)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (r, corner);
        }

        g.setColour (on ? kAccent : kLine);
        g.drawRoundedRectangle (r, corner, 1.0f);

        g.setColour (on ? kBg : kText);
        g.setFont (caps (button.getProperties().contains ("small") ? 10.0f : 12.0f, 0.12f));
        g.drawText (button.getButtonText(), r.toNearestInt(), juce::Justification::centred, false);
    }
    else
    {
        constexpr float w = 26.0f, h = 14.0f;
        const juce::Rectangle<float> sw (b.getX() + 2.0f, b.getCentreY() - h * 0.5f, w, h);

        g.setColour (on ? kAccent.withAlpha (0.9f) : kTrack);
        g.fillRoundedRectangle (sw, h * 0.5f);

        const float kd = h - 4.0f;
        const float kx = on ? sw.getRight() - kd - 2.0f : sw.getX() + 2.0f;
        g.setColour (on ? kBg : kMuted);
        g.fillEllipse (kx, sw.getY() + 2.0f, kd, kd);

        g.setColour (on ? kText : kMuted);
        g.setFont (caps (11.0f, 0.12f));
        g.drawText (button.getButtonText(), b.withTrimmedLeft (w + 10.0f).toNearestInt(),
                    juce::Justification::centredLeft, false);
    }
}

void MasterLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                              const juce::Colour&, bool highlighted, bool down)
{
    const auto r = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();

    if (button.getProperties().contains ("tab"))
    {
        g.setColour (on ? kPanelHi : (highlighted ? kPanel : juce::Colours::transparentBlack));
        g.fillRoundedRectangle (r, 8.0f);
        if (on)
        {
            g.setColour (kAccent);
            g.fillRoundedRectangle (r.getX() + 12.0f, r.getBottom() - 3.0f, r.getWidth() - 24.0f, 2.5f, 1.2f);
        }
        return;
    }

    if (button.getProperties().contains ("pill"))
    {
        const float corner = r.getHeight() * 0.5f;
        g.setColour (on ? kAccent : (down ? kAccent.withAlpha (0.35f) : kPanelHi));
        g.fillRoundedRectangle (r, corner);
        if (highlighted && ! on)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (r, corner);
        }
        g.setColour (on ? kAccent : kLine);
        g.drawRoundedRectangle (r, corner, 1.0f);
        return;
    }

    const float corner = r.getHeight() * 0.5f;
    g.setColour (down ? kAccent.withAlpha (0.35f) : kPanelHi);
    g.fillRoundedRectangle (r, corner);
    if (highlighted && ! down)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (r, corner);
    }
    g.setColour (kLine);
    g.drawRoundedRectangle (r, corner, 1.0f);
}

void MasterLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const bool on = button.getToggleState();
    auto bounds = button.getLocalBounds();

    if (button.getProperties().contains ("tab"))
    {
        g.setColour (on ? kText : kMuted);
        g.setFont (caps (12.0f, 0.16f));
        g.drawText (button.getButtonText(), bounds.withTrimmedLeft (30), juce::Justification::centredLeft, false);
        return;
    }

    if (button.getProperties().contains ("pill"))
        g.setColour (on ? kBg : kText);
    else
        g.setColour (button.isEnabled() ? kText : kMuted);

    g.setFont (caps (10.5f, 0.12f));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred, false);
}

juce::Label* MasterLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::Font (juce::FontOptions (11.5f)));
    l->setColour (juce::Label::textColourId, kText);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return l;
}

juce::Font MasterLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (12.0f));
}

//==============================================================================
KnobCell::KnobCell (juce::AudioProcessorValueTreeState& state, const juce::String& title,
                    const juce::String& suffix, int decimals, bool big)
    : apvts (state), unit (suffix), places (decimals)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 16);
    slider.setRotaryParameters (kDialStart, kDialEnd, true);
    slider.getProperties().set ("big", big);

    label.setText (title.toUpperCase(), juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (caps (10.0f, 0.14f));
    label.setColour (juce::Label::textColourId, kMuted);
    label.setInterceptsMouseClicks (false, false);

    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void KnobCell::bind (const juce::String& paramId)
{
    att.reset();
    att = std::make_unique<SliderAttachment> (apvts, paramId, slider);

    const auto u = unit;
    const int p = places;
    slider.textFromValueFunction = [u, p] (double v) { return formatValue ((float) v, u, p); };
    slider.valueFromTextFunction = [] (const juce::String& t)
    {
        double v = t.getDoubleValue();
        if (t.containsIgnoreCase ("k"))
            v *= 1000.0;
        return v;
    };
    slider.updateText();
}

void KnobCell::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

//==============================================================================
EqGraph::EqGraph (MasterProcessor& p, std::function<void (int)> onSelect, std::function<int()> selected)
    : proc (p), selectFn (std::move (onSelect)), selectedFn (std::move (selected))
{
    spec.fill (-120.0f);
}

juce::Rectangle<float> EqGraph::plot() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (10.0f).withTrimmedRight (10.0f)
                           .withTrimmedTop (10.0f).withTrimmedBottom (24.0f);
}

float EqGraph::freqToX (float f) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
}

float EqGraph::xToFreq (float x) const
{
    const auto p = plot();
    return 20.0f * std::pow (1000.0f, (x - p.getX()) / p.getWidth());
}

float EqGraph::gainToY (float g) const
{
    const auto p = plot();
    return p.getCentreY() - g / 18.0f * (p.getHeight() * 0.5f);
}

float EqGraph::yToGain (float y) const
{
    const auto p = plot();
    return (p.getCentreY() - y) / (p.getHeight() * 0.5f) * 18.0f;
}

float EqGraph::bandValue (int band, const char* suffix) const
{
    return getParam (proc.apvts, MasterProcessor::eqId (band, suffix));
}

void EqGraph::setBandValue (int band, const char* suffix, float value)
{
    if (auto* prm = proc.apvts.getParameter (MasterProcessor::eqId (band, suffix)))
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

int EqGraph::nodeAt (juce::Point<float> pt) const
{
    int best = -1;
    float bestD = 15.0f;
    for (int b = 0; b < MasterProcessor::kEqBands; ++b)
    {
        const juce::Point<float> c (freqToX (bandValue (b, "freq")), gainToY (bandValue (b, "gain")));
        const float d = c.getDistanceFrom (pt);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void EqGraph::updateSpectrum()
{
    const uint32_t w = proc.specWrite.load (std::memory_order_acquire);
    constexpr int N = 2048;

    for (int k = 0; k < N; ++k)
    {
        const uint32_t idx = (w - (uint32_t) N + (uint32_t) k) & (uint32_t) (MasterProcessor::kSpecRing - 1);
        const float hann = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) k / (float) N);
        fftData[(size_t) k] = proc.specRing[idx].load (std::memory_order_relaxed) * hann;
    }
    std::fill (fftData.begin() + N, fftData.end(), 0.0f);

    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (int k = 0; k <= N / 2; ++k)
    {
        const float amp = fftData[(size_t) k] * 4.0f / (float) N;
        const float db = 20.0f * std::log10 (std::max (amp, 1.0e-6f));
        float& s = spec[(size_t) k];
        s = db > s ? 0.55f * db + 0.45f * s : 0.88f * s + 0.12f * db;
    }
}

void EqGraph::paint (juce::Graphics& g)
{
    drawBox (g, getLocalBounds().toFloat(), kPanel);
    const auto pl = plot();
    const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;

    // grid
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (int db = -12; db <= 12; db += 6)
    {
        const float y = gainToY ((float) db);
        g.setColour (db == 0 ? kLine.brighter (0.25f) : kLine.withAlpha (0.55f));
        g.drawHorizontalLine ((int) y, pl.getX(), pl.getRight());
        g.setColour (kMuted.withAlpha (0.8f));
        g.drawText (juce::String (db), (int) pl.getX() + 2, (int) y - 12, 28, 12, juce::Justification::left, false);
    }

    const float gridF[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    for (float f : gridF)
    {
        const float x = freqToX (f);
        g.setColour (kLine.withAlpha (0.55f));
        g.drawVerticalLine ((int) x, pl.getY(), pl.getBottom());
        g.setColour (kMuted.withAlpha (0.9f));
        g.drawText (formatHz (f), (int) x - 20, (int) pl.getBottom() + 4, 40, 14, juce::Justification::centred, false);
    }

    // spectrum
    {
        juce::Path sp;
        bool started = false;
        for (float x = pl.getX(); x <= pl.getRight(); x += 2.0f)
        {
            const float f0 = xToFreq (x), f1 = xToFreq (x + 2.0f);
            const float b0 = f0 * 2048.0f / (float) sr, b1 = f1 * 2048.0f / (float) sr;
            const int i0 = juce::jlimit (0, 1023, (int) std::floor (b0));
            const int i1 = juce::jlimit (i0, 1024, (int) std::ceil (b1));

            float db;
            if (i1 <= i0 + 1)
            {
                const float frac = juce::jlimit (0.0f, 1.0f, b0 - (float) i0);
                db = spec[(size_t) i0] * (1.0f - frac) + spec[(size_t) std::min (i0 + 1, 1024)] * frac;
            }
            else
            {
                db = -200.0f;
                for (int i = i0; i <= i1; ++i)
                    db = std::max (db, spec[(size_t) i]);
            }

            db += 3.0f * std::log2 (std::max (f0, 20.0f) / 1000.0f);
            const float y = pl.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 90.0f) * pl.getHeight();

            if (! started) { sp.startNewSubPath (x, y); started = true; }
            else sp.lineTo (x, y);
        }

        juce::Path fill (sp);
        fill.lineTo (pl.getRight(), pl.getBottom());
        fill.lineTo (pl.getX(), pl.getBottom());
        fill.closeSubPath();
        g.setColour (kAccent.withAlpha (0.14f));
        g.fillPath (fill);
        g.setColour (kAccent.withAlpha (0.45f));
        g.strokePath (sp, juce::PathStrokeType (1.2f));
    }

    // EQ curve
    {
        mdsp::Coeffs co[MasterProcessor::kEqBands];
        bool on[MasterProcessor::kEqBands];
        for (int b = 0; b < MasterProcessor::kEqBands; ++b)
        {
            on[b] = bandValue (b, "on") > 0.5f;
            co[b] = mdsp::makeCoeffs (MasterProcessor::eqType (b), sr, bandValue (b, "freq"),
                                      bandValue (b, "gain"), bandValue (b, "q"));
        }

        juce::Path curve;
        bool started = false;
        for (float x = pl.getX(); x <= pl.getRight(); x += 2.0f)
        {
            const double w = 2.0 * mdsp::kPi * (double) xToFreq (x) / sr;
            double db = 0.0;
            for (int b = 0; b < MasterProcessor::kEqBands; ++b)
                if (on[b])
                    db += 20.0 * std::log10 (mdsp::magnitudeAt (co[b], w));

            const float y = juce::jlimit (pl.getY(), pl.getBottom(), gainToY ((float) db));
            if (! started) { curve.startNewSubPath (x, y); started = true; }
            else curve.lineTo (x, y);
        }

        const float cy = gainToY (0.0f);
        juce::Path fill (curve);
        fill.lineTo (pl.getRight(), cy);
        fill.lineTo (pl.getX(), cy);
        fill.closeSubPath();
        g.setColour (kAccent.withAlpha (0.10f));
        g.fillPath (fill);
        g.setColour (kAccent);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
    }

    // nodes
    const int sel = selectedFn();
    for (int b = 0; b < MasterProcessor::kEqBands; ++b)
    {
        const bool on = bandValue (b, "on") > 0.5f;
        const juce::Point<float> c (freqToX (bandValue (b, "freq")), gainToY (bandValue (b, "gain")));
        const float rad = b == sel ? 9.0f : 7.5f;

        if (b == sel)
        {
            g.setColour (kAccent.withAlpha (0.25f));
            g.fillEllipse (c.x - rad - 4.0f, c.y - rad - 4.0f, (rad + 4.0f) * 2.0f, (rad + 4.0f) * 2.0f);
        }

        g.setColour (on ? kAccent : kTrack);
        g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
        g.setColour (b == sel ? kText : kBg);
        g.drawEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f, 1.5f);

        g.setColour (on ? kBg : kMuted);
        g.setFont (caps (10.0f, 0.0f));
        g.drawText (juce::String (b + 1), (int) (c.x - rad), (int) (c.y - rad), (int) (rad * 2.0f),
                    (int) (rad * 2.0f), juce::Justification::centred, false);
    }
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    dragBand = nodeAt (e.position);
    if (dragBand < 0)
        return;

    selectFn (dragBand);
    for (auto* s : { "freq", "gain", "on" })
        if (auto* prm = proc.apvts.getParameter (MasterProcessor::eqId (dragBand, s)))
            prm->beginChangeGesture();

    if (bandValue (dragBand, "on") < 0.5f)
        setBandValue (dragBand, "on", 1.0f);
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;

    setBandValue (dragBand, "freq", juce::jlimit (20.0f, 20000.0f, xToFreq (e.position.x)));
    setBandValue (dragBand, "gain", juce::jlimit (-18.0f, 18.0f, yToGain (e.position.y)));
    repaint();
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragBand < 0)
        return;

    for (auto* s : { "freq", "gain", "on" })
        if (auto* prm = proc.apvts.getParameter (MasterProcessor::eqId (dragBand, s)))
            prm->endChangeGesture();
    dragBand = -1;
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = nodeAt (e.position);
    if (b < 0)
        return;

    if (auto* prm = proc.apvts.getParameter (MasterProcessor::eqId (b, "on")))
    {
        prm->beginChangeGesture();
        setBandValue (b, "on", bandValue (b, "on") > 0.5f ? 0.0f : 1.0f);
        prm->endChangeGesture();
    }
    repaint();
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = nodeAt (e.position);
    if (b < 0)
        b = selectedFn();

    if (auto* prm = proc.apvts.getParameter (MasterProcessor::eqId (b, "q")))
    {
        const float q = juce::jlimit (0.2f, 10.0f, bandValue (b, "q") * std::exp (w.deltaY * 1.2f));
        prm->beginChangeGesture();
        setBandValue (b, "q", q);
        prm->endChangeGesture();
    }
    repaint();
}

//==============================================================================
EqPage::EqPage (MasterProcessor& p)
    : proc (p),
      graph (p, [this] (int b) { selectBand (b); }, [this] { return selected; }),
      freqK (p.apvts, "Frequency", "Hz", 0, true),
      gainK (p.apvts, "Gain", "dB", 1, true),
      qK (p.apvts, "Q", "", 2, true)
{
    addAndMakeVisible (graph);
    addAndMakeVisible (freqK);
    addAndMakeVisible (gainK);
    addAndMakeVisible (qK);

    bandOn.setButtonText ("ON");
    bandOn.getProperties().set ("pill", true);
    bandOn.getProperties().set ("small", true);
    addAndMakeVisible (bandOn);

    for (int i = 0; i < MasterProcessor::kEqBands; ++i)
    {
        auto& b = bandBtn[(size_t) i];
        b.setButtonText (juce::String (i + 1));
        b.getProperties().set ("pill", true);
        b.onClick = [this, i] { selectBand (i); };
        addAndMakeVisible (b);
    }

    fillCombo (modeBox, proc.apvts, "eq_mode");
    modeAtt = std::make_unique<ComboAttachment> (proc.apvts, "eq_mode", modeBox);
    addAndMakeVisible (modeBox);

    selectBand (0);
}

void EqPage::selectBand (int b)
{
    selected = b;
    freqK.bind (MasterProcessor::eqId (b, "freq"));
    gainK.bind (MasterProcessor::eqId (b, "gain"));
    qK.bind (MasterProcessor::eqId (b, "q"));

    onAtt.reset();
    onAtt = std::make_unique<ButtonAttachment> (proc.apvts, MasterProcessor::eqId (b, "on"), bandOn);

    for (int i = 0; i < MasterProcessor::kEqBands; ++i)
        bandBtn[(size_t) i].setToggleState (i == b, juce::dontSendNotification);

    repaint();
}

void EqPage::resized()
{
    graph.setBounds (0, 0, getWidth(), 350);

    for (int i = 0; i < MasterProcessor::kEqBands; ++i)
        bandBtn[(size_t) i].setBounds (16 + i * 50, 366, 44, 26);

    bandOn.setBounds (326, 366, 56, 26);
    modeBox.setBounds (getWidth() - 16 - 110, 366, 110, 26);

    freqK.setBounds (16, 404, 150, 150);
    gainK.setBounds (176, 404, 150, 150);
    qK.setBounds (336, 404, 150, 150);
}

void EqPage::paint (juce::Graphics& g)
{
    static const char* const names[] = { "LOW SHELF", "BELL", "BELL", "BELL", "BELL", "HIGH SHELF" };
    drawCaption (g, names[selected], { 396, 366, 120, 26 }, kAccent);
    drawCaption (g, "PROCESS", { getWidth() - 16 - 110 - 80, 366, 74, 26 }, kMuted, 10.5f,
                 juce::Justification::centredRight);

    drawCaption (g, "DRAG POINTS TO SHAPE", { 520, 440, 232, 18 }, kMuted);
    drawCaption (g, "SCROLL OVER A POINT FOR Q", { 520, 462, 232, 18 }, kMuted);
    drawCaption (g, "DOUBLE-CLICK TO SWITCH A BAND", { 520, 484, 232, 18 }, kMuted);
}

void EqPage::refresh()
{
    graph.updateSpectrum();
    graph.repaint();
}

//==============================================================================
DynPage::DynPage (MasterProcessor& p) : proc (p)
{
    static const char* const titles[5] = { "Threshold", "Ratio", "Attack", "Release", "Gain" };
    static const char* const units[5]  = { "dB", ":1", "ms", "ms", "dB" };
    static const int places[5]         = { 1, 1, 1, 0, 1 };
    static const char* const suffix[5] = { "thr", "ratio", "att", "rel", "gain" };

    for (int b = 0; b < MasterProcessor::kDynBands; ++b)
    {
        for (int k = 0; k < 5; ++k)
        {
            auto cell = std::make_unique<KnobCell> (proc.apvts, titles[k], units[k], places[k]);
            cell->bind (MasterProcessor::dynId (b, suffix[k]));
            addAndMakeVisible (*cell);
            knobs[(size_t) (b * 5 + k)] = std::move (cell);
        }

        auto& t = bypass[(size_t) b];
        t.setButtonText ("BYPASS");
        t.getProperties().set ("pill", true);
        t.getProperties().set ("small", true);
        addAndMakeVisible (t);
        bypassAtt[(size_t) b] = std::make_unique<ButtonAttachment> (proc.apvts, MasterProcessor::dynId (b, "byp"), t);
    }

    for (int i = 0; i < 3; ++i)
    {
        xover[(size_t) i] = std::make_unique<KnobCell> (proc.apvts, "Crossover " + juce::String (i + 1), "Hz", 0);
        xover[(size_t) i]->bind ("dyn_x" + juce::String (i + 1));
        addAndMakeVisible (*xover[(size_t) i]);
    }
}

void DynPage::resized()
{
    for (int b = 0; b < MasterProcessor::kDynBands; ++b)
    {
        const int cx = 12 + b * 186;
        bypass[(size_t) b].setBounds (cx + 180 - 70, 12, 64, 22);

        for (int k = 0; k < 5; ++k)
        {
            const int col = k % 2, row = k / 2;
            knobs[(size_t) (b * 5 + k)]->setBounds (cx + 26 + col * 77, 44 + row * 112, 77, 110);
        }
    }

    for (int i = 0; i < 3; ++i)
        xover[(size_t) i]->setBounds (12 + i * 130, 420, 110, 130);
}

void DynPage::paint (juce::Graphics& g)
{
    const float f1 = getParam (proc.apvts, "dyn_x1"), f2 = getParam (proc.apvts, "dyn_x2"),
                f3 = getParam (proc.apvts, "dyn_x3");

    const juce::String names[4] = { "LOW", "LOW MID", "HIGH MID", "HIGH" };
    const juce::String ranges[4] = { "< " + formatHz (f1), formatHz (f1) + " - " + formatHz (f2),
                                     formatHz (f2) + " - " + formatHz (f3), "> " + formatHz (f3) };

    for (int b = 0; b < MasterProcessor::kDynBands; ++b)
    {
        const int cx = 12 + b * 186;
        drawBox (g, { (float) cx, 6.0f, 180.0f, 388.0f }, kPanelHi.withAlpha (0.45f), 8.0f);
        drawCaption (g, names[b], { cx + 12, 10, 90, 14 }, kText, 11.0f);
        drawCaption (g, ranges[b], { cx + 12, 24, 90, 12 }, kMuted, 9.5f);

        const juce::Rectangle<float> bar ((float) cx + 8.0f, 44.0f, 10.0f, 336.0f);
        drawVBar (g, bar, grShown[(size_t) b] / 18.0f, true, kAccent);
    }

    drawCaption (g, "CROSSOVERS", { 12, 400, 200, 16 }, kMuted, 10.5f);
    drawCaption (g, "FOUR BANDS, LINKWITZ-RILEY 24 dB/OCT. GAIN REDUCTION SHOWN ON THE LEFT OF EACH BAND.",
                 { 410, 440, 340, 60 }, kMuted, 9.5f);
}

void DynPage::refresh()
{
    for (int b = 0; b < MasterProcessor::kDynBands; ++b)
    {
        const float v = proc.dynGr[(size_t) b].exchange (0.0f);
        float& s = grShown[(size_t) b];
        s = v > s ? v : std::max (v, s - 0.7f);
    }
    repaint();
}

//==============================================================================
ExcPage::ExcPage (MasterProcessor& p)
    : proc (p),
      driveK (p.apvts, "Drive", "dB", 1, true),
      amountK (p.apvts, "Amount", "%", 0, true),
      freqK (p.apvts, "Frequency", "Hz", 0, true)
{
    driveK.bind ("exc_drive");
    amountK.bind ("exc_amount");
    freqK.bind ("exc_freq");
    addAndMakeVisible (driveK);
    addAndMakeVisible (amountK);
    addAndMakeVisible (freqK);

    fillCombo (modeBox, proc.apvts, "exc_mode");
    modeAtt = std::make_unique<ComboAttachment> (proc.apvts, "exc_mode", modeBox);
    addAndMakeVisible (modeBox);
}

void ExcPage::resized()
{
    driveK.setBounds (48, 60, 180, 210);
    amountK.setBounds (294, 60, 180, 210);
    freqK.setBounds (540, 60, 180, 210);
    modeBox.setBounds (294, 320, 180, 30);
}

void ExcPage::paint (juce::Graphics& g)
{
    drawCaption (g, "HARMONIC EXCITER", { 24, 18, 300, 18 }, kText, 12.0f);
    drawCaption (g, "MODE", { 294, 298, 180, 18 }, kMuted, 10.5f, juce::Justification::centred);

    drawCaption (g, "ADDS HARMONICS ABOVE THE FREQUENCY AND BLENDS THEM IN PARALLEL.",
                 { 24, 410, 720, 16 }, kMuted, 10.0f);
    drawCaption (g, "RUNS OVERSAMPLED (SEE THE OVERSAMPLING SELECTOR AT THE TOP) TO KEEP ALIASING OUT OF THE HIGHS.",
                 { 24, 430, 720, 16 }, kMuted, 10.0f);
    drawCaption (g, "SOFT = ODD HARMONICS   TUBE = EVEN + ODD   TAPE = SOFT WITH A TOUCH OF 2ND   HARD = BRIGHTER, EDGIER.",
                 { 24, 450, 720, 16 }, kMuted, 10.0f);
}

//==============================================================================
ImgPage::ImgPage (MasterProcessor& p) : proc (p)
{
    for (int b = 0; b < 4; ++b)
    {
        width[(size_t) b] = std::make_unique<KnobCell> (proc.apvts, "Width", "%", 0, true);
        width[(size_t) b]->bind (MasterProcessor::imgId (b, "width"));
        addAndMakeVisible (*width[(size_t) b]);
    }

    for (int i = 0; i < 3; ++i)
    {
        xover[(size_t) i] = std::make_unique<KnobCell> (proc.apvts, "Crossover " + juce::String (i + 1), "Hz", 0);
        xover[(size_t) i]->bind ("img_x" + juce::String (i + 1));
        addAndMakeVisible (*xover[(size_t) i]);
    }
}

void ImgPage::resized()
{
    for (int b = 0; b < 4; ++b)
        width[(size_t) b]->setBounds (12 + b * 186, 60, 180, 210);

    for (int i = 0; i < 3; ++i)
        xover[(size_t) i]->setBounds (12 + i * 130, 330, 110, 130);
}

void ImgPage::paint (juce::Graphics& g)
{
    const float f1 = getParam (proc.apvts, "img_x1"), f2 = getParam (proc.apvts, "img_x2"),
                f3 = getParam (proc.apvts, "img_x3");
    const juce::String names[4] = { "LOW", "LOW MID", "HIGH MID", "HIGH" };
    const juce::String ranges[4] = { "< " + formatHz (f1), formatHz (f1) + " - " + formatHz (f2),
                                     formatHz (f2) + " - " + formatHz (f3), "> " + formatHz (f3) };

    for (int b = 0; b < 4; ++b)
    {
        const int cx = 12 + b * 186;
        drawBox (g, { (float) cx, 12.0f, 180.0f, 270.0f }, kPanelHi.withAlpha (0.45f), 8.0f);
        drawCaption (g, names[b], { cx + 14, 20, 100, 16 }, kText, 11.0f);
        drawCaption (g, ranges[b], { cx + 14, 36, 140, 14 }, kMuted, 9.5f);
    }

    drawCaption (g, "CROSSOVERS", { 12, 306, 200, 16 }, kMuted, 10.5f);
    drawCaption (g, "100% = UNCHANGED   0% = MONO   200% = DOUBLE WIDTH", { 410, 350, 340, 16 }, kMuted, 10.0f);
    drawCaption (g, "WIDTH SCALES THE SIDE SIGNAL IN EACH BAND.", { 410, 372, 340, 16 }, kMuted, 10.0f);
}

//==============================================================================
MaxPage::MaxPage (MasterProcessor& p)
    : proc (p),
      gainK (p.apvts, "Gain", "dB", 1, true),
      ceilK (p.apvts, "Ceiling", "dB", 1, true),
      relK (p.apvts, "Release", "ms", 0, true)
{
    gainK.bind ("max_gain");
    ceilK.bind ("max_ceiling");
    relK.bind ("max_release");
    addAndMakeVisible (gainK);
    addAndMakeVisible (ceilK);
    addAndMakeVisible (relK);

    autoBtn.setButtonText ("AUTO RELEASE");
    addAndMakeVisible (autoBtn);
    autoAtt = std::make_unique<ButtonAttachment> (proc.apvts, "max_auto", autoBtn);
}

void MaxPage::resized()
{
    gainK.setBounds (24, 60, 180, 210);
    ceilK.setBounds (214, 60, 180, 210);
    relK.setBounds (404, 60, 180, 210);
    autoBtn.setBounds (404, 290, 180, 24);
}

void MaxPage::paint (juce::Graphics& g)
{
    drawCaption (g, "TRUE-PEAK MAXIMIZER", { 24, 18, 300, 18 }, kText, 12.0f);

    drawCaption (g, "GAIN REDUCTION", { 620, 18, 130, 16 }, kMuted, 10.5f, juce::Justification::centred);
    const juce::Rectangle<float> bar (672.0f, 44.0f, 26.0f, 380.0f);
    drawVBar (g, bar, grShown / 12.0f, true, grShown > 9.0f ? kRed : kAccent);

    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (20.0f)).withExtraKerningFactor (0.02f));
    g.drawText ("-" + juce::String (grShown, 1) + " dB", 620, 436, 130, 28, juce::Justification::centred, false);

    drawCaption (g, "RAISE GAIN TO PUSH THE MIX INTO THE LIMITER. THE CEILING IS THE LOUDEST THE OUTPUT CAN GO.",
                 { 24, 410, 560, 16 }, kMuted, 10.0f);
    drawCaption (g, "LOOK-AHEAD LIMITING WITH 4x INTER-SAMPLE PEAK DETECTION. LATENCY IS FIXED, ABOUT 2 MS PLUS THE EXCITER.",
                 { 24, 430, 580, 16 }, kMuted, 10.0f);
    drawCaption (g, "AUTO RELEASE SHORTENS THE RELEASE ON LIGHT LIMITING AND LENGTHENS IT ON HEAVY LIMITING.",
                 { 24, 450, 580, 16 }, kMuted, 10.0f);
}

void MaxPage::refresh()
{
    const float v = proc.maxGr.exchange (0.0f);
    grShown = v > grShown ? v : std::max (v, grShown - 0.5f);
    repaint();
}

//==============================================================================
MeterPanel::MeterPanel (MasterProcessor& p) : proc (p)
{
    inSlider.setSliderStyle (juce::Slider::LinearVertical);
    inSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (inSlider);
    inAtt = std::make_unique<SliderAttachment> (proc.apvts, "in_gain", inSlider);

    resetBtn.onClick = [this] { proc.resetLoudness(); };
    addAndMakeVisible (resetBtn);
}

void MeterPanel::resized()
{
    inSlider.setBounds (6, 36, 36, 260);
    resetBtn.setBounds (getWidth() - 14 - 62, 346, 62, 22);
}

void MeterPanel::refresh()
{
    auto track = [] (float& shown, float& hold, int& age, float newDb)
    {
        shown = newDb > shown ? newDb : std::max (newDb, shown - 1.6f);
        if (newDb >= hold) { hold = newDb; age = 0; }
        else if (++age > 45) hold = std::max (shown, hold - 1.0f);
    };

    for (int c = 0; c < 2; ++c)
    {
        track (inDb[c], inHold[c], inHoldAge[c], mdsp::toDb (proc.inPeak[(size_t) c].exchange (0.0f)));
        track (outDb[c], outHold[c], outHoldAge[c], mdsp::toDb (proc.outPeak[(size_t) c].exchange (0.0f)));
    }

    mom = proc.momentary.load();
    st = proc.shortTerm.load();
    integ = proc.integrated.load();
    tp = proc.truePeakMax.load();
    corr = proc.correlation.load();
    repaint();
}

void MeterPanel::paint (juce::Graphics& g)
{
    const float top = 36.0f, h = 260.0f;
    auto yOf = [top, h] (float db) { return top + h - juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * h; };

    drawCaption (g, "INPUT", { 4, 12, 44, 14 }, kMuted, 9.5f, juce::Justification::centred);
    drawCaption (g, "IN", { 50, 12, 26, 14 }, kMuted, 9.5f, juce::Justification::centred);
    drawCaption (g, "OUT", { 98, 12, 30, 14 }, kMuted, 9.5f, juce::Justification::centred);

    // input gain readout
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ((inSlider.getValue() > 0 ? "+" : "") + juce::String (inSlider.getValue(), 1) + " dB",
                0, (int) top + (int) h + 2, 48, 14, juce::Justification::centred, false);

    auto bars = [&] (float x, const float* db, const float* hold)
    {
        for (int c = 0; c < 2; ++c)
        {
            const juce::Rectangle<float> r (x + (float) c * 13.0f, top, 11.0f, h);
            g.setColour (kTrack);
            g.fillRoundedRectangle (r, 2.0f);

            const float y = yOf (db[c]);
            if (db[c] > -60.0f)
            {
                g.setColour (db[c] > -0.5f ? kRed : kAccent);
                g.fillRoundedRectangle (r.withTop (y), 2.0f);
            }

            g.setColour (hold[c] > -0.5f ? kRed : kText);
            g.fillRect (r.getX(), yOf (hold[c]) - 1.0f, r.getWidth(), 2.0f);
        }
    };
    bars (52.0f, inDb, inHold);
    bars (100.0f, outDb, outHold);

    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    for (int db : { 0, -6, -12, -24, -48 })
    {
        g.setColour (kMuted);
        g.drawText (juce::String (db), 78, (int) yOf ((float) db) - 6, 20, 12, juce::Justification::centred, false);
    }

    auto fmtPeak = [] (float d) { return d > -59.0f ? juce::String (d, 1) : juce::String ("-inf"); };
    g.setColour (kMuted);
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText (fmtPeak (std::max (inHold[0], inHold[1])), 50, (int) top + (int) h + 2, 28, 14, juce::Justification::centred, false);
    g.drawText (fmtPeak (std::max (outHold[0], outHold[1])), 98, (int) top + (int) h + 2, 28, 14, juce::Justification::centred, false);

    // loudness
    auto fmtL = [] (float v) { return v > -100.0f ? juce::String (v, 1) : juce::String ("--"); };

    auto line = [&] (int y, const juce::String& title, const juce::String& value, const juce::String& unit,
                     juce::Colour vc)
    {
        drawCaption (g, title, { 14, y, 110, 14 }, kMuted, 9.5f);
        g.setColour (vc);
        g.setFont (juce::Font (juce::FontOptions (22.0f)));
        g.drawText (value, 14, y + 14, 100, 26, juce::Justification::centredLeft, false);
        drawCaption (g, unit, { 104, y + 20, 60, 16 }, kMuted, 9.5f);
    };

    line (346, "MOMENTARY", fmtL (mom), "LUFS", kText);
    line (388, "SHORT TERM", fmtL (st), "LUFS", kText);
    line (430, "INTEGRATED", fmtL (integ), "LUFS", kAccent);
    line (472, "TRUE PEAK", tp > -100.0f ? juce::String (tp, 1) : juce::String ("--"), "dBTP",
          tp > -0.1f ? kRed : kText);

    // correlation
    drawCaption (g, "CORRELATION", { 14, 518, 120, 14 }, kMuted, 9.5f);
    const juce::Rectangle<float> cb (14.0f, 540.0f, (float) getWidth() - 28.0f, 8.0f);
    g.setColour (kTrack);
    g.fillRoundedRectangle (cb, 4.0f);
    const float cx = cb.getCentreX();
    const float px = cb.getX() + (corr * 0.5f + 0.5f) * cb.getWidth();
    g.setColour (corr < 0.0f ? kRed : kAccent);
    g.fillRoundedRectangle (juce::Rectangle<float> (std::min (cx, px), cb.getY(), std::max (2.0f, std::abs (px - cx)), cb.getHeight()), 3.0f);
    g.setColour (kText);
    g.fillRect (cx - 0.5f, cb.getY() - 2.0f, 1.0f, cb.getHeight() + 4.0f);
}

//==============================================================================
MasterEditor::MasterEditor (MasterProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      eqPage (p), dynPage (p), excPage (p), imgPage (p), maxPage (p), meters (p)
{
    setLookAndFeel (&lnf);
    setSize (1000, 660);

    static const char* const names[5] = { "EQ", "DYNAMICS", "EXCITER", "IMAGER", "MAXIMIZER" };
    static const char* const ids[5]   = { "eq_on", "dyn_on", "exc_on", "img_on", "max_on" };

    for (int i = 0; i < 5; ++i)
    {
        auto& t = tabs[(size_t) i];
        t.setButtonText (names[i]);
        t.setClickingTogglesState (true);
        t.setRadioGroupId (101);
        t.getProperties().set ("tab", true);
        t.onClick = [this, i] { showPage (i); };
        addAndMakeVisible (t);
    }

    for (int i = 0; i < 5; ++i)
    {
        auto& b = power[(size_t) i];
        b.getProperties().set ("power", true);
        addAndMakeVisible (b);
        powerAtt[(size_t) i] = std::make_unique<ButtonAttachment> (proc.apvts, ids[i], b);
    }

    fillCombo (osBox, proc.apvts, "os");
    osAtt = std::make_unique<ComboAttachment> (proc.apvts, "os", osBox);
    addAndMakeVisible (osBox);

    bypassBtn.setButtonText ("BYPASS");
    bypassBtn.getProperties().set ("pill", true);
    addAndMakeVisible (bypassBtn);
    bypassAtt = std::make_unique<ButtonAttachment> (proc.apvts, "bypass", bypassBtn);

    addAndMakeVisible (eqPage);
    addAndMakeVisible (dynPage);
    addAndMakeVisible (excPage);
    addAndMakeVisible (imgPage);
    addAndMakeVisible (maxPage);
    addAndMakeVisible (meters);

    showPage (0);
    startTimerHz (30);
}

MasterEditor::~MasterEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MasterEditor::showPage (int index)
{
    current = index;
    eqPage.setVisible (index == 0);
    dynPage.setVisible (index == 1);
    excPage.setVisible (index == 2);
    imgPage.setVisible (index == 3);
    maxPage.setVisible (index == 4);
    for (int i = 0; i < 5; ++i)
        tabs[(size_t) i].setToggleState (i == index, juce::dontSendNotification);
}

void MasterEditor::timerCallback()
{
    meters.refresh();

    switch (current)
    {
        case 0: eqPage.refresh(); break;
        case 1: dynPage.refresh(); break;
        case 4: maxPage.refresh(); break;
        default: break;
    }
}

void MasterEditor::resized()
{
    for (int i = 0; i < 5; ++i)
    {
        tabs[(size_t) i].setBounds (196 + i * 104, 14, 100, 38);
        power[(size_t) i].setBounds (196 + i * 104 + 8, 14 + 11, 16, 16);
    }

    osBox.setBounds (748, 28, 96, 26);
    bypassBtn.setBounds (860, 26, 116, 28);

    const juce::Rectangle<int> pageArea (16, 76, 768, 568);
    eqPage.setBounds (pageArea);
    dynPage.setBounds (pageArea);
    excPage.setBounds (pageArea);
    imgPage.setBounds (pageArea);
    maxPage.setBounds (pageArea);
    meters.setBounds (800, 76, 184, 568);
}

void MasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (30.0f).withStyle ("Bold")).withExtraKerningFactor (0.01f));
    g.drawText ("Masterlad", 24, 8, 170, 36, juce::Justification::centredLeft, false);
    drawCaption (g, "SONGCRAFT AUDIO", { 26, 42, 160, 14 }, kAccent, 9.5f);

    drawCaption (g, "OVERSAMPLING", { 748, 10, 100, 14 }, kMuted, 9.5f);

    drawBox (g, { 16.0f, 76.0f, 768.0f, 568.0f }, kPanel);
    drawBox (g, { 800.0f, 76.0f, 184.0f, 568.0f }, kPanel);
}
