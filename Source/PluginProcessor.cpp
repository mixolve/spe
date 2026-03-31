#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto analyserMinFrequency = 20.0f;
constexpr auto analyserMaxFrequency = 22000.0f;
constexpr auto analyserMinDecibels = -120.0f;
constexpr auto analyserMaxDecibels = 12.0f;

juce::String formatDecibelValue(const float value)
{
    return juce::String::formatted("%.0f dB", static_cast<double>(value));
}

juce::String formatFrequencyValue(const float value)
{
    return juce::String::formatted("%.0f Hz", static_cast<double>(value));
}

juce::String formatSlopeValue(const float value)
{
    return juce::String::formatted("%.2f dB/oct", static_cast<double>(value));
}

juce::String formatRatioValue(const float value)
{
    return juce::String::formatted("%.2f:1", static_cast<double>(value));
}

juce::String formatTimeValue(const float value)
{
    return juce::String::formatted("%.0f ms", static_cast<double>(value));
}
}

SpeAudioProcessor::SpeAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "spe_state", createParameterLayout())
{
    fftSizeParam = parameters.getRawParameterValue(paramFftSizeId);
    overlapParam = parameters.getRawParameterValue(paramOverlapId);
    leftParam = parameters.getRawParameterValue(paramLeftId);
    rightParam = parameters.getRawParameterValue(paramRightId);
    rangeLowParam = parameters.getRawParameterValue(paramRangeLowId);
    rangeHighParam = parameters.getRawParameterValue(paramRangeHighId);
    slopeParam = parameters.getRawParameterValue(paramSlopeId);
    timeParam = parameters.getRawParameterValue(paramTimeId);
    thresholdParam = parameters.getRawParameterValue(paramThresholdId);
    dualMonoLeftThresholdParam = parameters.getRawParameterValue(paramDualMonoLeftThresholdId);
    dualMonoRightThresholdParam = parameters.getRawParameterValue(paramDualMonoRightThresholdId);
    inputGainParam = parameters.getRawParameterValue(paramInputGainId);
    attackParam = parameters.getRawParameterValue(paramAttackId);
    releaseParam = parameters.getRawParameterValue(paramReleaseId);
    kneeParam = parameters.getRawParameterValue(paramKneeId);
    ratioParam = parameters.getRawParameterValue(paramRatioId);
    makeupParam = parameters.getRawParameterValue(paramMakeupId);
    bypassParam = parameters.getRawParameterValue(paramBypassId);
    dualMonoBypassParam = parameters.getRawParameterValue(paramDualMonoBypassId);
    deltaParam = parameters.getRawParameterValue(paramDeltaId);
    dspFftSizeParam = parameters.getRawParameterValue(paramDspFftSizeId);
    dspSlopeParam = parameters.getRawParameterValue(paramDspSlopeId);
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

void SpeAudioProcessor::prepareToPlay(double sampleRate, int)
{
    spectralCompressor.prepare(sampleRate, getTotalNumInputChannels());
    activeLatencySamples = getSelectedDspFftSize();
    setLatencySamples(activeLatencySamples);
    resetDeltaDelay();
    outputAnalyser.prepare(sampleRate);
}

void SpeAudioProcessor::releaseResources()
{
    resetDeltaDelay();
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

    const auto analysisSettings = getAnalysisSettings();
    auto compressorSettings = getCompressorSettings();
    const auto deltaEnabled = isDeltaEnabled();
    const auto channelsToUse = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());

    if (compressorSettings.fftSize != activeLatencySamples)
    {
        activeLatencySamples = compressorSettings.fftSize;
        setLatencySamples(activeLatencySamples);
        resetDeltaDelay();
    }

    deltaDryBuffer.setSize(channelsToUse, buffer.getNumSamples(), false, false, true);
    populateAlignedDryBuffer(buffer, deltaDryBuffer, channelsToUse, activeLatencySamples);

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    const auto inputGainDb = juce::jlimit(-24.0f, 24.0f, inputGainParam != nullptr ? inputGainParam->load(std::memory_order_relaxed) : 0.0f);
    const auto inputGainLinear = juce::Decibels::decibelsToGain(inputGainDb);

    if (std::abs(inputGainDb) > 1.0e-6f)
    {
        buffer.applyGain(inputGainLinear);

        if (deltaEnabled)
            deltaDryBuffer.applyGain(inputGainLinear);
    }

    if (deltaEnabled)
        compressorSettings.makeupDb = 0.0f;

    spectralCompressor.processBuffer(buffer, channelsToUse, compressorSettings);

    if (deltaEnabled)
    {
        for (auto channel = 0; channel < channelsToUse; ++channel)
        {
            buffer.applyGain(channel, 0, buffer.getNumSamples(), -1.0f);
            buffer.addFrom(channel, 0, deltaDryBuffer, channel, 0, buffer.getNumSamples());
        }
    }

    outputAnalyser.pushBuffer(buffer,
                              channelsToUse,
                              analysisSettings.fftSize,
                              analysisSettings.overlapFactor,
                              analysisSettings.averagingTimeMs);
}

bool SpeAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* SpeAudioProcessor::createEditor()
{
    return new SpeAudioProcessorEditor(*this);
}

void SpeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto stateXml = parameters.copyState().createXml())
        copyXmlToBinary(*stateXml, destData);
}

void SpeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto stateXml = getXmlFromBinary(data, sizeInBytes))
        if (stateXml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*stateXml));
}

