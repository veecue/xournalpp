#include "XournalWidget.h"
#include <gdk/gdkkeysyms.h>
#include "../control/Control.h"
#include <math.h>
#include "../control/ev-metadata-manager.h"
#include "Shadow.h"
#include "../util/Util.h"

XournalWidget::XournalWidget(GtkWidget * parent, Control * control) {
	this->control = control;
	registerListener(control);

	initScrollHandler(parent);

	widget = gtk_layout_new(NULL, NULL);
	GTK_WIDGET_SET_FLAGS(widget, GTK_CAN_FOCUS);

	gtk_container_add(GTK_CONTAINER(parent), widget);
	gtk_widget_show(widget);

	viewPages = NULL;
	viewPagesLen = 0;
	margin = 75;
	currentPage = 0;
	lastSelectedPage = -1;
	lastWidgetSize = 0;

	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(parent), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	g_signal_connect(widget, "size-allocate", G_CALLBACK(sizeAllocate), this);
	g_signal_connect(widget, "button_press_event", G_CALLBACK(onButtonPressEventCallback), this);
	gtk_widget_set_events(widget, GDK_BUTTON_PRESS_MASK);

	g_signal_connect(G_OBJECT(widget), "expose_event", G_CALLBACK(exposeEventCallback), this);

	GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(parent));
	g_signal_connect(adj, "value-changed", G_CALLBACK( onVscrollChanged), this);

	gtk_widget_set_events(widget, GDK_EXPOSURE_MASK | GDK_KEY_PRESS_MASK);
	gtk_signal_connect(GTK_OBJECT(widget), "key_press_event", GTK_SIGNAL_FUNC(onKeyPressCallback), this);
	gtk_signal_connect(GTK_OBJECT(widget), "key_release_event", GTK_SIGNAL_FUNC(onKeyReleaseCallback), this);

	control->getZoomControl()->addZoomListener(this);

	gtk_widget_set_can_default(widget, true);
	gtk_widget_grab_default(widget);

	gtk_widget_grab_focus(widget);
}

XournalWidget::~XournalWidget() {
}

void XournalWidget::onVscrollChanged(GtkAdjustment *adjustment, XournalWidget * xournal) {
	xournal->onScrolled();
}

int XournalWidget::getCurrentPage() {
	return currentPage;
}

bool XournalWidget::onKeyPressCallback(GtkWidget *widget, GdkEventKey *event, XournalWidget * xournal) {
	return xournal->onKeyPressEvent(widget, event);
}

bool XournalWidget::onKeyReleaseCallback(GtkWidget *widget, GdkEventKey *event, XournalWidget * xournal) {
	return xournal->onKeyReleaseEvent(event);
}

const int scrollKeySize = 10;

bool XournalWidget::onKeyPressEvent(GtkWidget *widget, GdkEventKey *event) {
	int p = getCurrentPage();
	if (p >= 0 && p < this->viewPagesLen) {
		PageView * v = this->viewPages[p];
		if (v->onKeyPressEvent(event)) {
			return true;
		}
	}

	// Esc leaves fullscreen mode
	if (event->keyval == GDK_Escape || event->keyval == GDK_F11) {
		if (control->isFullscreen()) {
			control->enableFullscreen(false);
			return true;
		}
	}

	// F5 starts presentation modus
	if (event->keyval == GDK_F5) {
		if (!control->isFullscreen()) {
			control->enableFullscreen(true, true);
			return true;
		}
	}

	if (event->keyval == GDK_Page_Down) {
		control->goToNextPage();
		return true;
	}
	if (event->keyval == GDK_Page_Up) {
		control->goToPreviousPage();
		return true;
	}

	if (event->keyval == GDK_Up) {
		control->scrollRelative(0, -scrollKeySize);
		return true;
	}

	if (event->keyval == GDK_Down) {
		control->scrollRelative(0, scrollKeySize);
		return true;
	}

	if (event->keyval == GDK_Left) {
		control->scrollRelative(-scrollKeySize, 0);
		return true;
	}

	if (event->keyval == GDK_Right) {
		control->scrollRelative(scrollKeySize, 0);
		return true;
	}

	return false;
}

bool XournalWidget::onKeyReleaseEvent(GdkEventKey *event) {
	int p = getCurrentPage();
	if (p >= 0 && p < this->viewPagesLen) {
		PageView * v = this->viewPages[p];
		if (v->onKeyReleaseEvent(event)) {
			return true;
		}
	}

	return false;
}

