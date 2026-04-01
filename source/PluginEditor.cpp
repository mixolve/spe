#include "PluginEditor.h"

#include <cmath>
#include <limits>

namespace
{
constexpr int fixedEditorHeight = 516;
constexpr int parameterPanelWidth = 294;
constexpr int collapsedEditorWidth = parameterPanelWidth;
constexpr int panelGap = 0;
constexpr int minimumAnalyserWidth = 1;
constexpr int minimumVisibleEditorWidth = parameterPanelWidth + panelGap + minimumAnalyserWidth;
constexpr int defaultAnalyserWidth = 1400;
constexpr int fixedEditorWidth = parameterPanelWidth + panelGap + defaultAnalyserWidth;
constexpr int maximumEditorWidth = std::numeric_limits<int>::max();
constexpr int editorInsetX = 4;
constexpr int editorInsetBottom = 4;
constexpr int parameterGap = 2;
constexpr int footerHeight = 20;
constexpr int sectionWidth = parameterPanelWidth - (editorInsetX * 2);
constexpr int controlRowWidth = sectionWidth - 8;
constexpr int parameterValueWidth = 94;
constexpr int parameterNameWidth = controlRowWidth - parameterGap - parameterValueWidth;
constexpr auto analyserMinFrequency = 20.0f;
constexpr auto analyserMaxFrequency = 22000.0f;
constexpr auto analyserSlopeReferenceFrequency = 632.455532f;
constexpr float uiFontSize = 20.0f;
constexpr float valueBoxDragNormalisedPerPixel = 0.0050f;
constexpr float smoothWheelStepThreshold = 0.030f;
constexpr float wheelStepMultiplier = 2.0f;

const auto uiWhite = juce::Colour(0xffffffff);
const auto uiBlack = juce::Colour(0xff000000);
const auto uiAccent = juce::Colour(0xff9999ff);
const auto uiChanged = juce::Colour(0xffffcc99);
const auto uiGrey950 = juce::Colour(0xff121212);
const auto uiGrey900 = juce::Colour(0xff1a1a1a);
const auto uiGrey800 = juce::Colour(0xff242424);
const auto uiGrey700 = juce::Colour(0xff363636);
const auto uiGrey500 = juce::Colour(0xff707070);

juce::FontOptions makeUiFontOptions(const int styleFlags = juce::Font::plain, const float height = uiFontSize)
{
#if JUCE_TARGET_HAS_BINARY_DATA
    const auto useBold = (styleFlags & juce::Font::bold) != 0;

    static const auto regularTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::SometypeMonoRegular_ttf,
                                                                                BinaryData::SometypeMonoRegular_ttfSize);
    static const auto boldTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::SometypeMonoBold_ttf,
                                                                             BinaryData::SometypeMonoBold_ttfSize);

    if (auto typeface = useBold ? boldTypeface : regularTypeface)
        return juce::FontOptions(typeface).withHeight(height);
#endif

    return juce::FontOptions("Sometype Mono", height, styleFlags);
}

juce::Font makeUiFont(const int styleFlags = juce::Font::plain, const float height = uiFontSize)
{
    return juce::Font(makeUiFontOptions(styleFlags, height));
}

juce::String formatValueBoxText(const double value)
{
    auto roundedValue = std::round(value * 10.0) / 10.0;

    if (std::abs(roundedValue) < 0.05)
        roundedValue = 0.0;

    return juce::String::formatted("%+08.1f", roundedValue);
}

double parseNumericInput(const juce::String& text)
{
    return text.trim()
        .retainCharacters("0123456789+-.")
        .getDoubleValue();
}

void clearKeyboardFocus(juce::Component& component)
{
    if (auto* focusedComponent = juce::Component::getCurrentlyFocusedComponent())
        focusedComponent->giveAwayKeyboardFocus();

    if (auto* topLevel = component.getTopLevelComponent())
        topLevel->unfocusAllComponents();
}

bool isParameterChangedFromDefault(const juce::AudioProcessorValueTreeState& state, const juce::String& parameterId)
{
    if (auto* parameter = state.getParameter(parameterId))
        return std::abs(parameter->getValue() - parameter->getDefaultValue()) > 1.0e-6f;

    return false;
}

template <size_t N>
double findNearestChoiceIndex(const double value, const std::array<double, N>& choices)
{
    auto nearestIndex = 0;
    auto nearestDistance = std::abs(value - choices[0]);

    for (size_t index = 1; index < N; ++index)
    {
        const auto distance = std::abs(value - choices[index]);

        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearestIndex = static_cast<int>(index);
        }
    }

    return static_cast<double>(nearestIndex);
}

juce::Typeface::Ptr getSometypeMonoTypeface(const bool useBold)
{
#if JUCE_TARGET_HAS_BINARY_DATA
    static const auto regularTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::SometypeMonoRegular_ttf,
                                                                                BinaryData::SometypeMonoRegular_ttfSize);
    static const auto boldTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::SometypeMonoBold_ttf,
                                                                             BinaryData::SometypeMonoBold_ttfSize);

    return useBold ? boldTypeface : regularTypeface;
#else
    juce::ignoreUnused(useBold);
    return {};
#endif
}

struct VisibleFrequencyRange
{
    float minimumHz = analyserMinFrequency;
    float maximumHz = analyserMaxFrequency;
    float sourceMaximumHz = analyserMaxFrequency;
};

float getSourceMaximumFrequency(const double sampleRate)
{
    return juce::jlimit(analyserMinFrequency + 1.0f,
                        analyserMaxFrequency,
                        static_cast<float>(sampleRate * 0.5));
}

VisibleFrequencyRange getVisibleFrequencyRange(const SpeAudioProcessor::DisplaySettings& settings,
                                               const double sampleRate)
{
    const auto sourceMaximumHz = getSourceMaximumFrequency(sampleRate);
    const auto requestedLeftHz = juce::jlimit(0.0f, 1000.0f, settings.leftFrequencyHz);
    const auto requestedRightHz = juce::jlimit(1000.0f, analyserMaxFrequency, settings.rightFrequencyHz);
    const auto minimumHz = requestedLeftHz <= 0.0f
                             ? analyserMinFrequency
                             : juce::jlimit(analyserMinFrequency, sourceMaximumHz - 1.0f, requestedLeftHz);
    const auto maximumHz = juce::jlimit(minimumHz + 1.0f, sourceMaximumHz, requestedRightHz);

    return { minimumHz, maximumHz, sourceMaximumHz };
}

