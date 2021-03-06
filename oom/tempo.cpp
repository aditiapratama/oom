//=========================================================
//  OOMidi
//  OpenOctave Midi and Audio Editor
//  $Id: tempo.cpp,v 1.7.2.7 2008/05/21 00:28:52 terminator356 Exp $
//
//  (C) Copyright 1999/2000 Werner Schweer (ws@seh.de)
//=========================================================

#include <stdio.h>
#include <errno.h>
#include <cmath>

#include "tempo.h"
#include "globals.h"
#include "gconfig.h"
#include "xml.h"

TempoList tempomap;

//---------------------------------------------------------
//   TempoList
//---------------------------------------------------------

TempoList::TempoList()
{
	_tempo = 500000;
	insert(std::pair<const unsigned, TEvent*> (MAX_TICK + 1, new TEvent(_tempo, 0)));
	_tempoSN = 1;
	_globalTempo = 100;
	useList = true;
}

//---------------------------------------------------------
//   add
//---------------------------------------------------------

void TempoList::add(unsigned tick, int tempo)
{
	if (tick > MAX_TICK)
		tick = MAX_TICK;
	iTEvent e = upper_bound(tick);

	if (tick == e->second->tick)
		e->second->tempo = tempo;
	else
	{
		TEvent* ne = e->second;
		TEvent* ev = new TEvent(ne->tempo, ne->tick);
		ne->tempo = tempo;
		ne->tick = tick;
		insert(std::pair<const unsigned, TEvent*> (tick, ev));
	}
	normalize();
}

//---------------------------------------------------------
//   TempoList::normalize
//---------------------------------------------------------

void TempoList::normalize()
{
	int frame = 0;
	for (iTEvent e = begin(); e != end(); ++e)
	{
		e->second->frame = frame;
		unsigned dtick = e->first - e->second->tick;
		double dtime = double(dtick) / (config.division * _globalTempo * 10000.0 / e->second->tempo);
		frame += lrint(dtime * sampleRate);
	}
}

//---------------------------------------------------------
//   TempoList::dump
//---------------------------------------------------------

void TempoList::dump() const
{
	printf("\nTempoList:\n");
	for (ciTEvent i = begin(); i != end(); ++i)
	{
		printf("%6d %06d Tempo %6d Frame %d\n",
				i->first, i->second->tick, i->second->tempo,
				i->second->frame);
	}
}

//---------------------------------------------------------
//   clear
//---------------------------------------------------------

void TempoList::clear()
{
	for (iTEvent i = begin(); i != end(); ++i)
		delete i->second;
	TEMPOLIST::clear();
	insert(std::pair<const unsigned, TEvent*> (MAX_TICK + 1, new TEvent(500000, 0)));
	++_tempoSN;
}

//---------------------------------------------------------
//   tempo
//---------------------------------------------------------

int TempoList::tempo(unsigned tick) const
{
	if (useList)
	{
		ciTEvent i = upper_bound(tick);
		if (i == end())
		{
			if(debugMsg)
				printf("no TEMPO at tick %d,0x%x\n", tick, tick);
			return 1000;
		}
		return i->second->tempo;
	}
	else
		return _tempo;
}

//---------------------------------------------------------
//   del
//---------------------------------------------------------

void TempoList::del(unsigned tick)
{
	// printf("TempoList::del(%d)\n", tick);
	iTEvent e = find(tick);
	if (e == end())
	{
		if(debugMsg)
			printf("TempoList::del(%d): not found\n", tick);
		return;
	}
	del(e);
	++_tempoSN;
}

void TempoList::del(iTEvent e)
{
	iTEvent ne = e;
	++ne;
	if (ne == end())
	{
		if(debugMsg)
			printf("TempoList::del() HALLO\n");
		return;
	}
	ne->second->tempo = e->second->tempo;
	ne->second->tick = e->second->tick;
	erase(e);
	normalize();
	++_tempoSN;
}

//---------------------------------------------------------
//   change
//---------------------------------------------------------

void TempoList::change(unsigned tick, int newTempo)
{
	iTEvent e = find(tick);
	e->second->tempo = newTempo;
	normalize();
	++_tempoSN;
}

//---------------------------------------------------------
//   setTempo
//    called from transport window
//    & slave mode tempo changes
//---------------------------------------------------------

void TempoList::setTempo(unsigned tick, int newTempo)
{
	if (useList)
		add(tick, newTempo);
	else
		_tempo = newTempo;
	++_tempoSN;
}

//---------------------------------------------------------
//   setGlobalTempo
//---------------------------------------------------------

void TempoList::setGlobalTempo(int val)
{
	_globalTempo = val;
	++_tempoSN;
	normalize();
}

