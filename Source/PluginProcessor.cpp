#include "PluginProcessor.h"
#include "PluginEditor.h"

SpeAudioProcessor::SpeAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

SpeAudioProcessor::~SpeAudioProcessor() = default;

const juce::String SpeAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SpeAudioProcessor::acceptsMidi() const
{
    return false;
}

bool SpeAudioProcessor::producesMidi() const
{
    return false;
}

bool SpeAudioProcessor::isMidiEffect() const
{
    return false;
}

double SpeAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SpeAudioProcessor::getNumPrograms()
{
    return 1;
}

int SpeAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SpeAudioProcessor::setCurrentProgram(int)
{
}

const juce::String SpeAudioProcessor::getProgramName(int)
{
    return {};
}

void SpeAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void SpeAudioProcessor::prepareToPlay(double, int)
{
}

void SpeAudioProcessor::releaseResources()
{
}

bool SpeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainInput = layouts.getMainInputChannelSet();
    const auto mainOutput = layouts.getMainOutputChannelSet();

    if (mainInput != mainOutput)
        return false;

    return mainInput == juce::AudioChannelSet::mono()
        || mainInput == juce::AudioChannelSet::stereo();
}

void SpeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}

bool SpeAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SpeAudioProcessor::createEditor()
{
    return new SpeAudioProcessorEditor(*this);
}

void SpeAudioProcessor::getStateInformation(juce::MemoryBlock&)
{
}

void SpeAudioProcessor::setStateInformation(const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpeAudioProcessor();
}
