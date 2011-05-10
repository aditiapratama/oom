//=========================================================
//  OOMidi
//  OpenOctave Midi and Audio Editor
//  $Id: jackmidi.cpp,v 1.1.1.1 2010/01/27 09:06:43 terminator356 Exp $
//  (C) Copyright 1999-2010 Werner Schweer (ws@seh.de)
//=========================================================

#include <QString>

#include <stdio.h>

#include <jack/jack.h>
//#include <jack/midiport.h>

#include "jackmidi.h"
#include "song.h"
#include "globals.h"
#include "midi.h"
#include "mididev.h"
#include "../midiport.h"
#include "../midiseq.h"
#include "../midictrl.h"
#include "../audio.h"
#include "mpevent.h"
//#include "sync.h"
#include "audiodev.h"
#include "../mplugins/midiitransform.h"
#include "../mplugins/mitplugin.h"
#include "midimonitor.h"
#include "xml.h"

// Turn on debug messages.
//#define JACK_MIDI_DEBUG

extern unsigned int volatile lastExtMidiSyncTick;


//---------------------------------------------------------
//   MidiJackDevice
//   in_jack_port or out_jack_port can be null
//---------------------------------------------------------

MidiJackDevice::MidiJackDevice(const QString& n)
: MidiDevice(n)
{
	_in_client_jackport = NULL;
	_out_client_jackport = NULL;

	init();
}

MidiJackDevice::~MidiJackDevice()
{
#ifdef JACK_MIDI_DEBUG
	printf("MidiJackDevice::~MidiJackDevice()\n");
#endif  

	if (audioDevice)
	{
		if (_in_client_jackport)
			audioDevice->unregisterPort(_in_client_jackport);
		if (_out_client_jackport)
			audioDevice->unregisterPort(_out_client_jackport);
	}
}

//---------------------------------------------------------
//   createJackMidiDevice
//   If name parameter is blank, creates a new (locally) unique one.
//---------------------------------------------------------

MidiDevice* MidiJackDevice::createJackMidiDevice(QString name, int rwflags) // p3.3.55 1:Writable 2: Readable 3: Writable + Readable
{
	// p3.3.55
	int ni = 0;
	if (name.isEmpty())
	{
		for (; ni < 65536; ++ni)
		{
			name.sprintf("jack-midi-%d", ni);
			if (!midiDevices.find(name))
				break;
		}
	}
	if (ni >= 65536)
	{
		fprintf(stderr, "OOMidi: createJackMidiDevice failed! Can't find an unused midi device name 'jack-midi-[0-65535]'.\n");
		return 0;
	}

	MidiJackDevice* dev = new MidiJackDevice(name); // p3.3.55
	dev->setrwFlags(rwflags);
	midiDevices.add(dev);
	return dev;
}

//---------------------------------------------------------
//   setName
//---------------------------------------------------------

void MidiJackDevice::setName(const QString& s)
{
#ifdef JACK_MIDI_DEBUG
	printf("MidiJackDevice::setName %s new name:%s\n", name().toLatin1().constData(), s.toLatin1().constData());
#endif  
	_name = s;

	if (inClientPort())
		audioDevice->setPortName(inClientPort(), (s + QString(JACK_MIDI_IN_PORT_SUFFIX)).toLatin1().constData());
	if (outClientPort())
		audioDevice->setPortName(outClientPort(), (s + QString(JACK_MIDI_OUT_PORT_SUFFIX)).toLatin1().constData());
}

//---------------------------------------------------------
//   open
//---------------------------------------------------------

