/* -*- mode: C; c-basic-offset: 4;  -*- */

/*
 * Copyright (C) 2006-2010 by Hannu Jokinen
 * Full copyright text is included in the software package.
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "sqlite3.h"
#include "judoshiai.h"

struct treedata {
    GtkTreeStore  *treestore;
    GtkTreeIter    toplevel, child;
};

struct model_iter {
    GtkTreeModel *model;
    GtkTreeIter   iter;
};

#define NUM_JUDOKAS 100
#define NUM_WCLASSES 100
#define NEW_JUDOKA 0x7ffffff1
#define NEW_WCLASS 0x7ffffff2

char *belts[] = {
    "?", "6.kyu", "5.kyu", "4.kyu", "3.kyu", "2.kyu", "1.kyu",
    "1.dan", "2.dan", "3.dan", "4.dan", "5.dan", "6.dan", "7.dan", 0
};

struct judoka_widget {
    gint       index;
    gboolean   visible;
    gchar     *category;
    GtkWidget *last;
    GtkWidget *first;
    GtkWidget *birthyear;
    GtkWidget *belt;
    GtkWidget *club;
    GtkWidget *regcategory;
    GtkWidget *weight;
    GtkWidget *ok;
    GtkWidget *seeding;
    GtkWidget *hansokumake;
    GtkWidget *country;
    GtkWidget *id;
    GtkWidget *system;
};

static gboolean      editing_ongoing = FALSE;
static GtkWidget     *competitor_label = NULL;

void db_update_judoka(int num, struct judoka *j);
void db_add_judoka(int num, struct judoka *j);
gint sort_iter_compare_func(GtkTreeModel *model,
                            GtkTreeIter  *a,
                            GtkTreeIter  *b,
                            gpointer      userdata);
void toolbar_sort(void);


static void judoka_edited_callback(GtkWidget *widget, 
				   GdkEvent *event,
				   gpointer data)
{
    gint ret;
    gint event_id = (gint)event;
    struct judoka edited;
    struct judoka_widget *judoka_tmp = data;
    gint ix = judoka_tmp->index;
    struct category_data *catdata = avl_get_category(ix);
    gint system = catdata ? catdata->system : CAT_SYSTEM_DEFAULT << SYSTEM_WISH_SHIFT;

    memset(&edited, 0, sizeof(edited));
        
    edited.index    = judoka_tmp->index;
    edited.visible  = judoka_tmp->visible;
    edited.category = judoka_tmp->category;

    if (judoka_tmp->last)
        edited.last        = g_strdup(gtk_entry_get_text(GTK_ENTRY(judoka_tmp->last)));

    if (judoka_tmp->first)
        edited.first       = g_strdup(gtk_entry_get_text(GTK_ENTRY(judoka_tmp->first)));

    if (judoka_tmp->club)
        edited.club        = g_strdup(gtk_entry_get_text(GTK_ENTRY(judoka_tmp->club)));

    if (judoka_tmp->country)
        edited.country     = g_strdup(gtk_entry_get_text(GTK_ENTRY(judoka_tmp->country)));

    if (judoka_tmp->regcategory)
        edited.regcategory = g_strdup(gtk_entry_get_text(GTK_ENTRY(judoka_tmp->regcategory)));

    if (judoka_tmp->birthyear)
        edited.birthyear = atoi(gtk_entry_get_text(GTK_ENTRY(judoka_tmp->birthyear)));

    if (judoka_tmp->belt)
        edited.belt = gtk_combo_box_get_active(GTK_COMBO_BOX(judoka_tmp->belt));

    if (judoka_tmp->weight)
        edited.weight = weight_grams(gtk_entry_get_text(GTK_ENTRY(judoka_tmp->weight)));

    if (judoka_tmp->seeding)
        edited.deleted |= gtk_combo_box_get_active(GTK_COMBO_BOX(judoka_tmp->seeding)) << 2;

    if (judoka_tmp->hansokumake && 
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(judoka_tmp->hansokumake)))
        edited.deleted |= HANSOKUMAKE;

    if (judoka_tmp->id)
        edited.id = g_strdup(gtk_entry_get_text(GTK_ENTRY(judoka_tmp->id)));

    if (judoka_tmp->system)
	system = (system & ~SYSTEM_WISH_MASK) | 
	    (get_system_number_by_menu_pos(gtk_combo_box_get_active(GTK_COMBO_BOX(judoka_tmp->system)))
	     << SYSTEM_WISH_SHIFT);

    if (event_id != GTK_RESPONSE_OK || edited.last == NULL || 
        edited.last[0] == 0 || edited.last[0] == '?')
        goto out;

    if (!edited.visible) {
        /* Check if the category exists. */
        GtkTreeIter tmp_iter;
        if (find_iter_category(&tmp_iter, edited.last)) {
            struct judoka *g = get_data_by_iter(&tmp_iter);
            gint gix = g ? g->index : 0;
            free_judoka(g);

            if (ix != gix) {
                SHOW_MESSAGE("%s %s %s", _("Category"), edited.last, _("already exists!"));
                goto out;
            }
        }
    }

#if 0
    if (edited.visible && edited.birthyear > 20 && edited.birthyear <= 99)
        edited.birthyear += 1900;
    else if (edited.visible && edited.birthyear <= 20)
        edited.birthyear += 2000;