void SpeAudioProcessor::copyAnalyserData(std::array<float, analyserScopeSize>& destination,
                                         double& currentSampleRate) const
{
    outputAnalyser.copyScope(destination, currentSampleRate);
}

void SpeAudioProcessor::copyGainReductionData(std::array<float, analyserScopeSize>& destination) const
{
    spectralCompressor.copyReductionScope(destination);
}

SpeAudioProcessor::DisplaySettings SpeAudioProcessor::getDisplaySettings() const noexcept
{
    auto left = leftParam != nullptr ? leftParam->load(std::memory_order_relaxed) : 21.0f;
    auto right = rightParam != nullptr ? rightParam->load(std::memory_order_relaxed) : 20000.0f;
    auto low = rangeLowParam != nullptr ? rangeLowParam->load(std::memory_order_relaxed) : -60.0f;
    auto high = rangeHighParam != nullptr ? rangeHighParam->load(std::memory_order_relaxed) : 10.0f;

    left = juce::jlimit(0.0f, 1000.0f, left);
    right = juce::jlimit(1000.0f, analyserMaxFrequency, right);

    if (right <= left)
        right = juce::jmin(analyserMaxFrequency, left + 1.0f);

    if (high < low)
        std::swap(low, high);

    if ((high - low) < 6.0f)
    {
        const auto centre = 0.5f * (low + high);
        low = centre - 3.0f;
        high = centre + 3.0f;
    }

    return {
        left,
        right,
        juce::jlimit(analyserMinDecibels, analyserMaxDecibels - 6.0f, low),
        juce::jlimit(analyserMinDecibels + 6.0f, analyserMaxDecibels, high),
        juce::jlimit(analyserMinDecibels, analyserMaxDecibels, thresholdParam != nullptr ? thresholdParam->load(std::memory_order_relaxed) : 12.0f),
        juce::jlimit(0.0f, 6.0f, slopeParam != nullptr ? slopeParam->load(std::memory_order_relaxed) : 4.0f)
    };
}

SpeAudioProcessor::AnalysisSettings SpeAudioProcessor::getAnalysisSettings() const noexcept
{
    return { getSelectedAnalyserFftSize(), getSelectedOverlapFactor(), getSelectedAveragingTimeMs() };
}

SpeAudioProcessor::CompressorSettings SpeAudioProcessor::getCompressorSettings() const noexcept
{
    return {
        getSelectedDspFftSize(),
        getSelectedOverlapFactor(),
        juce::jlimit(-120.0f, 12.0f, thresholdParam != nullptr ? thresholdParam->load(std::memory_order_relaxed) : 12.0f),
        juce::jlimit(-120.0f, 12.0f, dualMonoLeftThresholdParam != nullptr ? dualMonoLeftThresholdParam->load(std::memory_order_relaxed) : 12.0f),
        juce::jlimit(-120.0f, 12.0f, dualMonoRightThresholdParam != nullptr ? dualMonoRightThresholdParam->load(std::memory_order_relaxed) : 12.0f),
        isBypassEnabled(),
        dualMonoBypassParam != nullptr && juce::roundToInt(dualMonoBypassParam->load(std::memory_order_relaxed)) != 0,
        juce::jlimit(0.0f, 6.0f, dspSlopeParam != nullptr ? dspSlopeParam->load(std::memory_order_relaxed) : 4.0f),
        juce::jlimit(0.0f, 200.0f, attackParam != nullptr ? attackParam->load(std::memory_order_relaxed) : 0.0f),
        juce::jlimit(0.0f, 2000.0f, releaseParam != nullptr ? releaseParam->load(std::memory_order_relaxed) : 0.0f),
        juce::jlimit(0.0f, 24.0f, kneeParam != nullptr ? kneeParam->load(std::memory_order_relaxed) : 0.0f),
        juce::jlimit(1.0f, 100.0f, ratioParam != nullptr ? ratioParam->load(std::memory_order_relaxed) : 100.0f),
        juce::jlimit(-24.0f, 24.0f, makeupParam != nullptr ? makeupParam->load(std::memory_order_relaxed) : 0.0f)
    };
}

bool SpeAudioProcessor::isDeltaEnabled() const noexcept
{
    return deltaParam != nullptr
        && juce::roundToInt(deltaParam->load(std::memory_order_relaxed)) != 0;
}

bool SpeAudioProcessor::isBypassEnabled() const noexcept
{
    return bypassParam != nullptr
        && juce::roundToInt(bypassParam->load(std::memory_order_relaxed)) != 0;
}

void SpeAudioProcessor::resetDeltaDelay() noexcept
{
    deltaDelayWriteIndex = 0;
    deltaDryBuffer.setSize(0, 0);

    for (auto& channelBuffer : deltaDelayBuffers)
        channelBuffer.fill(0.0f);
}