QString MidiJackDevice::open()
{
	_openFlags &= _rwFlags; // restrict to available bits

#ifdef JACK_MIDI_DEBUG
	printf("MidiJackDevice::open %s\n", name().toLatin1().constData());
#endif  

	QString s;
	// p3.3.55 Moved from createJackMidiDevice()
	if (_openFlags & 1)
	{
		if (!_out_client_jackport)
		{
			if(audioDevice)
			{
				if (audioDevice->deviceType() == AudioDevice::JACK_AUDIO)
				{
					s = name() + QString(JACK_MIDI_OUT_PORT_SUFFIX);
					_out_client_jackport = (jack_port_t*) audioDevice->registerOutPort(s.toLatin1().constData(), true);
					if (!_out_client_jackport)
					{
						fprintf(stderr, "OOMidi: MidiJackDevice::open failed creating output port name %s\n", s.toLatin1().constData());
						_openFlags &= ~1; // Remove the flag, but continue on...
					}
				}
			}
		}
	}
	else
	{
		if (_out_client_jackport)
		{
			// We want to unregister the port (which will also disconnect it), AND remove Routes, and then NULL-ify _out_client_jackport.
			// We could let our graph change callback (the gui thread one) remove the Routes (which it would anyway).
			// But that happens later (gui thread) and it needs a valid  _out_client_jackport,
			//  so use of a registration callback would be required to finally NULL-ify _out_client_jackport,
			//  and that would require some MidiDevice setter or re-scanner function.
			// So instead, manually remove the Routes (in the audio thread), then unregister the port, then immediately NULL-ify _out_client_jackport.
			// Our graph change callback (the gui thread one) will see a NULL  _out_client_jackport
			//  so it cannot possibly remove the Routes, but that won't matter - we are removing them manually.
			// This is the same technique that is used for audio elsewhere in the code, like Audio::msgSetChannels()
			//  (but not Song::connectJackRoutes() which keeps the Routes for when undoing deletion of a track).
			//
			// NOTE: TESTED: Possibly a bug in QJackCtl, with Jack-1 (not Jack-2 !):
			// After toggling the input/output green lights in the midi ports list (which gets us here), intermittently
			//  qjackctl refuses to draw connections. It allows them to be made (OOMidi responds) but blanks them out immediately
			//  and does not show 'disconnect', as if it is not properly aware of the connections.
			// But ALL else is OK - the connection is fine in OOMidi, verbose Jack messages show all went OK.
			// Yes, there's no doubt the connections are being made.
			// When I toggle the lights again (which kills, then recreates the ports here), the problem can disappear or come back again.
			// Also once observed a weird double connection from the port to two different Jack ports but one of
			//  the connections should not have been there and kept toggling along with the other (like a 'ghost' connection).
			audio->msgRemoveRoutes(Route(this, 0), Route()); // New function msgRemoveRoutes simply uses Routes, for their pointers.
			audioDevice->unregisterPort(_out_client_jackport);
		}
		_out_client_jackport = NULL;
	}

	if (_openFlags & 2)
	{
		if (!_in_client_jackport)
		{
			if (audioDevice->deviceType() == AudioDevice::JACK_AUDIO)
			{
				s = name() + QString(JACK_MIDI_IN_PORT_SUFFIX);
				_in_client_jackport = (jack_port_t*) audioDevice->registerInPort(s.toLatin1().constData(), true);
				if (!_in_client_jackport)
				{
					fprintf(stderr, "OOMidi: MidiJackDevice::open failed creating input port name %s\n", s.toLatin1().constData());
					_openFlags &= ~2; // Remove the flag, but continue on...
				}
			}
		}
	}
	else
	{
		if (_in_client_jackport)
		{
			audio->msgRemoveRoutes(Route(), Route(this, 0));
			audioDevice->unregisterPort(_in_client_jackport);
		}
		_in_client_jackport = NULL;
	}

	_writeEnable = bool(_openFlags & 1);
	_readEnable = bool(_openFlags & 2);

	return QString("OK");
}

//---------------------------------------------------------
//   close
//---------------------------------------------------------

void MidiJackDevice::close()
{
#ifdef JACK_MIDI_DEBUG
	printf("MidiJackDevice::close %s\n", name().toLatin1().constData());
#endif  

	_writeEnable = false;
	_readEnable = false;
}

//---------------------------------------------------------
//   writeRouting
//---------------------------------------------------------

