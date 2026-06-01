#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include <errno.h>
#include <curl/curl.h>
#include "assets_cacert_pem.h"

int payload_mgr_repository_install_commit(const char *filename, const char *uploaded_temp_path, const char *install_source, const char *install_source_detail, char *msg_buf, size_t msg_buf_size);

#include "pldmgr.h"
#include "autoload.h"



static const char **scan_dirs = (const char **)SCAN_DIRS;
static const int scan_dirs_count = SCAN_DIRS_COUNT;

typedef struct RepoPayload {
    char name[128];
    char filename[256];
    char url[1024];
    char source[1024];
    char source_direct[1024];
    char description[1024];
    char last_update[64];
    char version[64];
    char checksum[65];
} RepoPayload;

typedef struct JsonListBuilder {
    char *buf;
    size_t size;
    size_t pos;
    int first;
} JsonListBuilder;

typedef struct SHA256_CTX {
    unsigned char data[64];
    unsigned int datalen;
    unsigned long long bitlen;
    unsigned int state[8];
} SHA256_CTX;

static const unsigned int sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define SHA256_ROTR(a, b) (((a) >> (b)) | ((a) << (32 - (b))))
#define SHA256_CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_EP0(x) (SHA256_ROTR(x, 2) ^ SHA256_ROTR(x, 13) ^ SHA256_ROTR(x, 22))
#define SHA256_EP1(x) (SHA256_ROTR(x, 6) ^ SHA256_ROTR(x, 11) ^ SHA256_ROTR(x, 25))
#define SHA256_SIG0(x) (SHA256_ROTR(x, 7) ^ SHA256_ROTR(x, 18) ^ ((x) >> 3))
#define SHA256_SIG1(x) (SHA256_ROTR(x, 17) ^ SHA256_ROTR(x, 19) ^ ((x) >> 10))

/* Helper to check if a file has a supported extension */
static int is_supported_extension(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return 0;
    
    if (strcasecmp(ext, ".elf") == 0) return 1;
    if (strcasecmp(ext, ".bin") == 0) return 1;
    
    return 0;
}