void SpeAudioProcessor::populateAlignedDryBuffer(const juce::AudioBuffer<float>& inputBuffer,
                                                 juce::AudioBuffer<float>& delayedDryBuffer,
                                                 int channelsToUse,
                                                 int latencySamples) noexcept
{
    delayedDryBuffer.clear();

    if (channelsToUse <= 0)
        return;

    const auto delaySamples = juce::jlimit(0, deltaDelayBufferSize - 1, juce::jmax(0, latencySamples - 1));

    if (delaySamples == 0)
    {
        for (auto channel = 0; channel < channelsToUse; ++channel)
            delayedDryBuffer.copyFrom(channel, 0, inputBuffer, channel, 0, inputBuffer.getNumSamples());

        return;
    }

    for (auto sampleIndex = 0; sampleIndex < inputBuffer.getNumSamples(); ++sampleIndex)
    {
        auto readIndex = deltaDelayWriteIndex - delaySamples;

        if (readIndex < 0)
            readIndex += deltaDelayBufferSize;

        for (auto channel = 0; channel < channelsToUse; ++channel)
        {
            auto& delayBuffer = deltaDelayBuffers[static_cast<size_t>(channel)];
            delayedDryBuffer.setSample(channel, sampleIndex, delayBuffer[static_cast<size_t>(readIndex)]);
            delayBuffer[static_cast<size_t>(deltaDelayWriteIndex)] = inputBuffer.getSample(channel, sampleIndex);
        }

        deltaDelayWriteIndex = (deltaDelayWriteIndex + 1) % deltaDelayBufferSize;
    }
}

juce::AudioProcessorValueTreeState& SpeAudioProcessor::getValueTreeState() noexcept
{
    return parameters;
}

const juce::AudioProcessorValueTreeState& SpeAudioProcessor::getValueTreeState() const noexcept
{
    return parameters;
}

juce::AudioProcessorValueTreeState::ParameterLayout SpeAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameterLayout;

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramDualMonoLeftThresholdId, 1 },
        "DUAL-MONO - LL-THRESHOLD",
        juce::NormalisableRange<float> { -120.0f, 12.0f, 0.1f },
        12.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatDecibelValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramDualMonoRightThresholdId, 1 },
        "DUAL-MONO - RR-THRESHOLD",
        juce::NormalisableRange<float> { -120.0f, 12.0f, 0.1f },
        12.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatDecibelValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { paramDualMonoBypassId, 1 },
        "DUAL-MONO - BYPASS",
        true));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramThresholdId, 1 },
        "STEREO - THRESHOLD",
        juce::NormalisableRange<float> { -120.0f, 12.0f, 0.1f },
        12.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatDecibelValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { paramBypassId, 1 },
        "STEREO - BYPASS",
        true));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramInputGainId, 1 },
        "GLOBAL - IN-GAIN",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f },
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatDecibelValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramAttackId, 1 },
        "GLOBAL - ATTACK",
        juce::NormalisableRange<float> { 0.0f, 200.0f, 1.0f },
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatTimeValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramReleaseId, 1 },
        "GLOBAL - RELEASE",
        juce::NormalisableRange<float> { 0.0f, 2000.0f, 1.0f },
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatTimeValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramKneeId, 1 },
        "GLOBAL - KNEE",
        juce::NormalisableRange<float> { 0.0f, 24.0f, 0.1f },
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatDecibelValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramRatioId, 1 },
        "GLOBAL - RATIO",
        juce::NormalisableRange<float> { 1.0f, 100.0f, 0.1f },
        100.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatRatioValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { paramDspFftSizeId, 1 },
        "GLOBAL - WINDOW-SIZE",
        juce::StringArray { "1024", "2048", "4096", "8192", "16384" },
        3));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramDspSlopeId, 1 },
        "GLOBAL - SLOPE",
        juce::NormalisableRange<float> { 0.0f, 6.0f, 0.01f },
        4.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatSlopeValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramMakeupId, 1 },
        "GLOBAL - OUT-GAIN",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f },
        0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatDecibelValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { paramDeltaId, 1 },
        "GLOBAL - DELTA",
        false));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { paramFftSizeId, 1 },
        "ANALYSER - FFT-SIZE",
        juce::StringArray { "1024", "2048", "4096", "8192", "16384" },
        3));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { paramOverlapId, 1 },
        "ANALYSER - OVERLAP",
        juce::StringArray { "2x", "4x", "8x", "16x", "32x" },
        4));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramLeftId, 1 },
        "ANALYSER - LEFT",
        juce::NormalisableRange<float> { 0.0f, 1000.0f, 1.0f },
        21.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatFrequencyValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramRightId, 1 },
        "ANALYSER - RIGHT",
        juce::NormalisableRange<float> { 1000.0f, analyserMaxFrequency, 1.0f },
        20000.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatFrequencyValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramRangeLowId, 1 },
        "ANALYSER - LOW",
        juce::NormalisableRange<float> { -120.0f, -24.0f, 0.1f },
        -60.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatDecibelValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramRangeHighId, 1 },
        "ANALYSER - HIGH",
        juce::NormalisableRange<float> { -48.0f, 20.0f, 0.1f },
        10.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatDecibelValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramSlopeId, 1 },
        "ANALYSER - SLOPE",
        juce::NormalisableRange<float> { 0.0f, 6.0f, 0.01f },
        4.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatSlopeValue(value);
            })));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { paramTimeId, 1 },
        "ANALYSER - TIME",
        juce::NormalisableRange<float> { 0.0f, 1000.0f, 1.0f },
        50.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [] (float value, int)
            {
                return formatTimeValue(value);
            })));

    return { parameterLayout.begin(), parameterLayout.end() };
}