void MidiJackDevice::writeRouting(int level, Xml& xml) const
{
	// p3.3.45
	// If this device is not actually in use by the song, do not write any routes.
	// This prevents bogus routes from being saved and propagated in the oom file.
	if (midiPort() == -1)
		return;

	QString s;
	if (rwFlags() & 2) // Readable
	{
		for (ciRoute r = _inRoutes.begin(); r != _inRoutes.end(); ++r)
		{
			if (!r->name().isEmpty())
			{
				xml.tag(level++, "Route");

				s = QT_TRANSLATE_NOOP("@default", "source");
				if (r->type != Route::TRACK_ROUTE)
					s += QString(QT_TRANSLATE_NOOP("@default", " type=\"%1\"")).arg(r->type);

				s += QString(QT_TRANSLATE_NOOP("@default", " name=\"%1\"/")).arg(Xml::xmlString(r->name()));
				xml.tag(level, s.toLatin1().constData());

				xml.tag(level, "dest devtype=\"%d\" name=\"%s\"/", MidiDevice::JACK_MIDI, Xml::xmlString(name()).toLatin1().constData());

				xml.etag(level--, "Route");
			}
		}
	}

	for (ciRoute r = _outRoutes.begin(); r != _outRoutes.end(); ++r)
	{
		if (!r->name().isEmpty())
		{
			s = QT_TRANSLATE_NOOP("@default", "Route");
			if (r->channel != -1)
				s += QString(QT_TRANSLATE_NOOP("@default", " channel=\"%1\"")).arg(r->channel);

			xml.tag(level++, s.toLatin1().constData());

			xml.tag(level, "source devtype=\"%d\" name=\"%s\"/", MidiDevice::JACK_MIDI, Xml::xmlString(name()).toLatin1().constData());

			s = QT_TRANSLATE_NOOP("@default", "dest");
			if (r->type == Route::MIDI_DEVICE_ROUTE)
				s += QString(QT_TRANSLATE_NOOP("@default", " devtype=\"%1\"")).arg(r->device->deviceType());
			else
				if (r->type != Route::TRACK_ROUTE)
				s += QString(QT_TRANSLATE_NOOP("@default", " type=\"%1\"")).arg(r->type);

			s += QString(QT_TRANSLATE_NOOP("@default", " name=\"%1\"/")).arg(Xml::xmlString(r->name()));
			xml.tag(level, s.toLatin1().constData());


			xml.etag(level--, "Route");
		}
	}
}

//---------------------------------------------------------
//   putEvent
//---------------------------------------------------------

/* FIX: if we fail to transmit the event,
 *      we return false (indicating OK). Otherwise
 *      it seems oom will retry forever
 */
bool MidiJackDevice::putMidiEvent(const MidiPlayEvent& /*event*/)
{
	return false;
}

//---------------------------------------------------------
//   recordEvent
//---------------------------------------------------------

void MidiJackDevice::recordEvent(MidiRecordEvent& event)
{
	if (audio->isPlaying())
		event.setLoopNum(audio->loopCount());

	if (midiInputTrace)
	{
		printf("Jack MidiInput: ");
		event.dump();
	}

	int typ = event.type();

	if (_port != -1)
	{
		int idin = midiPorts[_port].syncInfo().idIn();

		//---------------------------------------------------
		// filter some SYSEX events
		//---------------------------------------------------

		if (typ == ME_SYSEX)
		{
			const unsigned char* p = event.data();
			int n = event.len();
			if (n >= 4)
			{
				if ((p[0] == 0x7f) && ((p[1] == 0x7f) || (idin == 0x7f) || (p[1] == idin)))
				{
					if (p[2] == 0x06)
					{
						midiSeq->mmcInput(_port, p, n);
						return;
					}
					if (p[2] == 0x01)
					{
						midiSeq->mtcInputFull(_port, p, n);
						return;
					}
				}
				else if (p[0] == 0x7e)
				{
					midiSeq->nonRealtimeSystemSysex(_port, p, n);
					return;
				}
			}
		}
		else
		{
			// Trigger general activity indicator detector. Sysex has no channel, don't trigger.
			midiPorts[_port].syncInfo().trigActDetect(event.channel());
		}
			
		//TODO: Jack in here and call our midimonitor with the data, it can then decide what to do
		//printf("MidiJackDevice::recordEvent _port:%d, event.port():%d\n",_port, event.port());
		if(midiMonitor->isManagedInputPort(_port))
		{
			//MidiRecordEvent ev(event);
			event.setPort(_port);
			midiMonitor->msgSendMidiInputEvent(event);
			return; //If we manage this input port return
		}
	}

	//
	//  process midi event input filtering and
	//    transformation
	//

	processMidiInputTransformPlugins(event);

	if (filterEvent(event, midiRecordType, false))
		return;

	if (!applyMidiInputTransformation(event))
	{
		if (midiInputTrace)
			printf("   midi input transformation: event filtered\n");
		return;
	}

	//
	// transfer noteOn events to gui for step recording and keyboard
	// remote control
	//
	if (typ == ME_NOTEON)
	{
		int pv = ((event.dataA() & 0xff) << 8) + (event.dataB() & 0xff);
		song->putEvent(pv);
	}

	//if(_recordFifo.put(MidiPlayEvent(event)))
	//  printf("MidiJackDevice::recordEvent: fifo overflow\n");

	// p3.3.38
	// Do not bother recording if it is NOT actually being used by a port.
	// Because from this point on, process handles things, by selected port.
	if (_port == -1)
		return;

	// Split the events up into channel fifos. Special 'channel' number 17 for sysex events.
	unsigned int ch = (typ == ME_SYSEX) ? MIDI_CHANNELS : event.channel();
	if (_recordFifo[ch].put(MidiPlayEvent(event)))
		printf("MidiJackDevice::recordEvent: fifo channel %d overflow\n", ch);
}