gboolean XournalWidget::onButtonPressEventCallback(GtkWidget *widget, GdkEventButton *event, XournalWidget * xournal) {
	xournal->resetFocus();
	return false;
}

// send the focus back to the appropriate widget
void XournalWidget::resetFocus() {
	gtk_widget_grab_focus(widget);
}

bool XournalWidget::searchTextOnPage(const char * text, int p, int * occures, double * top) {
	if (p < 0 || p >= this->viewPagesLen) {
		return false;
	}
	PageView * v = this->viewPages[p];

	return v->searchTextOnPage(text, occures, top);
}

void XournalWidget::forceUpdatePagenumbers() {
	int p = this->currentPage;
	this->currentPage = -1;
	control->firePageSelected(p);
}

PageView * XournalWidget::getViewFor(int pageNr) {
	if (pageNr < 0 || pageNr >= this->viewPagesLen) {
		return NULL;
	}
	return viewPages[pageNr];
}

PageView * XournalWidget::getViewAt(int x, int y) {
	for (int page = 0; page < viewPagesLen; page++) {
		PageView * p = viewPages[page];
		GtkAllocation alloc = { 0 };
		gtk_widget_get_allocation(p->getWidget(), &alloc);

		if (alloc.x <= x && alloc.x + alloc.width >= x && alloc.y <= y && alloc.y + alloc.height >= y) {
			return p;
		}
	}

	return NULL;
}

bool XournalWidget::exposeEventCallback(GtkWidget *widget, GdkEventExpose *event, XournalWidget * xournal) {
	xournal->paintBorder(widget, event);

	return true;
}

void XournalWidget::paintBorder(GtkWidget *widget, GdkEventExpose *event) {
	cairo_t *cr;
	cr = gdk_cairo_create(GTK_LAYOUT(widget)->bin_window);

	GtkAdjustment* h = gtk_layout_get_vadjustment(GTK_LAYOUT(this->widget));
	int scrollY = gtk_adjustment_get_value(h);
	GtkAllocation displaySize = { 0 };
	gtk_widget_get_allocation(this->widget, &displaySize);

	for (int i = 0; i < viewPagesLen; i++) {
		PageView * p = viewPages[i];
		GtkWidget * w = p->getWidget();
		GtkAllocation alloc = { 0 };
		gtk_widget_get_allocation(w, &alloc);

		if (scrollY > alloc.y + Shadow::getShadowBottomRightSize() + alloc.height) {
			continue;
		}
		if (scrollY + displaySize.height < alloc.y - Shadow::getShadowTopLeftSize()) {
			break;
		}

		double r = 0;
		double g = 0;
		double b = 0;

		if (p->isSelected()) {
			Shadow::drawShadow(cr, alloc.x - 2, alloc.y - 2, alloc.width + 4, alloc.height + 4, r, g, b);

			// Draw border
			Util::cairo_set_source_rgbi(cr, control->getSettings()->getSelectionColor());
			cairo_set_line_width(cr, 4.0);
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
			cairo_set_line_join(cr, CAIRO_LINE_JOIN_BEVEL);

			cairo_move_to(cr, alloc.x, alloc.y);
			cairo_line_to(cr, alloc.x, alloc.y + alloc.height);
			cairo_line_to(cr, alloc.x + alloc.width, alloc.y + alloc.height);
			cairo_line_to(cr, alloc.x + alloc.width, alloc.y);
			cairo_close_path(cr);

			cairo_stroke(cr);
		} else {
			Shadow::drawShadow(cr, alloc.x, alloc.y, alloc.width, alloc.height, r, g, b);
		}
	}

	cairo_destroy(cr);
}

void XournalWidget::pageSelected(int page) {
	if (currentPage == page) {
		return;
	}

	if (lastSelectedPage >= 0 && lastSelectedPage < viewPagesLen) {
		viewPages[lastSelectedPage]->setSelected(false);
	}

	this->currentPage = page;

	int pdfPage = -1;

	if (page >= 0 && page < viewPagesLen) {
		PageView * vp = viewPages[page];
		vp->setSelected(true);
		lastSelectedPage = page;
		pdfPage = vp->getPage()->getPdfPageNr();
	}

	control->updatePageNumbers(currentPage, pdfPage);

	control->updateBackgroundSizeButton();
}