#endif

    complete_add_if_not_exist(club_completer, edited.club);

    if (ix == NEW_JUDOKA) {
        const gchar *lastname = edited.last;

        if (edited.first == NULL || edited.first[0] == 0)
            edited.first = g_strdup("?");

        const gchar *firstname = edited.first;
        gchar *letter = g_utf8_strup(firstname, 1);
		
        edited.first = g_strdup_printf("%s%s", letter, 
                                       g_utf8_next_char(firstname));
        g_free((void *)firstname);
        g_free((void *)letter);

        edited.last = g_utf8_strup(lastname, -1);
        g_free((void *)lastname);

        edited.index = current_index++;
                
        if ((edited.regcategory == NULL || edited.regcategory[0] == 0) &&
            edited.weight > 10000) {
            gint gender = find_gender(edited.first);
            gint age = current_year - edited.birthyear;

            if (edited.regcategory)
                g_free((gchar *)edited.regcategory);

            edited.regcategory = find_correct_category(age, edited.weight, gender, NULL, TRUE);
        }

        db_add_judoka(edited.index, &edited);
    } else if (ix == NEW_WCLASS){
        edited.index = 0;
    } else {
        edited.index = ix;
        if (edited.visible) {
            db_update_judoka(edited.index, &edited);
        } else {
            /* update displayed category for competitors */
            GtkTreeIter iter, comp;
            if (find_iter(&iter, ix)) {
                gboolean ok = gtk_tree_model_iter_children(current_model, &comp, &iter);
                while (ok) {
                    gtk_tree_store_set((GtkTreeStore *)current_model, 
                                       &comp,
                                       COL_CATEGORY, edited.last,
                                       -1);
                    ok = gtk_tree_model_iter_next(current_model, &comp);
                }
				
            }
			
            /* update database */
            db_update_category(edited.index, &edited);
	    db_set_system(edited.index, system);
            //XXX???? db_read_categories();
            //XXX???? db_read_judokas();
        }
    }

    ret = display_one_judoka(&edited);
    if (ix == NEW_WCLASS && ret >= 0) {
        edited.index = ret;
        db_add_category(ret, &edited);
    }

    //db_read_matches();
    if (edited.visible)
        update_competitors_categories(edited.index);

    //matches_refresh();

    if (!edited.visible) {
        category_refresh(edited.index);
        update_category_status_info_all();
    }

out:
    g_free((gpointer)edited.category);
    g_free((gpointer)edited.last);
    g_free((gpointer)edited.first);
    g_free((gpointer)edited.club);
    g_free((gpointer)edited.country);
    g_free((gpointer)edited.regcategory);
    g_free((gpointer)edited.id);
    g_free(data);

    editing_ongoing = FALSE;
    gtk_widget_destroy(widget);
}

static GtkWidget *set_entry(GtkWidget *table, int row, 
			    char *text, const char *deftxt)
{
    GtkWidget *tmp;

    tmp = gtk_label_new(text);
    gtk_table_attach_defaults(GTK_TABLE(table), tmp, 0, 1, row, row+1);
    tmp = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(tmp), 20);
    gtk_entry_set_text(GTK_ENTRY(tmp), deftxt ? deftxt : "");
    gtk_table_attach_defaults(GTK_TABLE(table), tmp, 1, 2, row, row+1);
    return tmp;
}

/*
 * Activations 
 */

void view_on_row_activated(GtkTreeView        *treeview,
                           GtkTreePath        *path,
                           GtkTreeViewColumn  *col,
                           gpointer            userdata)
{
    GtkTreeModel *model;
    GtkTreeIter   iter;
    int i;
    GtkWidget *dialog, *table, *tmp;
    gchar   *last = NULL, 
        *first = NULL, 
        *club = NULL, 
        *regcategory = NULL,
        *category = NULL,
	*country = NULL,
	*id = NULL;
    guint belt, index, birthyear;
    gint weight;
    gboolean visible;
    guint deleted;
    char weight_s[10], birthyear_s[10];
    GtkAccelGroup *accel_group;
    struct category_data *catdata = NULL;
    /*if (editing_ongoing)
      return;*/

    struct judoka_widget *judoka_tmp = g_malloc(sizeof(*judoka_tmp));
    memset(judoka_tmp, 0, sizeof(*judoka_tmp));

    if (treeview) {
        model = gtk_tree_view_get_model(treeview);
        if (gtk_tree_model_get_iter(model, &iter, path)) {
            gtk_tree_model_get(model, &iter,
                               COL_INDEX, &index,
                               COL_LAST_NAME, &last,
                               COL_FIRST_NAME, &first, 
                               COL_BIRTHYEAR, &birthyear, 
                               COL_CLUB, &club, 
                               COL_WCLASS, &regcategory, 
                               COL_BELT, &belt, 
                               COL_WEIGHT, &weight, 
                               COL_VISIBLE, &visible,
                               COL_CATEGORY, &category,
                               COL_DELETED, &deleted,
			       COL_COUNTRY, &country,
			       COL_ID, &id,
                               -1);
            sprintf(weight_s, "%d,%02d", weight/1000, (weight%1000)/10);
            sprintf(birthyear_s, "%d", birthyear);

	    catdata = avl_get_category(index);
	}
    } else {
        visible = (gboolean) ((int)userdata == NEW_JUDOKA ? TRUE : FALSE);
        category = strdup("?");
        deleted = 0;
        sprintf(weight_s, "0");
        sprintf(birthyear_s, "0");
        belt = 0;
        index = (guint) userdata;
    }
	
	
    judoka_tmp->index = index;
    judoka_tmp->visible = visible;
    judoka_tmp->category = g_strdup(category);

    dialog = gtk_dialog_new_with_buttons (visible ? _("Competitor") : _("Category"),
                                          NULL,
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          NULL);

    GtkWidget *ok_button = gtk_dialog_add_button (GTK_DIALOG (dialog),
                                                  GTK_STOCK_OK,
                                                  GTK_RESPONSE_OK);
    accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group (GTK_WINDOW (dialog), accel_group);
    gtk_widget_add_accelerator(ok_button, "clicked", accel_group, GDK_Return, 0, GTK_ACCEL_VISIBLE );
    gtk_widget_add_accelerator(ok_button, "clicked", accel_group, GDK_KP_Enter, 0, GTK_ACCEL_VISIBLE );

    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(judoka_edited_callback), (gpointer)judoka_tmp);

    table = gtk_table_new(2, 8, FALSE);
    gtk_container_add(GTK_CONTAINER (GTK_DIALOG(dialog)->vbox), table);

    if (visible) {
        judoka_tmp->last = set_entry(table, 0, _("Last Name:"), last);
        judoka_tmp->first = set_entry(table, 1, _("First Name:"), first);
        judoka_tmp->birthyear = set_entry(table, 2, _("Year of Birth:"), birthyear_s);

        tmp = gtk_label_new(_("Grade:"));
        gtk_table_attach_defaults(GTK_TABLE(table), tmp, 0, 1, 3, 4);
        judoka_tmp->belt = tmp = gtk_combo_box_new_text();
        for (i = 0; belts[i]; i++)
            gtk_combo_box_append_text((GtkComboBox *)tmp, belts[i]);
        gtk_table_attach_defaults(GTK_TABLE(table), tmp, 1, 2, 3, 4);
        gtk_combo_box_set_active((GtkComboBox *)tmp, belt);

        judoka_tmp->club = set_entry(table, 4, _("Club:"), club);
        judoka_tmp->country = set_entry(table, 5, _("Country:"), country);
        judoka_tmp->regcategory = set_entry(table, 6, _("Category:"), regcategory);
        judoka_tmp->weight = set_entry(table, 7, _("Weight:"), weight_s);
        if (last && last[0])
            gtk_widget_grab_focus(judoka_tmp->weight);

        tmp = gtk_label_new(_("Seeding:"));
        gtk_table_attach_defaults(GTK_TABLE(table), tmp, 0, 1, 8, 9);
        judoka_tmp->seeding = tmp = gtk_combo_box_new_text();
        gtk_combo_box_append_text((GtkComboBox *)tmp, _("No seeding"));
        gtk_combo_box_append_text((GtkComboBox *)tmp, _("1"));
        gtk_combo_box_append_text((GtkComboBox *)tmp, _("2"));
        gtk_combo_box_append_text((GtkComboBox *)tmp, _("3"));
        gtk_combo_box_append_text((GtkComboBox *)tmp, _("4"));
        gtk_table_attach_defaults(GTK_TABLE(table), tmp, 1, 2, 8, 9);
        gtk_combo_box_set_active((GtkComboBox *)tmp, deleted >> 2);

        tmp = gtk_label_new("Hansoku-make:");
        gtk_table_attach_defaults(GTK_TABLE(table), tmp, 0, 1, 9, 10);
        judoka_tmp->hansokumake = gtk_check_button_new();
        gtk_table_attach_defaults(GTK_TABLE(table), judoka_tmp->hansokumake, 1, 2, 9, 10);
        if (deleted & HANSOKUMAKE)
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(judoka_tmp->hansokumake), TRUE);

        judoka_tmp->id = set_entry(table, 10, _("Id:"), id);

        g_signal_connect(G_OBJECT(judoka_tmp->club), "key-press-event", 
                         G_CALLBACK(complete_cb), club_completer);
    } else {
        judoka_tmp->last = set_entry(table, 0, _("Category:"), last ? last : "");

        tmp = gtk_label_new(_("System:"));
        gtk_table_attach_defaults(GTK_TABLE(table), tmp, 0, 1, 1, 2);
        judoka_tmp->system = tmp = gtk_combo_box_new_text();

	for (i = 0; i < NUM_SYSTEMS; i++)
	    gtk_combo_box_append_text(GTK_COMBO_BOX(tmp), get_system_name_for_menu(i));

        gtk_table_attach_defaults(GTK_TABLE(table), tmp, 1, 2, 1, 2);
	gtk_combo_box_set_active(GTK_COMBO_BOX(tmp), 
				 catdata ? 
				 get_system_menu_selection((catdata->system & SYSTEM_WISH_MASK) >> SYSTEM_WISH_SHIFT) :
				 CAT_SYSTEM_DEFAULT);

        tmp = gtk_label_new("Tatami:");
        gtk_table_attach_defaults(GTK_TABLE(table), tmp, 0, 1, 2, 3);
        judoka_tmp->belt = tmp = gtk_combo_box_new_text();
        gtk_combo_box_append_text(GTK_COMBO_BOX(tmp), "?");
        for (i = 0; i < NUM_TATAMIS; i++) {
            char buf[10];
            sprintf(buf, "T %d", i+1);
            gtk_combo_box_append_text(GTK_COMBO_BOX(tmp), buf);
        }
        gtk_table_attach_defaults(GTK_TABLE(table), tmp, 1, 2, 2, 3);
        gtk_combo_box_set_active(GTK_COMBO_BOX(tmp), belt);

        judoka_tmp->birthyear = set_entry(table, 3, _("Group:"), birthyear_s);
    }

    g_free(last); 
    g_free(first); 
    g_free(club); 
    g_free(regcategory);
    g_free(category);
    g_free(country);
    g_free(id);

    gtk_widget_show_all(dialog);
    editing_ongoing = TRUE;
}