float sampleScopeAtFrequency(const std::array<float, SpeAudioProcessor::analyserScopeSize>& scopeData,
                             const float frequency,
                             const float sourceMaximumHz)
{
    const auto clampedFrequency = juce::jlimit(analyserMinFrequency, sourceMaximumHz, frequency);
    const auto sourceProportion = std::log10(clampedFrequency / analyserMinFrequency)
                                / std::log10(sourceMaximumHz / analyserMinFrequency);
    const auto scopePosition = juce::jlimit(0.0f,
                                            static_cast<float>(SpeAudioProcessor::analyserScopeSize - 1),
                                            sourceProportion * static_cast<float>(SpeAudioProcessor::analyserScopeSize - 1));
    const auto lowerIndex = juce::jlimit(0,
                                         static_cast<int>(SpeAudioProcessor::analyserScopeSize) - 1,
                                         static_cast<int>(std::floor(scopePosition)));
    const auto upperIndex = juce::jlimit(0,
                                         static_cast<int>(SpeAudioProcessor::analyserScopeSize) - 1,
                                         lowerIndex + 1);
    const auto interpolation = scopePosition - static_cast<float>(lowerIndex);
    return juce::jmap(interpolation,
                      scopeData[static_cast<size_t>(lowerIndex)],
                      scopeData[static_cast<size_t>(upperIndex)]);
}
}

class WheelForwardingTextEditor final : public juce::TextEditor
{
public:
    explicit WheelForwardingTextEditor(juce::Slider& sliderToControl)
        : juce::TextEditor(sliderToControl.getName()),
          slider(sliderToControl)
    {
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        slider.mouseWheelMove(event.getEventRelativeTo(&slider), wheel);
    }

private:
    juce::Slider& slider;
};

class ValueBoxComponent final : public juce::Component
{
public:
    ValueBoxComponent(juce::Slider& sliderToControl, juce::RangedAudioParameter* parameterToControl)
        : slider(sliderToControl),
          parameter(parameterToControl)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }

    ~ValueBoxComponent() override
    {
        stopGlobalEditTracking();
    }

    std::function<void()> onResetRequest;
    std::function<juce::String()> displayTextProvider;
    std::function<juce::String()> editorTextProvider;

    void setOutlineColour(const juce::Colour colour)
    {
        if (outlineColour == colour)
            return;

        outlineColour = colour;

        if (editor != nullptr)
            editor->setColour(juce::TextEditor::outlineColourId, outlineColour);

        repaint();
    }

    void setHighlightColour(const juce::Colour colour)
    {
        if (highlightColour == colour)
            return;

        highlightColour = colour;

        if (editor != nullptr)
            editor->setColour(juce::TextEditor::highlightColourId, highlightColour);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(uiGrey800);
        g.fillRect(getLocalBounds());

        g.setColour(outlineColour);
        g.drawRect(getLocalBounds(), 1);

        g.setColour(uiWhite);
        g.setFont(makeUiFont());
        g.drawFittedText(displayTextProvider != nullptr ? displayTextProvider()
                                                        : slider.getTextFromValue(slider.getValue()),
                         getLocalBounds().reduced(4, 0),
                         juce::Justification::centredRight,
                         1,
                         1.0f);
    }

    void resized() override
    {
        if (editor != nullptr)
            editor->setBounds(getLocalBounds());
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        juce::ignoreUnused(event);

        if (parameter == nullptr)
            return;

        if (editor != nullptr)
            hideEditor(false);

        if (wheel.isInertial)
            return;

        const auto directionalDelta = (std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? -wheel.deltaX : wheel.deltaY)
                                    * (wheel.isReversed ? -1.0f : 1.0f);

        if (std::abs(directionalDelta) < 1.0e-6f)
            return;

        const auto range = parameter->getNormalisableRange();
        const auto interval = range.interval > 0.0f ? range.interval
                                                    : juce::jmax(0.001f, (range.end - range.start) * 0.001f);
        const auto isDiscreteChoiceParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr;

        int stepCount = 0;

        if (wheel.isSmooth)
        {
            smoothWheelAccumulator += directionalDelta;

            while (smoothWheelAccumulator >= smoothWheelStepThreshold)
            {
                ++stepCount;
                smoothWheelAccumulator -= smoothWheelStepThreshold;
            }

            while (smoothWheelAccumulator <= -smoothWheelStepThreshold)
            {
                --stepCount;
                smoothWheelAccumulator += smoothWheelStepThreshold;
            }
        }
        else
        {
            smoothWheelAccumulator = 0.0f;
            stepCount = directionalDelta > 0.0f ? 1 : -1;
        }

        if (stepCount == 0)
            return;

        if (isDiscreteChoiceParameter)
            stepCount = stepCount > 0 ? 1 : -1;

        const auto currentValue = parameter->convertFrom0to1(parameter->getValue());
        const auto stepScale = isDiscreteChoiceParameter ? 1.0f : wheelStepMultiplier;
        const auto unclampedValue = currentValue + (interval * stepScale * static_cast<float>(stepCount));
        const auto legalValue = range.snapToLegalValue(juce::jlimit(range.start, range.end, unclampedValue));

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(legalValue));
        parameter->endChangeGesture();
        clearKeyboardFocus(*this);
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        auto* clickedComponent = event.originalComponent;
        const auto clickIsInsideThisValueBox = clickedComponent != nullptr
            && (clickedComponent == this || isParentOf(clickedComponent));

        if (editor != nullptr && ! clickIsInsideThisValueBox)
        {
            hideEditor(false);
            return;
        }

        if (! clickIsInsideThisValueBox)
            return;

        if (event.mods.isPopupMenu())
            return;

        if (event.mods.isShiftDown())
        {
            if (onResetRequest)
                onResetRequest();

            clearKeyboardFocus(*this);
            return;
        }

        if (! event.mods.isLeftButtonDown() || parameter == nullptr)
            return;

        dragStartNormalisedValue = parameter->getValue();

        if (! isDragging)
        {
            parameter->beginChangeGesture();
            isDragging = true;
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (! isDragging || parameter == nullptr)
            return;

        const auto deltaPixels = -event.getDistanceFromDragStartY();
        const auto newNormalisedValue = juce::jlimit(0.0f,
                                                     1.0f,
                                                     dragStartNormalisedValue
                                                         + (static_cast<float>(deltaPixels) * valueBoxDragNormalisedPerPixel));

        parameter->setValueNotifyingHost(newNormalisedValue);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (isDragging && parameter != nullptr)
        {
            parameter->endChangeGesture();
            isDragging = false;
            clearKeyboardFocus(*this);
            return;
        }

        if (event.mods.isPopupMenu())
        {
            showEditor();
            return;
        }

        clearKeyboardFocus(*this);
    }