int SpeAudioProcessor::getSelectedAnalyserFftSize() const noexcept
{
    static constexpr std::array<int, 5> fftSizes { 1024, 2048, 4096, 8192, 16384 };
    const auto choiceIndex = fftSizeParam != nullptr
                           ? juce::jlimit(0, static_cast<int>(fftSizes.size()) - 1,
                                          juce::roundToInt(fftSizeParam->load(std::memory_order_relaxed)))
                           : 3;
    return fftSizes[static_cast<size_t>(choiceIndex)];
}

int SpeAudioProcessor::getSelectedDspFftSize() const noexcept
{
    static constexpr std::array<int, 5> fftSizes { 1024, 2048, 4096, 8192, 16384 };
    const auto choiceIndex = dspFftSizeParam != nullptr
                           ? juce::jlimit(0, static_cast<int>(fftSizes.size()) - 1,
                                          juce::roundToInt(dspFftSizeParam->load(std::memory_order_relaxed)))
                           : 3;
    return fftSizes[static_cast<size_t>(choiceIndex)];
}

int SpeAudioProcessor::getSelectedOverlapFactor() const noexcept
{
    static constexpr std::array<int, 5> overlapFactors { 2, 4, 8, 16, 32 };
    const auto choiceIndex = overlapParam != nullptr
                           ? juce::jlimit(0, static_cast<int>(overlapFactors.size()) - 1,
                                          juce::roundToInt(overlapParam->load(std::memory_order_relaxed)))
                           : 4;
    return overlapFactors[static_cast<size_t>(choiceIndex)];
}

float SpeAudioProcessor::getSelectedAveragingTimeMs() const noexcept
{
    return juce::jlimit(0.0f,
                        1000.0f,
                        timeParam != nullptr ? timeParam->load(std::memory_order_relaxed) : 50.0f);
}

SpeAudioProcessor::SpectralCompressor::SpectralCompressor()
{
    const std::array<int, 5> fftOrders { 10, 11, 12, 13, 14 };

    for (auto i = 0; i < static_cast<int>(fftOrders.size()); ++i)
    {
        const auto fftSize = 1 << fftOrders[static_cast<size_t>(i)];
        ffts[static_cast<size_t>(i)] = std::make_unique<juce::dsp::FFT>(fftOrders[static_cast<size_t>(i)]);

        for (auto sampleIndex = 0; sampleIndex < fftSize; ++sampleIndex)
        {
            windowTables[static_cast<size_t>(i)][static_cast<size_t>(sampleIndex)]
                = 0.5f * (1.0f - std::cos((2.0f * juce::MathConstants<float>::pi * static_cast<float>(sampleIndex))
                                          / static_cast<float>(fftSize - 1)));
        }
    }
}

void SpeAudioProcessor::SpectralCompressor::prepare(double newSampleRate, int numChannels)
{
    sampleRate = juce::jmax(1.0, newSampleRate);
    configuredChannels = juce::jlimit(0, maxChannels, numChannels);
    reconfigure(configuredChannels, 0, 0);
}

void SpeAudioProcessor::SpectralCompressor::copyReductionScope(std::array<float, analyserScopeSize>& destination) const
{
    const auto activeIndex = activeReductionScopeBuffer.load(std::memory_order_acquire);
    destination = reductionScopeBuffers[static_cast<size_t>(activeIndex)];
}

void SpeAudioProcessor::SpectralCompressor::reset() noexcept
{
    reconfigure(configuredChannels, currentFftSize, currentHopSize);
}