//---------------------------------------------------------
//   midiReceived
//---------------------------------------------------------

void MidiJackDevice::eventReceived(jack_midi_event_t* ev)
{
	MidiRecordEvent event;
	event.setB(0);

	// NOTE: From OOMidi-2. Not done here in OOMidi-1 (yet).
	// move all events 2*segmentSize into the future to get
	// jitterfree playback
	//
	//  cycle   n-1         n          n+1
	//          -+----------+----------+----------+-
	//               ^          ^          ^
	//               catch      process    play
	//
	//      const SeqTime* st = audio->seqTime();

	unsigned pos = audio->pos().frame();

	event.setTime(extSyncFlag.value() ? lastExtMidiSyncTick : (pos + ev->time));

	event.setChannel(*(ev->buffer) & 0xf);
	int type = *(ev->buffer) & 0xf0;
	int a = *(ev->buffer + 1) & 0x7f;
	int b = *(ev->buffer + 2) & 0x7f;
	event.setType(type);
	switch (type)
	{
		case ME_NOTEON:
		case ME_NOTEOFF:
		case ME_CONTROLLER:
			event.setA(*(ev->buffer + 1));
			event.setB(*(ev->buffer + 2));
			break;
		case ME_PROGRAM:
		case ME_AFTERTOUCH:
			event.setA(*(ev->buffer + 1));
			break;

		case ME_PITCHBEND:
			event.setA(((b << 7) + a) - 8192);
			break;

		case ME_SYSEX:
		{
			int type = *(ev->buffer) & 0xff;
			switch (type)
			{
				case ME_SYSEX:

					// TODO: Deal with large sysex, which are broken up into chunks!
					// For now, do not accept if the last byte is not EOX, meaning it's a chunk with more chunks to follow.
					if (*(((unsigned char*) ev->buffer) + ev->size - 1) != ME_SYSEX_END)
					{
						printf("MidiJackDevice::eventReceived sysex chunks not supported!\n");
						return;
					}

					//event.setTime(0);      // mark as used
					event.setType(ME_SYSEX);
					event.setData((unsigned char*) (ev->buffer + 1), ev->size - 2);
					break;
				case ME_MTC_QUARTER:
					if (_port != -1)
						midiSeq->mtcInputQuarter(_port, *(ev->buffer + 1));
					return;
				case ME_SONGPOS:
					if (_port != -1)
						midiSeq->setSongPosition(_port, *(ev->buffer + 1) | (*(ev->buffer + 2) >> 2)); // LSB then MSB
					return;
				case ME_CLOCK:
				case ME_TICK:
				case ME_START:
				case ME_CONTINUE:
				case ME_STOP:
					if (_port != -1)
						midiSeq->realtimeSystemInput(_port, type);
					return;
				default:
					printf("MidiJackDevice::eventReceived unsupported system event 0x%02x\n", type);
					return;
			}
		}
			break;
		default:
			printf("MidiJackDevice::eventReceived unknown event 0x%02x\n", type);
			//printf("MidiJackDevice::eventReceived unknown event 0x%02x size:%d buf:0x%02x 0x%02x 0x%02x ...0x%02x\n", type, ev->size, *(ev->buffer), *(ev->buffer + 1), *(ev->buffer + 2), *(ev->buffer + (ev->size - 1)));
			return;
	}

	if (midiInputTrace)
	{
		printf("MidiInput<%s>: ", name().toLatin1().constData());
		event.dump();
	}

#ifdef JACK_MIDI_DEBUG
	printf("MidiJackDevice::eventReceived time:%d type:%d ch:%d A:%d B:%d\n", event.time(), event.type(), event.channel(), event.dataA(), event.dataB());
#endif  

	// Let recordEvent handle it from here, with timestamps, filtering, gui triggering etc.
	recordEvent(event);
}