private:
    void showEditor()
    {
        if (editor != nullptr)
            return;

        auto textEditor = std::make_unique<WheelForwardingTextEditor>(slider);
        textEditor->setFont(makeUiFont());
        textEditor->setJustification(juce::Justification::centredRight);
        textEditor->setColour(juce::TextEditor::textColourId, uiWhite);
        textEditor->setColour(juce::TextEditor::backgroundColourId, uiGrey800);
        textEditor->setColour(juce::TextEditor::outlineColourId, outlineColour);
        textEditor->setColour(juce::TextEditor::focusedOutlineColourId, outlineColour);
        textEditor->setColour(juce::TextEditor::highlightColourId, highlightColour);
        textEditor->setText(editorTextProvider != nullptr ? editorTextProvider()
                                                          : slider.getTextFromValue(slider.getValue()),
                            false);
        textEditor->onReturnKey = [this] { hideEditor(false); };
        textEditor->onEscapeKey = [this] { hideEditor(true); };
        textEditor->onFocusLost = [this] { hideEditor(false); };

        addAndMakeVisible(*textEditor);
        editor = std::move(textEditor);
        startGlobalEditTracking();
        resized();
        editor->grabKeyboardFocus();
        editor->selectAll();
    }

    void hideEditor(const bool discard)
    {
        if (editor == nullptr)
            return;

        if (! discard && parameter != nullptr)
        {
            const auto enteredValue = slider.getValueFromText(editor->getText().trim());
            const auto clampedValue = juce::jlimit(static_cast<double>(slider.getMinimum()),
                                                   static_cast<double>(slider.getMaximum()),
                                                   enteredValue);

            parameter->beginChangeGesture();
            slider.setValue(clampedValue, juce::sendNotificationSync);
            parameter->endChangeGesture();
        }

        removeChildComponent(editor.get());
        editor.reset();
        stopGlobalEditTracking();
        clearKeyboardFocus(*this);
        repaint();
    }

    void startGlobalEditTracking()
    {
        if (isTrackingGlobalClicks)
            return;

        juce::Desktop::getInstance().addGlobalMouseListener(this);
        isTrackingGlobalClicks = true;
    }

    void stopGlobalEditTracking()
    {
        if (! isTrackingGlobalClicks)
            return;

        juce::Desktop::getInstance().removeGlobalMouseListener(this);
        isTrackingGlobalClicks = false;
    }

    juce::Slider& slider;
    juce::RangedAudioParameter* parameter = nullptr;
    bool isDragging = false;
    bool isTrackingGlobalClicks = false;
    float dragStartNormalisedValue = 0.0f;
    float smoothWheelAccumulator = 0.0f;
    juce::Colour outlineColour = uiGrey500;
    juce::Colour highlightColour = uiAccent;
    std::unique_ptr<WheelForwardingTextEditor> editor;
};

class BoxTextButton final : public juce::TextButton
{
public:
    explicit BoxTextButton(const juce::Colour accent)
        : accentColour(accent)
    {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }

    void paintButton(juce::Graphics& graphics, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused(shouldDrawButtonAsHighlighted);

        const auto fill = shouldDrawButtonAsDown ? uiGrey700 : uiGrey800;
        const auto outline = (alwaysAccentOutline || getToggleState()) ? accentColour : uiGrey500;

        graphics.setColour(fill);
        graphics.fillRect(getLocalBounds());

        graphics.setColour(outline);
        graphics.drawRect(getLocalBounds(), 1);

        graphics.setColour(uiWhite);
        graphics.setFont(makeUiFont());
        graphics.drawFittedText(getButtonText(), getLocalBounds().reduced(3), juce::Justification::centred, 1, 1.0f);
    }

    void setChangedState(const bool shouldBeChanged)
    {
        if (changedState == shouldBeChanged)
            return;

        changedState = shouldBeChanged;
        repaint();
    }

    void setAlwaysAccentOutline(const bool shouldAlwaysAccent)
    {
        if (alwaysAccentOutline == shouldAlwaysAccent)
            return;

        alwaysAccentOutline = shouldAlwaysAccent;
        repaint();
    }

private:
    juce::Colour accentColour;
    bool changedState = false;
    bool alwaysAccentOutline = false;
};

class SpeAudioProcessorEditor::SpeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override
    {
        if (auto typeface = getSometypeMonoTypeface(font.isBold()))
            return typeface;

        return LookAndFeel_V4::getTypefaceForFont(font);
    }
};

ParameterControl::ParameterControl(juce::AudioProcessorValueTreeState& state,
                                   const juce::String& parameterIdIn,
                                   const juce::String& titleText)
    : parameterId(parameterIdIn), title(titleText), parameter(state.getParameter(parameterIdIn))
{
    jassert(parameter != nullptr);

    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    titleLabel.setText(title, juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, uiWhite);
    titleLabel.setColour(juce::Label::backgroundColourId, uiGrey800);
    titleLabel.setColour(juce::Label::outlineColourId, uiGrey500);
    titleLabel.setFont(makeUiFont());
    titleLabel.setBorderSize({ 0, 6, 0, 2 });
    titleLabel.setMinimumHorizontalScale(1.0f);
    titleLabel.setInterceptsMouseClicks(false, false);
    titleLabel.setWantsKeyboardFocus(false);
    titleLabel.setMouseClickGrabsKeyboardFocus(false);

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setInterceptsMouseClicks(false, false);
    slider.setAlpha(0.0f);
    slider.setWantsKeyboardFocus(false);
    slider.setMouseClickGrabsKeyboardFocus(false);
    slider.textFromValueFunction = [this] (double value)
    {
        return formatDisplayValue(value);
    };
    slider.valueFromTextFunction = [this] (const juce::String& text)
    {
        const auto numericValue = parseNumericInput(text);
        const auto trimmedText = text.trim().toUpperCase();

        if (parameterId == SpeAudioProcessor::paramFftSizeId
            || parameterId == SpeAudioProcessor::paramDspFftSizeId)
        {
            static constexpr std::array<double, 5> fftChoices { 1024.0, 2048.0, 4096.0, 8192.0, 16384.0 };
            return findNearestChoiceIndex(numericValue, fftChoices);
        }

        if (parameterId == SpeAudioProcessor::paramOverlapId)
        {
            static constexpr std::array<double, 5> overlapChoices { 2.0, 4.0, 8.0, 16.0, 32.0 };
            return findNearestChoiceIndex(numericValue, overlapChoices);
        }

        if (parameterId == SpeAudioProcessor::paramDeltaId)
        {
            if (trimmedText.contains("ON"))
                return 1.0;

            if (trimmedText.contains("OFF"))
                return 0.0;

            return numericValue >= 0.5 ? 1.0 : 0.0;
        }

        return numericValue;
    };
    slider.onValueChange = [this]
    {
        if (valueBox != nullptr)
            valueBox->repaint();

        refreshChangedAppearance();

        if (auto* parent = getParentComponent())
            parent->repaint();
    };

    if (parameter != nullptr)
        slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));

    attachment = std::make_unique<Attachment>(state, parameterIdIn, slider);
    valueBox = std::make_unique<ValueBoxComponent>(slider, parameter);
    valueBox->onResetRequest = [this]
    {
        resetToDefaultValue();
    };
    valueBox->displayTextProvider = [this]
    {
        return formatDisplayValue(slider.getValue());
    };
    valueBox->editorTextProvider = [this]
    {
        return formatEditorValue();
    };
    applyCurrentAppearance();

    addAndMakeVisible(titleLabel);
    addChildComponent(slider);
    addAndMakeVisible(*valueBox);
    refreshChangedAppearance();
}