//---------------------------------------------------------
//   addTempo
//---------------------------------------------------------

void TempoList::addTempo(unsigned t, int tempo)
{
	add(t, tempo);
	++_tempoSN;
}

//---------------------------------------------------------
//   delTempo
//---------------------------------------------------------

void TempoList::delTempo(unsigned tick)
{
	del(tick);
	++_tempoSN;
}

void TempoList::delTempoRange(unsigned start, unsigned end)
{
	if(start == end)
		del(start);
	else
	{
		if(start > end)
		{	
			int tmp = end;
			end = start;
			start = tmp;
		}
		while(start < end)
		{
			del(start);
			++start;
		}
	}
}

//---------------------------------------------------------
//   changeTempo
//---------------------------------------------------------

void TempoList::changeTempo(unsigned tick, int newTempo)
{
	change(tick, newTempo);
	++_tempoSN;
}

//---------------------------------------------------------
//   setMasterFlag
//---------------------------------------------------------

bool TempoList::setMasterFlag(unsigned /*tick*/, bool val)
{
	if (useList != val)
	{
		useList = val;
		++_tempoSN;
		return true;
	}
	return false;
}

//---------------------------------------------------------
//   tick2frame
//---------------------------------------------------------

unsigned TempoList::tick2frame(unsigned tick, unsigned frame, int* sn) const
{
	return (*sn == _tempoSN) ? frame : tick2frame(tick, sn);
}

//---------------------------------------------------------
//   tick2frame
//---------------------------------------------------------

unsigned TempoList::tick2frame(unsigned tick, int* sn) const
{
	int f;
	if (useList)
	{
		ciTEvent i = upper_bound(tick);
		if (i == end())
		{
			if(debugMsg)
				printf("tick2frame(%d,0x%x): not found\n", tick, tick);
			// abort();
			return 0;
		}
		unsigned dtick = tick - i->second->tick;
		double dtime = double(dtick) / (config.division * _globalTempo * 10000.0 / i->second->tempo);
		unsigned dframe = lrint(dtime * sampleRate);
		f = i->second->frame + dframe;
	}
	else
	{
		double t = (double(tick) * double(_tempo)) / (double(config.division) * _globalTempo * 10000.0);
		f = lrint(t * sampleRate);
	}
	if (sn)
		*sn = _tempoSN;
	return f;
}

//---------------------------------------------------------
//   frame2tick
//    return cached value t if list did not change
//---------------------------------------------------------

unsigned TempoList::frame2tick(unsigned frame, unsigned t, int* sn) const
{
	return (*sn == _tempoSN) ? t : frame2tick(frame, sn);
}

//---------------------------------------------------------
//   frame2tick
//---------------------------------------------------------

unsigned TempoList::frame2tick(unsigned frame, int* sn) const
{
	unsigned tick;
	if (useList)
	{
		ciTEvent e;
		for (e = begin(); e != end();)
		{
			ciTEvent ee = e;
			++ee;
			if (ee == end())
				break;
			if (frame < ee->second->frame)
				break;
			e = ee;
		}
		unsigned te = e->second->tempo;
		int dframe = frame - e->second->frame;
		double dtime = double(dframe) / double(sampleRate);
		tick = e->second->tick + lrint(dtime * _globalTempo * config.division * 10000.0 / te);
	}
	else
		tick = lrint((double(frame) / double(sampleRate)) * _globalTempo * config.division * 10000.0 / double(_tempo));
	if (sn)
		*sn = _tempoSN;
	return tick;
}

//---------------------------------------------------------
//   deltaTick2frame
//---------------------------------------------------------

unsigned TempoList::deltaTick2frame(unsigned tick1, unsigned tick2, int* sn) const
{
	int f1, f2;
	if (useList)
	{
		ciTEvent i = upper_bound(tick1);
		if (i == end())
		{
			if(debugMsg)
				printf("TempoList::deltaTick2frame: tick1:%d not found\n", tick1);
			// abort();
			return 0;
		}
		unsigned dtick = tick1 - i->second->tick;
		double dtime = double(dtick) / (config.division * _globalTempo * 10000.0 / i->second->tempo);
		unsigned dframe = lrint(dtime * sampleRate);
		f1 = i->second->frame + dframe;

		i = upper_bound(tick2);
		if (i == end())
		{
			return 0;
		}
		dtick = tick2 - i->second->tick;
		dtime = double(dtick) / (config.division * _globalTempo * 10000.0 / i->second->tempo);
		dframe = lrint(dtime * sampleRate);
		f2 = i->second->frame + dframe;
	}
	else
	{
		double t = (double(tick1) * double(_tempo)) / (double(config.division) * _globalTempo * 10000.0);
		f1 = lrint(t * sampleRate);

		t = (double(tick2) * double(_tempo)) / (double(config.division) * _globalTempo * 10000.0);
		f2 = lrint(t * sampleRate);
	}
	if (sn)
		*sn = _tempoSN;
	// FIXME: Caution: This should be rounded off properly somehow, but how to do that?
	//                 But it seems to work so far.
	return f2 - f1;
}


