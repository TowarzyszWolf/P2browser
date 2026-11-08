/*
 * P2Browser engine probe, reconstructed from the code recorded in this chat.
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef P2_DATA_DIR
#define P2_DATA_DIR "/usr/share/p2browser-probe"
#endif
#ifndef P2_VERSION
#define P2_VERSION "0.1.1"
#endif
#if !WEBKIT_CHECK_VERSION(2, 26, 0)
#error "WebKitGTK >= 2.26 with the Linux sandbox is required."
#endif

typedef struct {
    GtkWidget *window, *address, *status;
    WebKitWebView *view;
    WebKitWebContext *context;
    FILE *events;
    gchar *report_dir, *data_dir;
    gint64 started;
    guint timer, watchdog, error_count;
    gboolean self_test, test_finished, test_started, js_enabled, stopping;
    gboolean navigation_failed, navigation_complete;
    gint exit_code;
} App;
static App app;

/* Never log URLs, page-provided error messages or form values. */
static void event(const char *category, const char *detail)
{
    if (!app.events || ftell(app.events) >= 1024 * 1024)
        return;
    fprintf(app.events, "%" G_GINT64_FORMAT "\t%s\t%s\n",
            (g_get_monotonic_time() - app.started) / 1000,
            category, detail ? detail : "-");
    fflush(app.events);
}

static gboolean make_report(const gchar *requested, GError **error)
{
    gchar *path;
    if (requested) {
        app.report_dir = g_canonicalize_filename(requested, NULL);
        if (g_mkdir(app.report_dir, 0700) != 0) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                        "Katalog raportu musi być nowy: %s", g_strerror(errno));
            return FALSE;
        }
    } else {
        app.report_dir = g_dir_make_tmp("p2browser-probe-XXXXXX", error);
        if (!app.report_dir)
            return FALSE;
    }
    path = g_build_filename(app.report_dir, "events.tsv", NULL);
    app.events = fopen(path, "wx");
    g_free(path);
    if (!app.events) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Nie można utworzyć dziennika: %s", g_strerror(errno));
        return FALSE;
    }
    fprintf(app.events, "elapsed_ms\tevent\tdetail\n");
    event("probe", P2_VERSION);
    {
        gchar *version = g_strdup_printf("%u.%u.%u",
            webkit_get_major_version(), webkit_get_minor_version(),
            webkit_get_micro_version());
        event("webkit_runtime", version);
        g_free(version);
    }
    return TRUE;
}

static void status_text(const gchar *message)
{
    if (app.status)
        gtk_label_set_text(GTK_LABEL(app.status), message);
}

static gchar *fixture_uri(void)
{
    gchar *path = g_build_filename(app.data_dir, "start.html", NULL);
    gchar *absolute = g_canonicalize_filename(path, NULL);
    gchar *uri = g_filename_to_uri(absolute, NULL, NULL);
    g_free(absolute);
    g_free(path);
    return uri;
}

static void navigate(const gchar *input)
{
    gchar *value = g_strdup(input), *uri = NULL, *scheme;
    g_strstrip(value);
    if (!*value) {
        g_free(value);
        return;
    }
    scheme = g_uri_parse_scheme(value);
    if (!scheme)
        uri = g_strconcat("https://", value, NULL);
    else if (!g_ascii_strcasecmp(scheme, "http") ||
             !g_ascii_strcasecmp(scheme, "https") ||
             !g_ascii_strcasecmp(scheme, "file") ||
             !g_ascii_strcasecmp(scheme, "about"))
        uri = g_strdup(value);
    else {
        status_text("Obsługiwane adresy: HTTP, HTTPS i pliki lokalne.");
        event("navigation_rejected", "unsupported_scheme");
    }
    if (uri)
        webkit_web_view_load_uri(app.view, uri);
    g_free(uri);
    g_free(scheme);
    g_free(value);
}

static void address_activate(GtkEntry *entry, gpointer unused)
{
    (void)unused;
    navigate(gtk_entry_get_text(entry));
}

