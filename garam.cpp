#include "clap/clap.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>

#define PLUGIN_ID "com.rajvik.garam"
#define PLUGIN_NAME "GARAM"
#define PLUGIN_VENDOR "Rajvik"
#define PLUGIN_VERSION "1.0.0"

// ─── Parameter IDs ───────────────────────────────────────────────
enum ParamId {
    // EQ Bands (each has freq, gain, Q)
    P_B1_FREQ = 0, P_B1_GAIN, P_B1_Q,
    P_B2_FREQ,     P_B2_GAIN, P_B2_Q,
    P_B3_FREQ,     P_B3_GAIN, P_B3_Q,
    P_B4_FREQ,     P_B4_GAIN, P_B4_Q,
    P_B5_FREQ,     P_B5_GAIN, P_B5_Q,

    // Shelf
    P_LS_FREQ, P_LS_GAIN,   // Low Shelf
    P_HS_FREQ, P_HS_GAIN,   // High Shelf

    // Saturation
    P_SAT_TAPE,   // Tape drive 0..1
    P_SAT_TUBE,   // Tube drive 0..1
    P_SAT_BLEND,  // Tape=0 .. Tube=1

    P_COUNT
};

// ─── Biquad Filter ───────────────────────────────────────────────
struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    double x1=0,x2=0,y1=0,y2=0;

    double process(double x) {
        double y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1; x1=x; y2=y1; y1=y;
        return y;
    }

    void reset() { x1=x2=y1=y2=0; }

    void setPeaking(double freq, double gainDB, double Q, double sr) {
        double A  = pow(10.0, gainDB/40.0);
        double w0 = 2.0*M_PI*freq/sr;
        double alpha = sin(w0)/(2.0*Q);
        double cosw0 = cos(w0);
        double a0 = 1 + alpha/A;
        b0 = (1 + alpha*A)/a0;
        b1 = (-2*cosw0)/a0;
        b2 = (1 - alpha*A)/a0;
        a1 = (-2*cosw0)/a0;
        a2 = (1 - alpha/A)/a0;
    }

    void setLowShelf(double freq, double gainDB, double sr) {
        double A  = pow(10.0, gainDB/40.0);
        double w0 = 2.0*M_PI*freq/sr;
        double cosw0 = cos(w0);
        double sinw0 = sin(w0);
        double alpha = sinw0/2.0 * sqrt((A+1/A)*(1/0.707 - 1)+2);
        double a0 = (A+1) + (A-1)*cosw0 + 2*sqrt(A)*alpha;
        b0 = A*((A+1)-(A-1)*cosw0+2*sqrt(A)*alpha)/a0;
        b1 = 2*A*((A-1)-(A+1)*cosw0)/a0;
        b2 = A*((A+1)-(A-1)*cosw0-2*sqrt(A)*alpha)/a0;
        a1 = -2*((A-1)+(A+1)*cosw0)/a0;
        a2 = ((A+1)+(A-1)*cosw0-2*sqrt(A)*alpha)/a0;
    }

    void setHighShelf(double freq, double gainDB, double sr) {
        double A  = pow(10.0, gainDB/40.0);
        double w0 = 2.0*M_PI*freq/sr;
        double cosw0 = cos(w0);
        double sinw0 = sin(w0);
        double alpha = sinw0/2.0 * sqrt((A+1/A)*(1/0.707 - 1)+2);
        double a0 = (A+1)-(A-1)*cosw0+2*sqrt(A)*alpha;
        b0 = A*((A+1)+(A-1)*cosw0+2*sqrt(A)*alpha)/a0;
        b1 = -2*A*((A-1)+(A+1)*cosw0)/a0;
        b2 = A*((A+1)+(A-1)*cosw0-2*sqrt(A)*alpha)/a0;
        a1 = 2*((A-1)-(A+1)*cosw0)/a0;
        a2 = ((A+1)-(A-1)*cosw0-2*sqrt(A)*alpha)/a0;
    }
};

// ─── Plugin State ────────────────────────────────────────────────
struct GaramPlugin {
    double sampleRate = 44100.0;
    double params[P_COUNT];