//---------------------------------------------------------
//   collectMidiEvents
//---------------------------------------------------------

void MidiJackDevice::collectMidiEvents()
{
	if (!_readEnable)
		return;

	if (!_in_client_jackport) // p3.3.55
		return;

	void* port_buf = jack_port_get_buffer(_in_client_jackport, segmentSize); // p3.3.55

	jack_midi_event_t event;
	jack_nframes_t eventCount = jack_midi_get_event_count(port_buf);
	for (jack_nframes_t i = 0; i < eventCount; ++i)
	{
		jack_midi_event_get(&event, port_buf, i);

#ifdef JACK_MIDI_DEBUG
		printf("MidiJackDevice::collectMidiEvents number:%d time:%d\n", i, event.time);
#endif  

		eventReceived(&event);
	}
}

//---------------------------------------------------------
//   putEvent
//    return true if event cannot be delivered
//---------------------------------------------------------

bool MidiJackDevice::putEvent(const MidiPlayEvent& ev)
{
	if (!_writeEnable)
		//return true;
		return false;

#ifdef JACK_MIDI_DEBUG
	printf("MidiJackDevice::putEvent time:%d type:%d ch:%d A:%d B:%d\n", ev.time(), ev.type(), ev.channel(), ev.dataA(), ev.dataB());
#endif  

	bool rv = eventFifo.put(ev);
	if (rv)
		printf("MidiJackDevice::putEvent: port overflow\n");

	return rv;
}

//---------------------------------------------------------
//   queueEvent
//   return true if successful
//   This sends the actual midi events to jack for processing
//---------------------------------------------------------

bool MidiJackDevice::queueEvent(const MidiPlayEvent& e)
{
	// Perhaps we can find use for this value later, together with the Jack midi OOMidi port(s).
	// No big deal if not. Not used for now.
	//int port = e.port();

	//if(port >= JACK_MIDI_CHANNELS)
	//  return false;

	//if (midiOutputTrace) {
	//      printf("MidiOut<%s>: jackMidi: ", portName(port).toLatin1().constData());
	//      e.dump();
	//      }

	//if(debugMsg)
	//  printf("MidiJackDevice::queueEvent\n");

	if (!_out_client_jackport) // p3.3.55
		return false;
	void* pb = jack_port_get_buffer(_out_client_jackport, segmentSize); // p3.3.55

	int frameOffset = audio->getFrameOffset();
	unsigned pos = audio->pos().frame();
	int ft = e.time() - frameOffset - pos;

	if (ft < 0)
		ft = 0;
	if (ft >= (int) segmentSize)
	{
		if(debugMsg)
			printf("MidiJackDevice::queueEvent: Event time:%d out of range. offset:%d ft:%d (seg=%d)\n", e.time(), frameOffset, ft, segmentSize);
		if (ft > (int) segmentSize)
			ft = segmentSize - 1;
	}

#ifdef JACK_MIDI_DEBUG
	printf("MidiJackDevice::queueEvent time:%d type:%d ch:%d A:%d B:%d\n", e.time(), e.type(), e.channel(), e.dataA(), e.dataB());
#endif  

	switch (e.type())
	{
		case ME_NOTEON:
		case ME_NOTEOFF:
		case ME_POLYAFTER:
		case ME_CONTROLLER:
		case ME_PITCHBEND:
		{
#ifdef JACK_MIDI_DEBUG
			printf("MidiJackDevice::queueEvent note on/off polyafter controller or pitch\n");
#endif  

			unsigned char* p = jack_midi_event_reserve(pb, ft, 3);
			if (p == 0)
			{
				fprintf(stderr, "MidiJackDevice::queueEvent #1: buffer overflow, event lost\n");
				return false;
			}
			p[0] = e.type() | e.channel();
			p[1] = e.dataA();
			p[2] = e.dataB();
		}
			break;

		case ME_PROGRAM:
		case ME_AFTERTOUCH:
		{
#ifdef JACK_MIDI_DEBUG
			printf("MidiJackDevice::queueEvent program or aftertouch\n");
#endif  

			unsigned char* p = jack_midi_event_reserve(pb, ft, 2);
			if (p == 0)
			{
				fprintf(stderr, "MidiJackDevice::queueEvent #2: buffer overflow, event lost\n");
				return false;
			}
			p[0] = e.type() | e.channel();
			p[1] = e.dataA();
		}
			break;
		case ME_SYSEX:
		{
#ifdef JACK_MIDI_DEBUG
			printf("MidiJackDevice::queueEvent sysex\n");
#endif  

			const unsigned char* data = e.data();
			int len = e.len();
			unsigned char* p = jack_midi_event_reserve(pb, ft, len + 2);
			if (p == 0)
			{
				fprintf(stderr, "MidiJackDevice::queueEvent #3: buffer overflow, event lost\n");
				return false;
			}
			p[0] = 0xf0;
			p[len + 1] = 0xf7;
			memcpy(p + 1, data, len);
		}
			break;
		case ME_SONGPOS:
		case ME_CLOCK:
		case ME_START:
		case ME_CONTINUE:
		case ME_STOP:
			printf("MidiJackDevice::queueEvent: event type %x not supported\n", e.type());
			return false;
			break;
	}

	return true;
}