ParameterControl::~ParameterControl() = default;

int ParameterControl::getPreferredHeight() const noexcept
{
    return 26;
}

bool ParameterControl::isChangedFromDefault() const noexcept
{
    return changedState;
}

void ParameterControl::resized()
{
    auto row = getLocalBounds();
    titleLabel.setBounds(row.removeFromLeft(parameterNameWidth));
    row.removeFromLeft(parameterGap);
    slider.setBounds(row);

    if (valueBox != nullptr)
        valueBox->setBounds(row);
}

void ParameterControl::applyCurrentAppearance()
{
    const auto outline = uiGrey500;
    const auto highlight = uiAccent;

    titleLabel.setColour(juce::Label::outlineColourId, outline);

    if (valueBox != nullptr)
    {
        valueBox->setOutlineColour(outline);
        valueBox->setHighlightColour(highlight);
    }
}

juce::String ParameterControl::formatDisplayValue(double value) const
{
    if (parameterId == SpeAudioProcessor::paramDeltaId)
    {
        if (parameter != nullptr)
            return parameter->getText(parameter->convertTo0to1(static_cast<float>(value)), 64).trim();

        return value >= 0.5 ? "ON" : "OFF";
    }

    if (parameterId == SpeAudioProcessor::paramFftSizeId
        || parameterId == SpeAudioProcessor::paramDspFftSizeId
        || parameterId == SpeAudioProcessor::paramOverlapId)
    {
        if (parameter != nullptr)
            return formatValueBoxText(parseNumericInput(parameter->getText(parameter->convertTo0to1(static_cast<float>(value)), 64)));

        return formatValueBoxText(value);
    }

    return formatValueBoxText(value);
}

juce::String ParameterControl::formatEditorValue() const
{
    if (parameterId == SpeAudioProcessor::paramDeltaId)
    {
        if (parameter != nullptr)
            return parameter->getText(parameter->convertTo0to1(static_cast<float>(slider.getValue())), 64).trim();

        return slider.getValue() >= 0.5 ? "ON" : "OFF";
    }

    if (parameterId == SpeAudioProcessor::paramFftSizeId
        || parameterId == SpeAudioProcessor::paramDspFftSizeId
        || parameterId == SpeAudioProcessor::paramOverlapId)
    {
        if (parameter != nullptr)
            return juce::String(parseNumericInput(parameter->getText(parameter->convertTo0to1(static_cast<float>(slider.getValue())), 64)),
                                0);

        return juce::String(slider.getValue(), 0);
    }

    if (parameterId == SpeAudioProcessor::paramLeftId
        || parameterId == SpeAudioProcessor::paramRightId
        || parameterId == SpeAudioProcessor::paramTimeId
        || parameterId == SpeAudioProcessor::paramAttackId
        || parameterId == SpeAudioProcessor::paramReleaseId)
        return juce::String(slider.getValue(), 0);

    if (parameterId == SpeAudioProcessor::paramSlopeId)
        return juce::String(slider.getValue(), 2);

    return juce::String(slider.getValue(), 1);
}

void ParameterControl::refreshChangedAppearance()
{
    if (parameter == nullptr)
        return;

    const auto newChangedState = std::abs(parameter->getValue() - parameter->getDefaultValue()) > 1.0e-6f;

    if (changedState == newChangedState)
        return;

    changedState = newChangedState;
    applyCurrentAppearance();
}

void ParameterControl::resetToDefaultValue()
{
    if (parameter == nullptr)
        return;

    parameter->beginChangeGesture();
    slider.setValue(parameter->convertFrom0to1(parameter->getDefaultValue()), juce::sendNotificationSync);
    parameter->endChangeGesture();
    refreshChangedAppearance();
}

SpectrumAnalyserComponent::SpectrumAnalyserComponent(SpeAudioProcessor& p)
    : processor(p)
{
    setInterceptsMouseClicks(false, false);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    startTimerHz(timerRateHz);
}

void SpectrumAnalyserComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour(uiGrey800);
    g.fillRect(bounds);

    const auto plotBounds = bounds.reduced(10.0f);
    g.setColour(uiGrey800);
    g.fillRect(plotBounds);
    const auto visibleRange = getVisibleFrequencyRange(displaySettings, sampleRate);

    juce::Path postSpectrumPath;
    juce::Path reductionPath;
    juce::Path thresholdPath;

    for (auto i = 0; i < SpeAudioProcessor::analyserScopeSize; ++i)
    {
        const auto proportion = static_cast<float>(i) / static_cast<float>(SpeAudioProcessor::analyserScopeSize - 1);
        const auto x = plotBounds.getX() + proportion * plotBounds.getWidth();
        const auto frequency = juce::mapToLog10(proportion, visibleRange.minimumHz, visibleRange.maximumHz);
        const auto sampledDecibels = sampleScopeAtFrequency(scopeData, frequency, visibleRange.sourceMaximumHz);
        const auto sampledReductionDb = juce::jmax(0.0f,
                                                   sampleScopeAtFrequency(gainReductionData,
                                                                          frequency,
                                                                          visibleRange.sourceMaximumHz));
        const auto octavesFromSlopeReference = std::log2(frequency / analyserSlopeReferenceFrequency);
        const auto displaySlopeOffset = displaySettings.slopeDbPerOct * octavesFromSlopeReference;
        const auto thresholdDbAtFrequency = displaySettings.thresholdDb;
        const auto postDecibels = sampledDecibels + displaySlopeOffset;
        const auto postY = decibelsToY(postDecibels, plotBounds);
        const auto thresholdY = decibelsToY(thresholdDbAtFrequency, plotBounds);
        const auto reductionY = decibelsToY(thresholdDbAtFrequency - sampledReductionDb, plotBounds);

        if (i == 0)
        {
            postSpectrumPath.startNewSubPath(x, postY);
            thresholdPath.startNewSubPath(x, thresholdY);
            reductionPath.startNewSubPath(x, thresholdY);
            reductionPath.lineTo(x, reductionY);
        }
        else
        {
            postSpectrumPath.lineTo(x, postY);
            thresholdPath.lineTo(x, thresholdY);
            reductionPath.lineTo(x, reductionY);
        }
    }

    juce::Path spectrumFillPath(postSpectrumPath);
    spectrumFillPath.lineTo(plotBounds.getRight(), plotBounds.getBottom());
    spectrumFillPath.lineTo(plotBounds.getX(), plotBounds.getBottom());
    spectrumFillPath.closeSubPath();

    for (auto i = static_cast<int>(SpeAudioProcessor::analyserScopeSize) - 1; i >= 0; --i)
    {
        const auto proportion = static_cast<float>(i) / static_cast<float>(SpeAudioProcessor::analyserScopeSize - 1);
        const auto x = plotBounds.getX() + proportion * plotBounds.getWidth();
        const auto thresholdDbAtFrequency = displaySettings.thresholdDb;
        reductionPath.lineTo(x, decibelsToY(thresholdDbAtFrequency, plotBounds));
    }

    reductionPath.closeSubPath();

    g.setColour(uiAccent);
    g.fillPath(spectrumFillPath);
    g.setColour(uiChanged);
    g.fillPath(reductionPath);
    g.strokePath(thresholdPath, juce::PathStrokeType(1.0f));
}