static void back_clicked(GtkButton *button, gpointer unused)
{
    (void)button; (void)unused;
    webkit_web_view_go_back(app.view);
}
static void forward_clicked(GtkButton *button, gpointer unused)
{
    (void)button; (void)unused;
    webkit_web_view_go_forward(app.view);
}
static void reload_clicked(GtkButton *button, gpointer unused)
{
    (void)button; (void)unused;
    webkit_web_view_reload(app.view);
}
static void stop_clicked(GtkButton *button, gpointer unused)
{
    (void)button; (void)unused;
    webkit_web_view_stop_loading(app.view);
    event("navigation", "stopped_by_user");
}

static void js_toggled(GtkToggleButton *button, gpointer unused)
{
    (void)unused;
    app.js_enabled = gtk_toggle_button_get_active(button);
    webkit_settings_set_enable_javascript(
        webkit_web_view_get_settings(app.view), app.js_enabled);
    event("javascript", app.js_enabled ? "engine_enabled" : "disabled");
    status_text("Ustawienie JS zmienione. Odśwież stronę.");
}

static void images_toggled(GtkToggleButton *button, gpointer unused)
{
    gboolean enabled = gtk_toggle_button_get_active(button);
    (void)unused;
    webkit_settings_set_auto_load_images(
        webkit_web_view_get_settings(app.view), enabled);
    event("images", enabled ? "enabled" : "disabled");
    status_text("Ustawienie obrazów zmienione. Odśwież stronę.");
}

static void uri_changed(GObject *object, GParamSpec *parameter, gpointer unused)
{
    const gchar *uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(object));
    (void)parameter; (void)unused;
    if (uri)
        gtk_entry_set_text(GTK_ENTRY(app.address), uri);
}

static void finish_test(gboolean success)
{
    gchar *path, *json;
    GError *error = NULL;
    if (app.test_finished)
        return;
    app.test_finished = TRUE;
    path = g_build_filename(app.report_dir, "self-test.json", NULL);
    json = g_strdup_printf(
        "{\"success\":%s,\"javascript\":%s,\"scope\":\"local_fixture_only\"}\n",
        success ? "true" : "false", app.js_enabled ? "true" : "false");
    if (!g_file_set_contents(path, json, -1, &error)) {
        g_printerr("Nie można zapisać wyniku: %s\n", error->message);
        g_clear_error(&error);
        success = FALSE;
    }
    g_free(path);
    g_free(json);
    app.exit_code = success ? 0 : 1;
    event("self_test", success ? "passed" : "failed");
    if (gtk_main_level())
        gtk_main_quit();
}

static void snapshot_ready(GObject *object, GAsyncResult *result, gpointer data)
{
    GError *error = NULL;
    cairo_surface_t *surface = webkit_web_view_get_snapshot_finish(
        WEBKIT_WEB_VIEW(object), result, &error);
    gboolean success = GPOINTER_TO_INT(data);
    if (!surface) {
        event("snapshot", "failed");
        g_clear_error(&error);
        finish_test(FALSE);
        return;
    }
    {
        gchar *path = g_build_filename(app.report_dir, "snapshot.png", NULL);
        if (cairo_surface_write_to_png(surface, path) != CAIRO_STATUS_SUCCESS)
            success = FALSE;
        g_free(path);
    }
    cairo_surface_destroy(surface);
    finish_test(success);
}

static void snapshot_test(gboolean success)
{
    webkit_web_view_get_snapshot(app.view, WEBKIT_SNAPSHOT_REGION_VISIBLE,
        WEBKIT_SNAPSHOT_OPTIONS_NONE, NULL, snapshot_ready,
        GINT_TO_POINTER(success));
}

static void javascript_test_ready(GObject *object, GAsyncResult *result,
                                  gpointer unused)
{
    GError *error = NULL;
    gboolean success = FALSE;
    (void)unused;
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    JSCValue *value = webkit_web_view_evaluate_javascript_finish(
        WEBKIT_WEB_VIEW(object), result, &error);
    if (value) {
        success = jsc_value_is_boolean(value) && jsc_value_to_boolean(value);
        g_object_unref(value);
    }
#else
    WebKitJavascriptResult *js_result = webkit_web_view_run_javascript_finish(
        WEBKIT_WEB_VIEW(object), result, &error);
    if (js_result) {
        JSCValue *value = webkit_javascript_result_get_js_value(js_result);
        success = jsc_value_is_boolean(value) && jsc_value_to_boolean(value);
        webkit_javascript_result_unref(js_result);
    }
#endif
    if (error) {
        event("self_test_javascript", "evaluation_failed");
        g_clear_error(&error);
    }
    snapshot_test(success);
}

