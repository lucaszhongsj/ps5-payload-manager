/* 
 * Next Menu Core - Main Entry Point
 * 
 * This is a native PS5 ELF daemon that hosts a web server
 * to manage payloads and system settings.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <microhttpd.h>

#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include "next_menu.h"
#include "assets_index_html.h"
#include "payload_mgr.h"
#include "ps5_launcher.h"

#define MAX_LOG_LINES 100
#define MAX_LOG_LINE_LEN 256

static char log_buffer[MAX_LOG_LINES][MAX_LOG_LINE_LEN];
static int log_head = 0;
static int log_count = 0;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t log_cond = PTHREAD_COND_INITIALIZER;
static int log_updated = 0;

static volatile sig_atomic_t resume_flag = 0;

void handle_sigcont(int sig) {
    resume_flag = 1;
}

void nm_log(const char *fmt, ...) {
    char line[MAX_LOG_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    /* Print to stdout */
    printf("%s", line);

    /* Remove trailing newline for internal storage if present */
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[len-1] = '\0';
        len--;
    }

    if (len == 0) return;

    /* Add to circular buffer with lock */
    pthread_mutex_lock(&log_mutex);
    strncpy(log_buffer[log_head], line, MAX_LOG_LINE_LEN);
    log_head = (log_head + 1) % MAX_LOG_LINES;
    if (log_count < MAX_LOG_LINES) log_count++;
    
    /* Signal SSE clients */
    log_updated++;
    pthread_cond_broadcast(&log_cond);
    pthread_mutex_unlock(&log_mutex);
}
/* SSE Log Callback */
struct LogSSEConnection {
    int last_log_version;
    int sent_initial;
};

static ssize_t log_stream_callback(void *cls, uint64_t pos, char *buf, size_t max) {
    struct LogSSEConnection *conn = (struct LogSSEConnection *)cls;
    
    pthread_mutex_lock(&log_mutex);
    
    /* Initial catch-up */
    if (!conn->sent_initial) {
        char initial_batch[MAX_LOG_LINES * (MAX_LOG_LINE_LEN + 16)];
        size_t offset = 0;
        
        for (int i = 0; i < log_count; i++) {
            int idx = (log_head - log_count + i + MAX_LOG_LINES) % MAX_LOG_LINES;
            offset += snprintf(initial_batch + offset, sizeof(initial_batch) - offset, "data: %s\n\n", log_buffer[idx]);
        }
        
        conn->sent_initial = 1;
        conn->last_log_version = log_updated;
        
        if (offset > 0) {
            size_t to_copy = (offset < max) ? offset : max;
            memcpy(buf, initial_batch, to_copy);
            pthread_mutex_unlock(&log_mutex);
            return to_copy;
        }
    }

    /* Wait for new logs */
    while (conn->last_log_version == log_updated) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 5; /* Keep-alive ping interval */
        
        if (pthread_cond_timedwait(&log_cond, &log_mutex, &ts) == ETIMEDOUT) {
            /* Send a comment keep-alive */
            const char *ping = ": ping\n\n";
            memcpy(buf, ping, strlen(ping));
            pthread_mutex_unlock(&log_mutex);
            return strlen(ping);
        }
    }

    /* Send new log */
    int idx = (log_head - 1 + MAX_LOG_LINES) % MAX_LOG_LINES;
    size_t len = snprintf(buf, max, "data: %s\n\n", log_buffer[idx]);
    conn->last_log_version = log_updated;
    
    pthread_mutex_unlock(&log_mutex);
    return len;
}

static void log_stream_cleanup(void *cls) {
    free(cls);
}
#define DEFAULT_PORT MENU_PORT

static volatile int server_active_flag = 0;

int nm_server_is_active() {
    return server_active_flag;
}

static volatile int keep_running = 1;

/* Global buffer for JSON responses */
static char response_buffer[65536];

/* State for POST requests */
struct PostStatus {
    char *data;
    size_t size;
    int error;
};

/* State for file uploads */
struct UploadStatus {
    FILE *fp;
    size_t total_size;
    int error;
};

