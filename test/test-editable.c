/* test-editable.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <libspelling.h>

static void
add_editable (GtkBox          *box,
              GtkWidget       *widget,
              const char      *title,
              SpellingChecker *checker)
{
  SpellingEditableAdapter *adapter;
  GtkEditable *editable = GTK_EDITABLE (widget);
  GtkEditable *delegate;
  GtkWidget *label = gtk_label_new (title);

  gtk_label_set_xalign (GTK_LABEL (label), 0);
  gtk_box_append (box, label);
  gtk_box_append (box, widget);
  gtk_editable_set_text (editable, "Helo world. Try a correktion.");
  adapter = spelling_editable_adapter_new (editable, checker);
  g_object_set_data_full (G_OBJECT (widget), "spelling-adapter", adapter, g_object_unref);

  while ((delegate = gtk_editable_get_delegate (editable)))
    editable = delegate;
  gtk_text_set_extra_menu (GTK_TEXT (editable), spelling_editable_adapter_get_menu_model (adapter));
}

static void
activate_cb (GtkApplication *application)
{
  g_autoptr(SpellingChecker) checker = spelling_checker_new (NULL, "en_US");
  GtkWidget *window = gtk_application_window_new (application);
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);

  gtk_window_set_title (GTK_WINDOW (window), "Editable Spellchecking");
  gtk_window_set_default_size (GTK_WINDOW (window), 480, -1);
  gtk_widget_set_margin_top (box, 24);
  gtk_widget_set_margin_bottom (box, 24);
  gtk_widget_set_margin_start (box, 24);
  gtk_widget_set_margin_end (box, 24);
  gtk_window_set_child (GTK_WINDOW (window), box);

  add_editable (GTK_BOX (box), gtk_entry_new (), "GtkEntry", checker);
  add_editable (GTK_BOX (box), gtk_search_entry_new (), "GtkSearchEntry", checker);
  add_editable (GTK_BOX (box), gtk_text_new (), "GtkText", checker);
  gtk_box_append (GTK_BOX (box),
                  gtk_label_new ("Right-click a misspelled word to see corrections."));
  gtk_window_present (GTK_WINDOW (window));
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GtkApplication) application = NULL;

  spelling_init ();
  application = gtk_application_new ("org.gnome.libspelling.Editable", G_APPLICATION_NON_UNIQUE);
  g_signal_connect (application, "activate", G_CALLBACK (activate_cb), NULL);

  return g_application_run (G_APPLICATION (application), argc, argv);
}
