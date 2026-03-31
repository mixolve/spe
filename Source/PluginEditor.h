#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpeAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SpeAudioProcessorEditor(SpeAudioProcessor&);
    ~SpeAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpeAudioProcessorEditor)
};