static void run_fixture_test(void)
{
    const gchar *title = webkit_web_view_get_title(app.view);
    const gchar *script =
        "(function(){"
        "var input=document.getElementById('test-input');"
        "input.value='Pentium II';"
        "document.getElementById('test-submit').click();"
        "return document.getElementById('test-output').textContent==='Witaj, Pentium II'"
        "&&document.title==='P2Browser — JS działa'"
        "&&getComputedStyle(document.getElementById('layout-test')).display==='flex';"
        "})()";
    if (app.test_started || app.test_finished)
        return;
    if (!app.js_enabled) {
        /* Title notifications may reach the UI after LOAD_FINISHED. */
        if (g_strcmp0(title, "P2Browser — test HTML i CSS") != 0)
            return;
        app.test_started = TRUE;
        snapshot_test(TRUE);
        return;
    }
    app.test_started = TRUE;
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    webkit_web_view_evaluate_javascript(app.view, script, -1, NULL, NULL,
                                       NULL, javascript_test_ready, NULL);
#else
    webkit_web_view_run_javascript(app.view, script, NULL,
                                   javascript_test_ready, NULL);
#endif
}

static void title_changed(GObject *object, GParamSpec *parameter, gpointer unused)
{
    (void)object; (void)parameter; (void)unused;
    if (app.self_test && app.navigation_complete && !app.navigation_failed)
        run_fixture_test();
}

static void load_changed(WebKitWebView *view, WebKitLoadEvent state,
                         gpointer unused)
{
    (void)view; (void)unused;
    switch (state) {
    case WEBKIT_LOAD_STARTED:
        app.error_count = 0;
        app.navigation_failed = FALSE;
        app.navigation_complete = FALSE;
        event("load", "started");
        status_text("Ładowanie…");
        break;
    case WEBKIT_LOAD_REDIRECTED:
        event("load", "redirected");
        break;
    case WEBKIT_LOAD_COMMITTED:
        event("load", "committed");
        break;
    case WEBKIT_LOAD_FINISHED:
        app.navigation_complete = TRUE;
        event("load", "finished");
        if (!app.navigation_failed)
            status_text("Załadowano. Prototyp pomiarowy z jednym widokiem strony.");
        if (app.self_test && !app.navigation_failed && !app.test_finished)
            run_fixture_test();
        break;
    }
}

static gboolean load_failed(WebKitWebView *view, WebKitLoadEvent state,
                            gchar *uri, GError *error, gpointer unused)
{
    gchar *detail = g_strdup_printf("domain=%s;code=%d",
        g_quark_to_string(error->domain), error->code);
    (void)view; (void)state; (void)uri; (void)unused;
    app.navigation_failed = TRUE;
    event("load_failed", detail);
    g_free(detail);
    status_text("Nie udało się załadować strony. Kod błędu zapisano w raporcie.");
    if (app.self_test)
        finish_test(FALSE);
    return FALSE;
}

static void process_terminated(WebKitWebView *view,
                               WebKitWebProcessTerminationReason reason,
                               gpointer unused)
{
    gchar *detail = g_strdup_printf("reason=%d", (int)reason);
    (void)view; (void)unused;
    event("web_process_terminated", detail);
    g_free(detail);
    status_text("Proces strony zakończył działanie. Sprawdź raport.");
    if (app.self_test)
        finish_test(FALSE);
}

static void script_error(WebKitUserContentManager *manager,
                          WebKitJavascriptResult *result, gpointer unused)
{
    JSCValue *value = webkit_javascript_result_get_js_value(result);
    gchar *text;
    unsigned int line, column;
    (void)manager; (void)unused;
    if (!jsc_value_is_string(value) || app.error_count >= 100)
        return;
    text = jsc_value_to_string(value);
    if (strlen(text) < 64 && sscanf(text, "error:%u:%u", &line, &column) == 2) {
        gchar *detail = g_strdup_printf("line=%u;column=%u", line, column);
        event("javascript_error", detail);
        g_free(detail);
        app.error_count++;
    } else if (g_strcmp0(text, "unhandled_rejection") == 0) {
        event("javascript_error", "unhandled_rejection");
        app.error_count++;
    }
    g_free(text);
}