//---------------------------------------------------------
//   deltaFrame2tick
//---------------------------------------------------------

unsigned TempoList::deltaFrame2tick(unsigned frame1, unsigned frame2, int* sn) const
{
	unsigned tick1, tick2;
	if (useList)
	{
		ciTEvent e;
		for (e = begin(); e != end();)
		{
			ciTEvent ee = e;
			++ee;
			if (ee == end())
				break;
			if (frame1 < ee->second->frame)
				break;
			e = ee;
		}
		unsigned te = e->second->tempo;
		int dframe = frame1 - e->second->frame;
		double dtime = double(dframe) / double(sampleRate);
		tick1 = e->second->tick + lrint(dtime * _globalTempo * config.division * 10000.0 / te);

		for (e = begin(); e != end();)
		{
			ciTEvent ee = e;
			++ee;
			if (ee == end())
				break;
			if (frame2 < ee->second->frame)
				break;
			e = ee;
		}
		te = e->second->tempo;
		dframe = frame2 - e->second->frame;
		dtime = double(dframe) / double(sampleRate);
		tick2 = e->second->tick + lrint(dtime * _globalTempo * config.division * 10000.0 / te);
	}
	else
	{
		tick1 = lrint((double(frame1) / double(sampleRate)) * _globalTempo * config.division * 10000.0 / double(_tempo));
		tick2 = lrint((double(frame2) / double(sampleRate)) * _globalTempo * config.division * 10000.0 / double(_tempo));
	}
	if (sn)
		*sn = _tempoSN;
	// FIXME: Caution: This should be rounded off properly somehow, but how to do that?
	//                 But it seems to work so far.
	return tick2 - tick1;
}

//---------------------------------------------------------
//   TempoList::write
//---------------------------------------------------------

void TempoList::write(int level, Xml& xml) const
{
	xml.put(level++, "<tempolist fix=\"%d\">", _tempo);
	if (_globalTempo != 100)
		xml.intTag(level, "globalTempo", _globalTempo);
	for (ciTEvent i = begin(); i != end(); ++i)
		i->second->write(level, xml, i->first);
    xml.tag(--level, "/tempolist");
}

//---------------------------------------------------------
//   TempoList::read
//---------------------------------------------------------

void TempoList::read(Xml& xml)
{
	for (;;)
	{
		Xml::Token token = xml.parse();
		const QString& tag = xml.s1();
		switch (token)
		{
			case Xml::Error:
			case Xml::End:
				return;
			case Xml::TagStart:
				if (tag == "tempo")
				{
					TEvent* t = new TEvent();
					unsigned tick = t->read(xml);
					iTEvent pos = find(tick);
					if (pos != end())
						erase(pos);
					insert(std::pair<const int, TEvent*> (tick, t));
				}
				else if (tag == "globalTempo")
					_globalTempo = xml.parseInt();
				else
					xml.unknown("TempoList");
				break;
			case Xml::Attribut:
				if (tag == "fix")
					_tempo = xml.s2().toInt();
				break;
			case Xml::TagEnd:
				if (tag == "tempolist")
				{
					normalize();
					++_tempoSN;
					return;
				}
			default:
				break;
		}
	}
}

//---------------------------------------------------------
//   TEvent::write
//---------------------------------------------------------

void TEvent::write(int level, Xml& xml, int at) const
{
	xml.tag(level++, "tempo at=\"%d\"", at);
	xml.intTag(level, "tick", tick);
	xml.intTag(level, "val", tempo);
    xml.tag(--level, "/tempo");
}

//---------------------------------------------------------
//   TEvent::read
//---------------------------------------------------------

int TEvent::read(Xml& xml)
{
	int at = 0;
	for (;;)
	{
		Xml::Token token = xml.parse();
		const QString& tag = xml.s1();
		switch (token)
		{
			case Xml::Error:
			case Xml::End:
				return 0;
			case Xml::TagStart:
				if (tag == "tick")
					tick = xml.parseInt();
				else if (tag == "val")
					tempo = xml.parseInt();
				else
					xml.unknown("TEvent");
				break;
			case Xml::Attribut:
				if (tag == "at")
					at = xml.s2().toInt();
				break;
			case Xml::TagEnd:
				if (tag == "tempo")
				{
					return at;
				}
			default:
				break;
		}
	}
	return 0;
}