Control * XournalWidget::getControl() {
	return control;
}

void XournalWidget::scrollTo(int pageNo, double y) {
	if (currentPage == pageNo) {
		return;
	}
	if (pageNo < 0 || pageNo >= viewPagesLen) {
		return;
	}
	// TODO: handle horizontal scrolling (dual page view)

	GtkAdjustment* h = gtk_layout_get_vadjustment(GTK_LAYOUT(widget));

	PageView * p = viewPages[pageNo];

	int pdfPage = p->getPage()->getPdfPageNr();

	GValue pY = { 0 };
	g_value_init(&pY, G_TYPE_INT);

	gtk_container_child_get_property(GTK_CONTAINER(widget), p->getWidget(), "y", &pY);
	int pos = g_value_get_int(&pY);

	control->firePageSelected(pageNo);

	y = y * control->getZoomControl()->getZoom();

	if (y == 0) {
		y = -10; // show the shadow on top
	}

	double v = (double) pos + y;
	double upper = gtk_adjustment_get_upper(h) - gtk_adjustment_get_page_size(h);

	if (upper < v) {
		v = upper;
	}

	gtk_adjustment_set_value(h, v);
}

void XournalWidget::onScrolled() {
	GtkAdjustment* h = gtk_layout_get_vadjustment(GTK_LAYOUT(widget));

	GtkAllocation allocation = { 0 };
	GtkWidget * w = gtk_widget_get_parent(widget);
	gtk_widget_get_allocation(w, &allocation);

	int scrollY = gtk_adjustment_get_value(h);

	int viewHeight = allocation.height;

	if (scrollY < 1) {
		control->firePageSelected(0);
		viewPages[0]->deleteViewBuffer();
		return;
	}

	int mostPageNr = 0;
	double mostPagePercent = 0;

	for (int page = 0; page < viewPagesLen; page++) {
		PageView * p = viewPages[page];
		GValue pY = { 0 };
		g_value_init(&pY, G_TYPE_INT);

		gtk_container_child_get_property(GTK_CONTAINER(widget), p->getWidget(), "y", &pY);
		int y = g_value_get_int(&pY);

		int pageHeight = p->getDisplayHeight();

		if (y > scrollY + viewHeight) {
			p->deleteViewBuffer();
			for (; page < viewPagesLen; page++) {
				p = viewPages[page];
				p->deleteViewBuffer();
			}

			break;
		}
		if (y + pageHeight >= scrollY) {
			int startY = 0;
			int endY = pageHeight;
			if (y <= scrollY) {
				startY = scrollY - y;
			}
			if (y + pageHeight > scrollY + viewHeight) {
				endY = pageHeight - ((y + pageHeight) - (scrollY + viewHeight));
			}

			double percent = ((double) (endY - startY)) / ((double) pageHeight);

			if (percent > mostPagePercent) {
				mostPagePercent = percent;
				mostPageNr = page;
			}
		} else {
			p->deleteViewBuffer();
		}
	}

	if (getControl()->getSettings()->isShowTwoPages() && mostPageNr < viewPagesLen - 1) {
		GValue pY1 = { 0 };
		g_value_init(&pY1, G_TYPE_INT);
		GValue pY2 = { 0 };
		g_value_init(&pY2, G_TYPE_INT);

		gtk_container_child_get_property(GTK_CONTAINER(widget), viewPages[mostPageNr]->getWidget(), "y", &pY1);
		int y1 = g_value_get_int(&pY1);
		gtk_container_child_get_property(GTK_CONTAINER(widget), viewPages[mostPageNr + 1]->getWidget(), "y", &pY2);
		int y2 = g_value_get_int(&pY2);

		// TODO: does not work for first and second page... why?
		if (y1 != y2 || !viewPages[mostPageNr + 1]->isSelected()) {
			// if the second page is selected DON'T select the first page.
			// Only select the first page if none is selected
			control->firePageSelected(mostPageNr);
		}
	} else {
		control->firePageSelected(mostPageNr);
	}
}

void XournalWidget::endTextSelection() {
	for (int i = 0; i < viewPagesLen; i++) {
		PageView * v = viewPages[i];
		v->endText();
	}
}

void XournalWidget::layerChanged(int page) {
	viewPages[page]->repaint();
}