//---------------------------------------------------------
//    processEvent
//---------------------------------------------------------

void MidiJackDevice::processEvent(const MidiPlayEvent& event)
{
	int chn = event.channel();
	unsigned t = event.time();
	int a = event.dataA();
	int b = event.dataB();
	// Perhaps we can find use for this value later, together with the Jack midi OOMidi port(s).
	// No big deal if not. Not used for now.
	int port = event.port();

	// TODO: No sub-tick playback resolution yet, with external sync.
	// Just do this 'standard midi 64T timing thing' for now until we figure out more precise external timings.
	// Does require relatively short audio buffers, in order to catch the resolution, but buffer <= 256 should be OK...
	// Tested OK so far with 128.
	if (extSyncFlag.value())
		t = audio->getFrameOffset() + audio->pos().frame();

#ifdef JACK_MIDI_DEBUG
	printf("MidiJackDevice::processEvent time:%d type:%d ch:%d A:%d B:%d\n", event.time(), event.type(), event.channel(), event.dataA(), event.dataB());
#endif  

	if (event.type() == ME_PROGRAM)
	{
		// don't output program changes for GM drum channel
		int hb = (a >> 16) & 0xff;
		int lb = (a >> 8) & 0xff;
		int pr = a & 0x7f;

		// p3.3.44
		//printf("MidiJackDevice::processEvent ME_PROGRAM time:%d type:%d ch:%d A:%d B:%d hb:%d lb:%d pr:%d\n",
		//       event.time(), event.type(), event.channel(), event.dataA(), event.dataB(), hb, lb, pr);

		//TODO: NOTE this is where program changes are sent we can later debug this to diagnose the dropped events
		if (hb != 0xff)
			queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_HBANK, hb));
		if (lb != 0xff)
			queueEvent(MidiPlayEvent(t + 1, port, chn, ME_CONTROLLER, CTRL_LBANK, lb));
		sleep(1);
		queueEvent(MidiPlayEvent(t + 2, port, chn, ME_PROGRAM, pr, 0));
	}
	else
		if (event.type() == ME_PITCHBEND)
	{
		int v = a + 8192;
		// p3.3.44
		//printf("MidiJackDevice::processEvent ME_PITCHBEND v:%d time:%d type:%d ch:%d A:%d B:%d\n", v, event.time(), event.type(), event.channel(), event.dataA(), event.dataB());

		queueEvent(MidiPlayEvent(t, port, chn, ME_PITCHBEND, v & 0x7f, (v >> 7) & 0x7f));
	}
	else
		if (event.type() == ME_CONTROLLER)
	{
		//int a      = event.dataA();
		//int b      = event.dataB();
		// Perhaps we can find use for this value later, together with the Jack midi OOMidi port(s).
		// No big deal if not. Not used for now.
		//int port   = event.port();

		int nvh = 0xff;
		int nvl = 0xff;
		if (_port != -1)
		{
			int nv = midiPorts[_port].nullSendValue();
			if (nv != -1)
			{
				nvh = (nv >> 8) & 0xff;
				nvl = nv & 0xff;
			}
		}

		if (a == CTRL_PITCH)
		{
			int v = b + 8192;
			// p3.3.44
			//printf("MidiJackDevice::processEvent CTRL_PITCH v:%d time:%d type:%d ch:%d A:%d B:%d\n", v, event.time(), event.type(), event.channel(), event.dataA(), event.dataB());

			queueEvent(MidiPlayEvent(t, port, chn, ME_PITCHBEND, v & 0x7f, (v >> 7) & 0x7f));
		}
		else if (a == CTRL_PROGRAM)
		{
			int hb = (b >> 16) & 0xff;
			int lb = (b >> 8) & 0xff;
			int pr = b & 0x7f;

			// p3.3.44
			//printf("MidiJackDevice::processEvent CTRL_PROGRAM time:%d type:%d ch:%d A:%d B:%d hb:%d lb:%d pr:%d\n",
			//       event.time(), event.type(), event.channel(), event.dataA(), event.dataB(), hb, lb, pr);

			if (hb != 0xff)
				queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_HBANK, hb));
			if (lb != 0xff)
				queueEvent(MidiPlayEvent(t + 1, port, chn, ME_CONTROLLER, CTRL_LBANK, lb));
			queueEvent(MidiPlayEvent(t + 2, port, chn, ME_PROGRAM, pr, 0));
		}
		else if (a < CTRL_14_OFFSET)
		{ // 7 Bit Controller
			queueEvent(event);
		}
		else if (a < CTRL_RPN_OFFSET)
		{ // 14 bit high resolution controller
			int ctrlH = (a >> 8) & 0x7f;
			int ctrlL = a & 0x7f;
			int dataH = (b >> 7) & 0x7f;
			int dataL = b & 0x7f;
			queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, ctrlH, dataH));
			queueEvent(MidiPlayEvent(t + 1, port, chn, ME_CONTROLLER, ctrlL, dataL));
		}
		else if (a < CTRL_NRPN_OFFSET)
		{ // RPN 7-Bit Controller
			int ctrlH = (a >> 8) & 0x7f;
			int ctrlL = a & 0x7f;
			queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_HRPN, ctrlH));
			queueEvent(MidiPlayEvent(t + 1, port, chn, ME_CONTROLLER, CTRL_LRPN, ctrlL));
			queueEvent(MidiPlayEvent(t + 2, port, chn, ME_CONTROLLER, CTRL_HDATA, b));

			t += 3;
			// Select null parameters so that subsequent data controller events do not upset the last *RPN controller.
			if (nvh != 0xff)
			{
				queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_HRPN, nvh & 0x7f));
				t += 1;
			}
			if (nvl != 0xff)
				queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_LRPN, nvl & 0x7f));
		}
		else if (a < CTRL_INTERNAL_OFFSET)
		{ // NRPN 7-Bit Controller
			int ctrlH = (a >> 8) & 0x7f;
			int ctrlL = a & 0x7f;
			queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_HNRPN, ctrlH));
			queueEvent(MidiPlayEvent(t + 1, port, chn, ME_CONTROLLER, CTRL_LNRPN, ctrlL));
			queueEvent(MidiPlayEvent(t + 2, port, chn, ME_CONTROLLER, CTRL_HDATA, b));

			t += 3;
			if (nvh != 0xff)
			{
				queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_HNRPN, nvh & 0x7f));
				t += 1;
			}
			if (nvl != 0xff)
				queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_LNRPN, nvl & 0x7f));
		}
		else if (a < CTRL_NRPN14_OFFSET)
		{ // RPN14 Controller
			int ctrlH = (a >> 8) & 0x7f;
			int ctrlL = a & 0x7f;
			int dataH = (b >> 7) & 0x7f;
			int dataL = b & 0x7f;
			queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_HRPN, ctrlH));
			queueEvent(MidiPlayEvent(t + 1, port, chn, ME_CONTROLLER, CTRL_LRPN, ctrlL));
			queueEvent(MidiPlayEvent(t + 2, port, chn, ME_CONTROLLER, CTRL_HDATA, dataH));
			queueEvent(MidiPlayEvent(t + 3, port, chn, ME_CONTROLLER, CTRL_LDATA, dataL));

			t += 4;
			if (nvh != 0xff)
			{
				queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_HRPN, nvh & 0x7f));
				t += 1;
			}
			if (nvl != 0xff)
				queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_LRPN, nvl & 0x7f));
		}
		else if (a < CTRL_NONE_OFFSET)
		{ // NRPN14 Controller
			int ctrlH = (a >> 8) & 0x7f;
			int ctrlL = a & 0x7f;
			int dataH = (b >> 7) & 0x7f;
			int dataL = b & 0x7f;
			queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_HNRPN, ctrlH));
			queueEvent(MidiPlayEvent(t + 1, port, chn, ME_CONTROLLER, CTRL_LNRPN, ctrlL));
			queueEvent(MidiPlayEvent(t + 2, port, chn, ME_CONTROLLER, CTRL_HDATA, dataH));
			queueEvent(MidiPlayEvent(t + 3, port, chn, ME_CONTROLLER, CTRL_LDATA, dataL));

			t += 4;
			if (nvh != 0xff)
			{
				queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_HNRPN, nvh & 0x7f));
				t += 1;
			}
			if (nvl != 0xff)
				queueEvent(MidiPlayEvent(t, port, chn, ME_CONTROLLER, CTRL_LNRPN, nvl & 0x7f));
		}
		else
		{
			printf("MidiJackDevice::processEvent: unknown controller type 0x%x\n", a);
		}
	}
	else
	{
		queueEvent(event);
	}
}