void SpeAudioProcessor::SpectralCompressor::processBuffer(juce::AudioBuffer<float>& buffer,
                                                          int numInputChannels,
                                                          const CompressorSettings& settings)
{
    const auto channelsToUse = juce::jlimit(0, maxChannels, juce::jmin(numInputChannels, buffer.getNumChannels()));

    if (channelsToUse <= 0)
        return;

    const auto fftSize = juce::jlimit(1024, maxFftSize, settings.fftSize);
    const auto overlapFactor = juce::jmax(1, settings.overlapFactor);
    const auto hopSize = juce::jmax(1, fftSize / overlapFactor);

    if (channelsToUse != configuredChannels || fftSize != currentFftSize || hopSize != currentHopSize)
        reconfigure(channelsToUse, fftSize, hopSize);

    const auto fftIndex = getFftIndexForSize(fftSize);

    for (auto sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
    {
        for (auto channel = 0; channel < channelsToUse; ++channel)
            hopBuffers[static_cast<size_t>(channel)][static_cast<size_t>(hopFill)] = buffer.getSample(channel, sampleIndex);

        ++hopFill;

        if (hopFill >= hopSize)
        {
            for (auto channel = 0; channel < channelsToUse; ++channel)
            {
                auto& state = channelStates[static_cast<size_t>(channel)];
                std::move(state.analysisFifo.begin() + hopSize,
                          state.analysisFifo.begin() + fftSize,
                          state.analysisFifo.begin());
                std::copy_n(hopBuffers[static_cast<size_t>(channel)].begin(),
                            hopSize,
                            state.analysisFifo.begin() + (fftSize - hopSize));
                state.analysisFilled = juce::jmin(fftSize, state.analysisFilled + hopSize);
            }

            if (channelStates[0].analysisFilled >= fftSize)
                processFrame(channelsToUse, settings, fftIndex, fftSize, hopSize);

            for (auto channel = 0; channel < channelsToUse; ++channel)
                pushOutputChunk(channelStates[static_cast<size_t>(channel)], fftSize, hopSize);

            hopFill = 0;
        }

        for (auto channel = 0; channel < channelsToUse; ++channel)
            buffer.setSample(channel, sampleIndex, dequeueOutputSample(channelStates[static_cast<size_t>(channel)]));
    }
}

void SpeAudioProcessor::SpectralCompressor::enqueueOutputSample(ChannelState& state, float sample) noexcept
{
    if (state.readyOutputCount >= maxQueueSize)
        return;

    state.readyOutput[static_cast<size_t>(state.readyOutputWrite)] = sample;
    state.readyOutputWrite = (state.readyOutputWrite + 1) % maxQueueSize;
    ++state.readyOutputCount;
}

float SpeAudioProcessor::SpectralCompressor::dequeueOutputSample(ChannelState& state) noexcept
{
    if (state.readyOutputCount <= 0)
        return 0.0f;

    const auto sample = state.readyOutput[static_cast<size_t>(state.readyOutputRead)];
    state.readyOutputRead = (state.readyOutputRead + 1) % maxQueueSize;
    --state.readyOutputCount;
    return sample;
}

void SpeAudioProcessor::SpectralCompressor::processFrame(int channelsToUse,
                                                         const CompressorSettings& settings,
                                                         int fftIndex,
                                                         int fftSize,
                                                         int hopSize) noexcept
{
    const auto attackCoefficient = calculateTimeCoefficient(settings.attackMs,
                                                            static_cast<float>(hopSize) / static_cast<float>(sampleRate));
    const auto releaseCoefficient = calculateTimeCoefficient(settings.releaseMs,
                                                             static_cast<float>(hopSize) / static_cast<float>(sampleRate));
    const auto makeupGain = juce::Decibels::decibelsToGain(settings.makeupDb);
    const auto& window = windowTables[static_cast<size_t>(fftIndex)];
    auto& fft = *ffts[static_cast<size_t>(fftIndex)];

    for (auto channel = 0; channel < channelsToUse; ++channel)
    {
        auto& state = channelStates[static_cast<size_t>(channel)];

        for (auto sampleIndex = 0; sampleIndex < fftSize; ++sampleIndex)
        {
            const auto windowedSample = state.analysisFifo[static_cast<size_t>(sampleIndex)]
                                      * window[static_cast<size_t>(sampleIndex)];
            state.frequencyData[static_cast<size_t>(sampleIndex)] = { windowedSample, 0.0f };
        }

        fft.perform(state.frequencyData.data(), state.frequencyData.data(), false);
    }

    for (auto bin = 0; bin <= fftSize / 2; ++bin)
    {
        std::array<float, maxChannels> channelMagnitudes {};
        std::array<float, maxChannels> dualMonoReductionDb {};
        std::array<float, maxChannels> dualMonoGain {};

        for (auto channel = 0; channel < channelsToUse; ++channel)
            channelMagnitudes[static_cast<size_t>(channel)]
                = std::abs(channelStates[static_cast<size_t>(channel)].frequencyData[static_cast<size_t>(bin)])
                / static_cast<float>(fftSize);

        const auto binFrequency = juce::jmax(analyserMinFrequency,
                                             (static_cast<float>(bin) * static_cast<float>(sampleRate))
                                                 / static_cast<float>(fftSize));
        const auto octavesAboveMin = std::log2(binFrequency / analyserMinFrequency);
        const auto stereoThresholdDb = settings.thresholdDb
                                     - (settings.slopeDbPerOct * juce::jmax(0.0f, octavesAboveMin));

        for (auto channel = 0; channel < channelsToUse; ++channel)
        {
            auto& smoothedDualMonoReduction = dualMonoSmoothedReductionDb[static_cast<size_t>(channel)][static_cast<size_t>(bin)];

            if (settings.dualMonoBypass)
            {
                smoothedDualMonoReduction = 0.0f;
                dualMonoReductionDb[static_cast<size_t>(channel)] = 0.0f;
                dualMonoGain[static_cast<size_t>(channel)] = 1.0f;
                continue;
            }

            const auto channelThresholdDb = channel == 0 ? settings.leftThresholdDb : settings.rightThresholdDb;
            const auto channelLevelDb = juce::Decibels::gainToDecibels(channelMagnitudes[static_cast<size_t>(channel)], -120.0f);
            const auto desiredDualMonoReductionDb = calculateReductionDb(channelLevelDb,
                                                                         channelThresholdDb,
                                                                         settings.ratio,
                                                                         settings.kneeDb);
            const auto dualMonoCoefficient = desiredDualMonoReductionDb > smoothedDualMonoReduction ? attackCoefficient : releaseCoefficient;
            smoothedDualMonoReduction = (dualMonoCoefficient * smoothedDualMonoReduction)
                                      + ((1.0f - dualMonoCoefficient) * desiredDualMonoReductionDb);
            dualMonoReductionDb[static_cast<size_t>(channel)] = smoothedDualMonoReduction;
            dualMonoGain[static_cast<size_t>(channel)] = juce::Decibels::decibelsToGain(-smoothedDualMonoReduction);
        }

        auto stereoDetectorMagnitude = 0.0f;

        for (auto channel = 0; channel < channelsToUse; ++channel)
            stereoDetectorMagnitude = juce::jmax(stereoDetectorMagnitude,
                                                 channelMagnitudes[static_cast<size_t>(channel)] * dualMonoGain[static_cast<size_t>(channel)]);

        auto& smoothedStereoReduction = stereoSmoothedReductionDb[static_cast<size_t>(bin)];

        if (settings.stereoBypass)
        {
            smoothedStereoReduction = 0.0f;
        }
        else
        {
            const auto stereoLevelDb = juce::Decibels::gainToDecibels(stereoDetectorMagnitude, -120.0f);
            const auto desiredStereoReductionDb = calculateReductionDb(stereoLevelDb,
                                                                       stereoThresholdDb,
                                                                       settings.ratio,
                                                                       settings.kneeDb);
            const auto stereoCoefficient = desiredStereoReductionDb > smoothedStereoReduction ? attackCoefficient : releaseCoefficient;
            smoothedStereoReduction = (stereoCoefficient * smoothedStereoReduction)
                                    + ((1.0f - stereoCoefficient) * desiredStereoReductionDb);
        }

        for (auto channel = 0; channel < channelsToUse; ++channel)
        {
            const auto totalReductionDb = dualMonoReductionDb[static_cast<size_t>(channel)] + smoothedStereoReduction;
            combinedReductionDb[static_cast<size_t>(bin)] = channel == 0
                ? totalReductionDb
                : juce::jmax(combinedReductionDb[static_cast<size_t>(bin)], totalReductionDb);
            const auto gain = makeupGain
                            * dualMonoGain[static_cast<size_t>(channel)]
                            * juce::Decibels::decibelsToGain(-smoothedStereoReduction);
            auto& frequencyData = channelStates[static_cast<size_t>(channel)].frequencyData;
            frequencyData[static_cast<size_t>(bin)] *= gain;

            if (bin > 0 && bin < (fftSize / 2))
                frequencyData[static_cast<size_t>(fftSize - bin)] *= gain;
        }
    }

    for (auto channel = 0; channel < channelsToUse; ++channel)
    {
        auto& state = channelStates[static_cast<size_t>(channel)];
        fft.perform(state.frequencyData.data(), state.frequencyData.data(), true);

        for (auto sampleIndex = 0; sampleIndex < fftSize; ++sampleIndex)
        {
            const auto synthesisWeight = window[static_cast<size_t>(sampleIndex)];
            const auto weightedSample = state.frequencyData[static_cast<size_t>(sampleIndex)].real() * synthesisWeight;
            state.outputAccum[static_cast<size_t>(sampleIndex)] += weightedSample;
            state.normalizationAccum[static_cast<size_t>(sampleIndex)] += synthesisWeight * synthesisWeight;
        }
    }

    const auto currentSampleRate = juce::jmax(1.0, sampleRate);
    const auto sourceMaximumHz = juce::jlimit(analyserMinFrequency + 1.0f,
                                              analyserMaxFrequency,
                                              static_cast<float>(currentSampleRate * 0.5));
    const auto publishedIndex = activeReductionScopeBuffer.load(std::memory_order_relaxed);
    const auto writeIndex = 1 - publishedIndex;
    auto& reductionScope = reductionScopeBuffers[static_cast<size_t>(writeIndex)];

    for (auto i = 0; i < analyserScopeSize; ++i)
    {
        const auto proportion = static_cast<float>(i) / static_cast<float>(analyserScopeSize - 1);
        const auto frequency = juce::mapToLog10(proportion, analyserMinFrequency, sourceMaximumHz);
        const auto fractionalBin = juce::jlimit(0.0f,
                                                static_cast<float>(fftSize / 2),
                                                frequency * static_cast<float>(fftSize)
                                                    / static_cast<float>(currentSampleRate));
        const auto lowerBin = juce::jlimit(0, fftSize / 2, static_cast<int>(std::floor(fractionalBin)));
        const auto upperBin = juce::jlimit(0, fftSize / 2, lowerBin + 1);
        const auto interpolation = fractionalBin - static_cast<float>(lowerBin);
        reductionScope[static_cast<size_t>(i)] = juce::jmap(interpolation,
                                                            combinedReductionDb[static_cast<size_t>(lowerBin)],
                                                            combinedReductionDb[static_cast<size_t>(upperBin)]);
    }

    activeReductionScopeBuffer.store(writeIndex, std::memory_order_release);
}

void SpeAudioProcessor::SpectralCompressor::pushOutputChunk(ChannelState& state, int fftSize, int hopSize) noexcept
{
    for (auto sampleIndex = 0; sampleIndex < hopSize; ++sampleIndex)
    {
        const auto normalization = state.normalizationAccum[static_cast<size_t>(sampleIndex)];
        const auto outputSample = normalization > 1.0e-6f
                                ? state.outputAccum[static_cast<size_t>(sampleIndex)] / normalization
                                : 0.0f;
        enqueueOutputSample(state, outputSample);
    }

    std::move(state.outputAccum.begin() + hopSize,
              state.outputAccum.begin() + fftSize,
              state.outputAccum.begin());
    std::fill(state.outputAccum.begin() + (fftSize - hopSize),
              state.outputAccum.begin() + fftSize,
              0.0f);

    std::move(state.normalizationAccum.begin() + hopSize,
              state.normalizationAccum.begin() + fftSize,
              state.normalizationAccum.begin());
    std::fill(state.normalizationAccum.begin() + (fftSize - hopSize),
              state.normalizationAccum.begin() + fftSize,
              0.0f);
}

void SpeAudioProcessor::SpectralCompressor::reconfigure(int channelsToUse, int fftSize, int hopSize) noexcept
{
    configuredChannels = juce::jlimit(0, maxChannels, channelsToUse);
    currentFftSize = fftSize;
    currentHopSize = hopSize;
    hopFill = 0;
    std::fill(stereoSmoothedReductionDb.begin(), stereoSmoothedReductionDb.end(), 0.0f);
    std::fill(combinedReductionDb.begin(), combinedReductionDb.end(), 0.0f);
    for (auto& channelReduction : dualMonoSmoothedReductionDb)
        std::fill(channelReduction.begin(), channelReduction.end(), 0.0f);
    activeReductionScopeBuffer.store(0, std::memory_order_relaxed);

    for (auto& reductionScope : reductionScopeBuffers)
        std::fill(reductionScope.begin(), reductionScope.end(), 0.0f);

    for (auto channel = 0; channel < maxChannels; ++channel)
    {
        auto& state = channelStates[static_cast<size_t>(channel)];
        std::fill(state.analysisFifo.begin(), state.analysisFifo.end(), 0.0f);
        std::fill(state.outputAccum.begin(), state.outputAccum.end(), 0.0f);
        std::fill(state.normalizationAccum.begin(), state.normalizationAccum.end(), 0.0f);
        std::fill(state.readyOutput.begin(), state.readyOutput.end(), 0.0f);
        std::fill(hopBuffers[static_cast<size_t>(channel)].begin(),
                  hopBuffers[static_cast<size_t>(channel)].end(),
                  0.0f);

        for (auto& value : state.frequencyData)
            value = {};

        state.readyOutputRead = 0;
        state.readyOutputWrite = 0;
        state.readyOutputCount = 0;
        state.analysisFilled = 0;
    }
}

int SpeAudioProcessor::SpectralCompressor::getFftIndexForSize(int fftSize) const noexcept
{
    switch (fftSize)
    {
        case 1024: return 0;
        case 2048: return 1;
        case 4096: return 2;
        case 8192: return 3;
        case 16384: return 4;
        default: return 3;
    }
}

float SpeAudioProcessor::SpectralCompressor::calculateReductionDb(float levelDb,
                                                                  float thresholdDb,
                                                                  float ratio,
                                                                  float kneeDb) noexcept
{
    const auto safeRatio = juce::jmax(1.0f, ratio);
    const auto safeKnee = juce::jmax(0.0f, kneeDb);
    const auto ratioFactor = 1.0f - (1.0f / safeRatio);
    const auto deltaDb = levelDb - thresholdDb;

    if (safeKnee > 0.0f)
    {
        const auto halfKnee = safeKnee * 0.5f;

        if (deltaDb <= -halfKnee)
            return 0.0f;

        if (deltaDb >= halfKnee)
            return ratioFactor * juce::jmax(0.0f, deltaDb);

        const auto kneePosition = deltaDb + halfKnee;
        return ratioFactor * (kneePosition * kneePosition) / (2.0f * safeKnee);
    }

    return ratioFactor * juce::jmax(0.0f, deltaDb);
}

float SpeAudioProcessor::SpectralCompressor::calculateTimeCoefficient(float timeMs,
                                                                      float frameDurationSeconds) noexcept
{
    if (timeMs <= 0.0f)
        return 0.0f;

    const auto timeSeconds = timeMs * 0.001f;
    return std::exp(-frameDurationSeconds / timeSeconds);
}

SpeAudioProcessor::PostAnalyser::PostAnalyser()
{
    const std::array<int, 5> fftOrders { 10, 11, 12, 13, 14 };

    for (auto i = 0; i < static_cast<int>(fftOrders.size()); ++i)
    {
        ffts[static_cast<size_t>(i)] = std::make_unique<juce::dsp::FFT>(fftOrders[static_cast<size_t>(i)]);
        windows[static_cast<size_t>(i)] = std::make_unique<juce::dsp::WindowingFunction<float>>(
            1 << fftOrders[static_cast<size_t>(i)],
            juce::dsp::WindowingFunction<float>::hann);
    }
}

void SpeAudioProcessor::PostAnalyser::prepare(double newSampleRate)
{
    sampleRate.store(newSampleRate, std::memory_order_relaxed);
    historyWriteIndex = 0;
    availableSamples = 0;
    samplesSinceLastTransform = 0;
    activeScopeBuffer.store(0, std::memory_order_relaxed);
    std::fill(sampleHistory.begin(), sampleHistory.end(), 0.0f);
    std::fill(fftData.begin(), fftData.end(), 0.0f);
    std::fill(smoothedMagnitudes.begin(), smoothedMagnitudes.end(), 0.0f);

    for (auto& scopeBuffer : scopeBuffers)
        std::fill(scopeBuffer.begin(), scopeBuffer.end(), analyserMinDecibels);
}

void SpeAudioProcessor::PostAnalyser::pushBuffer(const juce::AudioBuffer<float>& buffer,
                                                 int numInputChannels,
                                                 int fftSize,
                                                 int overlapFactor,
                                                 float averagingTimeMs)
{
    const auto channelsToUse = juce::jmin(numInputChannels, buffer.getNumChannels());

    if (channelsToUse <= 0)
        return;

    const auto normalisation = 1.0f / static_cast<float>(channelsToUse);

    for (auto sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
    {
        auto mixedSample = 0.0f;

        for (auto channel = 0; channel < channelsToUse; ++channel)
            mixedSample += buffer.getSample(channel, sampleIndex);

        pushSample(mixedSample * normalisation, fftSize, overlapFactor, averagingTimeMs);
    }
}

void SpeAudioProcessor::PostAnalyser::copyScope(std::array<float, analyserScopeSize>& destination,
                                                double& currentSampleRate) const
{
    const auto activeIndex = activeScopeBuffer.load(std::memory_order_acquire);
    destination = scopeBuffers[static_cast<size_t>(activeIndex)];
    currentSampleRate = sampleRate.load(std::memory_order_relaxed);
}

void SpeAudioProcessor::PostAnalyser::pushSample(float sample,
                                                 int fftSize,
                                                 int overlapFactor,
                                                 float averagingTimeMs) noexcept
{
    sampleHistory[static_cast<size_t>(historyWriteIndex)] = sample;
    historyWriteIndex = (historyWriteIndex + 1) % maxFftSize;
    availableSamples = juce::jmin(availableSamples + 1, maxFftSize);
    ++samplesSinceLastTransform;

    const auto hopSize = juce::jmax(1, fftSize / juce::jmax(1, overlapFactor));

    if (availableSamples >= fftSize && samplesSinceLastTransform >= hopSize)
    {
        generateSpectrum(fftSize, overlapFactor, averagingTimeMs);
        samplesSinceLastTransform = 0;
    }
}

void SpeAudioProcessor::PostAnalyser::generateSpectrum(int fftSize,
                                                       int overlapFactor,
                                                       float averagingTimeMs) noexcept
{
    std::fill(fftData.begin(), fftData.end(), 0.0f);

    const auto fftIndex = getFftIndexForSize(fftSize);

    for (auto i = 0; i < fftSize; ++i)
    {
        const auto historyIndex = (historyWriteIndex - fftSize + i + maxFftSize) % maxFftSize;
        fftData[static_cast<size_t>(i)] = sampleHistory[static_cast<size_t>(historyIndex)];
    }

    windows[static_cast<size_t>(fftIndex)]->multiplyWithWindowingTable(fftData.data(), static_cast<size_t>(fftSize));
    ffts[static_cast<size_t>(fftIndex)]->performFrequencyOnlyForwardTransform(fftData.data());

    const auto currentSampleRate = juce::jmax(1.0, sampleRate.load(std::memory_order_relaxed));
    const auto nyquist = static_cast<float>(currentSampleRate * 0.5);
    const auto maxFrequency = juce::jlimit(analyserMinFrequency + 1.0f, analyserMaxFrequency, nyquist);
    const auto hopSize = juce::jmax(1, fftSize / juce::jmax(1, overlapFactor));
    const auto frameIntervalSeconds = static_cast<float>(hopSize) / static_cast<float>(currentSampleRate);
    const auto smoothingTimeSeconds = averagingTimeMs * 0.001f;
    const auto smoothingCoefficient = smoothingTimeSeconds > 0.0f
                                        ? std::exp(-frameIntervalSeconds / smoothingTimeSeconds)
                                        : 0.0f;

    const auto publishedIndex = activeScopeBuffer.load(std::memory_order_relaxed);
    const auto writeIndex = 1 - publishedIndex;
    auto& scopeBuffer = scopeBuffers[static_cast<size_t>(writeIndex)];

    for (auto i = 0; i < analyserScopeSize; ++i)
    {
        const auto proportion = static_cast<float>(i) / static_cast<float>(analyserScopeSize - 1);
        const auto frequency = juce::mapToLog10(proportion, analyserMinFrequency, maxFrequency);
        const auto fractionalBin = juce::jlimit(0.0f,
                                                static_cast<float>(fftSize / 2),
                                                frequency * static_cast<float>(fftSize)
                                                    / static_cast<float>(currentSampleRate));
        const auto lowerBin = juce::jlimit(0, fftSize / 2, static_cast<int>(std::floor(fractionalBin)));
        const auto upperBin = juce::jlimit(0, fftSize / 2, lowerBin + 1);
        const auto interpolation = fractionalBin - static_cast<float>(lowerBin);
        const auto lowerMagnitude = fftData[static_cast<size_t>(lowerBin)] / static_cast<float>(fftSize);
        const auto upperMagnitude = fftData[static_cast<size_t>(upperBin)] / static_cast<float>(fftSize);
        const auto rawMagnitude = juce::jmax(juce::jmap(interpolation, lowerMagnitude, upperMagnitude), 0.0f);
        auto& smoothedMagnitude = smoothedMagnitudes[static_cast<size_t>(i)];
        smoothedMagnitude = smoothingCoefficient > 0.0f
                              ? (smoothingCoefficient * smoothedMagnitude)
                                  + ((1.0f - smoothingCoefficient) * rawMagnitude)
                              : rawMagnitude;
        scopeBuffer[static_cast<size_t>(i)] = juce::Decibels::gainToDecibels(juce::jmax(smoothedMagnitude, 1.0e-8f),
                                                                             analyserMinDecibels);
    }

    activeScopeBuffer.store(writeIndex, std::memory_order_release);
}

int SpeAudioProcessor::PostAnalyser::getFftIndexForSize(int fftSize) const noexcept
{
    switch (fftSize)
    {
        case 1024: return 0;
        case 2048: return 1;
        case 4096: return 2;
        case 8192: return 3;
        case 16384: return 4;
        default: return 1;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpeAudioProcessor();
}