void XournalWidget::getPasteTarget(double & x, double & y) {
	int pageNo = getCurrentPage();
	if (pageNo == -1) {
		return;
	}
	XojPage * page = control->getDocument()->getPage(pageNo);

	// TODO: calculate the visible rect and paste in the center of the visible rect of the page!

	if (page) {
		x = page->getWidth() / 2;
		y = page->getHeight() / 2;
	}
}

void XournalWidget::sizeAllocate(GtkWidget *widget, GtkRequisition *requisition, XournalWidget * xournal) {
	GtkAllocation alloc = { 0 };
	gtk_widget_get_allocation(widget, &alloc);

	if (xournal->lastWidgetSize != alloc.width) {
		xournal->layoutPages();
		xournal->lastWidgetSize = alloc.width;
	}
	xournal->control->calcZoomFitSize();
}

GtkWidget * XournalWidget::getWidget() {
	return widget;
}

void XournalWidget::initScrollHandler(GtkWidget * parent) {
	GtkBindingSet *bindingSet;

	bindingSet = gtk_binding_set_by_class(G_OBJECT_GET_CLASS(parent));
	gtk_binding_entry_add_signal(bindingSet, GDK_Up, (GdkModifierType) 0, "scroll_child", 2, GTK_TYPE_SCROLL_TYPE,
			GTK_SCROLL_STEP_BACKWARD, G_TYPE_BOOLEAN, FALSE);
	gtk_binding_entry_add_signal(bindingSet, GDK_KP_Up, (GdkModifierType) 0, "scroll_child", 2, GTK_TYPE_SCROLL_TYPE,
			GTK_SCROLL_STEP_BACKWARD, G_TYPE_BOOLEAN, FALSE);
	gtk_binding_entry_add_signal(bindingSet, GDK_Down, (GdkModifierType) 0, "scroll_child", 2, GTK_TYPE_SCROLL_TYPE,
			GTK_SCROLL_STEP_FORWARD, G_TYPE_BOOLEAN, FALSE);
	gtk_binding_entry_add_signal(bindingSet, GDK_KP_Down, (GdkModifierType) 0, "scroll_child", 2, GTK_TYPE_SCROLL_TYPE,
			GTK_SCROLL_STEP_FORWARD, G_TYPE_BOOLEAN, FALSE);
	gtk_binding_entry_add_signal(bindingSet, GDK_Left, (GdkModifierType) 0, "scroll_child", 2, GTK_TYPE_SCROLL_TYPE,
			GTK_SCROLL_STEP_BACKWARD, G_TYPE_BOOLEAN, TRUE);
	gtk_binding_entry_add_signal(bindingSet, GDK_KP_Left, (GdkModifierType) 0, "scroll_child", 2, GTK_TYPE_SCROLL_TYPE,
			GTK_SCROLL_STEP_BACKWARD, G_TYPE_BOOLEAN, TRUE);
	gtk_binding_entry_add_signal(bindingSet, GDK_Right, (GdkModifierType) 0, "scroll_child", 2, GTK_TYPE_SCROLL_TYPE,
			GTK_SCROLL_STEP_FORWARD, G_TYPE_BOOLEAN, TRUE);
	gtk_binding_entry_add_signal(bindingSet, GDK_KP_Right, (GdkModifierType) 0, "scroll_child", 2,
			GTK_TYPE_SCROLL_TYPE, GTK_SCROLL_STEP_FORWARD, G_TYPE_BOOLEAN, TRUE);
}

void XournalWidget::zoomIn() {
	control->getZoomControl()->zoomIn();
}

void XournalWidget::zoomOut() {
	control->getZoomControl()->zoomOut();
}

void XournalWidget::zoomChanged(double lastZoom) {
	GtkAdjustment* h = gtk_layout_get_vadjustment(GTK_LAYOUT(widget));
	GtkAllocation allocation = { 0 };
	GtkWidget * w = gtk_widget_get_parent(widget);
	gtk_widget_get_allocation(w, &allocation);
	double scrollY = gtk_adjustment_get_value(h);

	layoutPages();

	for (int i = 0; i < viewPagesLen; i++) {
		PageView * pageView = viewPages[i];
		pageView->updateSize();
	}

	double zoom = control->getZoomControl()->getZoom();
	gtk_adjustment_set_value(h, scrollY / lastZoom * control->getZoomControl()->getZoom());

	const char * file = control->getDocument()->getEvMetadataFilename();
	if (file) {
		ev_metadata_manager_set_double(file, "zoom", zoom);
	}
}

