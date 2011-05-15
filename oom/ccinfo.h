//=========================================================
//  OOMidi
//  OpenOctave Midi and Audio Editor
//  $Id: $
//
//  (C) Copyright 2011 Andrew Williams and Christopher Cherrett
//=========================================================

#ifndef _OOM_CCINFO_H_
#define _OOM_CCINFO_H_

#include <QObject>

class Track;

class CCInfo : public QObject
{
	Track* m_track;
	int m_port;
	int m_channel;
	int m_control;
	int m_ccnum;

public:
	CCInfo(Track* , int port, int chan, int control, int cc);
	CCInfo();
	CCInfo(const CCInfo&);
	~CCInfo();

	int port()	{ return m_port; }
	void setPort(int p) { m_port = p; }
	int channel() { return m_channel; }
	void setChannel(int c) { m_channel = c; }
	int controller() { return m_control; }
	void setController(int c) { m_control = c; }
	int assignedControl() { return m_ccnum; }
	void setAssignedControl(int c) { m_ccnum = c; }
	void setTrack(Track* t) { m_track = t; }
	Track* track() { return m_track; }

	inline bool operator==(CCInfo nfo)
	{
		return nfo.port() == m_port && nfo.channel() == m_channel && nfo.controller() == m_control 
			&& nfo.assignedControl() == m_ccnum && nfo.track() == m_track;
	}

	inline uint qHash(CCInfo m)
	{
		return (m.channel() ^ m.port())*(m.assignedControl()+m.controller());
	}

};

#endif