static gboolean sample_memory(gpointer unused)
{
    FILE *file = fopen("/proc/self/status", "r");
    char buffer[256];
    unsigned long rss = 0, swap = 0;
    gchar *detail, *tooltip;
    (void)unused;
    if (!file)
        return G_SOURCE_CONTINUE;
    while (fgets(buffer, sizeof buffer, file)) {
        unsigned long number;
        if (sscanf(buffer, "VmRSS: %lu kB", &number) == 1)
            rss = number;
        if (sscanf(buffer, "VmSwap: %lu kB", &number) == 1)
            swap = number;
    }
    fclose(file);
    detail = g_strdup_printf("ui_rss_kib=%lu;ui_swap_kib=%lu", rss, swap);
    event("memory_ui_only", detail);
    tooltip = g_strdup_printf(
        "RSS samego interfejsu: %.1f MiB | swap interfejsu: %.1f MiB | raport: %s",
        rss / 1024.0, swap / 1024.0, app.report_dir);
    gtk_widget_set_tooltip_text(app.status, tooltip);
    g_free(detail);
    g_free(tooltip);
    return G_SOURCE_CONTINUE;
}

static gboolean quit_timeout(gpointer unused)
{
    (void)unused;
    app.watchdog = 0;
    event("watchdog", "time_limit");
    if (app.self_test)
        finish_test(FALSE);
    else
        gtk_main_quit();
    return G_SOURCE_REMOVE;
}

static void window_destroy(GtkWidget *widget, gpointer unused)
{
    (void)widget; (void)unused;
    app.stopping = TRUE;
    if (app.self_test && !app.test_finished)
        finish_test(FALSE);
    else if (gtk_main_level())
        gtk_main_quit();
}

static gboolean permission_request(WebKitWebView *view,
                                    WebKitPermissionRequest *request,
                                    gpointer unused)
{
    (void)view; (void)unused;
    webkit_permission_request_deny(request);
    event("permission", "denied_in_probe");
    status_text("Ta wersja pomiarowa nie udostępnia uprawnień urządzeń.");
    return TRUE;
}

static void download_started(WebKitWebContext *context,
                              WebKitDownload *download, gpointer unused)
{
    (void)context; (void)unused;
    webkit_download_cancel(download);
    event("download", "cancelled_not_implemented");
    status_text("Menedżer pobierania nie jest jeszcze częścią prototypu.");
}

static gboolean navigation_policy(WebKitWebView *view,
                                   WebKitPolicyDecision *decision,
                                   WebKitPolicyDecisionType type,
                                   gpointer unused)
{
    (void)view; (void)unused;
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        WebKitNavigationAction *action =
            webkit_navigation_policy_decision_get_navigation_action(
                WEBKIT_NAVIGATION_POLICY_DECISION(decision));
        WebKitURIRequest *request = webkit_navigation_action_get_request(action);
        webkit_web_view_load_uri(app.view, webkit_uri_request_get_uri(request));
        webkit_policy_decision_ignore(decision);
        event("new_window", "opened_in_current_view");
        return TRUE;
    }
    return FALSE;
}

static void add_button(GtkWidget *box, const char *label, GCallback callback)
{
    GtkWidget *item = gtk_button_new_with_label(label);
    gtk_box_pack_start(GTK_BOX(box), item, FALSE, FALSE, 0);
    g_signal_connect(item, "clicked", callback, NULL);
}

