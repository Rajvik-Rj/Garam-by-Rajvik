#include <JuceHeader.h>

class GaramProcessor : public juce::AudioProcessor {
public:
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        double freqs[5] = {80,250,800,3000,10000};
        for(int i=0;i<5;i++){
            auto prefix = "b" + std::to_string(i+1);
            layout.add(std::make_unique<juce::AudioParameterFloat>(prefix+"freq","Band"+std::to_string(i+1)+" Freq",juce::NormalisableRange<float>(20,20000,1,0.3),(float)freqs[i]));
            layout.add(std::make_unique<juce::AudioParameterFloat>(prefix+"gain","Band"+std::to_string(i+1)+" Gain",juce::NormalisableRange<float>(-18,18,0.1),0.0f));
            layout.add(std::make_unique<juce::AudioParameterFloat>(prefix+"q","Band"+std::to_string(i+1)+" Q",juce::NormalisableRange<float>(0.1,10,0.01),0.707f));
        }
        layout.add(std::make_unique<juce::AudioParameterFloat>("lsfreq","LowShelf Freq",juce::NormalisableRange<float>(20,1000,1),100.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("lsgain","LowShelf Gain",juce::NormalisableRange<float>(-18,18,0.1),0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("hsfreq","HiShelf Freq",juce::NormalisableRange<float>(2000,20000,1),8000.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("hsgain","HiShelf Gain",juce::NormalisableRange<float>(-18,18,0.1),0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("tapedrive","Tape Drive",juce::NormalisableRange<float>(0,1,0.01),0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("tubedrive","Tube Drive",juce::NormalisableRange<float>(0,1,0.01),0.0f));
        layout.add(std::make_unique<juce::AudioParameterFloat>("satblend","Sat Blend",juce::NormalisableRange<float>(0,1,0.01),0.0f));
        return layout;
    }

    GaramProcessor() : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo())
        .withOutput("Output", juce::AudioChannelSet::stereo())),
        apvts(*this, nullptr, "PARAMS", createLayout()) {}

    void prepareToPlay(double sr, int samplesPerBlock) override {
        juce::dsp::ProcessSpec spec{sr,(uint32_t)samplesPerBlock,2};
        chain.prepare(spec);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        updateChain();
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        chain.process(ctx);

        // Saturation
        float tape  = *apvts.getRawParameterValue("tapedrive");
        float tube  = *apvts.getRawParameterValue("tubedrive");
        float blend = *apvts.getRawParameterValue("satblend");
        for(int ch=0;ch<buffer.getNumChannels();ch++){
            auto* data = buffer.getWritePointer(ch);
            for(int s=0;s<buffer.getNumSamples();s++){
                float x = data[s];
                float t1 = tape>0 ? std::tanh(x*(1+tape*5))/std::tanh(1+tape*5) : x;
                float t2 = tube>0 ? (x>=0?(1-std::exp(-x*(1+tube*4)))/(1+tube*4):-(1-std::exp(x*(1+tube*4)))/(1+tube*4)*0.8f) : x;
                data[s] = t1*(1-blend)+t2*blend;
            }
        }
    }

    void updateChain() {
        double sr = getSampleRate();
        auto& eq = chain;
        double freqs[5]={80,250,800,3000,10000};
        for(int i=0;i<5;i++){
            auto prefix = "b"+std::to_string(i+1);
            float f = *apvts.getRawParameterValue(prefix+"freq");
            float g = *apvts.getRawParameterValue(prefix+"gain");
            float q = *apvts.getRawParameterValue(prefix+"q");
            *eq.get<0>().state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(sr,f,q,juce::Decibels::decibelsToGain(g));
        }
        *eq.get<1>().state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(sr,*apvts.getRawParameterValue("lsfreq"),0.707f,juce::Decibels::decibelsToGain(*apvts.getRawParameterValue("lsgain")));
        *eq.get<2>().state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(sr,*apvts.getRawParameterValue("hsfreq"),0.707f,juce::Decibels::decibelsToGain(*apvts.getRawParameterValue("hsgain")));
    }

    using Chain = juce::dsp::ProcessorChain
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Filter<float>
    >;
    Chain chain;

    const juce::String getName() const override { return "GARAM"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GaramProcessor(); }