    // Per-channel filters (stereo = 2)
    Biquad band[5][2];
    Biquad lowShelf[2];
    Biquad highShelf[2];

    void initParams() {
        // Band defaults: freq, gain=0, Q=0.707
        double freqs[5] = {80, 250, 800, 3000, 10000};
        for(int i=0;i<5;i++){
            params[P_B1_FREQ + i*3] = freqs[i];
            params[P_B1_GAIN + i*3] = 0.0;
            params[P_B1_Q   + i*3] = 0.707;
        }
        params[P_LS_FREQ] = 100.0; params[P_LS_GAIN] = 0.0;
        params[P_HS_FREQ] = 8000.0; params[P_HS_GAIN] = 0.0;
        params[P_SAT_TAPE]  = 0.0;
        params[P_SAT_TUBE]  = 0.0;
        params[P_SAT_BLEND] = 0.0;
    }

    void updateFilters() {
        for(int i=0;i<5;i++) {
            double f = params[P_B1_FREQ + i*3];
            double g = params[P_B1_GAIN + i*3];
            double q = params[P_B1_Q   + i*3];
            for(int ch=0;ch<2;ch++)
                band[i][ch].setPeaking(f, g, q, sampleRate);
        }
        for(int ch=0;ch<2;ch++) {
            lowShelf[ch].setLowShelf(params[P_LS_FREQ], params[P_LS_GAIN], sampleRate);
            highShelf[ch].setHighShelf(params[P_HS_FREQ], params[P_HS_GAIN], sampleRate);
        }
    }

    // Tape saturation: soft-knee tanh style
    double tapeSaturate(double x, double drive) {
        if(drive < 1e-6) return x;
        double d = 1.0 + drive * 5.0;
        return tanh(x * d) / tanh(d);
    }

    // Tube saturation: asymmetric even-harmonic
    double tubeSaturate(double x, double drive) {
        if(drive < 1e-6) return x;
        double d = 1.0 + drive * 4.0;
        double y = x * d;
        if(y >= 0) return (1.0 - exp(-y)) / d;
        else       return -(1.0 - exp( y)) / d * 0.8; // asymmetry
    }

    void processBlock(float** inputs, float** outputs, uint32_t frames) {
        updateFilters();

        double tapeDrive  = params[P_SAT_TAPE];
        double tubeDrive  = params[P_SAT_TUBE];
        double blend      = params[P_SAT_BLEND]; // 0=tape, 1=tube

        for(int ch=0; ch<2; ch++) {
            float* in  = inputs[ch];
            float* out = outputs[ch];

            for(uint32_t s=0; s<frames; s++) {
                double x = (double)in[s];

                // ── EQ ──
                x = lowShelf[ch].process(x);
                for(int i=0;i<5;i++) x = band[i][ch].process(x);
                x = highShelf[ch].process(x);

                // ── Saturation ──
                double tape = tapeSaturate(x, tapeDrive);
                double tube = tubeSaturate(x, tubeDrive);
                x = tape*(1.0-blend) + tube*blend;

                out[s] = (float)x;
            }
        }
    }
};

// ─── CLAP Callbacks ──────────────────────────────────────────────
static const clap_plugin_descriptor_t s_desc = {
    .clap_version = CLAP_VERSION_INIT,
    .id      = PLUGIN_ID,
    .name    = PLUGIN_NAME,
    .vendor  = PLUGIN_VENDOR,
    .version = PLUGIN_VERSION,
    .description = "Warm EQ + Saturation by Rajvik",
    .features = (const char*[]){
        CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
        CLAP_PLUGIN_FEATURE_EQUALIZER,
        nullptr
    }
};