void SpectrumAnalyserComponent::timerCallback()
{
    processor.copyAnalyserData(scopeData, sampleRate);
    processor.copyGainReductionData(gainReductionData);
    displaySettings = processor.getDisplaySettings();
    analysisSettings = processor.getAnalysisSettings();
    updateTimerRate();
    repaint();
}

void SpectrumAnalyserComponent::updateTimerRate()
{
    const auto overlapFactor = juce::jmax(1, analysisSettings.overlapFactor);
    const auto hopSize = juce::jmax(1, analysisSettings.fftSize / overlapFactor);
    const auto analyserUpdateRateHz = sampleRate / static_cast<double>(hopSize);
    const auto targetRateHz = juce::jlimit(12, 120, juce::roundToInt(analyserUpdateRateHz));

    if (targetRateHz != timerRateHz)
    {
        timerRateHz = targetRateHz;
        startTimerHz(timerRateHz);
    }
}

float SpectrumAnalyserComponent::frequencyToX(float frequency, juce::Rectangle<float> bounds) const
{
    const auto visibleRange = getVisibleFrequencyRange(displaySettings, sampleRate);
    const auto clampedFrequency = juce::jlimit(visibleRange.minimumHz, visibleRange.maximumHz, frequency);
    const auto proportion = std::log10(clampedFrequency / visibleRange.minimumHz)
                          / std::log10(visibleRange.maximumHz / visibleRange.minimumHz);
    return bounds.getX() + proportion * bounds.getWidth();
}

float SpectrumAnalyserComponent::decibelsToY(float decibels, juce::Rectangle<float> bounds) const
{
    return juce::jmap(juce::jlimit(displaySettings.rangeLowDb, displaySettings.rangeHighDb, decibels),
                      displaySettings.rangeLowDb,
                      displaySettings.rangeHighDb,
                      bounds.getBottom(),
                      bounds.getY());
}