void XournalWidget::pageSizeChanged(int page) {
	PageView * v = this->viewPages[page];
	v->updateSize();
	layoutPages();
}

void XournalWidget::pageChanged(int page) {
	if (page >= 0 && page < this->viewPagesLen) {
		this->viewPages[page]->repaint();
	}
}

void XournalWidget::pageDeleted(int page) {
	delete this->viewPages[page];
	for (int i = page; i < this->viewPagesLen; i++) {
		this->viewPages[i] = this->viewPages[i + 1];
	}

	this->viewPagesLen--;

	layoutPages();
}

void XournalWidget::pageInserted(int page) {
	PageView ** lastViewPages = this->viewPages;

	this->viewPages = new PageView *[this->viewPagesLen + 1];

	for (int i = 0; i < page; i++) {
		this->viewPages[i] = lastViewPages[i];
	}

	for (int i = page; i < this->viewPagesLen; i++) {
		this->viewPages[i + 1] = lastViewPages[i];
	}

	this->viewPagesLen++;

	delete[] lastViewPages;

	Document * doc = control->getDocument();

	PageView * pageView = new PageView(this, doc->getPage(page));
	this->viewPages[page] = pageView;
	gtk_layout_put(GTK_LAYOUT(widget), pageView->getWidget(), 0, 0);

	layoutPages();
	updateXEvents();
}

double XournalWidget::getZoom() {
	return control->getZoomControl()->getZoom();
}

void XournalWidget::updateXEvents() {
	for (int i = 0; i < viewPagesLen; i++) {
		PageView * pageView = viewPages[i];
		pageView->updateXEvents();
	}
}

void XournalWidget::layoutPages() {
	GtkAllocation alloc = { 0 };

	gtk_widget_get_allocation(widget, &alloc);

	bool showTwoPages = getControl()->getSettings()->isShowTwoPages();

	if (showTwoPages) {
		int width = alloc.width;
		int height = XOURNAL_PADDING_TOP_LEFT;

		// TODO: handle single landscape page better
		// If there is a landscape page, display them on a single line, not with another page

		// calc size for the widget
		for (int i = 0; i < viewPagesLen; i++) {
			int w = viewPages[i]->getDisplayWidth() + XOURNAL_PADDING_TOP_LEFT + XOURNAL_PADDING;
			int h = viewPages[i]->getDisplayHeight();
			if (i < viewPagesLen - 1) {

				i++;
				w += viewPages[i]->getDisplayWidth();
				w += XOURNAL_PADDING;
				h = MAX(h, viewPages[i]->getDisplayHeight());
			}
			if (width < w) {
				width = w;
			}
			height += h;
			height += XOURNAL_PADDING;
		}

		// layout pages
		int y = XOURNAL_PADDING_TOP_LEFT;
		for (int i = 0; i < viewPagesLen; i++) {
			int x = 0;
			int h = viewPages[i]->getDisplayHeight();
			if (i < viewPagesLen - 1) {
				x = width - viewPages[i]->getDisplayWidth() - viewPages[i + 1]->getDisplayWidth() - XOURNAL_PADDING
						- +XOURNAL_PADDING_TOP_LEFT;
				x /= 2;

				gtk_layout_move(GTK_LAYOUT(widget), viewPages[i]->getWidget(), x, y);

				x += viewPages[i]->getDisplayWidth() + XOURNAL_PADDING;

				i++;

				h = MAX(h, viewPages[i]->getDisplayHeight());
			} else {
				x = width - viewPages[i]->getDisplayWidth();
				x /= 2;
			}

			gtk_layout_move(GTK_LAYOUT(widget), viewPages[i]->getWidget(), x, y);
			y += h;
			y += XOURNAL_PADDING;
		}

		gtk_layout_set_size(GTK_LAYOUT(widget), width, height);
		this->lastWidgetSize = width;
	} else {
		int width = alloc.width;
		int height = XOURNAL_PADDING_TOP_LEFT;

		// calc size for the widget
		for (int i = 0; i < viewPagesLen; i++) {
			PageView * pageView = viewPages[i];
			int w = pageView->getDisplayWidth();
			if (width < w) {
				width = w;
			}
			height += pageView->getDisplayHeight();
			height += XOURNAL_PADDING;
		}
		gtk_layout_set_size(GTK_LAYOUT(widget), width, height);
		this->lastWidgetSize = width;

		// layout pages
		int y = XOURNAL_PADDING_TOP_LEFT;
		int x = XOURNAL_PADDING_TOP_LEFT;
		for (int i = 0; i < viewPagesLen; i++) {
			PageView * pageView = viewPages[i];

			x = width - pageView->getDisplayWidth();
			x /= 2;

			gtk_layout_move(GTK_LAYOUT(widget), pageView->getWidget(), x, y);
			y += pageView->getDisplayHeight();
			y += XOURNAL_PADDING;
		}
	}

	// Need to redraw shadows and borders
	g_idle_add((GSourceFunc) widgetRepaintCallback, widget);
}

