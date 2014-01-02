/*
 * Xournal++
 *
 * Undo move action (EditSelection)
 *
 * @author Xournal Team
 * http://xournal.sf.net
 *
 * @license GPL
 */

#ifndef __RELMOVEUNDOACTION_H__
#define __RELMOVEUNDOACTION_H__

#include "UndoAction.h"
#include <glib.h>

class XojPage;
class Layer;
class Redrawable;

class RelMoveUndoAction: public UndoAction
{
public:
	RelMoveUndoAction(Layer* sourceLayer, PageRef sourcePage, Redrawable* sourceView,
	                  GList* selected, double mx, double my, Layer* targetLayer, PageRef targetPage,
	                  Redrawable* targetView);
	virtual ~RelMoveUndoAction();

public:
	virtual bool undo(Control* control);
	virtual bool redo(Control* control);
	XojPage** getPages();
	virtual String getText();

private:
	void switchLayer(GList* entries, Layer* oldLayer, Layer* newLayer);
	void repaint(Redrawable *view, bool target);
	void repaint();
	void move();

private:
	XOJ_TYPE_ATTRIB;

	GList* elements;
	PageRef targetPage;

	Layer* sourceLayer;
	Layer* targetLayer;

	Redrawable* sourceView;
	Redrawable* targetView;

	String text;

	friend class EditSelection;

	double dx, dy;
};

#endif /* __RELMOVEUNDOACTION_H__ */