SpeAudioProcessorEditor::SpeAudioProcessorEditor(SpeAudioProcessor& p)
    : AudioProcessorEditor(&p),
      valueTreeState(p.getValueTreeState()),
      spectrumAnalyser(p),
      dualMonoLeftThresholdControl(p.getValueTreeState(), SpeAudioProcessor::paramDualMonoLeftThresholdId, "LL-THRESHOLD"),
      dualMonoLeftAdaptiveControl(p.getValueTreeState(), SpeAudioProcessor::paramDualMonoLeftAdaptiveId, "LL-ADAPTIVE"),
      dualMonoLeftAdaptiveOffsetControl(p.getValueTreeState(), SpeAudioProcessor::paramDualMonoLeftAdaptiveOffsetId, "LL-OFFSET"),
      dualMonoRightThresholdControl(p.getValueTreeState(), SpeAudioProcessor::paramDualMonoRightThresholdId, "RR-THRESHOLD"),
      dualMonoRightAdaptiveControl(p.getValueTreeState(), SpeAudioProcessor::paramDualMonoRightAdaptiveId, "RR-ADAPTIVE"),
      dualMonoRightAdaptiveOffsetControl(p.getValueTreeState(), SpeAudioProcessor::paramDualMonoRightAdaptiveOffsetId, "RR-OFFSET"),
      thresholdControl(p.getValueTreeState(), SpeAudioProcessor::paramThresholdId, "THRESHOLD"),
      stereoAdaptiveControl(p.getValueTreeState(), SpeAudioProcessor::paramStereoAdaptiveId, "ADAPTIVE"),
      stereoAdaptiveOffsetControl(p.getValueTreeState(), SpeAudioProcessor::paramStereoAdaptiveOffsetId, "OFFSET"),
      inputGainControl(p.getValueTreeState(), SpeAudioProcessor::paramInputGainId, "IN-GAIN"),
      attackControl(p.getValueTreeState(), SpeAudioProcessor::paramAttackId, "ATTACK"),
      releaseControl(p.getValueTreeState(), SpeAudioProcessor::paramReleaseId, "RELEASE"),
      kneeControl(p.getValueTreeState(), SpeAudioProcessor::paramKneeId, "KNEE"),
      ratioControl(p.getValueTreeState(), SpeAudioProcessor::paramRatioId, "RATIO"),
      makeupControl(p.getValueTreeState(), SpeAudioProcessor::paramMakeupId, "OUT-GAIN"),
      dspFftSizeControl(p.getValueTreeState(), SpeAudioProcessor::paramDspFftSizeId, "WINDOW-SIZE"),
      dspSlopeControl(p.getValueTreeState(), SpeAudioProcessor::paramDspSlopeId, "SLOPE"),
      fftSizeControl(p.getValueTreeState(), SpeAudioProcessor::paramFftSizeId, "FFT-SIZE"),
      overlapControl(p.getValueTreeState(), SpeAudioProcessor::paramOverlapId, "OVERLAP"),
      leftControl(p.getValueTreeState(), SpeAudioProcessor::paramLeftId, "LEFT"),
      rightControl(p.getValueTreeState(), SpeAudioProcessor::paramRightId, "RIGHT"),
      rangeLowControl(p.getValueTreeState(), SpeAudioProcessor::paramRangeLowId, "LOW"),
      rangeHighControl(p.getValueTreeState(), SpeAudioProcessor::paramRangeHighId, "HIGH"),
      slopeControl(p.getValueTreeState(), SpeAudioProcessor::paramSlopeId, "SLOPE"),
      timeControl(p.getValueTreeState(), SpeAudioProcessor::paramTimeId, "TIME"),
      lookAndFeel(std::make_unique<SpeLookAndFeel>())
{
    setLookAndFeel(lookAndFeel.get());
    setFocusContainerType(juce::Component::FocusContainerType::none);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    focusProxyEditor.setAlpha(0.0f);
    focusProxyEditor.setInterceptsMouseClicks(false, false);
    focusProxyEditor.setScrollbarsShown(false);
    focusProxyEditor.setCaretVisible(false);
    focusProxyEditor.setPopupMenuEnabled(false);
    focusProxyEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    focusProxyEditor.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    focusProxyEditor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    focusProxyEditor.setColour(juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(focusProxyEditor);

    addAndMakeVisible(spectrumAnalyser);
    globalHeader = std::make_unique<BoxTextButton>(uiAccent);
    globalHeader->setButtonText("GLOBAL");
    globalHeader->setClickingTogglesState(true);
    globalHeader->setToggleState(globalExpanded, juce::dontSendNotification);
    globalHeader->onClick = [this]
    {
        openSection(Section::global);
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*globalHeader);
    dualMonoHeader = std::make_unique<BoxTextButton>(uiAccent);
    dualMonoHeader->setButtonText("DUAL-MONO");
    dualMonoHeader->setClickingTogglesState(true);
    dualMonoHeader->setToggleState(dualMonoExpanded, juce::dontSendNotification);
    dualMonoHeader->onClick = [this]
    {
        openSection(Section::dualMono);
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*dualMonoHeader);
    stereoHeader = std::make_unique<BoxTextButton>(uiAccent);
    stereoHeader->setButtonText("STEREO");
    stereoHeader->setClickingTogglesState(true);
    stereoHeader->setToggleState(stereoExpanded, juce::dontSendNotification);
    stereoHeader->onClick = [this]
    {
        openSection(Section::stereo);
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*stereoHeader);
    analyserHeader = std::make_unique<BoxTextButton>(uiAccent);
    analyserHeader->setButtonText("ANALYSER");
    analyserHeader->setClickingTogglesState(true);
    analyserHeader->setToggleState(analyserExpanded, juce::dontSendNotification);
    analyserHeader->onClick = [this]
    {
        openSection(Section::analyser);
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*analyserHeader);
    dualMonoBypassButton = std::make_unique<BoxTextButton>(uiAccent);
    dualMonoBypassButton->setButtonText("BYPASS");
    dualMonoBypassButton->setClickingTogglesState(true);
    dualMonoBypassButton->onClick = [this]
    {
        updateSectionStates();
        clearKeyboardFocus(*this);
    };
    dualMonoBypassAttachment = std::make_unique<ButtonAttachment>(p.getValueTreeState(), SpeAudioProcessor::paramDualMonoBypassId, *dualMonoBypassButton);
    addAndMakeVisible(*dualMonoBypassButton);
    bypassButton = std::make_unique<BoxTextButton>(uiAccent);
    bypassButton->setButtonText("BYPASS");
    bypassButton->setClickingTogglesState(true);
    bypassButton->onClick = [this]
    {
        updateSectionStates();
        clearKeyboardFocus(*this);
    };
    bypassAttachment = std::make_unique<ButtonAttachment>(p.getValueTreeState(), SpeAudioProcessor::paramBypassId, *bypassButton);
    addAndMakeVisible(*bypassButton);
    deltaButton = std::make_unique<BoxTextButton>(uiAccent);
    deltaButton->setButtonText("DELTA");
    deltaButton->setClickingTogglesState(true);
    deltaButton->onClick = [this]
    {
        updateSectionStates();
        clearKeyboardFocus(*this);
    };
    deltaAttachment = std::make_unique<ButtonAttachment>(p.getValueTreeState(), SpeAudioProcessor::paramDeltaId, *deltaButton);
    addAndMakeVisible(*deltaButton);
    analyserVisibilityButton = std::make_unique<BoxTextButton>(uiAccent);
    analyserVisibilityButton->setButtonText("HIDE");
    analyserVisibilityButton->setClickingTogglesState(true);
    analyserVisibilityButton->setToggleState(analyserVisible, juce::dontSendNotification);
    analyserVisibilityButton->onClick = [this]
    {
        analyserVisible = analyserVisibilityButton->getToggleState();
        updateEditorWidthForAnalyserVisibility();
        updateSectionStates();
        resized();
        clearKeyboardFocus(*this);
    };
    addAndMakeVisible(*analyserVisibilityButton);
    addAndMakeVisible(dualMonoLeftThresholdControl);
    addAndMakeVisible(dualMonoLeftAdaptiveControl);
    addAndMakeVisible(dualMonoLeftAdaptiveOffsetControl);
    addAndMakeVisible(dualMonoRightThresholdControl);
    addAndMakeVisible(dualMonoRightAdaptiveControl);
    addAndMakeVisible(dualMonoRightAdaptiveOffsetControl);
    addAndMakeVisible(thresholdControl);
    addAndMakeVisible(stereoAdaptiveControl);
    addAndMakeVisible(stereoAdaptiveOffsetControl);
    addAndMakeVisible(inputGainControl);
    addAndMakeVisible(attackControl);
    addAndMakeVisible(releaseControl);
    addAndMakeVisible(kneeControl);
    addAndMakeVisible(ratioControl);
    addAndMakeVisible(makeupControl);
    addAndMakeVisible(dspFftSizeControl);
    addAndMakeVisible(dspSlopeControl);
    addAndMakeVisible(fftSizeControl);
    addAndMakeVisible(overlapControl);
    addAndMakeVisible(leftControl);
    addAndMakeVisible(rightControl);
    addAndMakeVisible(rangeLowControl);
    addAndMakeVisible(rangeHighControl);
    addAndMakeVisible(slopeControl);
    addAndMakeVisible(timeControl);
    footerLabel.setText("SPE by MIXOLVE", juce::dontSendNotification);
    footerLabel.setJustificationType(juce::Justification::centred);
    footerLabel.setColour(juce::Label::textColourId, uiWhite);
    footerLabel.setFont(makeUiFont());
    footerLabel.setInterceptsMouseClicks(false, false);
    footerLabel.setWantsKeyboardFocus(false);
    footerLabel.setMouseClickGrabsKeyboardFocus(false);
    addAndMakeVisible(footerLabel);

    setResizable(true, false);
    setResizeLimits(minimumVisibleEditorWidth, fixedEditorHeight, maximumEditorWidth, fixedEditorHeight);
    setSize(fixedEditorWidth, fixedEditorHeight);
    lastExpandedEditorWidth = fixedEditorWidth;

    updateSectionStates();
}

SpeAudioProcessorEditor::~SpeAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void SpeAudioProcessorEditor::openSection(const Section section)
{
    const auto clickedExpanded = (section == Section::dualMono && dualMonoExpanded)
                              || (section == Section::stereo && stereoExpanded)
                              || (section == Section::global && globalExpanded)
                              || (section == Section::analyser && analyserExpanded);

    dualMonoExpanded = false;
    stereoExpanded = false;
    globalExpanded = false;
    analyserExpanded = false;

    if (! clickedExpanded)
    {
        dualMonoExpanded = section == Section::dualMono;
        stereoExpanded = section == Section::stereo;
        globalExpanded = section == Section::global;
        analyserExpanded = section == Section::analyser;
    }

    updateSectionStates();
    resized();
}

juce::ComponentPeer* SpeAudioProcessorEditor::createNewPeer(int styleFlags, void* nativeWindowToAttachTo)
{
    auto* peer = Component::createNewPeer(styleFlags | juce::ComponentPeer::windowIgnoresKeyPresses,
                                          nativeWindowToAttachTo);

    juce::MessageManager::callAsync([safeEditor = juce::Component::SafePointer<SpeAudioProcessorEditor>(this)]
    {
        if (safeEditor != nullptr)
            safeEditor->beginFocusReleasePasses();
    });

    return peer;
}

void SpeAudioProcessorEditor::beginFocusReleasePasses()
{
    if (getPeer() == nullptr || ! isShowing())
        return;

    focusProxyEditor.grabKeyboardFocus();
    focusReleasePassesRemaining = 20;
    startTimer(50);
}

void SpeAudioProcessorEditor::timerCallback()
{
    clearKeyboardFocus(*this);

    if (--focusReleasePassesRemaining <= 0)
        stopTimer();
}

void SpeAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    clearKeyboardFocus(*this);
}

