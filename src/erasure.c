/*
  use the following to compile (didn't include in the Makefile)

  gcc src/erasure.c -o erasure `pkg-config --cflags --libs gtk+-2.0`

 */

#include <gtk/gtk.h>
#include <stdlib.h>

GtkWidget *hscale;
GtkWidget *text;
double previous_loss_rate = 0;
int indicator_set = 0;

static void scale_set_default_values( GtkScale *scale )
{
    gtk_range_set_update_policy (GTK_RANGE (scale),
                                 GTK_UPDATE_CONTINUOUS);
    gtk_scale_set_digits (scale, 1);
    gtk_scale_set_value_pos (scale, GTK_POS_TOP);
    gtk_scale_set_draw_value (scale, TRUE);
}

static void controls_quit ( GtkWidget *widget){
  if (indicator_set != 0){
    char command[200];
    sprintf(command, 
            "iptables -D INPUT -m statistic --mode random --probability %f -j DROP", 
            previous_loss_rate/100);
    system(command);
  }
  gtk_main_quit();
}

static void set_loss_rate( GtkWidget *widget, gpointer data){
  GtkAdjustment *adj = (GtkAdjustment*) data;
  char command[200];

  if(indicator_set == 0){
    // no rule to delete
    indicator_set = 1;
  }else{
    // delete the previous rule otherwise
    sprintf(command, 
            "iptables -D INPUT -m statistic --mode random --probability %f -j DROP", 
            previous_loss_rate/100);
    system(command);
  }

  previous_loss_rate = adj->value;
  
  sprintf(command, 
          "iptables -A INPUT -m statistic --mode random --probability %f -j DROP", 
          previous_loss_rate/100);
  system(command);  
  
  sprintf(command, "Current loss rate: %2.1f %%", previous_loss_rate);
  gtk_label_set_text((GtkLabel*)text, command);  
}

static void reset_loss_rate( GtkWidget *widget, gpointer data){
  GtkAdjustment *adj = (GtkAdjustment*) data;
  char command[200];

  if(indicator_set == 0){
    // already reset, do nothing
  }else{
    sprintf(command, 
            "iptables -D INPUT -m statistic --mode random --probability %f -j DROP", 
            previous_loss_rate/100);
    system(command);
    indicator_set = 0;
    previous_loss_rate = 0;
    gtk_adjustment_set_value(adj, 0.0);
  }
  
  sprintf(command, "Current loss rate: %2.1f %%", previous_loss_rate);
  gtk_label_set_text((GtkLabel*)text, command);    
}

/* makes the sample window */

static void create_controls( void )
{
    GtkWidget *window;
    GtkWidget *box1, *box3;
    GtkWidget *setButton, *delButton;
    GtkWidget *separator;
    GtkObject *adj;

    /* Standard window-creating stuff */
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    g_signal_connect (window, "destroy", G_CALLBACK (controls_quit), NULL);
    gtk_window_set_title (GTK_WINDOW (window), "loss rate controls");

    box1 = gtk_vbox_new (FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (box1), 10);
    gtk_container_add (GTK_CONTAINER (window), box1);
    gtk_widget_show (box1);

    char currtext[100];
    sprintf(currtext, "Current loss rate: %2.1f %%", previous_loss_rate);
    text = gtk_label_new(currtext);
    gtk_box_pack_start (GTK_BOX (box1), text, FALSE, FALSE, 5);
    gtk_widget_show (text);

    /* value, lower, upper, step_increment, page_increment, page_size */
    /* Note that the page_size value only makes a difference for
     * scrollbar widgets, and the highest value you'll get is actually
     * (upper - page_size). */
    adj = gtk_adjustment_new (0.0, 0.0, 21.0, 0.1, 1.0, 1.0);

    box3 = gtk_vbox_new (FALSE, 10);
    gtk_box_pack_start (GTK_BOX (box1), box3, TRUE, TRUE, 0);
    gtk_widget_show (box3);
    gtk_container_set_border_width (GTK_CONTAINER (box3), 10);

    hscale = gtk_hscale_new (GTK_ADJUSTMENT (adj));
    gtk_widget_set_size_request (GTK_WIDGET (hscale), 200, -1);
    scale_set_default_values (GTK_SCALE (hscale));
    gtk_box_pack_start (GTK_BOX (box3), hscale, TRUE, TRUE, 0);
    gtk_widget_show (hscale);

    setButton = gtk_button_new_with_label ("Set loss rate");
    g_signal_connect(setButton, "clicked",
                     G_CALLBACK (set_loss_rate),
                     adj);
    gtk_box_pack_start (GTK_BOX (box1), setButton, TRUE, TRUE, 2);
    gtk_widget_set_can_default (setButton, TRUE);
    gtk_widget_grab_default (setButton);
    gtk_widget_show (setButton);

    delButton = gtk_button_new_with_label ("Reset to 0");
    g_signal_connect(delButton, "clicked",
                     G_CALLBACK (reset_loss_rate),
                     adj);
    gtk_box_pack_start (GTK_BOX (box1), delButton, TRUE, TRUE, 2);
    gtk_widget_set_can_default (delButton, TRUE);
    gtk_widget_grab_default (delButton);
    gtk_widget_show (delButton);

    gtk_widget_show (window);
}

int main( int   argc,
          char *argv[] )
{
    gtk_init (&argc, &argv);
    create_controls ();
    gtk_main ();
    return 0;
}