/* Callback for handling HTTP requests */
static enum MHD_Result on_request(void *cls, struct MHD_Connection *conn,
                                const char *url, const char *method,
                                const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls) {
    
    /* Handle CORS Preflight (OPTIONS) */
    if (strcmp(method, "OPTIONS") == 0) {
        struct MHD_Response *resp = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(resp, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        MHD_add_response_header(resp, "Access-Control-Allow-Headers", "Content-Type");
        enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, resp);
        MHD_destroy_response(resp);
        return ret;
    }

    /* Set flag that we received a request (meaning browser is open) */
    server_active_flag = 1;

    /* Initial call for a new request */
    if (*con_cls == NULL) {
        if (strncmp(url, ROUTE_UPLOAD, strlen(ROUTE_UPLOAD)) == 0) {
            struct UploadStatus *status = malloc(sizeof(struct UploadStatus));
            status->fp = NULL;
            status->total_size = 0;
            status->error = 0;

            const char *filename = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
            if (filename) {
                char path[512];
                snprintf(path, sizeof(path), "%s/%s", BASE_DATA_DIR, filename);
                
                mkdir(BASE_DATA_DIR, 0777);
                
                status->fp = fopen(path, "wb");
                if (!status->fp) {
                    nm_log("[NextMenu] !!! FAILED to open file: %s\n", path);
                    status->error = 1;
                } else {
                    nm_log("[NextMenu] Starting upload to: %s\n", path);
                }
            } else {
                nm_log("[NextMenu] !!! Upload failed: Missing filename parameter\n");
                status->error = 1;
            }
            *con_cls = status;
            return MHD_YES;
        }

        if (strcmp(url, ROUTE_SET_CONFIG) == 0 && strcmp(method, "POST") == 0) {
            struct PostStatus *status = malloc(sizeof(struct PostStatus));
            status->data = NULL;
            status->size = 0;
            status->error = 0;
            *con_cls = status;
            return MHD_YES;
        }
        
        *con_cls = (void*)1;
        return MHD_YES;
    }

    /* Handle POST data for set_config */
    if (strcmp(url, ROUTE_SET_CONFIG) == 0 && strcmp(method, "POST") == 0) {
        struct PostStatus *status = (struct PostStatus *)*con_cls;
        if (*upload_data_size != 0) {
            char *new_data = realloc(status->data, status->size + *upload_data_size + 1);
            if (!new_data) {
                status->error = 1;
            } else {
                status->data = new_data;
                memcpy(status->data + status->size, upload_data, *upload_data_size);
                status->size += *upload_data_size;
                status->data[status->size] = '\0';
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            /* Finished receiving JSON */
            nm_log("[NextMenu] Received config update: %s\n", status->data ? status->data : "(null)");
            
            /* Very basic JSON extraction for "AUTOLOAD_LIST":"..." and "AUTOLOAD_ENABLED":true/false */
            if (status->data && !status->error) {
                int enabled = -1;
                char *enabled_pos = strstr(status->data, "\"AUTOLOAD_ENABLED\"");
                if (enabled_pos) {
                    char *val = strchr(enabled_pos, ':');
                    if (val) {
                        val++;
                        while (*val == ' ') val++;
                        if (strncmp(val, "true", 4) == 0) enabled = 1;
                        else if (strncmp(val, "false", 5) == 0) enabled = 0;
                    }
                }

                if (enabled != -1) {
                    mkdir(BASE_DATA_DIR, 0777);
                    FILE *ef = fopen(NEXT_CONFIG_PATH, "w");
                    if (ef) {
                        fprintf(ef, "AUTOLOAD_ENABLED=%d\n", enabled);
                        fclose(ef);
                        nm_log("[NextMenu] Saved config to %s\n", NEXT_CONFIG_PATH);
                    }
                    if (enabled == 0) nm_autoload_abort();
                }

                char *list_start = strstr(status->data, "\"AUTOLOAD_LIST\"");
                if (list_start) {
                    char *val = strchr(list_start, ':');
                    if (val) {
                        val++;
                        while (*val == ' ' || *val == '\"') val++;
                        
                        /* Find the end of the string value */
                        char *list_end = strchr(val, '\"');
                        size_t list_len = list_end ? (size_t)(list_end - val) : 0;
                        char *list_val = malloc(list_len + 1);
                        if (list_val) {
                            memcpy(list_val, val, list_len);
                            list_val[list_len] = '\0';
                            
                            mkdir(BASE_DATA_DIR, 0777);
                            FILE *f = fopen(AUTOLOAD_CONFIG_PATH, "w");
                            if (f) {
                                char *token = strtok(list_val, ",");
                                while (token) {
                                    fprintf(f, "%s\n", token);
                                    token = strtok(NULL, ",");
                                }
                                fclose(f);
                                nm_log("[NextMenu] Saved autoload list to %s\n", AUTOLOAD_CONFIG_PATH);
                            }
                            free(list_val);
                        }
                    }
                }
            }

            if (status->data) free(status->data);
            free(status);
            *con_cls = NULL;

            struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(MSG_OK), (void *)MSG_OK, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
            enum MHD_Result ret = MHD_queue_response(conn, MHD_HTTP_OK, resp);
            MHD_destroy_response(resp);
            return ret;
        }
    }

    /* Chunked data arrival */
    if (strncmp(url, ROUTE_UPLOAD, strlen(ROUTE_UPLOAD)) == 0) {
        struct UploadStatus *status = (struct UploadStatus *)*con_cls;
        if (*upload_data_size != 0) {
            if (status->fp && !status->error) {
                size_t written = fwrite(upload_data, 1, *upload_data_size, status->fp);
                if (written != *upload_data_size) {
                    nm_log("[NextMenu] !!! Write error: expected %zu, got %zu\n", *upload_data_size, written);
                    status->error = 1;
                }
                status->total_size += written;
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else {
            /* Upload finished */
            if (status->fp) {
                fflush(status->fp);
                fclose(status->fp);
            }
            nm_log("[NextMenu] Upload finished. Total bytes: %zu, Error: %d\n", status->total_size, status->error);
            
            int err = status->error;
            free(status);
            *con_cls = NULL;

            const char *msg = err ? "Error during upload\n" : MSG_OK;
            struct MHD_Response *resp = MHD_create_response_from_buffer(strlen(msg), (void *)msg, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
            enum MHD_Result ret = MHD_queue_response(conn, err ? MHD_HTTP_INTERNAL_SERVER_ERROR : MHD_HTTP_OK, resp);
            MHD_destroy_response(resp);
            return ret;
        }
    }

    /* Only log significant requests, not pollers like /log */
    if (strcmp(url, ROUTE_LOG) != 0 && strcmp(url, ROUTE_INDEX) != 0 && strcmp(url, ROUTE_INDEX_HTML) != 0) {
        nm_log("[NextMenu] Request: %s %s\n", method, url);
    }

    struct MHD_Response *resp = NULL;
    enum MHD_Result ret;

    /* Route: Index or index.html */
    if (strcmp(url, ROUTE_INDEX) == 0 || strcmp(url, ROUTE_INDEX_HTML) == 0) {
        resp = MHD_create_response_from_buffer(assets_index_html_len, 
                                             (void *)assets_index_html, 
                                             MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/html");
    } else if (strcmp(url, ROUTE_LIST_PAYLOADS) == 0) {
        size_t len = payload_mgr_list_json(response_buffer, sizeof(response_buffer));
        resp = MHD_create_response_from_buffer(len, (void *)response_buffer, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
    } else if (strncmp(url, ROUTE_LOAD_PAYLOAD, strlen(ROUTE_LOAD_PAYLOAD)) == 0) {
        const char *path = url + strlen(ROUTE_LOAD_PAYLOAD);
        if (ps5_launch_elf(path) == 0) {
            resp = MHD_create_response_from_buffer(strlen(MSG_OK), (void *)MSG_OK, MHD_RESPMEM_PERSISTENT);
        } else {
            const char *err = "Failed to launch payload\n";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_PERSISTENT);
        }
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strncmp(url, ROUTE_DELETE, strlen(ROUTE_DELETE)) == 0) {
        const char *filename = MHD_lookup_connection_value(conn, MHD_GET_ARGUMENT_KIND, "filename");
        if (filename) {
            char path[512];
            /* Basic safety: skip if filename contains / or .. */
            if (strstr(filename, "/") || strstr(filename, "..")) {
                const char *err = "Invalid filename\n";
                resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_PERSISTENT);
            } else {
                snprintf(path, sizeof(path), "%s/%s", BASE_DATA_DIR, filename);
                if (remove(path) == 0) {
                    nm_log("[NextMenu] Deleted payload: %s\n", path);
                    resp = MHD_create_response_from_buffer(strlen(MSG_OK), (void *)MSG_OK, MHD_RESPMEM_PERSISTENT);
                } else {
                    const char *err = "Failed to delete file\n";
                    resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_PERSISTENT);
                }
            }
        } else {
            const char *err = "Missing filename\n";
            resp = MHD_create_response_from_buffer(strlen(err), (void *)err, MHD_RESPMEM_PERSISTENT);
        }
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_SHUTDOWN) == 0) {
        const char *msg = "Next Menu Core shutting down...\n";
        nm_log("[NextMenu] %s", msg);
        resp = MHD_create_response_from_buffer(strlen(msg), (void *)msg, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
        keep_running = 0; /* Signal main loop to exit */
    } else if (strcmp(url, ROUTE_LOG) == 0) {
        size_t pos = 0;
        pos += snprintf(response_buffer + pos, sizeof(response_buffer) - pos, "{\"logs\":[");
        for (int i = 0; i < log_count; i++) {
            int idx = (log_head - log_count + i + MAX_LOG_LINES) % MAX_LOG_LINES;
            /* Escape quotes in log lines for simple JSON safety */
            pos += snprintf(response_buffer + pos, sizeof(response_buffer) - pos, "\"%s\"%s", 
                           log_buffer[idx], (i == log_count - 1) ? "" : ",");
        }
        pos += snprintf(response_buffer + pos, sizeof(response_buffer) - pos, "]}");
        resp = MHD_create_response_from_buffer(pos, (void *)response_buffer, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_VERSION) == 0) {
        resp = MHD_create_response_from_buffer(strlen(MENU_VERSION), (void *)MENU_VERSION, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_GETIP) == 0) {
        char ip[64];
        if (nm_get_local_ip(ip, sizeof(ip)) != 0) {
            strcpy(ip, "0.0.0.0");
        }
        resp = MHD_create_response_from_buffer(strlen(ip), (void *)ip, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_AUTOLOAD_STATUS) == 0) {
        int total, done;
        char current[128];
        nm_autoload_get_status(&total, &done, current);
        int remaining = nm_autoload_get_remaining_seconds();
        
        char list_buf[4096] = "";
        FILE *f = fopen(AUTOLOAD_CONFIG_PATH, "r");
        if (f) {
            char line[256];
            int first = 1;
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = 0;
                if (strlen(line) == 0 || line[0] == '!') continue;
                if (!first) strcat(list_buf, ",");
                strcat(list_buf, line);
                first = 0;
            }
            fclose(f);
        }

        snprintf(response_buffer, sizeof(response_buffer), 
                "{\"remaining\":%d,\"total\":%d,\"done\":%d,\"current\":\"%s\",\"list\":\"%s\"}", 
                remaining, total, done, current, list_buf);
        resp = MHD_create_response_from_buffer(strlen(response_buffer), (void *)response_buffer, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_AUTOLOAD_CLEAR) == 0) {
        nm_autoload_reset();
        const char *msg = "Autoload status cleared.\n";
        resp = MHD_create_response_from_buffer(strlen(msg), (void *)msg, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, ROUTE_ABORT) == 0) {
        nm_autoload_abort();
        const char *msg = "Autoload sequence aborted.\n";
        resp = MHD_create_response_from_buffer(strlen(msg), (void *)msg, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    } else if (strcmp(url, "/autoload_status") == 0) {
        snprintf(response_buffer, sizeof(response_buffer), "{\"remaining\":%d}", nm_autoload_get_remaining_seconds());
        resp = MHD_create_response_from_buffer(strlen(response_buffer), (void *)response_buffer, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, ROUTE_GET_CONFIG) == 0) {
        /* Check if enabled */
        int enabled = 0;
        FILE *ef = fopen(NEXT_CONFIG_PATH, "r");
        if (ef) {
            char line[128];
            while (fgets(line, sizeof(line), ef)) {
                if (strncmp(line, "AUTOLOAD_ENABLED=", 17) == 0) {
                    enabled = atoi(line + 17);
                }
            }
            fclose(ef);
        }

        /* Get list */
        char list_buf[4096] = {0};
        FILE *f = fopen(AUTOLOAD_CONFIG_PATH, "r");
        if (f) {
            char line[256];
            int first = 1;
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\r\n")] = 0;
                if (strlen(line) == 0) continue;
                if (!first) strncat(list_buf, ",", sizeof(list_buf) - strlen(list_buf) - 1);
                strncat(list_buf, line, sizeof(list_buf) - strlen(list_buf) - 1);
                first = 0;
            }
            fclose(f);
        }

        snprintf(response_buffer, sizeof(response_buffer), "{\"AUTOLOAD_ENABLED\":%s,\"AUTOLOAD_LIST\":\"%s\"}", 
                enabled ? "true" : "false", list_buf);
        resp = MHD_create_response_from_buffer(strlen(response_buffer), (void *)response_buffer, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(resp, "Content-Type", "application/json");
    } else if (strcmp(url, "/events") == 0) {
        struct LogSSEConnection *sse = malloc(sizeof(struct LogSSEConnection));
        sse->last_log_version = 0;
        sse->sent_initial = 0;
        
        resp = MHD_create_response_from_callback(MHD_SIZE_UNKNOWN, 1024, &log_stream_callback, sse, &log_stream_cleanup);
        MHD_add_response_header(resp, "Content-Type", "text/event-stream");
        MHD_add_response_header(resp, "Cache-Control", "no-cache");
        MHD_add_response_header(resp, "Connection", "keep-alive");
    } else {
        /* Default: 404 for now */
        const char *not_found = "404 Not Found\n";
        resp = MHD_create_response_from_buffer(strlen(not_found), (void *)not_found, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(resp, "Content-Type", "text/plain");
    }

    if (!resp) return MHD_NO;

    /* Add CORS headers */
    MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
    
    ret = MHD_queue_response(conn, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);

    return ret;
}

/* PS5 System Calls (Internal) */
extern int sceNetCtlInit();
extern int sceUserServiceInitialize(void*);

int main(int argc, char *argv[]) {
    struct MHD_Daemon *daemon;
    unsigned short port = DEFAULT_PORT;
    nm_log("[NextMenu] Starting Native Core v%s on port %d...\n", MENU_VERSION, port);

    /* Initialize PS5 System Services */
    nm_log("[NextMenu] Initializing system services...\n");
    if (sceNetCtlInit() == 0) {
        nm_log("[NextMenu] Network Controller initialized.\n");
    }
    
    int user_prio = 256;
    if (sceUserServiceInitialize(&user_prio) == 0) {
        nm_log("[NextMenu] User Service initialized.\n");
    }

    /* Signal Resilience */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGCONT, handle_sigcont);

    /* Start the MHD daemon */
    daemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG,
                             port, NULL, NULL, &on_request, NULL,
                             MHD_OPTION_END);

    if (NULL == daemon) {
        nm_log("[NextMenu] Failed to start HTTP daemon!\n");
        return 1;
    }

    nm_log("[NextMenu] Server is running. Visit /shutdown to exit.\n");

    /* Startup Notification */
    char current_ip[64] = "unknown";
    if (nm_get_local_ip(current_ip, sizeof(current_ip)) == 0) {
        nm_notify("Next Menu v%s\nIP: %s\nPort: %d", MENU_VERSION, current_ip, port);
    } else {
        nm_notify("Next Menu v%s\nWaiting for Network...", MENU_VERSION);
    }

    /* Start Autoload Sequence (if config exists) */
    nm_autoload_start();

    /* Watchdog and main loop */
    int network_check_timer = 0;
    while (keep_running) {
        usleep(100000); /* 100ms sleep */
        
        /* Immediate Wake-up Recovery */
        if (resume_flag) {
            resume_flag = 0;
            nm_log("[NextMenu] Console resumed from standby. Refreshing network stack...\n");
            nm_autoload_reset(); /* Reset UI state if needed */
            
            /* Force a check right now */
            network_check_timer = 50; 
        }

        /* Network Watchdog (every 5 seconds) */
        if (++network_check_timer >= 50) {
            network_check_timer = 0;
            char new_ip[64] = "unknown";
            int has_ip = (nm_get_local_ip(new_ip, sizeof(new_ip)) == 0);
            
            /* If IP changed or we recovered from no-IP state, or if we just resumed */
            if (has_ip && (strcmp(new_ip, current_ip) != 0 || strcmp(current_ip, "unknown") == 0)) {
                nm_log("[NextMenu] Network state refresh: %s -> %s. Restarting server...\n", current_ip, new_ip);
                if (daemon) MHD_stop_daemon(daemon);
                
                /* Give it a moment to release ports and for system to stabilize */
                usleep(800000); 
                
                daemon = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG,
                                         port, NULL, NULL, &on_request, NULL,
                                         MHD_OPTION_END);
                
                if (daemon) {
                    strcpy(current_ip, new_ip);
                    nm_log("[NextMenu] Server restored on %s:%d\n", current_ip, port);
                    nm_notify("Next Menu: Service Restored\nIP: %s", current_ip);
                } else {
                    nm_log("[NextMenu] !!! Failed to restore server!\n");
                }
            } else if (!has_ip && strcmp(current_ip, "unknown") != 0) {
                /* Lost connection */
                nm_log("[NextMenu] Network lost (was %s)\n", current_ip);
                strcpy(current_ip, "unknown");
            }
        }
    }

    nm_log("[NextMenu] Shutting down...\n");
    if (daemon) MHD_stop_daemon(daemon);
    
    /* Give some time for sockets to close before process exits */
    sleep(1);
    
    return 0;
}
