//
// C++ Interface: simplesynth
//
// Description:
//
//
// Author: Mathias Lundgren <lunar_shuttle@users.sf.net>, (C) 2004
//
// Copyright: See COPYING file that comes with this distribution
//
//
#ifndef SIMPLESYNTH_H
#define SIMPLESYNTH_H

#include <sndfile.h>
#include "libsynti/mess.h"
#include "common.h"
//#include "libsynti/mpevent.h"
#include "oom/mpevent.h"   
#include "simpledrumsgui.h"
#include "ssplugin.h"

#define SS_NO_SAMPLE       0
#define SS_NO_PLUGIN       0

#define SS_PROCESS_BUFFER_SIZE 4096 //TODO: Add initialization method for nr of frames in each process from OOMidi - if nr of frames > than this, this will fail
#define SS_SENDFX_BUFFER_SIZE  SS_PROCESS_BUFFER_SIZE

enum SS_ChannelState
   {
      SS_CHANNEL_INACTIVE=0,
      SS_SAMPLE_PLAYING,
   };

enum SS_State
   {
      SS_INITIALIZING=0,
      SS_LOADING_SAMPLE,
      SS_CLEARING_SAMPLE,
      SS_RUNNING,
   };

enum SS_SendFXState
   {
      SS_SENDFX_OFF=0,
      SS_SENDFX_ON
   };

struct SS_SendFx
   {
      SS_SendFXState state;
      LadspaPlugin*  plugin;
      int            inputs;
      int            outputs;
      int            retgain_ctrlval;
      double         retgain;
      int            nrofparameters;
   };

struct SS_Sample
   {
      float*      data;
      int         samplerate;
      int         bits;
      std::string filename;
      long        samples;
      long        frames;
      int         channels;
      SF_INFO     sfinfo;
   };

struct SS_Channel
   {
      SS_ChannelState state;
      const char*     name;
      SS_Sample*      sample;
      int             playoffset;
      bool            noteoff_ignore;

      double          volume;
      int             volume_ctrlval;

      double          cur_velo;
      double          gain_factor;

      int             pan;
      double          balanceFactorL;
      double          balanceFactorR;

      bool            channel_on;

      //Send fx:
      double          sendfxlevel[SS_NR_OF_SENDEFFECTS];
   };

struct SS_Controller
   {
      std::string name;
      int num;
      int min, max;
   };

struct SS_SampleLoader
   {
      SS_Channel*  channel;
      std::string  filename;
      int          ch_no;
   };

class SimpleSynth : public Mess
   {
   public:
      SimpleSynth(int);

      virtual ~SimpleSynth();

      virtual bool guiVisible() const;
      virtual bool hasGui() const;
      virtual bool playNote(int arg1, int arg2, int arg3);
      virtual bool processEvent(const MidiPlayEvent& arg1);
      virtual bool setController(int arg1, int arg2, int arg3);
      virtual bool sysex(int arg1, const unsigned char* arg2);
      virtual const char* getPatchName(int arg1, int arg2, int arg3) const;
      virtual const MidiPatch* getPatchInfo(int arg1, const MidiPatch* arg2) const;
      virtual int getControllerInfo(int arg1, const char** arg2, int* arg3, int* arg4, int* arg5);
      virtual void process(float** data, int offset, int len);
      virtual void showGui(bool arg1);
      virtual void getInitData(int*, const unsigned char**);
      bool init(const char* name);
      void guiSendSampleLoaded(bool success, int ch, const char* filename);
      void guiSendError(const char* errorstring);

      static const char* synth_state_descr[];
      static const char* channel_state_descr[];

private:
      SimpleSynthGui* gui;

      SS_Channel channels[SS_NR_OF_CHANNELS];
      SS_Controller controllers[SS_NR_OF_CONTROLLERS];
      bool setController(int channel, int id, int val, bool fromGui);
      bool loadSample(int ch_no, const char* filename);
      void parseInitData(const unsigned char* data);
      void updateVolume(int ch, int in_volume_ctrlval);
      void updateBalance(int ch, int pan);
      void guiNotifySampleCleared(int ch);
      void guiUpdateBalance(int ch, int bal);
      void guiUpdateVolume(int ch, int val);
      void guiUpdateNoff(int ch, bool b);
      void guiUpdateChoff(int ch, bool b);
      void guiUpdateMasterVol(int val);
      void guiUpdateFxParameter(int fxid, int param, float val);
      void guiUpdateSendFxLevel(int channel, int fxid, int level);
      bool initSendEffect(int sendeffectid, QString lib, QString name);
      void setSendFxLevel(int channel, int effectid, double val);
      void cleanupPlugin(int id);
      void setFxParameter(int fxid, int param, float val);
      void clearSample(int ch);
      double master_vol;
      int master_vol_ctrlval;

      //Send effects:
      SS_SendFx sendEffects[SS_NR_OF_SENDEFFECTS];
      float* sendFxLineOut[SS_NR_OF_SENDEFFECTS][2]; //stereo output (fed into LADSPA inputs),sent from the individual channels -> LADSPA fx
      float* sendFxReturn[SS_NR_OF_SENDEFFECTS][2];  //stereo inputs, from LADSPA plugins, sent from LADSPA -> SS and added to the mix
      double* processBuffer[2];
   };

static void* loadSampleThread(void*);
static pthread_mutex_t SS_LoaderMutex;
static SS_State synth_state;
static SimpleSynth* simplesynth_ptr;

#endif
