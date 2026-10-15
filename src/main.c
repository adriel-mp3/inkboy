#include <stdio.h>

#include <gtk/gtk.h>

static void
on_button_clicked(GtkWidget *button, gpointer user_data)
{
    (void)button;
    (void)user_data;

    g_print("Hello, Kindle!\n");
    fflush(stdout);
}

int
main(int argc, char **argv)
{
    GtkWidget *window;
    GtkWidget *button;

    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(
        GTK_WINDOW(window),
        "L:A_N:application_ID:com.inkboy.hello_PC:T");
    gtk_window_set_default_size(GTK_WINDOW(window), 500, 300);
    gtk_container_set_border_width(GTK_CONTAINER(window), 24);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    button = gtk_button_new_with_label("Hello, Kindle!");
    g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), NULL);
    gtk_container_add(GTK_CONTAINER(window), button);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