static void build_window(void)
{
    GtkWidget *box, *toolbar, *js_toggle, *images_toggle;
    WebKitSettings *settings;
    WebKitUserContentManager *manager;
    WebKitUserScript *script;
    app.context = webkit_web_context_new_ephemeral();
    webkit_web_context_set_sandbox_enabled(app.context, TRUE);
    webkit_web_context_set_cache_model(app.context,
                                       WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
    g_signal_connect(app.context, "download-started",
                      G_CALLBACK(download_started), NULL);
    manager = webkit_user_content_manager_new();
    g_signal_connect(manager, "script-message-received::p2ProbeError",
                      G_CALLBACK(script_error), NULL);
    webkit_user_content_manager_register_script_message_handler(
        manager, "p2ProbeError");
    script = webkit_user_script_new(
        "window.addEventListener('error',function(e){"
        "window.webkit.messageHandlers.p2ProbeError.postMessage("
        "'error:'+(e.lineno||0)+':'+(e.colno||0));});"
        "window.addEventListener('unhandledrejection',function(){"
        "window.webkit.messageHandlers.p2ProbeError.postMessage('unhandled_rejection');});",
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL);
    webkit_user_content_manager_add_script(manager, script);
    webkit_user_script_unref(script);
    app.view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
        "web-context", app.context, "user-content-manager", manager, NULL));
    g_object_unref(manager);
    settings = webkit_web_view_get_settings(app.view);
    webkit_settings_set_enable_javascript(settings, app.js_enabled);
    webkit_settings_set_auto_load_images(settings, FALSE);
    webkit_settings_set_media_playback_requires_user_gesture(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, FALSE);
    webkit_settings_set_enable_webaudio(settings, FALSE);
    webkit_settings_set_enable_page_cache(settings, FALSE);
    webkit_settings_set_enable_smooth_scrolling(settings, FALSE);
    webkit_settings_set_hardware_acceleration_policy(
        settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
    event("graphics_policy", "hardware_acceleration_never_requested");
    event("sandbox", webkit_web_context_get_sandbox_enabled(app.context)
                         ? "enabled" : "not_enabled");
    event("javascript", app.js_enabled ? "engine_enabled" : "disabled");
    event("scope", "single_view_engine_probe");
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "P2Browser — prototyp silnika");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1000, 740);
    g_signal_connect(app.window, "destroy", G_CALLBACK(window_destroy), NULL);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_container_add(GTK_CONTAINER(app.window), box);
    gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
    add_button(toolbar, "Wstecz", G_CALLBACK(back_clicked));
    add_button(toolbar, "Dalej", G_CALLBACK(forward_clicked));
    add_button(toolbar, "Odśwież", G_CALLBACK(reload_clicked));
    add_button(toolbar, "Stop", G_CALLBACK(stop_clicked));
    app.address = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.address), "Adres strony");
    gtk_box_pack_start(GTK_BOX(toolbar), app.address, TRUE, TRUE, 0);
    g_signal_connect(app.address, "activate", G_CALLBACK(address_activate), NULL);
    js_toggle = gtk_check_button_new_with_label("JS silnika");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(js_toggle), app.js_enabled);
    gtk_box_pack_start(GTK_BOX(toolbar), js_toggle, FALSE, FALSE, 0);
    g_signal_connect(js_toggle, "toggled", G_CALLBACK(js_toggled), NULL);
    images_toggle = gtk_check_button_new_with_label("Obrazy");
    gtk_box_pack_start(GTK_BOX(toolbar), images_toggle, FALSE, FALSE, 0);
    g_signal_connect(images_toggle, "toggled", G_CALLBACK(images_toggled), NULL);
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(app.view), TRUE, TRUE, 0);
    app.status = gtk_label_new("Prototyp pomiarowy. Raport jest zapisywany lokalnie.");
    gtk_label_set_xalign(GTK_LABEL(app.status), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(app.status), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(box), app.status, FALSE, FALSE, 0);
    g_signal_connect(app.view, "notify::uri", G_CALLBACK(uri_changed), NULL);
    g_signal_connect(app.view, "notify::title", G_CALLBACK(title_changed), NULL);
    g_signal_connect(app.view, "load-changed", G_CALLBACK(load_changed), NULL);
    g_signal_connect(app.view, "load-failed", G_CALLBACK(load_failed), NULL);
    g_signal_connect(app.view, "web-process-terminated",
                      G_CALLBACK(process_terminated), NULL);
    g_signal_connect(app.view, "permission-request",
                      G_CALLBACK(permission_request), NULL);
    g_signal_connect(app.view, "decide-policy", G_CALLBACK(navigation_policy), NULL);
    gtk_widget_show_all(app.window);
}