void new_judoka(GtkWidget *w, gpointer data)
{
    view_on_row_activated(NULL, NULL, NULL, (gpointer)NEW_JUDOKA);
}

void new_regcategory(GtkWidget *w, gpointer data)
{
    view_on_row_activated(NULL, NULL, NULL, (gpointer)NEW_WCLASS);
}

/*
 * Create the view
 */

static GtkTreeModel *create_and_fill_model (void)
{
    struct treedata td;
    GtkTreeSortable *sortable;

    td.treestore = gtk_tree_store_new(NUM_COLS,
                                      G_TYPE_UINT,
                                      G_TYPE_STRING, /* last */
                                      G_TYPE_STRING, /* first */
                                      G_TYPE_UINT,   /* birthyear */
                                      G_TYPE_UINT,   /* belt */
                                      G_TYPE_STRING, /* club */
				      G_TYPE_STRING, /* country */
                                      G_TYPE_STRING, /* regcategory */
                                      G_TYPE_INT,    /* weight */
                                      G_TYPE_BOOLEAN,/* visible */
                                      G_TYPE_STRING, /* category */
                                      G_TYPE_UINT,   /* deleted */
				      G_TYPE_STRING  /* id */
        );
    current_model = (GtkTreeModel *)td.treestore;
    sortable = GTK_TREE_SORTABLE(current_model);
# if 0
    gtk_tree_sortable_set_sort_func(sortable, SORTID_NAME, sort_iter_compare_func,
                                    GINT_TO_POINTER(SORTID_NAME), NULL);

    gtk_tree_sortable_set_sort_func(sortable, SORTID_WEIGHT, sort_iter_compare_func,
                                    GINT_TO_POINTER(SORTID_NAME), NULL);
#endif
    /* set initial sort order */
    gtk_tree_sortable_set_sort_column_id(sortable, SORTID_NAME, GTK_SORT_ASCENDING);

    db_read_categories();
    db_read_judokas();

    return GTK_TREE_MODEL(td.treestore);
}