void SpeAudioProcessorEditor::parentHierarchyChanged()
{
    beginFocusReleasePasses();
}

void SpeAudioProcessorEditor::paint(juce::Graphics& g)
{
    updateSectionStates();
    g.fillAll(uiGrey800);
}

void SpeAudioProcessorEditor::updateEditorWidthForAnalyserVisibility()
{
    if (analyserVisible)
    {
        const auto restoredWidth = juce::jmax(minimumVisibleEditorWidth, lastExpandedEditorWidth);
        setResizable(true, false);
        setResizeLimits(minimumVisibleEditorWidth, fixedEditorHeight, maximumEditorWidth, fixedEditorHeight);
        setSize(restoredWidth, fixedEditorHeight);
        return;
    }

    lastExpandedEditorWidth = juce::jmax(minimumVisibleEditorWidth, getWidth());
    setResizable(false, false);
    setResizeLimits(collapsedEditorWidth, fixedEditorHeight, collapsedEditorWidth, fixedEditorHeight);
    setSize(collapsedEditorWidth, fixedEditorHeight);
}

void SpeAudioProcessorEditor::updateSectionStates()
{
    const auto dualMonoSectionChanged = dualMonoLeftThresholdControl.isChangedFromDefault()
        || dualMonoRightThresholdControl.isChangedFromDefault()
        || dualMonoLeftAdaptiveControl.isChangedFromDefault()
        || dualMonoRightAdaptiveControl.isChangedFromDefault()
        || dualMonoLeftAdaptiveOffsetControl.isChangedFromDefault()
        || dualMonoRightAdaptiveOffsetControl.isChangedFromDefault()
        || isParameterChangedFromDefault(valueTreeState, SpeAudioProcessor::paramDualMonoBypassId);

    if (dualMonoHeader != nullptr)
    {
        dualMonoHeader->setToggleState(dualMonoExpanded, juce::dontSendNotification);
        dualMonoHeader->setChangedState(dualMonoSectionChanged);
    }

    const auto stereoSectionChanged = thresholdControl.isChangedFromDefault()
        || stereoAdaptiveControl.isChangedFromDefault()
        || stereoAdaptiveOffsetControl.isChangedFromDefault()
        || isParameterChangedFromDefault(valueTreeState, SpeAudioProcessor::paramBypassId);

    if (stereoHeader != nullptr)
    {
        stereoHeader->setToggleState(stereoExpanded, juce::dontSendNotification);
        stereoHeader->setChangedState(stereoSectionChanged);
    }

    const auto globalSectionChanged = inputGainControl.isChangedFromDefault()
        || attackControl.isChangedFromDefault()
        || releaseControl.isChangedFromDefault()
        || kneeControl.isChangedFromDefault()
        || ratioControl.isChangedFromDefault()
        || dspFftSizeControl.isChangedFromDefault()
        || dspSlopeControl.isChangedFromDefault()
        || makeupControl.isChangedFromDefault()
        || isParameterChangedFromDefault(valueTreeState, SpeAudioProcessor::paramDeltaId);

    if (globalHeader != nullptr)
    {
        globalHeader->setToggleState(globalExpanded, juce::dontSendNotification);
        globalHeader->setChangedState(globalSectionChanged);
    }

    const auto analyserSectionChanged = fftSizeControl.isChangedFromDefault()
        || overlapControl.isChangedFromDefault()
        || leftControl.isChangedFromDefault()
        || rightControl.isChangedFromDefault()
        || rangeLowControl.isChangedFromDefault()
        || rangeHighControl.isChangedFromDefault()
        || slopeControl.isChangedFromDefault()
        || timeControl.isChangedFromDefault();

    if (analyserHeader != nullptr)
    {
        analyserHeader->setToggleState(analyserExpanded, juce::dontSendNotification);
        analyserHeader->setChangedState(analyserSectionChanged);
    }

    if (analyserVisibilityButton != nullptr)
    {
        analyserVisibilityButton->setToggleState(analyserVisible, juce::dontSendNotification);
        analyserVisibilityButton->setButtonText("HIDE");
    }

    inputGainControl.setVisible(globalExpanded);
    attackControl.setVisible(globalExpanded);
    releaseControl.setVisible(globalExpanded);
    kneeControl.setVisible(globalExpanded);
    ratioControl.setVisible(globalExpanded);
    dspFftSizeControl.setVisible(globalExpanded);
    dspSlopeControl.setVisible(globalExpanded);
    makeupControl.setVisible(globalExpanded);
    if (deltaButton != nullptr)
        deltaButton->setVisible(globalExpanded);
    dualMonoLeftThresholdControl.setVisible(dualMonoExpanded);
    dualMonoRightThresholdControl.setVisible(dualMonoExpanded);
    dualMonoLeftAdaptiveControl.setVisible(dualMonoExpanded);
    dualMonoRightAdaptiveControl.setVisible(dualMonoExpanded);
    dualMonoLeftAdaptiveOffsetControl.setVisible(dualMonoExpanded);
    dualMonoRightAdaptiveOffsetControl.setVisible(dualMonoExpanded);
    if (dualMonoBypassButton != nullptr)
        dualMonoBypassButton->setVisible(dualMonoExpanded);
    thresholdControl.setVisible(stereoExpanded);
    stereoAdaptiveControl.setVisible(stereoExpanded);
    stereoAdaptiveOffsetControl.setVisible(stereoExpanded);
    if (bypassButton != nullptr)
        bypassButton->setVisible(stereoExpanded);
    fftSizeControl.setVisible(analyserExpanded);
    overlapControl.setVisible(analyserExpanded);
    leftControl.setVisible(analyserExpanded);
    rightControl.setVisible(analyserExpanded);
    rangeLowControl.setVisible(analyserExpanded);
    rangeHighControl.setVisible(analyserExpanded);
    slopeControl.setVisible(analyserExpanded);
    timeControl.setVisible(analyserExpanded);
    spectrumAnalyser.setVisible(analyserVisible);
    if (analyserVisibilityButton != nullptr)
        analyserVisibilityButton->setVisible(analyserExpanded);
}