int main(int argc, char **argv)
{
    gchar *report_dir = NULL, *data_dir = NULL;
    gchar **remaining = NULL;
    gboolean version = FALSE;
    gint quit_after = 0;
    GError *error = NULL;
    GOptionContext *options;
    GOptionEntry entries[] = {
        {"version", 0, 0, G_OPTION_ARG_NONE, &version, "Wersja bez GUI", NULL},
        {"javascript", 0, 0, G_OPTION_ARG_NONE, &app.js_enabled, "Włącz JS silnika", NULL},
        {"report-dir", 0, 0, G_OPTION_ARG_FILENAME, &report_dir, "Nowy katalog raportu", "DIR"},
        {"data-dir", 0, 0, G_OPTION_ARG_FILENAME, &data_dir, "Katalog strony testowej", "DIR"},
        {"self-test", 0, 0, G_OPTION_ARG_NONE, &app.self_test, "Test lokalnego HTML/CSS/JS", NULL},
        {"quit-after", 0, 0, G_OPTION_ARG_INT, &quit_after, "Zakończ po podanej liczbie sekund", "SECONDS"},
        {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &remaining, "Adres strony", "URL"},
        {NULL, 0, 0, 0, NULL, NULL, NULL}
    };
    umask(0077);
    options = g_option_context_new("[URL] — lokalny prototyp pomiarowy");
    g_option_context_add_main_entries(options, entries, NULL);
    if (!g_option_context_parse(options, &argc, &argv, &error)) {
        g_printerr("Błąd argumentów: %s\n", error->message);
        g_error_free(error);
        g_option_context_free(options);
        return 2;
    }
    g_option_context_free(options);
    if (version) {
        g_print("p2browser-probe %s; WebKitGTK %u.%u.%u; %u-bit\n",
                P2_VERSION, webkit_get_major_version(), webkit_get_minor_version(),
                webkit_get_micro_version(), (unsigned)(sizeof(void *) * 8));
        return 0;
    }
    if (quit_after < 0 || quit_after > 86400 ||
        (remaining && remaining[0] && remaining[1]) ||
        (app.self_test && remaining && remaining[0])) {
        g_printerr("Nieprawidłowy czas lub liczba adresów. --self-test używa lokalnej strony.\n");
        return 2;
    }
    app.started = g_get_monotonic_time();
    if (!make_report(report_dir, &error)) {
        g_printerr("Błąd raportu: %s\n", error->message);
        g_clear_error(&error);
        return 2;
    }
    g_print("Raport: %s\nPID interfejsu: %ld\n", app.report_dir, (long)getpid());
    fflush(stdout);
    if (!gtk_init_check(&argc, &argv)) {
        event("startup_failed", "display_unavailable");
        g_printerr("Brak dostępnego ekranu. Raport zapisano.\n");
        fclose(app.events);
        return 3;
    }
    app.data_dir = g_strdup(data_dir ? data_dir : P2_DATA_DIR);
    if (app.self_test && !quit_after)
        quit_after = 30;
    build_window();
    app.timer = g_timeout_add_seconds(2, sample_memory, NULL);
    if (quit_after)
        app.watchdog = g_timeout_add_seconds((guint)quit_after, quit_timeout, NULL);
    if (remaining && remaining[0]) {
        navigate(remaining[0]);
        gtk_main();
    } else {
        gchar *uri = fixture_uri();
        if (!uri) {
            event("startup_failed", "fixture_path");
            app.exit_code = 2;
        } else {
            navigate(uri);
            gtk_main();
            g_free(uri);
        }
    }
    if (app.timer)
        g_source_remove(app.timer);
    if (app.watchdog)
        g_source_remove(app.watchdog);
    if (!app.stopping)
        gtk_widget_destroy(app.window);
    g_object_unref(app.context);
    event("probe", "shutdown");
    fclose(app.events);
    g_free(app.report_dir);
    g_free(app.data_dir);
    g_free(report_dir);
    g_free(data_dir);
    g_strfreev(remaining);
    return app.exit_code;
}
