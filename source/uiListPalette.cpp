/*
 * Copyright (c) 2009-2012, Albertas Vyšniauskas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *     * Neither the name of the software author nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "uiListPalette.h"
#include "uiUtilities.h"
#include "gtk/ColorCell.h"
#include "ColorSource.h"
#include "DragDrop.h"
#include "GlobalState.h"
#include "uiApp.h"
#include "GlobalStateStruct.h"
#include "DynvHelpers.h"
#include "Vector2.h"
#include "Internationalisation.h"

#include <boost/format.hpp>

#include <sstream>
#include <iostream>
#include <iomanip>

using namespace math;
using namespace std;

typedef struct ListPaletteArgs{
	ColorSource source;
	GtkWidget *treeview;
  gint scroll_timeout;

	Vec2<int> last_click_position;
	bool disable_selection;
	GtkWidget* count_label;
	GlobalState* gs;
}ListPaletteArgs;

static void destroy_arguments(gpointer data);
static struct ColorObject* get_color_object(struct DragDrop* dd);
static struct ColorObject** get_color_object_list(struct DragDrop* dd, uint32_t *colorobject_n);

#define SCROLL_EDGE_SIZE 15 //SCROLL_EDGE_SIZE from gtktreeview.c

static void add_scroll_timeout(ListPaletteArgs *args);
static void remove_scroll_timeout(ListPaletteArgs *args);
static gboolean scroll_row_timeout(ListPaletteArgs *args);
static void palette_list_vertical_autoscroll(GtkTreeView *treeview);

static void update_counts(ListPaletteArgs *args);

static gboolean scroll_row_timeout(ListPaletteArgs *args){
  palette_list_vertical_autoscroll(GTK_TREE_VIEW(args->treeview));
	return true;
}

static void add_scroll_timeout(ListPaletteArgs *args){
  if (!args->scroll_timeout){
		args->scroll_timeout = gdk_threads_add_timeout(150, (GSourceFunc)scroll_row_timeout, args);
	}
}

static void remove_scroll_timeout(ListPaletteArgs *args){
  if (args->scroll_timeout){
		g_source_remove(args->scroll_timeout);
		args->scroll_timeout = 0;
	}
}

static bool drag_end(struct DragDrop* dd, GtkWidget *widget, GdkDragContext *context){
	remove_scroll_timeout((ListPaletteArgs*)dd->userdata);
	update_counts((ListPaletteArgs*)dd->userdata);
	return true;
}

typedef struct SelectionBoundsArgs{
	int min_index;
	int max_index;
	int last_index;
	bool discontinuous;
} SelectionBoundsArgs;


static void find_selection_bounds(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, gpointer data){
	gint *indices = gtk_tree_path_get_indices(path);
	SelectionBoundsArgs *args = (SelectionBoundsArgs *) data;
	int index = indices[0]; // currently indices are all 1d.
	int diff = index - args->last_index;
	if ((args->last_index != 0x7fffffff) && (diff != 1) && (diff != -1)){
		args->discontinuous = true;
	}
	if (index > args->max_index){
		args->max_index = index;
	}
	if (index < args->min_index){
		args->min_index = index;
	}
	args->last_index = index;
}

static void update_counts(ListPaletteArgs *args){
	stringstream s;
	GtkTreeSelection *sel;
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(args->treeview));
	SelectionBoundsArgs bounds;
	int selected_count;
	int total_colors;

	if (!args->count_label){
		return;
	}

	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(args->treeview));
	selected_count = gtk_tree_selection_count_selected_rows(sel);
	total_colors = gtk_tree_model_iter_n_children(model, NULL);

	bounds.discontinuous = false;
	bounds.min_index = 0x7fffffff;
	bounds.last_index = 0x7fffffff;
	bounds.max_index = 0;
	if (selected_count > 0){
		s.str("");
		s << "#";
		gtk_tree_selection_selected_foreach(sel, &find_selection_bounds, &bounds);
		if (bounds.min_index < bounds.max_index){
			s << bounds.min_index;
			if (bounds.discontinuous){
				s << "..";
			}else{
				s << "-";
			}
			s << bounds.max_index;
		}else{
			s << bounds.min_index;
		}

#ifdef ENABLE_NLS
		s << " (" << boost::format(ngettext("%d color", "%d colors", selected_count)) % selected_count << ")";
#else
		s << " (" << ((selected_count == 1) ? "color" : "colors") << ")";
#endif
		s << " " << _("selected") << ". ";
	}

#ifdef ENABLE_NLS
	s << boost::format(ngettext("Total %d color", "Total %d colors", total_colors)) % total_colors;
#else
	s << "Total " << total_colors << " colors.";
#endif

	gtk_label_set_text(GTK_LABEL(args->count_label), s.str().c_str());
}

static void palette_list_vertical_autoscroll(GtkTreeView *treeview){
	GdkRectangle visible_rect;
	gint y;
	gint offset;

	gdk_window_get_pointer(gtk_tree_view_get_bin_window(treeview), NULL, &y, NULL);
	gint dy;
	gtk_tree_view_convert_bin_window_to_tree_coords(treeview, 0, 0, 0, &dy);
	y += dy;

	gtk_tree_view_get_visible_rect(treeview, &visible_rect);

	offset = y - (visible_rect.y + 2 * SCROLL_EDGE_SIZE);
	if (offset > 0) {
		offset = y - (visible_rect.y + visible_rect.height - 2 * SCROLL_EDGE_SIZE);
		if (offset < 0)
			return;
	}

	GtkAdjustment *adjustment = gtk_tree_view_get_vadjustment(treeview);
	gtk_adjustment_set_value(adjustment, min(max(gtk_adjustment_get_value(adjustment) + offset, 0.0), gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size (adjustment)));
}


static void palette_list_entry_fill(GtkListStore* store, GtkTreeIter *iter, struct ColorObject* color_object, ListPaletteArgs* args){
	Color color;
	color_object_get_color(color_object, &color);
	gchar* text = main_get_color_text(args->gs, &color, COLOR_TEXT_TYPE_COLOR_LIST);

	const char* name = dynv_get_string_wd(color_object->params, "name", "");

	gtk_list_store_set(store, iter, 0, color_object_ref(color_object), 1, text, 2, name, -1);

	if (text) g_free(text);
}

static void palette_list_entry_update_row(GtkListStore* store, GtkTreeIter *iter, struct ColorObject* color_object, ListPaletteArgs* args){
	Color color;
	color_object_get_color(color_object, &color);
	gchar* text = main_get_color_text(args->gs, &color, COLOR_TEXT_TYPE_COLOR_LIST);
	const char* name = dynv_get_string_wd(color_object->params, "name", "");
	gtk_list_store_set(store, iter, 1, text, 2, name, -1);
	if (text) g_free(text);
}

static void palette_list_entry_update_name(GtkListStore* store, GtkTreeIter *iter, struct ColorObject* color_object){
	const char* name = dynv_get_string_wd(color_object->params, "name", "");
	gtk_list_store_set(store, iter, 2, name, -1);
}

static void palette_list_cell_edited(GtkCellRendererText *cell, gchar *path, gchar *new_text, gpointer user_data) {
	GtkTreeIter iter;
	GtkTreeModel *model=GTK_TREE_MODEL(user_data);

	gtk_tree_model_get_iter_from_string(model, &iter, path );

	gtk_list_store_set(GTK_LIST_STORE(model), &iter,
		2, new_text,
	-1);

	struct ColorObject *color_object;
	gtk_tree_model_get(model, &iter, 0, &color_object, -1);
	dynv_set_string(color_object->params, "name", new_text);
}

static void palette_list_row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column, gpointer user_data) {
	ListPaletteArgs* args = (ListPaletteArgs*)user_data;

	GtkTreeModel* model;
	GtkTreeIter iter;

	model=gtk_tree_view_get_model(tree_view);
	gtk_tree_model_get_iter(model, &iter, path);

	struct ColorObject *color_object;
	gtk_tree_model_get(model, &iter, 0, &color_object, -1);

	ColorSource *color_source = (ColorSource*)dynv_get_pointer_wd(args->gs->params, "CurrentColorSource", 0);
	color_source_set_color(color_source, color_object);
	update_counts(args);
}

static int palette_list_preview_on_insert(struct ColorList* color_list, struct ColorObject* color_object){
	palette_list_add_entry(GTK_WIDGET(color_list->userdata), color_object);
	return 0;
}

static int palette_list_preview_on_clear(struct ColorList* color_list){
	palette_list_remove_all_entries(GTK_WIDGET(color_list->userdata));
	return 0;
}

static void destroy_cb(GtkWidget* widget, ListPaletteArgs *args){
	remove_scroll_timeout(args);
	palette_list_remove_all_entries(widget);
}

GtkWidget* palette_list_get_widget(struct ColorList *color_list){
	return (GtkWidget*)color_list->userdata;
}

GtkWidget* palette_list_preview_new(GlobalState* gs, bool expander, bool expanded, struct ColorList* color_list, struct ColorList** out_color_list){

	ListPaletteArgs* args = new ListPaletteArgs;
	args->gs = gs;
	args->scroll_timeout = 0;
	args->count_label = NULL;

	GtkListStore  		*store;
	GtkCellRenderer     *renderer;
	GtkTreeViewColumn   *col;
	GtkWidget           *view;

	view = gtk_tree_view_new();
	args->treeview = view;

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), 0);

	store = gtk_list_store_new (3, G_TYPE_POINTER, G_TYPE_STRING, G_TYPE_STRING);

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(col,GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable(col, 0);
	renderer = custom_cell_renderer_color_new();
	custom_cell_renderer_color_set_size(renderer, 16, 16);
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "color", 0);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view), false);
	gtk_tree_view_set_model(GTK_TREE_VIEW(view), GTK_TREE_MODEL(store));
	g_object_unref(GTK_TREE_MODEL(store));

	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

	gtk_drag_source_set(view, GDK_BUTTON1_MASK, 0, 0, GdkDragAction(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_ASK));

	struct DragDrop dd;
	dragdrop_init(&dd, gs);

	dd.get_color_object = get_color_object;
	dd.get_color_object_list = get_color_object_list;
	dd.drag_end = drag_end;
	dd.handler_map = dynv_system_get_handler_map(gs->colors->params);
	dd.userdata = args;

	dragdrop_widget_attach(view, DragDropFlags(DRAGDROP_SOURCE), &dd);

	if (out_color_list) {
		struct dynvHandlerMap* handler_map = dynv_system_get_handler_map(color_list->params);
		struct ColorList* cl=color_list_new(handler_map);
		dynv_handler_map_release(handler_map);

		cl->userdata=view;
		cl->on_insert=palette_list_preview_on_insert;
		cl->on_clear=palette_list_preview_on_clear;
		*out_color_list=cl;

	}

	GtkWidget *scrolled_window;
	scrolled_window=gtk_scrolled_window_new (0,0);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scrolled_window), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(scrolled_window), view);

	GtkWidget *main_widget = scrolled_window;
	if (expander){
		GtkWidget *expander=gtk_expander_new(_("Preview"));
		gtk_container_add(GTK_CONTAINER(expander), scrolled_window);
		gtk_expander_set_expanded(GTK_EXPANDER(expander), expanded);

		main_widget = expander;
	}

	g_object_set_data_full(G_OBJECT(view), "arguments", args, destroy_arguments);
	g_signal_connect(G_OBJECT(view), "destroy", G_CALLBACK(destroy_cb), args);

	return main_widget;
}

static struct ColorObject** get_color_object_list(struct DragDrop* dd, uint32_t *colorobject_n){

	GtkTreeIter iter;
	GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dd->widget));
	GtkTreeModel* model;
	gint selected = gtk_tree_selection_count_selected_rows(selection);
	if (selected <= 1){
    if (colorobject_n) *colorobject_n = selected;
		return 0;
	}

	GList *list = gtk_tree_selection_get_selected_rows(selection, &model);

  struct ColorObject** color_objects = new struct ColorObject*[selected];
	if (colorobject_n) *colorobject_n = selected;

	if (list){
		GList *i = list;

		struct ColorObject* color_object;
		uint32_t j = 0;
		while (i) {
			gtk_tree_model_get_iter(model, &iter, (GtkTreePath*) (i->data));
			gtk_tree_model_get(model, &iter, 0, &color_object, -1);

			color_objects[j] = color_object_ref(color_object);

			i = g_list_next(i);
			j++;
		}

		g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
		g_list_free (list);
	}

	return color_objects;
}

static struct ColorObject* get_color_object(struct DragDrop* dd){

	GtkTreeIter iter;
	GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dd->widget));
	GtkTreeModel* model;
	GList *list = gtk_tree_selection_get_selected_rows ( selection, &model );

	if (list){
		GList *i = list;

		struct ColorObject* color_object;
		while (i) {
			gtk_tree_model_get_iter(model, &iter, (GtkTreePath*) (i->data));
			gtk_tree_model_get(model, &iter, 0, &color_object, -1);

			g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
			g_list_free (list);
			return color_object_ref(color_object);

			i = g_list_next(i);
		}

		g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
		g_list_free (list);
	}

	return 0;
}

static int set_color_object_list_at(struct DragDrop* dd, struct ColorObject** colorobjects, uint32_t colorobject_n, int x, int y, bool move){
	ListPaletteArgs* args = (ListPaletteArgs*)dd->userdata;
	remove_scroll_timeout(args);

	GtkTreePath* path;
	GtkTreeViewDropPosition pos;
	GtkTreeIter iter, iter2;

	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(dd->widget));
  bool copy = false;
	bool path_is_valid = false;

	if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(dd->widget), x, y, &path, &pos)){
		gtk_tree_model_get_iter(model, &iter, path);

		if (pos==GTK_TREE_VIEW_DROP_BEFORE || pos==GTK_TREE_VIEW_DROP_INTO_OR_BEFORE){
			path_is_valid = true;
		}else if (pos==GTK_TREE_VIEW_DROP_AFTER || pos==GTK_TREE_VIEW_DROP_INTO_OR_AFTER){
			path_is_valid = true;
		}else{
			return -1;
		}
	}

  for (uint32_t i = 0; i != colorobject_n; i++){
		struct ColorObject *colorobject = 0;
		if (pos==GTK_TREE_VIEW_DROP_BEFORE || pos==GTK_TREE_VIEW_DROP_INTO_OR_BEFORE){
			colorobject = colorobjects[i];
		}else if (pos==GTK_TREE_VIEW_DROP_AFTER || pos==GTK_TREE_VIEW_DROP_INTO_OR_AFTER){
			colorobject = colorobjects[colorobject_n - i - 1];
		}else{
			colorobject = colorobjects[i];
		}

		if (move){
			struct ColorObject *reference_color_object;
			gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &reference_color_object, -1);
			if (reference_color_object == colorobject){
				// Reference item is going to be removed, so any further inserts
				// will fail if the same iterator is used. Iterator is moved forward
				// to avoid that.
				if (pos==GTK_TREE_VIEW_DROP_BEFORE || pos==GTK_TREE_VIEW_DROP_INTO_OR_BEFORE){
					if (!gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter)){
						path_is_valid = false;
					}
				}else if (pos==GTK_TREE_VIEW_DROP_AFTER || pos==GTK_TREE_VIEW_DROP_INTO_OR_AFTER){
					GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(model), &iter);
					if (gtk_tree_path_prev(path)) {
						if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, path)){
							path_is_valid = false;
						}
					}else{
						path_is_valid = false;
					}
					gtk_tree_path_free(path);
				}
			}

			if (colorobject->refcnt != 1){	//only one reference, can't be in palette
				color_list_remove_color_object(args->gs->colors, colorobject);
			}
		}else{
			colorobject = color_object_copy(colorobject);
			copy = true;
		}

		if (path_is_valid){

			if (pos==GTK_TREE_VIEW_DROP_BEFORE || pos==GTK_TREE_VIEW_DROP_INTO_OR_BEFORE){
				gtk_list_store_insert_before(GTK_LIST_STORE(model), &iter2, &iter);
				palette_list_entry_fill(GTK_LIST_STORE(model), &iter2, colorobject, args);
				color_list_add_color_object(args->gs->colors, colorobject, false);
			}else if (pos==GTK_TREE_VIEW_DROP_AFTER || pos==GTK_TREE_VIEW_DROP_INTO_OR_AFTER){
				gtk_list_store_insert_after(GTK_LIST_STORE(model), &iter2, &iter);
				palette_list_entry_fill(GTK_LIST_STORE(model), &iter2, colorobject, args);
				color_list_add_color_object(args->gs->colors, colorobject, false);
			}else{
				if (copy) color_object_release(colorobject);
				return -1;
			}

		}else{
			color_list_add_color_object(args->gs->colors, colorobject, true);
			if (copy) color_object_release(colorobject);
		}
	}
	update_counts(args);
	return 0;
}

static int set_color_object_at(struct DragDrop* dd, struct ColorObject* colorobject, int x, int y, bool move){
	ListPaletteArgs* args = (ListPaletteArgs*)dd->userdata;

	remove_scroll_timeout((ListPaletteArgs*)dd->userdata);

	GtkTreePath* path;
	GtkTreeViewDropPosition pos;
	GtkTreeIter iter, iter2;

	GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(dd->widget));
  bool copy = false;

	if (move){
		if (colorobject->refcnt != 1){	//only one reference, can't be in palette
			color_list_remove_color_object(args->gs->colors, colorobject);
		}
	}else{
		colorobject = color_object_copy(colorobject);
		copy = true;
	}

	if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(dd->widget), x, y, &path, &pos)){
		gtk_tree_model_get_iter(model, &iter, path);

		GdkModifierType mask;
		gdk_window_get_pointer(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(dd->widget)), NULL, NULL, &mask);

		if ((mask & GDK_CONTROL_MASK) && (pos == GTK_TREE_VIEW_DROP_INTO_OR_AFTER || pos == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE)){
			Color color;
			color_object_get_color(colorobject, &color);

			struct ColorObject* original_color_object;
			gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, 0, &original_color_object, -1);

			color_object_set_color(original_color_object, &color);
			palette_list_entry_update_row(GTK_LIST_STORE(model), &iter, original_color_object, args);

		}else if (pos==GTK_TREE_VIEW_DROP_BEFORE || pos==GTK_TREE_VIEW_DROP_INTO_OR_BEFORE){
			gtk_list_store_insert_before(GTK_LIST_STORE(model), &iter2, &iter);
			palette_list_entry_fill(GTK_LIST_STORE(model), &iter2, colorobject, args);
			color_list_add_color_object(args->gs->colors, colorobject, false);
		}else if (pos==GTK_TREE_VIEW_DROP_AFTER || pos==GTK_TREE_VIEW_DROP_INTO_OR_AFTER){
			gtk_list_store_insert_after(GTK_LIST_STORE(model), &iter2, &iter);
			palette_list_entry_fill(GTK_LIST_STORE(model), &iter2, colorobject, args);
			color_list_add_color_object(args->gs->colors, colorobject, false);
		}else{
			if (copy) color_object_release(colorobject);
			update_counts(args);
			return -1;
		}

		if (copy) color_object_release(colorobject);
	}else{
		color_list_add_color_object(args->gs->colors, colorobject, true);
		update_counts(args);
		if (copy) color_object_release(colorobject);
	}
	update_counts(args);
	return 0;
}

static bool test_at(struct DragDrop* dd, int x, int y){

	GtkTreePath* path;
	GtkTreeViewDropPosition pos;

	if (gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(dd->widget), x, y, &path, &pos)){
		GdkModifierType mask;
		gdk_window_get_pointer(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(dd->widget)), NULL, NULL, &mask);

		if ((mask & GDK_CONTROL_MASK) && (pos == GTK_TREE_VIEW_DROP_INTO_OR_AFTER || pos == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE)){
			pos = GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
		}else	if (pos == GTK_TREE_VIEW_DROP_BEFORE || pos == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE){
			pos = GTK_TREE_VIEW_DROP_BEFORE;
		}else if (pos==GTK_TREE_VIEW_DROP_AFTER || pos == GTK_TREE_VIEW_DROP_INTO_OR_AFTER){
			pos = GTK_TREE_VIEW_DROP_AFTER;
		}

		gtk_tree_view_set_drag_dest_row(GTK_TREE_VIEW(dd->widget), path, pos);
		add_scroll_timeout((ListPaletteArgs*)dd->userdata);
	}else{
		gtk_tree_view_unset_rows_drag_dest(GTK_TREE_VIEW(dd->widget));
	}

	return true;
}

static void destroy_arguments(gpointer data){
	ListPaletteArgs* args = (ListPaletteArgs*)data;
	delete args;
}

static gboolean disable_palette_selection_function(GtkTreeSelection *selection, GtkTreeModel *model, GtkTreePath *path, gboolean path_currently_selected, ListPaletteArgs *args) {
  return args->disable_selection;
}

static void disable_palette_selection(GtkWidget *widget, gboolean disable, int x, int y, ListPaletteArgs *args) {
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
	args->disable_selection = disable;
	gtk_tree_selection_set_select_function(selection, (GtkTreeSelectionFunc)disable_palette_selection_function, args, NULL);

  args->last_click_position.x = x;
  args->last_click_position.y = y;
}

static gboolean on_palette_button_press(GtkWidget *widget, GdkEventButton *event, ListPaletteArgs *args) {
	disable_palette_selection(widget, true, -1, -1, args);

	if (event->button != 1)
		return false;
	if (event->state & (GDK_SHIFT_MASK|GDK_CONTROL_MASK))
		return false;

	GtkTreePath *path = NULL;
	if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
				event->x, event->y,
				&path,
				NULL,
				NULL, NULL))
		return false;

	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
	if (gtk_tree_selection_path_is_selected(selection, path)) {
		disable_palette_selection(widget, false, event->x, event->y, args);
	}
	if (path)
		gtk_tree_path_free(path);
	update_counts((ListPaletteArgs*)args);
	return false;
}

static gboolean on_palette_button_release(GtkWidget *widget, GdkEventButton *event, ListPaletteArgs *args) {
	if (args->last_click_position != Vec2<int>(-1, -1)) {
		Vec2<int> click_pos = args->last_click_position;
		disable_palette_selection(widget, true, -1, -1, args);
		if (click_pos.x == event->x && click_pos.y == event->y) {
			GtkTreePath *path = NULL;
			GtkTreeViewColumn *column;
			if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y, &path, &column, NULL, NULL)) {
				gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, column, FALSE);
			}
			if (path)
				gtk_tree_path_free(path);
		}
	}
	update_counts((ListPaletteArgs*)args);
	return false;
}

static void on_palette_cursor_changed(GtkTreeView *treeview, ListPaletteArgs *args){
	update_counts(args);
}

static gboolean on_palette_select_all(GtkTreeView *treeview, ListPaletteArgs *args){
	update_counts(args);
	return FALSE;
}

static gboolean on_palette_unselect_all(GtkTreeView *treeview, ListPaletteArgs *args){
	update_counts(args);
	return FALSE;
}

GtkWidget* palette_list_new(GlobalState* gs, GtkWidget* count_label){

	ListPaletteArgs* args = new ListPaletteArgs;
	args->gs = gs;
	args->count_label = count_label;
	args->scroll_timeout = 0;

	GtkListStore  		*store;
	GtkCellRenderer     *renderer;
	GtkTreeViewColumn   *col;
	GtkWidget           *view;

	view = gtk_tree_view_new ();
	args->treeview = view;

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), 1);

	store = gtk_list_store_new (3, G_TYPE_POINTER, G_TYPE_STRING, G_TYPE_STRING);

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(col,GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable(col,1);
	gtk_tree_view_column_set_title(col, _("Color"));
	renderer = custom_cell_renderer_color_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "color", 0);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(col,GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable(col,1);
	gtk_tree_view_column_set_title(col, _("Color"));
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "text", 1);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

	col = gtk_tree_view_column_new();
	gtk_tree_view_column_set_sizing(col,GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable(col,1);
	gtk_tree_view_column_set_title(col, _("Name"));
	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_add_attribute(col, renderer, "text", 2);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);
	g_object_set(renderer, "editable", TRUE, NULL);
	g_signal_connect(renderer, "edited", (GCallback) palette_list_cell_edited, store);

	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view), false);
	gtk_tree_view_set_model(GTK_TREE_VIEW(view), GTK_TREE_MODEL(store));
	g_object_unref(GTK_TREE_MODEL(store));

	GtkTreeSelection *selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW(view) );

	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

	g_signal_connect(G_OBJECT(view), "row-activated", G_CALLBACK(palette_list_row_activated), args);
	g_signal_connect(G_OBJECT(view), "button-press-event", G_CALLBACK(on_palette_button_press), args);
	g_signal_connect(G_OBJECT(view), "button-release-event", G_CALLBACK(on_palette_button_release), args);
	g_signal_connect(G_OBJECT(view), "cursor-changed", G_CALLBACK(on_palette_cursor_changed), args);
	g_signal_connect_after(G_OBJECT(view), "select-all", G_CALLBACK(on_palette_select_all), args);
	g_signal_connect_after(G_OBJECT(view), "unselect-all", G_CALLBACK(on_palette_unselect_all), args);

	///gtk_tree_view_set_reorderable(GTK_TREE_VIEW (view), TRUE);
	gtk_drag_dest_set( view, GtkDestDefaults(GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT), 0, 0, GdkDragAction(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_ASK));
	gtk_drag_source_set( view, GDK_BUTTON1_MASK, 0, 0, GdkDragAction(GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_ASK));

	struct DragDrop dd;
	dragdrop_init(&dd, gs);

	dd.get_color_object = get_color_object;
	dd.get_color_object_list = get_color_object_list;
	dd.set_color_object_at = set_color_object_at;
	dd.set_color_object_list_at = set_color_object_list_at;
	dd.handler_map = dynv_system_get_handler_map(gs->colors->params);
	dd.test_at = test_at;
	dd.drag_end = drag_end;
	dd.userdata = args;

	dragdrop_widget_attach(view, DragDropFlags(DRAGDROP_SOURCE | DRAGDROP_DESTINATION), &dd);

	g_object_set_data_full(G_OBJECT(view), "arguments", args, destroy_arguments);
	g_signal_connect(G_OBJECT(view), "destroy", G_CALLBACK(destroy_cb), args);

	if (count_label){
		update_counts(args);
	}

	return view;
}

void palette_list_remove_all_entries(GtkWidget* widget) {
	ListPaletteArgs* args = (ListPaletteArgs*)g_object_get_data(G_OBJECT(widget), "arguments");
	GtkTreeIter iter;
	GtkListStore *store;
	gboolean valid;

	store=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

	while (valid){
		struct ColorObject* c;
		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &c, -1);
		color_object_release(c);
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
	}

	gtk_list_store_clear(GTK_LIST_STORE(store));

	update_counts(args);
}

gint32 palette_list_get_selected_count(GtkWidget* widget) {
	return gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));
}

gint32 palette_list_get_count(GtkWidget* widget){
	GtkTreeModel *store;
	store=gtk_tree_view_get_model(GTK_TREE_VIEW(widget));
	return gtk_tree_model_iter_n_children(store, NULL);
}

gint32 palette_list_get_selected_color(GtkWidget* widget, Color* color) {
	GtkTreeSelection *selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW(widget) );
	GtkListStore *store;
	GtkTreeIter iter;

	if (gtk_tree_selection_count_selected_rows(selection)!=1){
		return -1;
	}

	store=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));

	GList *list = gtk_tree_selection_get_selected_rows ( selection, 0 );
	GList *i=list;

	struct ColorObject* c;
	while (i) {
		gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, (GtkTreePath*)i->data);

		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &c, -1);
		color_object_get_color(c, color);
		break;

		i = g_list_next(i);
	}

	g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free (list);

	return 0;
}

void palette_list_remove_selected_entries(GtkWidget* widget) {
	ListPaletteArgs* args = (ListPaletteArgs*)g_object_get_data(G_OBJECT(widget), "arguments");
	GtkTreeIter iter;
	GtkListStore *store;
	gboolean valid;

	store=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

	struct ColorObject* color_object;

	while (valid){
		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &color_object, -1);
		if (color_object->selected){
			valid = gtk_list_store_remove(GTK_LIST_STORE(store), &iter);
			color_object_release(color_object);
		}else{
			valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
		}
	}

	update_counts(args);
}

void palette_list_add_entry(GtkWidget* widget, struct ColorObject* color_object){
	ListPaletteArgs* args = (ListPaletteArgs*)g_object_get_data(G_OBJECT(widget), "arguments");

	GtkTreeIter iter1;
	GtkListStore *store;

	store=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));

	gtk_list_store_append(store, &iter1);
	palette_list_entry_fill(store, &iter1, color_object, args);
	update_counts(args);
}

int palette_list_remove_entry(GtkWidget* widget, struct ColorObject* r_color_object){
	ListPaletteArgs* args = (ListPaletteArgs*)g_object_get_data(G_OBJECT(widget), "arguments");
	GtkTreeIter iter;
	GtkListStore *store;
	gboolean valid;

	store=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

	struct ColorObject* color_object;

	while (valid){
		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, 0, &color_object, -1);
		if (color_object == r_color_object){
			valid = gtk_list_store_remove(GTK_LIST_STORE(store), &iter);
			color_object_release(color_object);
			return 0;
		}
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
	}
	update_counts(args);
	return -1;
}

static void execute_callback(GtkListStore *store, GtkTreeIter *iter, ListPaletteArgs* args, PaletteListCallback callback, void *userdata){

	struct ColorObject* color_object;
	gtk_tree_model_get(GTK_TREE_MODEL(store), iter, 0, &color_object, -1);

	PaletteListCallbackReturn r = callback(color_object, userdata);
	switch (r){
		case PALETTE_LIST_CALLBACK_UPDATE_NAME:
			palette_list_entry_update_name(store, iter, color_object);
			break;
		case PALETTE_LIST_CALLBACK_UPDATE_ROW:
			palette_list_entry_update_row(store, iter, color_object, args);
			break;
		case PALETTE_LIST_CALLBACK_NO_UPDATE:
			break;
	}
}

static void execute_replace_callback(GtkListStore *store, GtkTreeIter *iter, ListPaletteArgs* args, PaletteListReplaceCallback callback, void *userdata){

	struct ColorObject *color_object, *orig_color_object;
	gtk_tree_model_get(GTK_TREE_MODEL(store), iter, 0, &color_object, -1);
	orig_color_object = color_object;

	color_object_ref(color_object);
	PaletteListCallbackReturn r = callback(&color_object, userdata);
	if (color_object != orig_color_object){
		gtk_list_store_set(store, iter, 0, color_object, -1);
	}
	switch (r){
		case PALETTE_LIST_CALLBACK_UPDATE_NAME:
			palette_list_entry_update_name(store, iter, color_object);
			break;
		case PALETTE_LIST_CALLBACK_UPDATE_ROW:
			palette_list_entry_update_row(store, iter, color_object, args);
			break;
		case PALETTE_LIST_CALLBACK_NO_UPDATE:
			break;
	}
	color_object_release(color_object);
}

gint32 palette_list_foreach(GtkWidget* widget, PaletteListCallback callback, void *userdata){
	ListPaletteArgs* args = (ListPaletteArgs*)g_object_get_data(G_OBJECT(widget), "arguments");
	GtkTreeIter iter;
	GtkListStore *store;
	gboolean valid;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));
	valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

	struct ColorObject* color_object;

	while (valid){
		execute_callback(store, &iter, args, callback, userdata);
		valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
	}
	return 0;
}


gint32 palette_list_foreach_selected(GtkWidget* widget, PaletteListCallback callback, void *userdata){
	ListPaletteArgs* args = (ListPaletteArgs*)g_object_get_data(G_OBJECT(widget), "arguments");
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
	GtkListStore *store;
	GtkTreeIter iter;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));

	GList *list = gtk_tree_selection_get_selected_rows(selection, 0);
	GList *i = list;

	while (i) {
		gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, (GtkTreePath*) (i->data));
		execute_callback(store, &iter, args, callback, userdata);
		i = g_list_next(i);
	}

	g_list_foreach(list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(list);
	return 0;
}

gint32 palette_list_foreach_selected(GtkWidget* widget, PaletteListReplaceCallback callback, void *userdata){
	ListPaletteArgs* args = (ListPaletteArgs*)g_object_get_data(G_OBJECT(widget), "arguments");
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
	GtkListStore *store;
	GtkTreeIter iter;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));

	GList *list = gtk_tree_selection_get_selected_rows(selection, 0);
	GList *i = list;

	while (i) {
		gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, (GtkTreePath*) (i->data));
		execute_replace_callback(store, &iter, args, callback, userdata);
		i = g_list_next(i);
	}

	g_list_foreach(list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(list);
	return 0;
}

gint32 palette_list_forfirst_selected(GtkWidget* widget, PaletteListCallback callback, void *userdata)
{
	ListPaletteArgs* args = (ListPaletteArgs*)g_object_get_data(G_OBJECT(widget), "arguments");
	GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
	GtkListStore *store;
	GtkTreeIter iter;

	store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(widget)));

	GList *list = gtk_tree_selection_get_selected_rows(selection, 0);
	GList *i = list;

	if (i) {
		gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &iter, (GtkTreePath*) (i->data));
		execute_callback(store, &iter, args, callback, userdata);
	}

	g_list_foreach(list, (GFunc)gtk_tree_path_free, NULL);
	g_list_free(list);
	return 0;
}