static int is_allowed_usb_path(const char *path) {
    if (!path) return 0;
    size_t len = strlen(path);
    if (len < 10) return 0;
    if (strstr(path, "..")) return 0;
    if (strncmp(path, "/mnt/usb", 8) != 0) return 0;
    if (!isdigit((unsigned char)path[8])) return 0;
    if (path[9] != '/') return 0;
    return is_supported_extension(path);
}

    static void sha256_transform(SHA256_CTX *ctx, const unsigned char data[]) {
        unsigned int a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

        for (i = 0, j = 0; i < 16; i++, j += 4) {
            m[i] = ((unsigned int)data[j] << 24) | ((unsigned int)data[j + 1] << 16) |
                   ((unsigned int)data[j + 2] << 8) | ((unsigned int)data[j + 3]);
        }
        for (; i < 64; i++) {
            m[i] = SHA256_SIG1(m[i - 2]) + m[i - 7] + SHA256_SIG0(m[i - 15]) + m[i - 16];
        }

        a = ctx->state[0];
        b = ctx->state[1];
        c = ctx->state[2];
        d = ctx->state[3];
        e = ctx->state[4];
        f = ctx->state[5];
        g = ctx->state[6];
        h = ctx->state[7];

        for (i = 0; i < 64; i++) {
            t1 = h + SHA256_EP1(e) + SHA256_CH(e, f, g) + sha256_k[i] + m[i];
            t2 = SHA256_EP0(a) + SHA256_MAJ(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        ctx->state[0] += a;
        ctx->state[1] += b;
        ctx->state[2] += c;
        ctx->state[3] += d;
        ctx->state[4] += e;
        ctx->state[5] += f;
        ctx->state[6] += g;
        ctx->state[7] += h;
    }

    static void sha256_init(SHA256_CTX *ctx) {
        ctx->datalen = 0;
        ctx->bitlen = 0;
        ctx->state[0] = 0x6a09e667;
        ctx->state[1] = 0xbb67ae85;
        ctx->state[2] = 0x3c6ef372;
        ctx->state[3] = 0xa54ff53a;
        ctx->state[4] = 0x510e527f;
        ctx->state[5] = 0x9b05688c;
        ctx->state[6] = 0x1f83d9ab;
        ctx->state[7] = 0x5be0cd19;
    }

    static void sha256_update(SHA256_CTX *ctx, const unsigned char data[], size_t len) {
        size_t i;
        for (i = 0; i < len; i++) {
            ctx->data[ctx->datalen] = data[i];
            ctx->datalen++;
            if (ctx->datalen == 64) {
                sha256_transform(ctx, ctx->data);
                ctx->bitlen += 512;
                ctx->datalen = 0;
            }
        }
    }

    static void sha256_final(SHA256_CTX *ctx, unsigned char hash[]) {
        unsigned int i = ctx->datalen;

        if (ctx->datalen < 56) {
            ctx->data[i++] = 0x80;
            while (i < 56) {
                ctx->data[i++] = 0x00;
            }
        } else {
            ctx->data[i++] = 0x80;
            while (i < 64) {
                ctx->data[i++] = 0x00;
            }
            sha256_transform(ctx, ctx->data);
            memset(ctx->data, 0, 56);
        }

        ctx->bitlen += (unsigned long long)ctx->datalen * 8ULL;
        ctx->data[63] = (unsigned char)(ctx->bitlen);
        ctx->data[62] = (unsigned char)(ctx->bitlen >> 8);
        ctx->data[61] = (unsigned char)(ctx->bitlen >> 16);
        ctx->data[60] = (unsigned char)(ctx->bitlen >> 24);
        ctx->data[59] = (unsigned char)(ctx->bitlen >> 32);
        ctx->data[58] = (unsigned char)(ctx->bitlen >> 40);
        ctx->data[57] = (unsigned char)(ctx->bitlen >> 48);
        ctx->data[56] = (unsigned char)(ctx->bitlen >> 56);
        sha256_transform(ctx, ctx->data);

        for (i = 0; i < 4; i++) {
            hash[i] = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
            hash[i + 4] = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
            hash[i + 8] = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
            hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
            hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
            hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
            hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
            hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
        }
    }

    static int compute_sha256_file(const char *path, char out_hex[65]) {
        FILE *f = fopen(path, "rb");
        unsigned char hash[32];
        unsigned char buf[4096];
        size_t n;
        SHA256_CTX ctx;

        if (!f) {
            return -1;
        }

        sha256_init(&ctx);
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            sha256_update(&ctx, buf, n);
        }
        fclose(f);

        sha256_final(&ctx, hash);
        for (int i = 0; i < 32; i++) {
            snprintf(out_hex + (i * 2), 3, "%02x", hash[i]);
        }
        out_hex[64] = '\0';
        return 0;
    }

    static int mkdir_if_missing(const char *path) {
        struct stat st;
        if (stat(path, &st) == 0) {
            return S_ISDIR(st.st_mode) ? 0 : -1;
        }
        return mkdir(path, 0777);
    }

    static int ensure_dir_recursive(const char *path) {
        char tmp[512];
        size_t len = strlen(path);
        if (len >= sizeof(tmp)) {
            return -1;
        }

        strncpy(tmp, path, sizeof(tmp));
        tmp[len] = '\0';

        for (size_t i = 1; i < len; i++) {
            if (tmp[i] == '/') {
                tmp[i] = '\0';
                if (strlen(tmp) > 0 && mkdir_if_missing(tmp) != 0) {
                    return -1;
                }
                tmp[i] = '/';
            }
        }
        return mkdir_if_missing(tmp);
    }

    static int json_append(JsonListBuilder *jb, const char *fmt, ...) {
        va_list args;
        int written;

        if (jb->pos >= jb->size) {
            return -1;
        }

        va_start(args, fmt);
        written = vsnprintf(jb->buf + jb->pos, jb->size - jb->pos, fmt, args);
        va_end(args);

        if (written < 0) {
            return -1;
        }

        if ((size_t)written >= jb->size - jb->pos) {
            jb->pos = jb->size - 1;
            jb->buf[jb->pos] = '\0';
            return -1;
        }

        jb->pos += (size_t)written;
        return 0;
    }

    static int read_file_text(const char *path, char **out_buf, size_t *out_size) {
        FILE *f;
        long fsize;
        char *buf;
        size_t nread;

        *out_buf = NULL;
        *out_size = 0;

        f = fopen(path, "rb");
        if (!f) {
            return -1;
        }

        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            return -1;
        }
        fsize = ftell(f);
        if (fsize < 0) {
            fclose(f);
            return -1;
        }
        if (fseek(f, 0, SEEK_SET) != 0) {
            fclose(f);
            return -1;
        }

        buf = (char *)malloc((size_t)fsize + 1);
        if (!buf) {
            fclose(f);
            return -1;
        }

        nread = fread(buf, 1, (size_t)fsize, f);
        fclose(f);
        buf[nread] = '\0';

        *out_buf = buf;
        *out_size = nread;
        return 0;
    }

    static int write_file_text(const char *path, const char *data, size_t size) {
        FILE *f = fopen(path, "wb");
        if (!f) {
            return -1;
        }
        if (fwrite(data, 1, size, f) != size) {
            fclose(f);
            return -1;
        }
        fclose(f);
        return 0;
    }

    static size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
        size_t written = fwrite(ptr, size, nmemb, stream);
        return written;
    }

    static int download_to_file(const char *url, const char *out_path) {
        CURL *curl;
        FILE *fp;
        CURLcode res = CURLE_FAILED_INIT;

        if (!url || !out_path) {
            pldmgr_log("[PLDMGR] download_to_file: missing url or path\n");
            return -1;
        }

        fp = fopen(out_path, "wb");
        if (!fp) {
            pldmgr_log("[PLDMGR] download_to_file: failed to open %s\n", out_path);
            return -1;
        }

        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            
            // Set user agent
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "pldmgr/1.0");
            
            // Securely verify SSL against embedded CA bundle
            struct curl_blob blob;
            blob.data = (void *)assets_cacert_pem;
            blob.len = assets_cacert_pem_len;
            blob.flags = CURL_BLOB_COPY;

            curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &blob);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
            
            // Allow redirection
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            res = curl_easy_perform(curl);
            
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            
            curl_easy_cleanup(curl);

            if (res != CURLE_OK) {
                pldmgr_log("[PLDMGR] curl_easy_perform failed: %s url=%s\n", curl_easy_strerror(res), url);
            } else if (http_code != 200) {
                pldmgr_log("[PLDMGR] HTTP status %ld for %s\n", http_code, url);
                res = CURLE_HTTP_RETURNED_ERROR;
            }
        }

        fclose(fp);

        if (res != CURLE_OK) {
            remove(out_path);
            return -1;
        }

        return 0;
    }

    static int parse_config_last_update(long *out_ts) {
        FILE *f;
        char line[256];
        long ts = 0;

        *out_ts = 0;
        f = fopen(PLDMGR_CONFIG_PATH, "r");
        if (!f) {
            return 0;
        }

        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "LAST_REPOSITORY_UPDATE=", 23) == 0) {
                ts = atol(line + 23);
                break;
            }
        }
        fclose(f);

        *out_ts = ts;
        return 0;
    }

    static int upsert_next_config_value(const char *key, const char *value) {
        FILE *f;
        char line[256];
        char old_lines[64][256];
        int line_count = 0;
        int replaced = 0;
        size_t key_len = strlen(key);

        ensure_dir_recursive(BASE_DATA_DIR);

        f = fopen(PLDMGR_CONFIG_PATH, "r");
        if (f) {
            while (line_count < 64 && fgets(line, sizeof(line), f)) {
                if (strncmp(line, key, key_len) == 0 && line[key_len] == '=') {
                    snprintf(old_lines[line_count], sizeof(old_lines[line_count]), "%s=%s\n", key, value);
                    replaced = 1;
                } else {
                    strncpy(old_lines[line_count], line, sizeof(old_lines[line_count]));
                    old_lines[line_count][sizeof(old_lines[line_count]) - 1] = '\0';
                }
                line_count++;
            }
            fclose(f);
        }

        if (!replaced && line_count < 64) {
            snprintf(old_lines[line_count], sizeof(old_lines[line_count]), "%s=%s\n", key, value);
            line_count++;
        }

        f = fopen(PLDMGR_CONFIG_PATH, "w");
        if (!f) {
            return -1;
        }
        for (int i = 0; i < line_count; i++) {
            fputs(old_lines[i], f);
        }
        fclose(f);
        return 0;
    }

    static int write_config_last_update(long ts) {
        char ts_buf[64];
        snprintf(ts_buf, sizeof(ts_buf), "%ld", ts);
        return upsert_next_config_value("LAST_REPOSITORY_UPDATE", ts_buf);
    }

    static int json_extract_string(const char *obj_start, const char *obj_end, const char *key, char *out, size_t out_size) {
        char key_pattern[96];
        const char *p;
        const char *colon;
        const char *q;
        size_t pos = 0;

        if (out_size == 0) {
            return -1;
        }
        out[0] = '\0';

        snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key);
        p = strstr(obj_start, key_pattern);
        if (!p || p >= obj_end) {
            return -1;
        }

        colon = strchr(p + strlen(key_pattern), ':');
        if (!colon || colon >= obj_end) {
            return -1;
        }

        q = colon + 1;
        while (q < obj_end && isspace((unsigned char)*q)) {
            q++;
        }
        if (q >= obj_end || *q != '"') {
            return -1;
        }
        q++;

        while (q < obj_end && *q != '"') {
            if (*q == '\\' && (q + 1) < obj_end) {
                q++;
            }
            if (pos + 1 < out_size) {
                out[pos++] = *q;
            }
            q++;
        }
        out[pos] = '\0';
        return 0;
    }

    static int parse_repository_payloads(const char *json, RepoPayload **out_items, size_t *out_count) {
        const char *p = json;
        RepoPayload *items = NULL;
        size_t count = 0;

        *out_items = NULL;
        *out_count = 0;

        while ((p = strchr(p, '{')) != NULL) {
            const char *end = strchr(p, '}');
            RepoPayload item;
            RepoPayload *tmp;
            if (!end) {
                break;
            }

            memset(&item, 0, sizeof(item));
            if (json_extract_string(p, end, "name", item.name, sizeof(item.name)) != 0 ||
                json_extract_string(p, end, "filename", item.filename, sizeof(item.filename)) != 0 ||
                json_extract_string(p, end, "url", item.url, sizeof(item.url)) != 0) {
                p = end + 1;
                continue;
            }

            json_extract_string(p, end, "source", item.source, sizeof(item.source));
            json_extract_string(p, end, "source_direct", item.source_direct, sizeof(item.source_direct));
            json_extract_string(p, end, "description", item.description, sizeof(item.description));
            json_extract_string(p, end, "last_update", item.last_update, sizeof(item.last_update));
            json_extract_string(p, end, "version", item.version, sizeof(item.version));
            json_extract_string(p, end, "checksum", item.checksum, sizeof(item.checksum));

            tmp = (RepoPayload *)realloc(items, (count + 1) * sizeof(RepoPayload));
            if (!tmp) {
                free(items);
                return -1;
            }
            items = tmp;
            items[count] = item;
            count++;
            p = end + 1;
        }

        *out_items = items;
        *out_count = count;
        return 0;
    }

    static int load_cached_repository(RepoPayload **out_items, size_t *out_count) {
        char *json = NULL;
        size_t size = 0;
        int ret;

        if (read_file_text(REPOSITORY_CACHE_PATH, &json, &size) != 0 || !json || size == 0) {
            if (json) {
                free(json);
            }
            return -1;
        }

        ret = parse_repository_payloads(json, out_items, out_count);
        free(json);
        return ret;
    }

    static int remove_regular_files_in_dir(const char *dir_path, const char *new_filename) {
        DIR *dir = opendir(dir_path);
        struct dirent *entry;
        if (!dir) {
            return 0;
        }

        while ((entry = readdir(dir)) != NULL) {
            char full_path[512];
            struct stat st;

            if (entry->d_name[0] == '.') {
                continue;
            }

            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            if (stat(full_path, &st) != 0) {
                continue;
            }
            if (S_ISREG(st.st_mode)) {
                if (is_supported_extension(entry->d_name)) {
                    pldmgr_autoload_update_config_entry(entry->d_name, new_filename);
                }
                remove(full_path);
            }
        }

        closedir(dir);
        return 0;
    }

    static int write_payload_details_json(const RepoPayload *item, const char *details_path, const char *install_source, const char *install_source_detail) {
        char name[256], filename[384], url[1400], source[1400], source_direct[1400];
        char description[1400], last_update[128], version[128], checksum[128];
        char downloaded_at[64], i_src[256], i_detail[1400];
        char json_buf[8192];
        time_t now = time(NULL);
        struct tm tmv;

        memset(&tmv, 0, sizeof(tmv));
        localtime_r(&now, &tmv);
        strftime(downloaded_at, sizeof(downloaded_at), "%Y-%m-%dT%H:%M:%S%z", &tmv);

        pldmgr_json_escape(item->name, name, sizeof(name));
        pldmgr_json_escape(item->filename, filename, sizeof(filename));
        pldmgr_json_escape(item->url, url, sizeof(url));
        pldmgr_json_escape(item->source, source, sizeof(source));
        pldmgr_json_escape(item->source_direct, source_direct, sizeof(source_direct));
        pldmgr_json_escape(item->description, description, sizeof(description));
        pldmgr_json_escape(item->last_update, last_update, sizeof(last_update));
        pldmgr_json_escape(item->version, version, sizeof(version));
        pldmgr_json_escape(item->checksum, checksum, sizeof(checksum));
        pldmgr_json_escape(install_source ? install_source : "unknown", i_src, sizeof(i_src));
        pldmgr_json_escape(install_source_detail ? install_source_detail : "", i_detail, sizeof(i_detail));

        snprintf(json_buf, sizeof(json_buf),
                 "{\n"
                 "  \"name\": \"%s\",\n"
                 "  \"filename\": \"%s\",\n"
                 "  \"url\": \"%s\",\n"
                 "  \"source\": \"%s\",\n"
                 "  \"source_direct\": \"%s\",\n"
                 "  \"description\": \"%s\",\n"
                 "  \"last_update\": \"%s\",\n"
                 "  \"version\": \"%s\",\n"
                 "  \"checksum\": \"%s\",\n"
                 "  \"downloaded_at\": \"%s\",\n"
                 "  \"install_source\": \"%s\",\n"
                 "  \"install_source_detail\": \"%s\"\n"
                 "}\n",
                 name, filename, url, source, source_direct, description,
                 last_update, version, checksum, downloaded_at, i_src, i_detail);

        FILE *f = fopen(details_path, "w");
        if (!f) return -1;
        fwrite(json_buf, 1, strlen(json_buf), f);
        fclose(f);
        return 0;
    }

    static int write_simple_payload_details_json(const char *filename, const char *details_path, const char *install_source, const char *install_source_detail) {
        RepoPayload item;
        memset(&item, 0, sizeof(item));
        strncpy(item.name, filename, sizeof(item.name) - 1);
        strncpy(item.filename, filename, sizeof(item.filename) - 1);
        return write_payload_details_json(&item, details_path, install_source, install_source_detail);
    }

    int payload_mgr_write_metadata(const char *payload_path, const char *install_source, const char *install_source_detail) {
        char details_path[700];
        const char *filename = strrchr(payload_path, '/');
        if (filename) filename++;
        else filename = payload_path;

        snprintf(details_path, sizeof(details_path), "%s.json", payload_path);
        return write_simple_payload_details_json(filename, details_path, install_source, install_source_detail);
    }

    int payload_mgr_import_to_storage(const char *filename, const char *temp_path, const char *install_source, const char *install_source_detail, char *msg_buf, size_t msg_buf_size) {
        char folder_name[128];
        char payload_dir[512];
        char final_path[640];
        char details_path[700];

        pldmgr_utils_get_payload_folder_name(filename, folder_name, sizeof(folder_name));
        snprintf(payload_dir, sizeof(payload_dir), "%s/%s", PAYLOADS_STORAGE_DIR, folder_name);
        
        if (ensure_dir_recursive(payload_dir) != 0) {
            snprintf(msg_buf, msg_buf_size, "Failed to create directory");
            return -1;
        }

        snprintf(final_path, sizeof(final_path), "%s/%s", payload_dir, filename);
        snprintf(details_path, sizeof(details_path), "%s/%s.json", payload_dir, filename);

        /* Clean up previous version if it has the same folder name but different filename */
        remove_regular_files_in_dir(payload_dir, filename);

        if (rename(temp_path, final_path) != 0) {
            snprintf(msg_buf, msg_buf_size, "Failed to move file");
            return -1;
        }

        write_simple_payload_details_json(filename, details_path, install_source, install_source_detail);
        return 0;
    }

    int payload_mgr_check_existing(const char *filename, char *out_json, size_t out_size) {
        char folder_name[128];
        char folder_path[512];
        char file_path[640];
        struct stat st;

        pldmgr_utils_get_payload_folder_name(filename, folder_name, sizeof(folder_name));
        snprintf(folder_path, sizeof(folder_path), "%s/%s", PAYLOADS_STORAGE_DIR, folder_name);
        snprintf(file_path, sizeof(file_path), "%s/%s", folder_path, filename);

        int folder_exists = (stat(folder_path, &st) == 0 && S_ISDIR(st.st_mode));
        int file_exists = (stat(file_path, &st) == 0 && S_ISREG(st.st_mode));

        snprintf(out_json, out_size, "{\"status\":\"ok\", \"folder_exists\":%d, \"file_exists\":%d, \"folder_name\":\"%s\"}", 
                 folder_exists, file_exists, folder_name);
        return 0;
    }

    static void scan_payloads_recursive(const char *dir_path, int depth, int max_depth, JsonListBuilder *jb) {
        DIR *dir;
        struct dirent *entry;

        if (depth > max_depth) {
            return;
        }

        dir = opendir(dir_path);
        if (!dir) {
            return;
        }

        pldmgr_log("[PLDMGR] Scanning: %s\n", dir_path);

        while ((entry = readdir(dir)) != NULL) {
            char full_path[512];
            struct stat st;

            if (entry->d_name[0] == '.') {
                continue;
            }

            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            if (stat(full_path, &st) != 0) {
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                scan_payloads_recursive(full_path, depth + 1, max_depth, jb);
                continue;
            }

            if (S_ISREG(st.st_mode) && is_supported_extension(entry->d_name)) {
                pldmgr_log("[PLDMGR] Found payload: %s\n", full_path);
                if (!jb->first) {
                    if (json_append(jb, ",") != 0) {
                        break;
                    }
                }
                if (json_append(jb, "\"%s\"", full_path) != 0) {
                    break;
                }
                jb->first = 0;
            }
        }

        closedir(dir);
    }


    static int resolve_recursive(const char *dir_path, const char *filename, char *out_path, size_t out_size, int depth) {
        DIR *dir;
        struct dirent *entry;

        if (depth > 6) {
            return -1;
        }

        dir = opendir(dir_path);
        if (!dir) {
            return -1;
        }

        while ((entry = readdir(dir)) != NULL) {
            char full_path[512];
            struct stat st;

            if (entry->d_name[0] == '.') {
                continue;
            }

            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            if (stat(full_path, &st) != 0) {
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                if (resolve_recursive(full_path, filename, out_path, out_size, depth + 1) == 0) {
                    closedir(dir);
                    return 0;
                }
                continue;
            }

            if (S_ISREG(st.st_mode) && strcmp(entry->d_name, filename) == 0) {
                snprintf(out_path, out_size, "%s", full_path);
                closedir(dir);
                return 0;
            }
        }

        closedir(dir);
        return -1;
    }

    int pldmgr_read_config_bool(const char *key, int default_val) {
        FILE *f = fopen(PLDMGR_CONFIG_PATH, "r");
        if (!f) return default_val;
        char line[256];
        int res = default_val;
        size_t key_len = strlen(key);
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, key, key_len) == 0 && line[key_len] == '=') {
                res = atoi(line + key_len + 1);
                break;
            }
        }
        fclose(f);
        return res;
    }

    size_t payload_mgr_list_json(char *json_buf, size_t buf_size) {
        JsonListBuilder jb;

        ensure_dir_recursive(BASE_DATA_DIR);
        ensure_dir_recursive(PAYLOADS_STORAGE_DIR);

        jb.buf = json_buf;
        jb.size = buf_size;
        jb.pos = 0;
        jb.first = 1;

        if (json_append(&jb, "{\"payloads\":[") != 0) {
            return 0;
        }

        for (int i = 0; i < scan_dirs_count; i++) {
            scan_payloads_recursive(scan_dirs[i], 0, 5, &jb);
        }

        if (pldmgr_read_config_bool("SCAN_USB_PAYLOADS", 0)) {
            for (int i = 0; i < 8; i++) {
                char usb_root[32];
                snprintf(usb_root, sizeof(usb_root), "/mnt/usb%d", i);
                scan_payloads_recursive(usb_root, 0, 0, &jb);
            }
        }

        json_append(&jb, "]}");
        return jb.pos;
    }

    static int copy_file(const char *src, const char *dst) {
        FILE *fs = fopen(src, "rb");
        if (!fs) return -1;
        FILE *fd = fopen(dst, "wb");
        if (!fd) {
            fclose(fs);
            return -1;
        }
        unsigned char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fs)) > 0) {
            if (fwrite(buf, 1, n, fd) != n) {
                fclose(fs);
                fclose(fd);
                return -1;
            }
        }
        fclose(fs);
        fclose(fd);
        return 0;
    }

    int payload_mgr_usb_check(const char *usb_path, char *out_json, size_t out_size) {
        char usb_sha[65];
        char int_sha[65];
        char internal_path[512];
        const char *filename = strrchr(usb_path, '/');
        if (!filename) filename = usb_path;
        else filename++;

        if (!is_allowed_usb_path(usb_path)) {
            snprintf(out_json, out_size, "{\"error\":\"Invalid path\"}");
            return -1;
        }

        if (compute_sha256_file(usb_path, usb_sha) != 0) {
            snprintf(out_json, out_size, "{\"error\":\"Failed to compute USB SHA256\"}");
            return -1;
        }

        char folder_name[128];
        pldmgr_utils_get_payload_folder_name(filename, folder_name, sizeof(folder_name));
        snprintf(internal_path, sizeof(internal_path), "%s/%s/%s", PAYLOADS_STORAGE_DIR, folder_name, filename);
        
        char folder_path[512];
        snprintf(folder_path, sizeof(folder_path), "%s/%s", PAYLOADS_STORAGE_DIR, folder_name);

        struct stat st;
        int folder_exists = (stat(folder_path, &st) == 0 && S_ISDIR(st.st_mode));

        if (stat(internal_path, &st) == 0) {
            if (compute_sha256_file(internal_path, int_sha) == 0) {
                if (strcmp(usb_sha, int_sha) == 0) {
                    snprintf(out_json, out_size, "{\"status\":\"exists_same\", \"filename\":\"%s\", \"sha256\":\"%s\", \"folder_exists\":%d}", filename, usb_sha, folder_exists);
                } else {
                    snprintf(out_json, out_size, "{\"status\":\"exists_different\", \"filename\":\"%s\", \"sha256\":\"%s\", \"folder_exists\":%d}", filename, usb_sha, folder_exists);
                }
            } else {
                snprintf(out_json, out_size, "{\"status\":\"exists_error\", \"filename\":\"%s\", \"folder_exists\":%d}", filename, folder_exists);
            }
        } else {
            snprintf(out_json, out_size, "{\"status\":\"new\", \"filename\":\"%s\", \"sha256\":\"%s\", \"folder_exists\":%d}", filename, usb_sha, folder_exists);
        }
        
        return 0;
    }

    int payload_mgr_usb_move(const char *usb_path, int overwrite, char *out_json, size_t out_size) {
        char usb_sha[65];
        char check_sha[65];
        char temp_path[512];
        const char *filename = strrchr(usb_path, '/');
        if (!filename) filename = usb_path;
        else filename++;

        if (!is_allowed_usb_path(usb_path)) {
            snprintf(out_json, out_size, "{\"error\":\"Invalid path\"}");
            return -1;
        }

        if (!overwrite) {
            char folder_name[128];
            char final_path[640];
            pldmgr_utils_get_payload_folder_name(filename, folder_name, sizeof(folder_name));
            snprintf(final_path, sizeof(final_path), "%s/%s/%s", PAYLOADS_STORAGE_DIR, folder_name, filename);
            if (access(final_path, F_OK) == 0) {
                snprintf(out_json, out_size, "{\"error\":\"File exists\"}");
                return -1;
            }
        }

        snprintf(temp_path, sizeof(temp_path), "%s/%s.tmp", PAYLOADS_STORAGE_DIR, filename);

        if (compute_sha256_file(usb_path, usb_sha) != 0) {
            snprintf(out_json, out_size, "{\"error\":\"Failed to compute USB SHA256\"}");
            return -1;
        }

        ensure_dir_recursive(PAYLOADS_STORAGE_DIR);
        if (copy_file(usb_path, temp_path) != 0) {
            snprintf(out_json, out_size, "{\"error\":\"Failed to copy file to internal storage\"}");
            remove(temp_path);
            return -1;
        }

        if (compute_sha256_file(temp_path, check_sha) != 0 || strcmp(usb_sha, check_sha) != 0) {
            snprintf(out_json, out_size, "{\"error\":\"SHA256 verification failed after copy\"}");
            remove(temp_path);
            return -1;
        }

        char msg[256];
        if (payload_mgr_import_to_storage(filename, temp_path, "usb_move", usb_path, msg, sizeof(msg)) != 0) {
            snprintf(out_json, out_size, "{\"error\":\"%s\"}", msg);
            remove(temp_path);
            return -1;
        }

        if (remove(usb_path) != 0) {
            pldmgr_log("[PLDMGR] Warning: Failed to remove original file from USB: %s\n", usb_path);
            snprintf(out_json, out_size, "{\"status\":\"ok\", \"warning\":\"copied but failed to delete from usb\"}");
        } else {
            snprintf(out_json, out_size, "{\"status\":\"ok\"}");
        }

        return 0;
    }

    int payload_mgr_resolve_path(const char *filename, char *out_path, size_t out_size) {
        for (int i = 0; i < SCAN_DIRS_COUNT; i++) {
            if (resolve_recursive(SCAN_DIRS[i], filename, out_path, out_size, 0) == 0) {
                return 0;
            }
        }
        return -1;
    }

    int payload_mgr_delete_payload_file(const char *filename) {
        char path[512];
        char details_path[640];
        char dir_path[512];
        char *last_slash;

        if (!filename || strstr(filename, "/") || strstr(filename, "..")) {
            return -1;
        }

        if (payload_mgr_resolve_path(filename, path, sizeof(path)) != 0) {
            return -1;
        }

        if (remove(path) != 0) {
            return -1;
        }

        snprintf(details_path, sizeof(details_path), "%s.json", path);
        remove(details_path);
        
        /* Remove from autoload.txt if present */
        pldmgr_autoload_update_config_entry(filename, NULL);
        
        /* Try to remove the containing directory (will fail if not empty) */
        strncpy(dir_path, path, sizeof(dir_path));
        dir_path[sizeof(dir_path) - 1] = '\0';
        last_slash = strrchr(dir_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            rmdir(dir_path);
        }
        
        return 0;
    }

    /*
     * payload_mgr_repository_push_json
     *
     * Called when the browser POSTs the raw ps5_payloads.json content it
     * fetched over HTTPS.  Validates the JSON, then atomically replaces the
     * local cache so subsequent /repository_payloads calls work offline.
     */
    int payload_mgr_repository_push_json(const char *json, size_t len) {
        RepoPayload *items = NULL;
        size_t count = 0;
        char tmp_path[512];
        time_t now = time(NULL);

        if (!json || len == 0) {
            pldmgr_log("[PLDMGR] !!! repository_push: empty body\n");
            return -1;
        }

        if (parse_repository_payloads(json, &items, &count) != 0 || count == 0) {
            pldmgr_log("[PLDMGR] !!! repository_push: JSON parse failed or 0 entries\n");
            if (items) free(items);
            return -1;
        }
        free(items);

        ensure_dir_recursive(BASE_DATA_DIR);
        ensure_dir_recursive(PAYLOADS_STORAGE_DIR);

        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", REPOSITORY_CACHE_PATH);
        if (write_file_text(tmp_path, json, len) != 0) {
            remove(tmp_path);
            return -1;
        }
        if (rename(tmp_path, REPOSITORY_CACHE_PATH) != 0) {
            remove(tmp_path);
            return -1;
        }

        write_config_last_update((long)now);
        pldmgr_log("[PLDMGR] Repository cache updated via browser push (%zu entries)\n", count);
        return 0;
    }

    int payload_mgr_repository_ensure_fresh(int force_refresh) {
        long last_update = 0;
        time_t now = time(NULL);
        char tmp_path[512];
        char *json = NULL;
        size_t json_size = 0;
        RepoPayload *items = NULL;
        size_t count = 0;

        ensure_dir_recursive(BASE_DATA_DIR);
        ensure_dir_recursive(PAYLOADS_STORAGE_DIR);

        parse_config_last_update(&last_update);

        if (!force_refresh && access(REPOSITORY_CACHE_PATH, F_OK) == 0) {
            long delta = (long)now - last_update;
            if (delta >= 0 && delta < REPOSITORY_REFRESH_INTERVAL_SEC) {
                return 0;
            }
        }

        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", REPOSITORY_CACHE_PATH);
        if (download_to_file(REPOSITORY_SOURCE_URL, tmp_path) != 0) {
            return -1;
        }

        if (read_file_text(tmp_path, &json, &json_size) != 0 || json_size == 0) {
            remove(tmp_path);
            if (json) {
                free(json);
            }
            return -1;
        }

        if (parse_repository_payloads(json, &items, &count) != 0 || count == 0) {
            pldmgr_log("[PLDMGR] !!! Repository payload JSON parse failed\n");
            free(items);
            free(json);
            remove(tmp_path);
            return -1;
        }

        free(items);
        free(json);

        if (rename(tmp_path, REPOSITORY_CACHE_PATH) != 0) {
            remove(tmp_path);
            return -1;
        }

        write_config_last_update((long)now);
        pldmgr_log("[PLDMGR] Repository cache refreshed (%ld entries timestamp)\n", (long)count);
        return 0;
    }

    size_t payload_mgr_repository_list_json(char *json_buf, size_t buf_size, int force_refresh) {
        char *cached = NULL;
        size_t cached_size = 0;
        long last_update = 0;
        size_t pos = 0;

        if (force_refresh) {
            if (payload_mgr_repository_ensure_fresh(1) != 0) {
                parse_config_last_update(&last_update);
                snprintf(json_buf, buf_size,
                         "{\"payloads\":[],\"last_update\":%ld,\"cache_status\":\"error\"}",
                         last_update);
                return strlen(json_buf);
            }
        }

        parse_config_last_update(&last_update);

        if (read_file_text(REPOSITORY_CACHE_PATH, &cached, &cached_size) != 0 || cached_size == 0) {
            if (cached) {
                free(cached);
            }
            snprintf(json_buf, buf_size,
                     "{\"payloads\":[],\"last_update\":%ld,\"cache_status\":\"missing\"}",
                     last_update);
            return strlen(json_buf);
        }

        pos += (size_t)snprintf(json_buf + pos, buf_size - pos,
                                "{\"payloads\":");
        if (pos >= buf_size || buf_size - pos <= 64 || cached_size > (buf_size - pos - 64)) {
            snprintf(json_buf, buf_size,
                     "{\"payloads\":[],\"last_update\":%ld,\"cache_status\":\"truncated\"}",
                     last_update);
            free(cached);
            return strlen(json_buf);
        }
        if (pos < buf_size) {
            size_t copy_len = cached_size;
            memcpy(json_buf + pos, cached, copy_len);
            pos += copy_len;
        }

        pos += (size_t)snprintf(json_buf + pos, buf_size - pos,
                                ",\"last_update\":%ld,\"cache_status\":\"ok\",\"repo_url\":\"%s\"}",
                                last_update, REPOSITORY_SOURCE_URL);

        free(cached);
        return pos;
    }

    int payload_mgr_repository_install_download(const char *filename, const char *install_source_detail, char *msg_buf, size_t msg_buf_size) {
        RepoPayload *items = NULL;
        size_t count = 0;
        int found = -1;
        char tmp_path[512];

        if (msg_buf && msg_buf_size > 0) {
            msg_buf[0] = '\0';
        }

        if (!filename || strlen(filename) == 0 || strstr(filename, "/") || strstr(filename, "..")) {
            snprintf(msg_buf, msg_buf_size, "Invalid filename");
            return -1;
        }

        if (load_cached_repository(&items, &count) != 0 || count == 0) {
            if (payload_mgr_repository_ensure_fresh(1) == 0) {
                if (load_cached_repository(&items, &count) != 0 || count == 0) {
                    snprintf(msg_buf, msg_buf_size, "Repository cache missing or invalid");
                    if (items) {
                        free(items);
                    }
                    return -1;
                }
            } else {
                snprintf(msg_buf, msg_buf_size, "Repository cache missing or invalid");
                if (items) {
                    free(items);
                }
                return -1;
            }
        }

        for (size_t i = 0; i < count; i++) {
            if (strcmp(items[i].filename, filename) == 0) {
                found = (int)i;
                break;
            }
        }

        if (found < 0) {
            free(items);
            snprintf(msg_buf, msg_buf_size, "Payload not found in repository");
            return -1;
        }

        ensure_dir_recursive(PAYLOADS_STORAGE_DIR);
        snprintf(tmp_path, sizeof(tmp_path), "%s/%s.part", PAYLOADS_STORAGE_DIR, items[found].filename);

        if (download_to_file(items[found].url, tmp_path) != 0) {
            free(items);
            snprintf(msg_buf, msg_buf_size, "Download failed");
            remove(tmp_path);
            return -1;
        }

        const char *detail = (install_source_detail && install_source_detail[0])
                                 ? install_source_detail
                                 : REPOSITORY_SOURCE_URL;

        if (payload_mgr_repository_install_commit(items[found].filename, tmp_path,
                                                  "repository", detail,
                                                  msg_buf, msg_buf_size) != 0) {
            free(items);
            return -1;
        }

        free(items);
        return 0;
    }

    int payload_mgr_repository_install_commit(const char *filename, const char *uploaded_temp_path, const char *install_source, const char *install_source_detail, char *msg_buf, size_t msg_buf_size) {
        RepoPayload *items = NULL;
        size_t count = 0;
        int found = -1;
        char payload_dir[512];
        char final_path[640];
        char details_path[700];

        if (!filename || strlen(filename) == 0 || strstr(filename, "/") || strstr(filename, "..")) {
            snprintf(msg_buf, msg_buf_size, "Invalid filename");
            return -1;
        }

        if (load_cached_repository(&items, &count) != 0 || count == 0) {
            if (payload_mgr_repository_ensure_fresh(1) == 0) {
                if (load_cached_repository(&items, &count) != 0 || count == 0) {
                    snprintf(msg_buf, msg_buf_size, "Repository cache missing or invalid");
                    if (items) {
                        free(items);
                    }
                    return -1;
                }
            } else {
                snprintf(msg_buf, msg_buf_size, "Repository cache missing or invalid");
                if (items) {
                    free(items);
                }
                return -1;
            }
        }

        if (count == 0) {
            snprintf(msg_buf, msg_buf_size, "Repository cache missing or invalid");
            if (items) {
                free(items);
            }
            return -1;
        }

        for (size_t i = 0; i < count; i++) {
            if (strcmp(items[i].filename, filename) == 0) {
                found = (int)i;
                break;
            }
        }

        if (found < 0) {
            free(items);
            snprintf(msg_buf, msg_buf_size, "Payload not found in repository");
            return -1;
        }

        char folder_name[128];
        pldmgr_utils_get_payload_folder_name(items[found].filename, folder_name, sizeof(folder_name));
        snprintf(payload_dir, sizeof(payload_dir), "%s/%s", PAYLOADS_STORAGE_DIR, folder_name);
        if (ensure_dir_recursive(payload_dir) != 0) {
            free(items);
            snprintf(msg_buf, msg_buf_size, "Failed to create payload directory");
            return -1;
        }

        snprintf(final_path, sizeof(final_path), "%s/%s", payload_dir, items[found].filename);
        snprintf(details_path, sizeof(details_path), "%s/%s.json", payload_dir, items[found].filename);

        if (strlen(items[found].checksum) == 64) {
            char calculated[65];
            if (compute_sha256_file(uploaded_temp_path, calculated) != 0) {
                free(items);
                remove(uploaded_temp_path);
                snprintf(msg_buf, msg_buf_size, "Checksum computation failed");
                return -1;
            }
            if (strcasecmp(calculated, items[found].checksum) != 0) {
                pldmgr_log("[PLDMGR] !!! Checksum mismatch for %s\n", items[found].filename);
                free(items);
                remove(uploaded_temp_path);
                snprintf(msg_buf, msg_buf_size, "Checksum mismatch");
                return -1;
            }
        }

        /* Verify succeeded, now clear previous payload and metadata. */
        remove_regular_files_in_dir(payload_dir, items[found].filename);

        if (rename(uploaded_temp_path, final_path) != 0) {
            free(items);
            remove(uploaded_temp_path);
            snprintf(msg_buf, msg_buf_size, "Failed to finalize payload file");
            return -1;
        }

        if (write_payload_details_json(&items[found], details_path, install_source, install_source_detail) != 0) {
            free(items);
            remove(final_path);
            snprintf(msg_buf, msg_buf_size, "Failed to write payload metadata");
            return -1;
        }

        pldmgr_log("[PLDMGR] Repository payload installed: %s -> %s\n", items[found].filename, final_path);
        snprintf(msg_buf, msg_buf_size, "Installed %s", items[found].filename);
        free(items);
        return 0;
    }

    int get_elf_pldmgr_version(const char *path, char *out_version, size_t out_size) {
        FILE *f = fopen(path, "rb");
        if (!f) return -1;
        
        char sig[16];
        sig[0] = 'P'; sig[1] = 'L'; sig[2] = 'D'; sig[3] = 'M'; sig[4] = 'G'; 
        sig[5] = 'R'; sig[6] = '_'; sig[7] = 'V'; sig[8] = 'E'; sig[9] = 'R'; sig[10] = ':'; sig[11] = '\0';
        size_t sig_len = 11;
        
        char buffer[8192];
        size_t bytes_read;
        int match_idx = 0;
        
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
            for (size_t i = 0; i < bytes_read; i++) {
                if (buffer[i] == sig[match_idx]) {
                    match_idx++;
                    if (match_idx == sig_len) {
                        size_t v_idx = 0;
                        i++;
                        while (i < bytes_read && buffer[i] != '\0' && v_idx < out_size - 1) {
                            out_version[v_idx++] = buffer[i++];
                        }
                        if (i < bytes_read && buffer[i] == '\0') {
                            if (v_idx > 0) {
                                out_version[v_idx] = '\0';
                                fclose(f);
                                return 0;
                            } else {
                                match_idx = 0;
                                continue;
                            }
                        } else {
                            while (v_idx < out_size - 1) {
                                int c = fgetc(f);
                                if (c == EOF || c == '\0') break;
                                out_version[v_idx++] = (char)c;
                            }
                            out_version[v_idx] = '\0';
                            if (v_idx > 0) {
                                fclose(f);
                                return 0;
                            } else {
                                match_idx = 0;
                                continue;
                            }
                        }
                    }
                } else {
                    match_idx = 0;
                    if (buffer[i] == sig[0]) match_idx = 1;
                }
            }
        }
        fclose(f);
        return -1;
    }

    static int parse_version_part(const char **p) {
        int val = 0;
        while (**p >= '0' && **p <= '9') {
            val = val * 10 + (**p - '0');
            (*p)++;
        }
        if (**p == '.') (*p)++;
        return val;
    }

    static int compare_versions(const char *v1, const char *v2) {
        const char *p1 = v1;
        const char *p2 = v2;
        
        int maj1 = parse_version_part(&p1);
        int maj2 = parse_version_part(&p2);
        if (maj1 != maj2) return maj1 - maj2;
        
        int min1 = parse_version_part(&p1);
        int min2 = parse_version_part(&p2);
        if (min1 != min2) return min1 - min2;
        
        int pat1 = parse_version_part(&p1);
        int pat2 = parse_version_part(&p2);
        
        int res = pat1 - pat2;
        pldmgr_log("[PLDMGR] Comparing versions: %s vs %s -> %d\n", v1, v2, res);
        return res;
    }

    int payload_mgr_check_self_update(char *out_path, size_t out_size) {
        DIR *dir = opendir(PAYLOADS_STORAGE_DIR);
        if (!dir) return -1;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;

            /* Check for pldmgr or payload-manager directory (case-insensitive) */
            if (strcasecmp(entry->d_name, "pldmgr") == 0 || strcasecmp(entry->d_name, "payload-manager") == 0) {
                char subdir_path[512];
                snprintf(subdir_path, sizeof(subdir_path), "%s/%s", PAYLOADS_STORAGE_DIR, entry->d_name);
                
                struct stat st;
                if (stat(subdir_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                    DIR *sdir = opendir(subdir_path);
                    if (sdir) {
                        struct dirent *sentry;
                        while ((sentry = readdir(sdir)) != NULL) {
                            if (sentry->d_name[0] == '.') continue;
                            
                            /* Look for .elf or .bin files that match our name */
                            if (strstr(sentry->d_name, ".elf") || strstr(sentry->d_name, ".bin")) {
                                if (strcasestr(sentry->d_name, "pldmgr") || strcasestr(sentry->d_name, "payload-manager")) {
                                    char full_path[512];
                                    snprintf(full_path, sizeof(full_path), "%s/%s", subdir_path, sentry->d_name);
                                    
                                    /* Verify ELF Magic */
                                    unsigned char magic[4];
                                    FILE *ef = fopen(full_path, "rb");
                                    if (ef) {
                                        if (fread(magic, 1, 4, ef) == 4 && magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
                                            fclose(ef);
                                            char version[64];
                                            if (get_elf_pldmgr_version(full_path, version, sizeof(version)) == 0) {
                                                pldmgr_log("[PLDMGR] Found potential update: %s (v%s)\n", full_path, version);
                                                if (compare_versions(version, MENU_VERSION) > 0) {
                                                    strncpy(out_path, full_path, out_size);
                                                    closedir(sdir);
                                                    closedir(dir);
                                                    return 0;
                                                }
                                            }
                                        } else {
                                            fclose(ef);
                                        }
                                    }
                                }
                            }
                        }
                        closedir(sdir);
                    }
                }
            }
        }

        closedir(dir);
        return -1;
    }