void SpeAudioProcessorEditor::resized()
{
    if (analyserVisible)
        lastExpandedEditorWidth = juce::jmax(minimumVisibleEditorWidth, getWidth());

    auto bounds = getLocalBounds();
    focusProxyEditor.setBounds(0, 0, 1, 1);
    auto parameterBounds = bounds.removeFromLeft(parameterPanelWidth);
    if (analyserVisible)
    {
        bounds.removeFromLeft(panelGap);
        spectrumAnalyser.setBounds(bounds);
    }
    else
    {
        spectrumAnalyser.setBounds({});
    }

    parameterBounds.removeFromLeft(editorInsetX);
    parameterBounds.removeFromRight(editorInsetX);
    parameterBounds.removeFromBottom(editorInsetBottom);
    parameterBounds.removeFromTop(4);

    auto footerBounds = parameterBounds.removeFromBottom(footerHeight);
    footerLabel.setBounds(footerBounds.translated(0, -3));
    parameterBounds.removeFromBottom(4);

    auto placeHeader = [&parameterBounds] (BoxTextButton& header)
    {
        auto headerBounds = parameterBounds.removeFromTop(22);
        headerBounds.removeFromLeft(4);
        headerBounds.removeFromRight(4);
        header.setBounds(headerBounds);

        if (! parameterBounds.isEmpty())
            parameterBounds.removeFromTop(6);
    };

    auto placeControl = [&parameterBounds] (ParameterControl& control)
    {
        auto controlBounds = parameterBounds.removeFromTop(control.getPreferredHeight());
        controlBounds.removeFromLeft(4);
        controlBounds.removeFromRight(4);
        control.setBounds(controlBounds);

        if (! parameterBounds.isEmpty())
            parameterBounds.removeFromTop(6);
    };

    if (dualMonoHeader != nullptr)
        placeHeader(*dualMonoHeader);

    if (dualMonoExpanded)
    {
        placeControl(dualMonoLeftThresholdControl);
        placeControl(dualMonoLeftAdaptiveControl);
        placeControl(dualMonoLeftAdaptiveOffsetControl);
        placeControl(dualMonoRightThresholdControl);
        placeControl(dualMonoRightAdaptiveControl);
        placeControl(dualMonoRightAdaptiveOffsetControl);

        if (dualMonoBypassButton != nullptr)
        {
            auto toggleBounds = parameterBounds.removeFromTop(24);
            toggleBounds.removeFromLeft(4);
            toggleBounds.removeFromRight(4);
            dualMonoBypassButton->setBounds(toggleBounds);

            if (! parameterBounds.isEmpty())
                parameterBounds.removeFromTop(6);
        }
    }

    if (stereoHeader != nullptr)
        placeHeader(*stereoHeader);

    if (stereoExpanded)
    {
        placeControl(thresholdControl);
        placeControl(stereoAdaptiveControl);
        placeControl(stereoAdaptiveOffsetControl);

        if (bypassButton != nullptr)
        {
            auto toggleBounds = parameterBounds.removeFromTop(24);
            toggleBounds.removeFromLeft(4);
            toggleBounds.removeFromRight(4);
            bypassButton->setBounds(toggleBounds);

            if (! parameterBounds.isEmpty())
                parameterBounds.removeFromTop(6);
        }
    }

    if (globalHeader != nullptr)
        placeHeader(*globalHeader);

    if (globalExpanded)
    {
        placeControl(inputGainControl);
        placeControl(attackControl);
        placeControl(releaseControl);
        placeControl(kneeControl);
        placeControl(ratioControl);
        placeControl(dspFftSizeControl);
        placeControl(dspSlopeControl);
        placeControl(makeupControl);

        if (deltaButton != nullptr)
        {
            auto toggleBounds = parameterBounds.removeFromTop(24);
            toggleBounds.removeFromLeft(4);
            toggleBounds.removeFromRight(4);
            deltaButton->setBounds(toggleBounds);

            if (! parameterBounds.isEmpty())
                parameterBounds.removeFromTop(6);
        }
    }

    if (analyserHeader != nullptr)
        placeHeader(*analyserHeader);

    if (! analyserExpanded)
        return;

    placeControl(fftSizeControl);
    placeControl(overlapControl);
    placeControl(leftControl);
    placeControl(rightControl);
    placeControl(rangeLowControl);
    placeControl(rangeHighControl);
    placeControl(slopeControl);
    placeControl(timeControl);

    if (analyserVisibilityButton != nullptr)
    {
        auto toggleBounds = parameterBounds.removeFromTop(24);
        toggleBounds.removeFromLeft(4);
        toggleBounds.removeFromRight(4);
        analyserVisibilityButton->setBounds(toggleBounds);
    }
}

void SpeAudioProcessorEditor::visibilityChanged()
{
    beginFocusReleasePasses();
}