//---------------------------------------------------------
//    processMidi called from audio process only.
//---------------------------------------------------------

void MidiJackDevice::processMidi()
{
	if (!_out_client_jackport) // p3.3.55
		return;
	void* port_buf = jack_port_get_buffer(_out_client_jackport, segmentSize); // p3.3.55
	jack_midi_clear_buffer(port_buf);

	while (!eventFifo.isEmpty())
	{
		MidiPlayEvent e(eventFifo.get());
		int evTime = e.time();
		// Is event marked to be played immediately?
		if (evTime == 0)
		{
			// Nothing to do but stamp the event to be queued for frame 0+.
			e.setTime(audio->getFrameOffset() + audio->pos().frame());
		}

#ifdef JACK_MIDI_DEBUG
		printf("MidiJackDevice::processMidi eventFifo time:%d type:%d ch:%d A:%d B:%d\n", e.time(), e.type(), e.channel(), e.dataA(), e.dataB());
#endif  
		processEvent(e);
	}

	MPEventList* el = playEvents();
	if (el->empty())
		return;

	iMPEvent i = nextPlayEvent();
	for (; i != el->end(); ++i)
	{
		// p3.3.39 Update hardware state so knobs and boxes are updated. Optimize to avoid re-setting existing values.
		// Same code as in MidiPort::sendEvent()
		if (_port != -1)
		{
			MidiPort* mp = &midiPorts[_port];
			if (i->type() == ME_CONTROLLER)
			{
				int da = i->dataA();
				int db = i->dataB();
				db = mp->limitValToInstrCtlRange(da, db);
				if (!mp->setHwCtrlState(i->channel(), da, db))
					continue;
			}
			else if (i->type() == ME_PITCHBEND)
			{
				// p3.3.44
				//printf("MidiJackDevice::processMidi playEvents ME_PITCHBEND time:%d type:%d ch:%d A:%d B:%d\n", (*i).time(), (*i).type(), (*i).channel(), (*i).dataA(), (*i).dataB());

				int da = mp->limitValToInstrCtlRange(CTRL_PITCH, i->dataA());
				if (!mp->setHwCtrlState(i->channel(), CTRL_PITCH, da))
					continue;
			}
			else if (i->type() == ME_PROGRAM)
			{
				if (!mp->setHwCtrlState(i->channel(), CTRL_PROGRAM, i->dataA()))
					continue;
			}
		}

		processEvent(*i);
	}

	setNextPlayEvent(i);
}

//---------------------------------------------------------
//   initMidiJack
//    return true on error
//---------------------------------------------------------

bool initMidiJack()
{
	return false;
}