void last_name_cell_data_func (GtkTreeViewColumn *col,
                               GtkCellRenderer   *renderer,
                               GtkTreeModel      *model,
                               GtkTreeIter       *iter,
                               gpointer           user_data)
{
    gchar  buf[100];
    gboolean visible;
    gchar *last;
    guint  deleted;
    guint  index;
    guint  weight;

    gtk_tree_model_get(model, iter, 
                       COL_INDEX, &index,
                       COL_LAST_NAME, &last, 
                       COL_VISIBLE, &visible, 
                       COL_WEIGHT, &weight,
                       COL_DELETED, &deleted, -1);

    if (visible) {
        if (deleted >> 2)
            g_snprintf(buf, sizeof(buf), "%s (%d)", last, deleted >> 2);
        else
            g_snprintf(buf, sizeof(buf), "%s", last);

        if (deleted & HANSOKUMAKE)
            g_object_set(renderer, "strikethrough", TRUE, "cell-background-set", FALSE, NULL);
        else
            g_object_set(renderer, "strikethrough", FALSE, "cell-background-set", FALSE, NULL);
    } else {
        gint status = 0;
		
        if (user_data == NULL) {
            //status = weight;
            struct category_data *catdata = avl_get_category(index);
            status = catdata ? catdata->match_status : 0;
        }

        if ((status & MATCH_EXISTS) && (status & MATCH_UNMATCHED) == 0)
            g_object_set(renderer, 
                         "cell-background", "Green", 
                         "cell-background-set", TRUE, 
                         NULL);
        else if (status & MATCH_MATCHED)
            g_object_set(renderer, 
                         "cell-background", "Orange", 
                         "cell-background-set", TRUE, 
                         NULL);
        else if (status & MATCH_EXISTS)
            g_object_set(renderer, 
                         "cell-background", "Yellow", 
                         "cell-background-set", TRUE, 
                         NULL);
        else
            g_object_set(renderer, 
                         "cell-background-set", FALSE, 
                         NULL);

        g_snprintf(buf, sizeof(buf), "%s", last);
        g_object_set(renderer, "strikethrough", FALSE, NULL);
    }

    g_object_set(renderer, "text", buf, NULL);
    g_free(last);
}

void first_name_cell_data_func (GtkTreeViewColumn *col,
				GtkCellRenderer   *renderer,
				GtkTreeModel      *model,
				GtkTreeIter       *iter,
				gpointer           user_data)
{
    gchar  buf[100];
    gboolean visible;
    gchar *first = NULL;
    guint  deleted;
    guint  index;
    guint  weight;

    gtk_tree_model_get(model, iter, 
                       COL_INDEX, &index,
                       COL_FIRST_NAME, &first, 
                       COL_VISIBLE, &visible, 
                       COL_WEIGHT, &weight,
                       COL_DELETED, &deleted, -1);
  
    if (visible) {
        g_object_set(renderer,
                     "weight-set", FALSE,
                     NULL);
        g_snprintf(buf, sizeof(buf), "%s", first);
    } else {
        gchar print_stat = ' ';
        gint n = gtk_tree_model_iter_n_children(model, iter);
        struct category_data *data = avl_get_category(index);
        if (data && (data->match_status & CAT_PRINTED))
            print_stat = 'P';

        if (n <= 2)
            g_object_set(renderer,
                         "weight", PANGO_WEIGHT_BOLD,
                         "weight-set", TRUE,
                         NULL);
        else
            g_object_set(renderer,
                         "weight-set", FALSE,
                         NULL);

        g_snprintf(buf, sizeof(buf), "[%d] %c", n, print_stat);
    }

    g_object_set(renderer, "text", buf, NULL);
    g_free(first);
}

void weight_cell_data_func (GtkTreeViewColumn *col,
                            GtkCellRenderer   *renderer,
                            GtkTreeModel      *model,
                            GtkTreeIter       *iter,
                            gpointer           user_data)
{
    gchar  buf[64];
    gint   weight, birthyear;
    gboolean visible;

    gtk_tree_model_get(model, iter, 
                       COL_WEIGHT, &weight, 
                       COL_VISIBLE, &visible, 
                       COL_BIRTHYEAR, &birthyear,
                       -1);
  
    if (visible) {
        g_snprintf(buf, sizeof(buf), "%d,%02d", weight/1000, (weight%1000)/10);
        g_object_set(renderer, "foreground-set", FALSE, NULL); /* print this normal */
    } else {
        g_snprintf(buf, sizeof(buf), "%s %d", _("Group"), birthyear);
    }

    g_object_set(renderer, "text", buf, NULL);
}

void belt_cell_data_func (GtkTreeViewColumn *col,
                          GtkCellRenderer   *renderer,
                          GtkTreeModel      *model,
                          GtkTreeIter       *iter,
                          gpointer           user_data)
{
    gchar  buf[64];
    guint  belt;
    gboolean visible;

    gtk_tree_model_get(model, iter, COL_BELT, &belt, COL_VISIBLE, &visible, -1);

    strcpy(buf, "?");
  
    if (visible) {
        if (belt >= 0 && belt < 14) {
            g_snprintf(buf, sizeof(buf), "%s", belts[belt]);
            g_object_set(renderer, "foreground-set", FALSE, NULL); /* print this normal */
        }
    } else {
        if (belt >= 0 && belt <= NUM_TATAMIS) {
            g_snprintf(buf, sizeof(buf), "T %d", belt);
            g_object_set(renderer, "foreground-set", FALSE, NULL); /* print this normal */
        }
    }

    g_object_set(renderer, "text", buf, NULL);
}

static gchar *selected_regcategory = NULL;
static gboolean selected_visible;

static gboolean view_on_button_pressed(GtkWidget *treeview, 
                                       GdkEventButton *event, 
                                       gpointer userdata)
{
    gboolean handled = FALSE;

    /* single click with the left or right mouse button? */
    if (event->type == GDK_BUTTON_PRESS  &&  
        (event->button == 1 || event->button == 3)) {
        GtkTreePath *path;
        GtkTreeModel *model;
        GtkTreeIter iter;
        guint index;

        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
                                          (gint) event->x, 
                                          (gint) event->y,
                                          &path, NULL, NULL, NULL)) {
            GtkTreeIter parent;
            model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));
            gtk_tree_model_get_iter(model, &iter, path);

            if (selected_regcategory)
                g_free(selected_regcategory);
            selected_regcategory = NULL;

            gtk_tree_model_get(model, &iter,
                               COL_VISIBLE, &selected_visible, -1);

            if (selected_visible) {
                if (gtk_tree_model_iter_parent(model, &parent, &iter)) {
                    gtk_tree_model_get(model, &parent,
                                       COL_INDEX, &index,
                                       COL_LAST_NAME, &selected_regcategory,
                                       -1);
                }
            } else {
                gtk_tree_model_get(model, &iter,
                                   COL_INDEX, &index,
                                   COL_LAST_NAME, &selected_regcategory,
                                   -1);
            }

            if (event->button == 3) {
                view_popup_menu(treeview, event, 
                                (gpointer)index,
                                selected_regcategory,
                                selected_visible);
                if (selected_visible == FALSE)
                    current_category = index;
                handled = TRUE;
            } else if (event->button == 1 && !selected_visible) {
                current_category = index;
            }

            gtk_tree_path_free(path);
        }
        return handled; /* we handled this */
    }

    return handled; /* we did not handle this */
}

