//=========================================================
//  OOMidi
//  OpenOctave Midi and Audio Editor
//    $Id: listedit.h,v 1.3.2.3 2006/09/19 22:03:33 spamatica Exp $
//  (C) Copyright 1999 Werner Schweer (ws@seh.de)
//=========================================================

#ifndef __LIST_EDIT_H__
#define __LIST_EDIT_H__

#include "AbstractMidiEditor.h"
#include "noteinfo.h"
#include "cobject.h"

class QAction;
class QActionGroup;
class QCloseEvent;
class QKeyEvent;
class QShowEvent;
class QTreeWidget;
class QTreeWidgetItem;


class Event;
class MidiTrack;
class PartList;
class MidiPart;
class MidiPart;
class Xml;

//---------------------------------------------------------
//   ListEdit
//---------------------------------------------------------

class ListEdit : public AbstractMidiEditor
{
    QTreeWidget* liste;
    QMenu* menuEdit;
    QActionGroup* insertItems;
    QAction *_editEventValueAction;
    QToolBar* listTools;
    MidiTrack* curTrack;
    MidiPart* curPart;
    int selectedTick;
    int curPartId;

    enum
    {
	CMD_DELETE, CMD_EDIT_VALUE
    };

    Q_OBJECT
    virtual void closeEvent(QCloseEvent*);
    virtual void showEvent(QShowEvent*);
    virtual void keyPressEvent(QKeyEvent*);
    void initShortcuts();
    unsigned getSelectedTick();
    QAction *insertNote, *insertSysEx, *insertCtrl, *insertMeta, *insertCAfter, *insertPAfter;

private slots:
    void editInsertNote();
    void editInsertSysEx();
    void editInsertCtrl();
    void editInsertMeta();
    void editInsertCAfter();
    void editInsertPAfter();
    void editEvent(Event&, MidiPart*);
    void selectionChanged();
    void doubleClicked(QTreeWidgetItem*);
    void cmd(int cmd);
    void configChanged();

public slots:
    void songChanged(int);

signals:
    void deleted(unsigned long);

protected:
    void resizeEvent(QResizeEvent *);

public:
    ListEdit(PartList*);
    ~ListEdit();
    virtual void readStatus(Xml&);
    virtual void writeStatus(int, Xml&) const;
};

#endif