bool XournalWidget::widgetRepaintCallback(GtkWidget * widget) {
	gtk_widget_queue_draw(widget);
	return false;
}

void XournalWidget::updateBackground() {
	if (GDK_IS_WINDOW(GTK_LAYOUT(widget)->bin_window)) {
		gdk_window_set_background(GTK_LAYOUT(widget)->bin_window, &widget->style->dark[GTK_STATE_NORMAL]);
		gtk_widget_queue_draw(GTK_WIDGET(widget));
	}
}

void XournalWidget::documentChanged(DocumentChangeType type) {
	if (type != DOCUMENT_CHANGE_CLEARED && type != DOCUMENT_CHANGE_COMPLETE) {
		return;
	}
	for (int i = 0; i < viewPagesLen; i++) {
		delete viewPages[i];
	}
	delete[] viewPages;

	updateBackground();

	Document * doc = control->getDocument();

	viewPagesLen = doc->getPageCount();
	viewPages = new PageView*[viewPagesLen];

	for (int i = 0; i < viewPagesLen; i++) {
		PageView * pageView = new PageView(this, doc->getPage(i));
		viewPages[i] = pageView;
		gtk_layout_put(GTK_LAYOUT(widget), pageView->getWidget(), 0, 0);
	}

	layoutPages();
}

bool XournalWidget::cut() {
	int p = getCurrentPage();
	if (p < 0 || p >= viewPagesLen) {
		return false;
	}

	PageView * page = viewPages[p];
	return page->cut();
}

bool XournalWidget::copy() {
	int p = getCurrentPage();
	if (p < 0 || p >= viewPagesLen) {
		return false;
	}

	PageView * page = viewPages[p];
	return page->copy();
}

bool XournalWidget::paste() {
	int p = getCurrentPage();
	if (p < 0 || p >= viewPagesLen) {
		return false;
	}

	PageView * page = viewPages[p];
	return page->paste();
}

bool XournalWidget::actionDelete() {
	int p = getCurrentPage();
	if (p < 0 || p >= viewPagesLen) {
		return false;
	}

	PageView * page = viewPages[p];
	return page->actionDelete();
}

Document * XournalWidget::getDocument() {
	return control->getDocument();
}

ArrayIterator<PageView *> XournalWidget::pageViewIterator() {
	return ArrayIterator<PageView *> (viewPages, viewPagesLen);
}

static gboolean onScrolledwindowMainScrollEvent(GtkWidget *widget, GdkEventScroll *event, XournalWidget * xournal) {
	guint state;

	state = event->state & gtk_accelerator_get_default_mod_mask();

	if (state == GDK_CONTROL_MASK) {
		if (event->direction == GDK_SCROLL_UP || event->direction == GDK_SCROLL_LEFT) {
			xournal->zoomIn();
		} else {
			xournal->zoomOut();
		}

		return true;
	}

	// Shift+Wheel scrolls the in the perpendicular direction
	if (state & GDK_SHIFT_MASK) {
		if (event->direction == GDK_SCROLL_UP) {
			event->direction = GDK_SCROLL_LEFT;
		} else if (event->direction == GDK_SCROLL_LEFT) {
			event->direction = GDK_SCROLL_UP;
		} else if (event->direction == GDK_SCROLL_DOWN) {
			event->direction = GDK_SCROLL_RIGHT;
		} else if (event->direction == GDK_SCROLL_RIGHT) {
			event->direction = GDK_SCROLL_DOWN;
		}

		event->state &= ~GDK_SHIFT_MASK;
		state &= ~GDK_SHIFT_MASK;
	}

	return false;
}