static bool plugin_init(const clap_plugin_t* p) {
    auto* g = (GaramPlugin*)p->plugin_data;
    g->initParams();
    return true;
}
static void plugin_destroy(const clap_plugin_t* p) {
    delete (GaramPlugin*)p->plugin_data;
}
static bool plugin_activate(const clap_plugin_t* p, double sr, uint32_t, uint32_t) {
    auto* g = (GaramPlugin*)p->plugin_data;
    g->sampleRate = sr;
    g->updateFilters();
    return true;
}
static void plugin_deactivate(const clap_plugin_t*) {}
static bool plugin_start_processing(const clap_plugin_t*) { return true; }
static void plugin_stop_processing(const clap_plugin_t*) {}
static void plugin_reset(const clap_plugin_t* p) {
    auto* g = (GaramPlugin*)p->plugin_data;
    for(int i=0;i<5;i++) for(int ch=0;ch<2;ch++) g->band[i][ch].reset();
    for(int ch=0;ch<2;ch++) { g->lowShelf[ch].reset(); g->highShelf[ch].reset(); }
}

static clap_process_status plugin_process(const clap_plugin_t* p, const clap_process_t* proc) {
    auto* g = (GaramPlugin*)p->plugin_data;
    float* inputs[2]  = { proc->audio_inputs[0].data32[0],  proc->audio_inputs[0].data32[1]  };
    float* outputs[2] = { proc->audio_outputs[0].data32[0], proc->audio_outputs[0].data32[1] };
    g->processBlock(inputs, outputs, proc->frames_count);

    // Pass MIDI through
    if(proc->out_events) {
        uint32_t n = proc->in_events->size(proc->in_events);
        for(uint32_t i=0;i<n;i++)
            proc->out_events->try_push(proc->out_events, proc->in_events->get(proc->in_events, i));
    }
    return CLAP_PROCESS_CONTINUE;
}

static const void* plugin_get_extension(const clap_plugin_t* p, const char* id);

// ─── Params Extension ────────────────────────────────────────────
struct ParamInfo {
    const char* name; double minV, maxV, defV; const char* module;
};

static ParamInfo paramInfoTable[P_COUNT] = {
    {"Band1 Freq",  20,20000,  80,  "EQ"},{"Band1 Gain",-18,18,0,"EQ"},{"Band1 Q",0.1,10,0.707,"EQ"},
    {"Band2 Freq",  20,20000,  250, "EQ"},{"Band2 Gain",-18,18,0,"EQ"},{"Band2 Q",0.1,10,0.707,"EQ"},
    {"Band3 Freq",  20,20000,  800, "EQ"},{"Band3 Gain",-18,18,0,"EQ"},{"Band3 Q",0.1,10,0.707,"EQ"},
    {"Band4 Freq",  20,20000,  3000,"EQ"},{"Band4 Gain",-18,18,0,"EQ"},{"Band4 Q",0.1,10,0.707,"EQ"},
    {"Band5 Freq",  20,20000, 10000,"EQ"},{"Band5 Gain",-18,18,0,"EQ"},{"Band5 Q",0.1,10,0.707,"EQ"},
    {"LowShelf Freq",20,1000, 100,  "EQ"},{"LowShelf Gain",-18,18,0,"EQ"},
    {"HiShelf Freq",2000,20000,8000,"EQ"},{"HiShelf Gain",-18,18,0,"EQ"},
    {"Tape Drive",  0,1,0, "Saturation"},
    {"Tube Drive",  0,1,0, "Saturation"},
    {"Sat Blend",   0,1,0, "Saturation"},
};

static uint32_t params_count(const clap_plugin_t*) { return P_COUNT; }

static bool params_get_info(const clap_plugin_t*, uint32_t idx, clap_param_info_t* info) {
    if(idx >= P_COUNT) return false;
    auto& pi = paramInfoTable[idx];
    info->id    = idx;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
    strncpy(info->name,   pi.name,   CLAP_NAME_SIZE-1);
    strncpy(info->module, pi.module, CLAP_PATH_SIZE-1);
    info->min_value     = pi.minV;
    info->max_value     = pi.maxV;
    info->default_value = pi.defV;
    return true;
}

static bool params_get_value(const clap_plugin_t* p, clap_id id, double* val) {
    auto* g = (GaramPlugin*)p->plugin_data;
    if(id >= P_COUNT) return false;
    *val = g->params[id];
    return true;
}

