#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ValueBoxComponent;
class BoxTextButton;

class ParameterControl final : public juce::Component
{
public:
    ParameterControl(juce::AudioProcessorValueTreeState& state,
                     const juce::String& parameterIdIn,
                     const juce::String& titleText);
    ~ParameterControl() override;

    int getPreferredHeight() const noexcept;
    void resized() override;

private:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void applyCurrentAppearance();
    juce::String formatDisplayValue(double value) const;
    juce::String formatEditorValue() const;
    void refreshChangedAppearance();
    void resetToDefaultValue();

public:
    bool isChangedFromDefault() const noexcept;

private:
    juce::String parameterId;
    juce::String title;
    juce::Slider slider;
    juce::Label titleLabel;
    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<ValueBoxComponent> valueBox;
    std::unique_ptr<Attachment> attachment;
    bool changedState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterControl)
};

class SpectrumAnalyserComponent final : public juce::Component,
                                        private juce::Timer
{
public:
    explicit SpectrumAnalyserComponent(SpeAudioProcessor&);

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    float frequencyToX(float frequency, juce::Rectangle<float> bounds) const;
    float decibelsToY(float decibels, juce::Rectangle<float> bounds) const;
    void updateTimerRate();

    SpeAudioProcessor& processor;
    std::array<float, SpeAudioProcessor::analyserScopeSize> scopeData {};
    std::array<float, SpeAudioProcessor::analyserScopeSize> gainReductionData {};
    SpeAudioProcessor::DisplaySettings displaySettings;
    SpeAudioProcessor::AnalysisSettings analysisSettings;
    double sampleRate = 44100.0;
    int timerRateHz = 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumAnalyserComponent)
};

class SpeAudioProcessorEditor final : public juce::AudioProcessorEditor
                                    , private juce::Timer
{
public:
    explicit SpeAudioProcessorEditor(SpeAudioProcessor&);
    ~SpeAudioProcessorEditor() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void parentHierarchyChanged() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    enum class Section
    {
        dualMono,
        stereo,
        global,
        analyser
    };

    class SpeLookAndFeel;

    void beginFocusReleasePasses();
    juce::ComponentPeer* createNewPeer(int styleFlags, void* nativeWindowToAttachTo) override;
    void openSection(Section section);
    void timerCallback() override;
    void updateEditorWidthForAnalyserVisibility();
    void updateSectionStates();

    juce::AudioProcessorValueTreeState& valueTreeState;
    SpectrumAnalyserComponent spectrumAnalyser;
    ParameterControl dualMonoLeftThresholdControl;
    ParameterControl dualMonoRightThresholdControl;
    ParameterControl thresholdControl;
    ParameterControl inputGainControl;
    ParameterControl attackControl;
    ParameterControl releaseControl;
    ParameterControl kneeControl;
    ParameterControl ratioControl;
    ParameterControl makeupControl;
    ParameterControl dspFftSizeControl;
    ParameterControl dspSlopeControl;
    ParameterControl fftSizeControl;
    ParameterControl overlapControl;
    ParameterControl leftControl;
    ParameterControl rightControl;
    ParameterControl rangeLowControl;
    ParameterControl rangeHighControl;
    ParameterControl slopeControl;
    ParameterControl timeControl;
    std::unique_ptr<BoxTextButton> globalHeader;
    std::unique_ptr<BoxTextButton> dualMonoHeader;
    std::unique_ptr<BoxTextButton> stereoHeader;
    std::unique_ptr<BoxTextButton> analyserHeader;
    std::unique_ptr<BoxTextButton> dualMonoBypassButton;
    std::unique_ptr<BoxTextButton> bypassButton;
    std::unique_ptr<BoxTextButton> deltaButton;
    std::unique_ptr<BoxTextButton> analyserVisibilityButton;
    std::unique_ptr<ButtonAttachment> dualMonoBypassAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ButtonAttachment> deltaAttachment;
    juce::Label footerLabel;
    juce::TextEditor focusProxyEditor;
    std::unique_ptr<SpeLookAndFeel> lookAndFeel;
    bool dualMonoExpanded = false;
    bool stereoExpanded = true;
    bool globalExpanded = false;
    bool analyserExpanded = false;
    bool analyserVisible = true;
    int focusReleasePassesRemaining = 0;
    int lastExpandedEditorWidth = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpeAudioProcessorEditor)
};