#if 0
gboolean view_onPopupMenu (GtkWidget *treeview, gpointer userdata)
{
    view_popup_menu(treeview, NULL, userdata, "?", FALSE);
        
    return TRUE; /* we handled this */
}
#endif

void cell_edited_callback(GtkCellRendererText *cell,
                          gchar               *path_string,
                          gchar               *new_text,
                          gpointer             user_data)
{
    //g_print("new text=%s to %s\n", new_text, path_string);
}

static gboolean view_key_press(GtkWidget *widget, GdkEventKey *event, gpointer userdata)
{
    if (event->keyval == GDK_Shift_L ||
        event->keyval == GDK_Shift_R ||
        event->keyval == GDK_Control_L ||
        event->keyval == GDK_Control_R)
        return TRUE;
    return FALSE;
}

static GtkWidget *create_view_and_model(void)
{
    GtkTreeViewColumn   *col;
    GtkCellRenderer     *renderer;
    GtkWidget           *view;
    GtkTreeModel        *model;
    //GtkTreeSelection    *selection;

    gint col_offset;

    current_view = view = gtk_tree_view_new();
    //gtk_tree_view_set_enable_tree_lines(current_view, TRUE);

    /* --- Column last name --- */

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
                 "weight", PANGO_WEIGHT_BOLD,
                 "weight-set", TRUE,
                 "xalign", 0.0,
                 NULL);
    col_offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
                                                              -1, _("Last Name"),
                                                              renderer, "text",
                                                              COL_LAST_NAME,
                                                              NULL);
    col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_offset - 1);
    gtk_tree_view_column_set_cell_data_func(col, renderer, last_name_cell_data_func, NULL, NULL);
    //gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (col), TRUE);
    gtk_tree_view_column_set_sort_column_id(GTK_TREE_VIEW_COLUMN(col), COL_LAST_NAME);

    /* --- Column first name --- */

    renderer = gtk_cell_renderer_text_new();
    g_object_set (renderer, "xalign", 0.0, NULL);
    col_offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
                                                              -1, _("First Name"),
                                                              renderer, "text",
                                                              COL_FIRST_NAME,
                                                              //"visible",
                                                              //COL_VISIBLE,
                                                              NULL);
    col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_offset - 1);
    gtk_tree_view_column_set_cell_data_func(col, renderer, first_name_cell_data_func, NULL, NULL);
    //gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (col), TRUE);


    /* --- Column birthyear name --- */

    renderer = gtk_cell_renderer_text_new();
    g_object_set (renderer, "xalign", 0.5, NULL);
    col_offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
                                                              -1, _("Year of Birth"),
                                                              renderer, "text",
                                                              COL_BIRTHYEAR,
                                                              "visible",
                                                              COL_VISIBLE,
                                                              NULL);
    col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_offset - 1);
    //gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (col), TRUE);


    /* --- Column belt --- */

    renderer = gtk_cell_renderer_text_new();
    //g_object_set (renderer, "xalign", 0.0, NULL);
    col_offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
                                                              -1, _("Grade"),
                                                              renderer, "text",
                                                              COL_BELT,
                                                              NULL);
    col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_offset - 1);
    gtk_tree_view_column_set_cell_data_func(col, renderer, belt_cell_data_func, NULL, NULL);
    //gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (col), TRUE);
    gtk_tree_view_column_set_sort_column_id(GTK_TREE_VIEW_COLUMN(col), COL_BELT);

    /* --- Column club --- */

    renderer = gtk_cell_renderer_text_new();
    g_object_set (renderer, "xalign", 0.0, NULL);
    col_offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
                                                              -1, _("Club"),
                                                              renderer, "text",
                                                              COL_CLUB,
                                                              "visible",
                                                              COL_VISIBLE,
                                                              NULL);
    col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_offset - 1);
    //gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (col), TRUE);
    gtk_tree_view_column_set_sort_column_id(GTK_TREE_VIEW_COLUMN(col), COL_CLUB);

    /* --- Column country --- */

    renderer = gtk_cell_renderer_text_new();
    g_object_set (renderer, "xalign", 0.0, NULL);
    col_offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
                                                              -1, _("Country"),
                                                              renderer, "text",
                                                              COL_COUNTRY,
                                                              "visible",
                                                              COL_VISIBLE,
                                                              NULL);
    col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_offset - 1);
    //gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (col), TRUE);
    gtk_tree_view_column_set_sort_column_id(GTK_TREE_VIEW_COLUMN(col), COL_COUNTRY);

    /* --- Column regcategory --- */

    renderer = gtk_cell_renderer_text_new();
    g_object_set (renderer, "xalign", 0.5, NULL);
    col_offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
                                                              -1, _("Reg. Category"),
                                                              renderer, "text",
                                                              COL_WCLASS,
                                                              "visible",
                                                              COL_VISIBLE,
                                                              NULL);
    col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_offset - 1);
    //gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (col), TRUE);
    gtk_tree_view_column_set_sort_column_id(GTK_TREE_VIEW_COLUMN(col), COL_WCLASS);

    /* --- Column weight --- */

    renderer = gtk_cell_renderer_text_new();
    g_object_set (renderer, "xalign", 0.0, NULL);
    col_offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
                                                              -1, _("Weight"),
                                                              renderer, "text",
                                                              COL_WEIGHT,
                                                              NULL);
    col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_offset - 1);
    gtk_tree_view_column_set_cell_data_func(col, renderer, weight_cell_data_func, NULL, NULL);
    //gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (col), TRUE);
    gtk_tree_view_column_set_sort_column_id(GTK_TREE_VIEW_COLUMN(col), COL_WEIGHT);

    /* --- Column id --- */

    renderer = gtk_cell_renderer_text_new();
    g_object_set (renderer, "xalign", 0.0, NULL);
    col_offset = gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
                                                              -1, _("Id"),
                                                              renderer, "text",
                                                              COL_ID,
                                                              NULL);
    col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_offset - 1);
    gtk_tree_view_column_set_sort_column_id(GTK_TREE_VIEW_COLUMN(col), COL_ID);

    /*****/

    model = create_and_fill_model();
    gtk_tree_view_set_model(GTK_TREE_VIEW(view), model);
    gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (view), TRUE);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),
                                GTK_SELECTION_MULTIPLE);
				    
    g_object_unref(model); /* destroy model automatically with view */

    /*
     * gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),
     *                           GTK_SELECTION_NONE);
     */
    g_signal_connect(view, "row-activated", (GCallback) view_on_row_activated, NULL);
    g_signal_connect(view, "button-press-event", (GCallback) view_on_button_pressed, NULL);
    g_signal_connect(view, "key-press-event", G_CALLBACK(view_key_press), NULL);

    //g_signal_connect(view, "popup-menu", (GCallback) view_onPopupMenu, NULL);

    //selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    //gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE(current_model),
                                     COL_LAST_NAME,
                                     sort_iter_compare_func,
                                     GINT_TO_POINTER(SORTID_NAME),
                                     NULL);
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE(current_model),
                                     COL_WEIGHT,
                                     sort_iter_compare_func,
                                     GINT_TO_POINTER(SORTID_WEIGHT),
                                     NULL);
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE(current_model),
                                     COL_CLUB,
                                     sort_iter_compare_func,
                                     GINT_TO_POINTER(SORTID_CLUB),
                                     NULL);
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE(current_model),
                                     COL_WCLASS,
                                     sort_iter_compare_func,
                                     GINT_TO_POINTER(SORTID_WCLASS),
                                     NULL);
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE(current_model),
                                     COL_BELT,
                                     sort_iter_compare_func,
                                     GINT_TO_POINTER(SORTID_BELT),
                                     NULL);
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE(current_model),
                                     COL_COUNTRY,
                                     sort_iter_compare_func,
                                     GINT_TO_POINTER(SORTID_COUNTRY),
                                     NULL);

    gtk_tree_view_set_search_column(GTK_TREE_VIEW(view), COL_LAST_NAME);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view), TRUE);


    return view;
}

