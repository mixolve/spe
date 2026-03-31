#include "PluginEditor.h"

SpeAudioProcessorEditor::SpeAudioProcessorEditor(SpeAudioProcessor& p)
    : AudioProcessorEditor(&p)
{
    titleLabel.setText("spe", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::Font(juce::FontOptions(28.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);

    setSize(480, 320);
}

SpeAudioProcessorEditor::~SpeAudioProcessorEditor() = default;

void SpeAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(20, 24, 32));

    g.setColour(juce::Colour::fromRGB(64, 170, 120));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(16.0f), 18.0f, 2.0f);

    g.setColour(juce::Colours::whitesmoke);
    g.setFont(16.0f);
    g.drawFittedText("JUCE VST3 skeleton for macOS", getLocalBounds().reduced(32, 96),
                     juce::Justification::centredTop, 2);
}

void SpeAudioProcessorEditor::resized()
{
    titleLabel.setBounds(40, 40, getWidth() - 80, 40);
}