static bool params_value_to_text(const clap_plugin_t*, clap_id id, double val, char* buf, uint32_t sz) {
    if(id==P_B1_FREQ||id==P_B2_FREQ||id==P_B3_FREQ||id==P_B4_FREQ||id==P_B5_FREQ||id==P_LS_FREQ||id==P_HS_FREQ)
        snprintf(buf,sz,"%.1f Hz",val);
    else if(id==P_B1_GAIN||id==P_B2_GAIN||id==P_B3_GAIN||id==P_B4_GAIN||id==P_B5_GAIN||id==P_LS_GAIN||id==P_HS_GAIN)
        snprintf(buf,sz,"%.2f dB",val);
    else if(id==P_SAT_BLEND)
        snprintf(buf,sz,val<0.5?"Tape %.0f%%":"Tube %.0f%%", val<0.5?(1-val*2)*100:((val*2-1)*100));
    else snprintf(buf,sz,"%.3f",val);
    return true;
}

static bool params_text_to_value(const clap_plugin_t*, clap_id, const char* txt, double* val) {
    *val = atof(txt); return true;
}

static void params_flush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) {
    auto* g = (GaramPlugin*)p->plugin_data;
    uint32_t n = in->size(in);
    for(uint32_t i=0;i<n;i++){
        auto* ev = in->get(in,i);
        if(ev->type == CLAP_EVENT_PARAM_VALUE) {
            auto* pev = (const clap_event_param_value_t*)ev;
            if(pev->param_id < P_COUNT)
                g->params[pev->param_id] = pev->value;
        }
    }
}

static const clap_plugin_params_t s_params = {
    params_count, params_get_info, params_get_value,
    params_value_to_text, params_text_to_value, params_flush
};

// ─── Audio Ports ─────────────────────────────────────────────────
static uint32_t audio_ports_count(const clap_plugin_t*, bool) { return 1; }
static bool audio_ports_get(const clap_plugin_t*, uint32_t idx, bool, clap_audio_port_info_t* info) {
    if(idx) return false;
    info->id=0; info->channel_count=2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    strncpy(info->name,"Main",CLAP_NAME_SIZE-1);
    return true;
}
static const clap_plugin_audio_ports_t s_audio_ports = { audio_ports_count, audio_ports_get };

// ─── get_extension ───────────────────────────────────────────────
static const void* plugin_get_extension(const clap_plugin_t*, const char* id) {
    if(!strcmp(id, CLAP_EXT_PARAMS))       return &s_params;
    if(!strcmp(id, CLAP_EXT_AUDIO_PORTS))  return &s_audio_ports;
    return nullptr;
}
static void plugin_on_main_thread(const clap_plugin_t*) {}

// ─── Factory ─────────────────────────────────────────────────────
static uint32_t factory_get_count(const clap_plugin_factory_t*) { return 1; }
static const clap_plugin_descriptor_t* factory_get_descriptor(const clap_plugin_factory_t*, uint32_t) { return &s_desc; }

static const clap_plugin_t* factory_create(const clap_plugin_factory_t*, const clap_host_t*, const char* id) {
    if(strcmp(id, PLUGIN_ID)) return nullptr;
    auto* g = new GaramPlugin();
    auto* p = new clap_plugin_t{
        .desc            = &s_desc,
        .plugin_data     = g,
        .init            = plugin_init,
        .destroy         = plugin_destroy,
        .activate        = plugin_activate,
        .deactivate      = plugin_deactivate,
        .start_processing= plugin_start_processing,
        .stop_processing = plugin_stop_processing,
        .reset           = plugin_reset,
        .process         = plugin_process,
        .get_extension   = plugin_get_extension,
        .on_main_thread  = plugin_on_main_thread,
    };
    return p;
}

static const clap_plugin_factory_t s_factory = {
    factory_get_count, factory_get_descriptor, factory_create
};

// ─── Entry Point ─────────────────────────────────────────────────
static bool entry_init(const char*) { return true; }
static void entry_deinit() {}
static const void* entry_get_factory(const char* id) {
    if(!strcmp(id, CLAP_PLUGIN_FACTORY_ID)) return &s_factory;
    return nullptr;
}

extern "C" const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = entry_init,
    .deinit       = entry_deinit,
    .get_factory  = entry_get_factory,
};