#if 0
static GtkTreeViewSearchEqualFunc old_search_compare_func;

static gboolean compare_search_key(GtkTreeModel *model,
                                   gint column,
                                   const gchar *key,
                                   GtkTreeIter *iter,
                                   gpointer search_data)
{
    gboolean result = old_search_compare_func(model, column, key, iter, search_data);

    if (result == FALSE) {
        GtkTreePath* path = gtk_tree_model_get_path(GTK_TREE_MODEL(current_model), iter);
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(current_view), path, NULL, TRUE, 0.5, 0.0 );
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(current_view), path, NULL, FALSE);
        gtk_tree_path_free(path);
    }

    return result;
}
#endif

/*
 * Page init
 */

void set_judokas_page(GtkWidget *notebook)
{
    GtkWidget *judokas_scrolled_window;
    GtkWidget *view;
    GtkTreeViewColumn *col;

    /* 
     * list of judokas
     */
    judokas_scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(judokas_scrolled_window), 10);

    view = create_view_and_model();

    /* pack the table into the scrolled window */
    gtk_scrolled_window_add_with_viewport (
        GTK_SCROLLED_WINDOW(judokas_scrolled_window), view);

    competitor_label = gtk_label_new (_("Competitors"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), judokas_scrolled_window, competitor_label);
    update_category_status_info_all();

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(current_view), 0);
    g_signal_emit_by_name(col, "clicked");

#if 0
    old_search_compare_func = gtk_tree_view_get_search_equal_func(current_view);
    gtk_tree_view_set_search_equal_func (current_view, compare_search_key, NULL, NULL);
#endif
    //gtk_widget_show_all(notebook);
}

gint sort_by_regcategory(gchar *name1, gchar *name2)
{
    return compare_categories(name1, name2);
}

gint sort_by_name(const gchar *name1, const gchar *name2)
{
    if (name1 == NULL || name2 == NULL) {
        if (name1 == NULL && name2 == NULL)
            return 0;
        return (name1 == NULL) ? -1 : 1;
    }
    return g_utf8_collate(name1,name2);
}


gint sort_iter_compare_func(GtkTreeModel *model,
                            GtkTreeIter  *a,
                            GtkTreeIter  *b,
                            gpointer      userdata)
{
    gint sortcol = GPOINTER_TO_INT(userdata);
    gint ret = 0;

    switch (sortcol) {
    case SORTID_NAME:
    {
        gchar *name1, *name2;
        gboolean visible1, visible2;

        gtk_tree_model_get(model, a, COL_LAST_NAME, &name1, COL_VISIBLE, &visible1, -1);
        gtk_tree_model_get(model, b, COL_LAST_NAME, &name2, COL_VISIBLE, &visible2, -1);

        if (visible1 == FALSE && visible2 == FALSE)
            ret = sort_by_regcategory(name1, name2);
        else
            ret = sort_by_name(name1, name2);

        g_free(name1);
        g_free(name2);
    }
    break;

    case SORTID_WEIGHT:
    {
        gint weight1, weight2;

        gtk_tree_model_get(model, a, COL_WEIGHT, &weight1, -1);
        gtk_tree_model_get(model, b, COL_WEIGHT, &weight2, -1);

        if (weight1 != weight2)
        {
            ret = (weight1 > weight2) ? 1 : -1;
        }
        /* else both equal => ret = 0 */
    }
    break;

    case SORTID_CLUB:
    {
        gchar *name1, *name2;
        gtk_tree_model_get(model, a, COL_CLUB, &name1, -1);
        gtk_tree_model_get(model, b, COL_CLUB, &name2, -1);
        ret = sort_by_name(name1, name2);
        g_free(name1);
        g_free(name2);
    }
    break;

    case SORTID_WCLASS:
    {
        gchar *name1, *name2;
        gint weight1, weight2;
        gtk_tree_model_get(model, a, COL_WCLASS, &name1, COL_WEIGHT, &weight1, -1);
        gtk_tree_model_get(model, b, COL_WCLASS, &name2, COL_WEIGHT, &weight2, -1);
        ret = sort_by_regcategory(name1, name2);
        if (ret == 0 && weight1 != weight2)
            ret = (weight1 > weight2) ? 1 : -1;
        g_free(name1);
        g_free(name2);
    }
    break;

    case SORTID_BELT:
    {
        guint belt1, belt2;

        gtk_tree_model_get(model, a, COL_BELT, &belt1, -1);
        gtk_tree_model_get(model, b, COL_BELT, &belt2, -1);

        if (belt1 != belt2) {
            ret = (belt1 > belt2) ? 1 : -1;
        } else {
            gint weight1, weight2;

            gtk_tree_model_get(model, a, COL_WEIGHT, &weight1, -1);
            gtk_tree_model_get(model, b, COL_WEIGHT, &weight2, -1);

            if (weight1 != weight2) {
                ret = (weight1 > weight2) ? 1 : -1;
            }
        }
    }
    break;

    case SORTID_COUNTRY:
    {
        gchar *club1, *club2, *country1, *country2;
        gtk_tree_model_get(model, a, COL_CLUB, &club1, COL_COUNTRY, &country1, -1);
        gtk_tree_model_get(model, b, COL_CLUB, &club2, COL_COUNTRY, &country2, -1);
        ret = sort_by_name(country1, country2);
	if (ret == 0)
	    ret = sort_by_name(club1, club2);
        g_free(club1);
        g_free(club2);
        g_free(country1);
        g_free(country2);
    }
    break;

    default:
        g_return_val_if_reached(0);
    }

    return ret;
}

static gboolean foreach_regcategory(GtkTreeModel *model,
                                    GtkTreePath  *path,
                                    GtkTreeIter  *iter,
                                    GList       **rowref_list)
{
    GtkTreeIter parent, child;

    g_assert ( rowref_list != NULL );

    if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(current_model),
                                   &parent, iter) == FALSE) {
        /* this is top level */
        if (gtk_tree_model_iter_children(GTK_TREE_MODEL(current_model),
                                         &child, iter) == FALSE) {
            /* no children */
            GtkTreeRowReference  *rowref;
            rowref = gtk_tree_row_reference_new(GTK_TREE_MODEL(current_model), path);
            *rowref_list = g_list_append(*rowref_list, rowref);
        }
    }

    return FALSE; /* do not stop walking the store, call us with next row */
}

void remove_empty_regcategories(GtkWidget *w, gpointer data)
{
    GList *rr_list = NULL;    /* list of GtkTreeRowReferences to remove */
    GList *node;

    gtk_tree_model_foreach(GTK_TREE_MODEL(current_model),
                           (GtkTreeModelForeachFunc) foreach_regcategory,
                           &rr_list);

    for ( node = rr_list;  node != NULL;  node = node->next ) {
        GtkTreePath *path;

        path = gtk_tree_row_reference_get_path((GtkTreeRowReference*)node->data);

        if (path) {
            GtkTreeIter  iter;

            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(current_model), &iter, path)) {
                struct judoka *j = get_data_by_iter(&iter);
                j->deleted |= DELETED;
                db_remove_matches(j->index);
                db_update_category(j->index, j);
                free_judoka(j);
                gtk_tree_store_remove((GtkTreeStore *)current_model, &iter);
            }

            /* FIXME/CHECK: Do we need to free the path here? */
        }
    }

    g_list_foreach(rr_list, (GFunc) gtk_tree_row_reference_free, NULL);
    g_list_free(rr_list);        

    SYS_LOG_INFO(_("Empty categories removed"));
}

/************************************************/

static gboolean foreach_competitor(GtkTreeModel *model,
                                   GtkTreePath  *path,
                                   GtkTreeIter  *iter,
                                   GList       **rowref_list)
{
    gint   weight;
    gboolean visible;

    g_assert ( rowref_list != NULL );

    gtk_tree_model_get(model, iter,
                       COL_VISIBLE, &visible,
                       COL_WEIGHT, &weight,
                       -1);

    if (weight < 10 && visible) {
        GtkTreeRowReference  *rowref;
        rowref = gtk_tree_row_reference_new(GTK_TREE_MODEL(current_model), path);
        *rowref_list = g_list_append(*rowref_list, rowref);
    }

    return FALSE; /* do not stop walking the store, call us with next row */
}

void remove_unweighed_competitors(GtkWidget *w, gpointer data)
{
    GList *rr_list = NULL;    /* list of GtkTreeRowReferences to remove */
    GList *node;

    gtk_tree_model_foreach(GTK_TREE_MODEL(current_model),
                           (GtkTreeModelForeachFunc) foreach_competitor,
                           &rr_list);

    for (node = rr_list; node != NULL; node = node->next) {
        GtkTreePath *path;

        path = gtk_tree_row_reference_get_path((GtkTreeRowReference*)node->data);

        if (path) {
            GtkTreeIter  iter;

            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(current_model), &iter, path)) {
                struct judoka *j = get_data_by_iter(&iter);

                if (j && (db_competitor_match_status(j->index) & MATCH_EXISTS) == 0) {
                    j->deleted |= DELETED;
                    db_update_judoka(j->index, j);
                    gtk_tree_store_remove((GtkTreeStore *)current_model, &iter);
                }
                free_judoka(j);
            }

            /* FIXME/CHECK: Do we need to free the path here? */
        }
    }

    g_list_foreach(rr_list, (GFunc) gtk_tree_row_reference_free, NULL);
    g_list_free(rr_list);        

    SYS_LOG_INFO(_("Unweighted competitors removed"));
}

static gboolean foreach_selected_competitor(GtkTreeModel *model,
                                            GtkTreePath  *path,
                                            GtkTreeIter  *iter,
                                            GList       **rowref_list)
{
    gboolean visible;
    GtkTreeSelection *selection = 
        gtk_tree_view_get_selection(GTK_TREE_VIEW(current_view));

    g_assert ( rowref_list != NULL );

    gtk_tree_model_get(model, iter,
                       COL_VISIBLE, &visible,
                       -1);

    if (visible && gtk_tree_selection_iter_is_selected(selection, iter)) {
        GtkTreeRowReference  *rowref;
        rowref = gtk_tree_row_reference_new(GTK_TREE_MODEL(current_model), path);
        *rowref_list = g_list_append(*rowref_list, rowref);
    }

    return FALSE; /* do not stop walking the store, call us with next row */
}

void remove_competitors(GtkWidget *w, gpointer data)
{
    GList *rr_list = NULL;    /* list of GtkTreeRowReferences to remove */
    GList *node;

    gtk_tree_model_foreach(GTK_TREE_MODEL(current_model),
                           (GtkTreeModelForeachFunc) foreach_selected_competitor,
                           &rr_list);

    for (node = rr_list; node != NULL; node = node->next) {
        GtkTreePath *path;

        path = gtk_tree_row_reference_get_path((GtkTreeRowReference*)node->data);

        if (path) {
            GtkTreeIter  iter;

            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(current_model), &iter, path)) {
                struct judoka *j = get_data_by_iter(&iter);

                if (j && (db_competitor_match_status(j->index) & MATCH_EXISTS)) {
                    SHOW_MESSAGE("%s %s: %s", 
                                 j->first, j->last, _("Matches exist. Undo the drawing first."));
                    free_judoka(j);
                } else if (j) {
                    j->deleted |= DELETED;
                    db_update_judoka(j->index, j);
                    free_judoka(j);
                    gtk_tree_store_remove((GtkTreeStore *)current_model, &iter);
                }
            }

            /* FIXME/CHECK: Do we need to free the path here? */
        }
    }

    g_list_foreach(rr_list, (GFunc) gtk_tree_row_reference_free, NULL);
    g_list_free(rr_list);        

    SYS_LOG_INFO(_("Competitors removed"));
}

/* barcode stuff */

static GtkWidget *barcode = NULL;

static gboolean foreach_comp_dsp(GtkTreeModel *model,
                                 GtkTreePath  *path,
                                 GtkTreeIter  *iter,
                                 void         *indx)
{
    gint   id;

    gtk_tree_model_get(model, iter,
                       COL_INDEX, &id,
                       -1);

    if (id == (gint)indx) {
        view_on_row_activated(GTK_TREE_VIEW(current_view), path, NULL, NULL);

        return TRUE;
    }

    return FALSE; /* do not stop walking the store, call us with next row */
}

static void display_competitor(gint indx)
{
    gtk_tree_model_foreach(GTK_TREE_MODEL(current_model),
                           (GtkTreeModelForeachFunc) foreach_comp_dsp,
                           (void *)indx);
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer userdata)
{
    static gchar keys[6] = {0};
    gchar  label[10];
    gint i;

    if (event->type != GDK_KEY_PRESS)
        return FALSE;

    if (event->keyval == GDK_Return || event->keyval == GDK_KP_Enter) {
        gint jnum = 0;
        for (i = 0; i < 6; i++) {
            jnum = jnum*10 + keys[i];
            keys[i] = 0;
            label[i] = '0';
        }
        label[6] = 0;

        display_competitor(jnum);
                
        if (barcode)
            gtk_label_set_text(GTK_LABEL(barcode), label);

        return TRUE;
    }

    if ((event->keyval >= GDK_0 && event->keyval <= GDK_9) ||
        (event->keyval >= GDK_KP_0 && event->keyval <= GDK_KP_9)) {
        for (i = 0; i < 5; i++)
            keys[i] = keys[i+1];

        if (event->keyval >= GDK_0 && event->keyval <= GDK_9)
            keys[5] = event->keyval - GDK_0;
        else
            keys[5] = event->keyval - GDK_KP_0;
                
        for (i = 0; i < 6; i++)
            label[i] = keys[i] + '0';
        label[6] = 0;

        if (barcode)
            gtk_label_set_text(GTK_LABEL(barcode), label);

        return TRUE;
    }

    if (event->keyval == GDK_BackSpace || event->keyval == GDK_Delete) {
        for (i = 5; i > 0; i--)
            keys[i] = keys[i-1];
        keys[0] = 0;
        for (i = 0; i < 6; i++)
            label[i] = keys[i] + '0';
        label[6] = 0;
        if (barcode)
            gtk_label_set_text(GTK_LABEL(barcode), label);

        return TRUE;
    }

    return FALSE;
}

void barcode_search(GtkWidget *w, gpointer data)
{
    GtkWidget *dialog, *label, *hbox;

    /* Create a non-modal dialog with one OK button. */
    dialog = gtk_dialog_new_with_buttons (_("Bar code search"), GTK_WINDOW(main_window),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          NULL);

    gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

    label = gtk_label_new (_("Type the barcode:"));
    barcode = gtk_label_new ("000000");

    hbox = gtk_hbox_new (FALSE, 5);
    gtk_container_set_border_width (GTK_CONTAINER (hbox), 10);
    gtk_box_pack_start_defaults (GTK_BOX (hbox), label);
    gtk_box_pack_start_defaults (GTK_BOX (hbox), barcode);

    gtk_box_pack_start_defaults (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox);
    gtk_widget_show_all (dialog);

    /* Call gtk_widget_destroy() when the dialog emits the response signal. */

    g_signal_connect(G_OBJECT(dialog), 
                     "key-press-event", G_CALLBACK(key_press), NULL);
}

void set_competitors_col_titles(void)
{
    GtkTreeViewColumn *col;

    if (!current_view)
        return;

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(current_view), COL_LAST_NAME-1);
    gtk_tree_view_column_set_title(col, _("Last Name"));

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(current_view), COL_FIRST_NAME-1);
    gtk_tree_view_column_set_title(col, _("First Name"));

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(current_view), COL_BIRTHYEAR-1);
    gtk_tree_view_column_set_title(col, _("Year of Birth"));

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(current_view), COL_BELT-1);
    gtk_tree_view_column_set_title(col, _("Grade"));

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(current_view), COL_CLUB-1);
    gtk_tree_view_column_set_title(col, _("Club"));

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(current_view), COL_COUNTRY-1);
    gtk_tree_view_column_set_title(col, _("Country"));

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(current_view), COL_WCLASS-1);
    gtk_tree_view_column_set_title(col, _("Reg. Category"));

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(current_view), COL_WEIGHT-1);
    gtk_tree_view_column_set_title(col, _("Weight"));

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(current_view), COL_ID-1);
    gtk_tree_view_column_set_title(col, _("Id"));


    if (competitor_label)
        gtk_label_set_text(GTK_LABEL(competitor_label), _("Competitors"));
}