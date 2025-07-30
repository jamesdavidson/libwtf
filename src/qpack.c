#include "qpack.h"

#include "log.h"
#include "lsxpack_header.h"
#include "utils.h"

static void wtf_qpack_unblocked(void* context)
{
    (void)context;
}

static struct lsxpack_header* wtf_qpack_prepare_decode(void* context, struct lsxpack_header* header,
                                                       size_t space)
{
    wtf_header_decode_context* ctx = (wtf_header_decode_context*)context;

    if (!ctx)
        return NULL;

    if (space > sizeof(ctx->decode_buffer)) {
        if (ctx->connection && ctx->connection->server && ctx->connection->server->context) {
            WTF_LOG_ERROR(ctx->connection->server->context, "qpack", "Header too large: %zu bytes",
                          space);
        }
        return NULL;
    }

    if (header) {
        header->buf = ctx->decode_buffer;
        header->val_len = (lsxpack_strlen_t)space;
    } else {
        header = &ctx->current_header;
        lsxpack_header_prepare_decode(header, ctx->decode_buffer, 0, space);
    }

    return header;
}

static wtf_context* get_log_context(wtf_header_decode_context* ctx)
{
    return (ctx && ctx->connection && ctx->connection->server)
        ? ctx->connection->server->context
        : NULL;
}

static bool validate_header_size(wtf_header_decode_context* ctx, size_t name_len, size_t value_len)
{
    if (name_len > 256 || value_len > 4096) {
        wtf_context* log_ctx = get_log_context(ctx);
        if (log_ctx) {
            WTF_LOG_ERROR(log_ctx, "qpack", "Header too large: name=%zu, value=%zu", name_len,
                          value_len);
        }
        return false;
    }
    return true;
}

static void update_request_field(char** field, const char* value, size_t value_len)
{
    if (*field) {
        free(*field);
    }
    *field = wtf_strndup(value, value_len);
}

static void process_pseudo_header(wtf_connect_request* request, const char* name, size_t name_len,
                                  const char* value, size_t value_len, wtf_context* log_ctx)
{
    if (name_len == 7 && strncmp(name, ":method", 7) == 0) {
        update_request_field(&request->method, value, value_len);
    } else if (name_len == 7 && strncmp(name, ":scheme", 7) == 0) {
        update_request_field(&request->scheme, value, value_len);
    } else if (name_len == 10 && strncmp(name, ":authority", 10) == 0) {
        update_request_field(&request->authority, value, value_len);
    } else if (name_len == 5 && strncmp(name, ":path", 5) == 0) {
        update_request_field(&request->path, value, value_len);
    } else if (name_len == 9 && strncmp(name, ":protocol", 9) == 0) {
        update_request_field(&request->protocol, value, value_len);
    } else {
        if (log_ctx) {
            WTF_LOG_DEBUG(log_ctx, "qpack", "Ignoring unknown pseudo-header: %.*s", (int)name_len,
                          name);
        }
    }
}

static void process_regular_header(wtf_connect_request* request, const char* name, size_t name_len,
                                   const char* value, size_t value_len, wtf_context* log_ctx)
{
    if (name_len == 6 && strncmp(name, "origin", 6) == 0) {
        update_request_field(&request->origin, value, value_len);
    } else {
        if (log_ctx) {
            WTF_LOG_DEBUG(log_ctx, "qpack", "Ignoring regular header: %.*s", (int)name_len, name);
        }
    }
}

static int wtf_qpack_process_header(void* context, struct lsxpack_header* header)
{
    // Validate input parameters
    wtf_header_decode_context* ctx = (wtf_header_decode_context*)context;
    if (!ctx || !header || !header->buf) {
        return -1;
    }

    wtf_connect_request* request = ctx->request;
    if (!request) {
        return -1;
    }

    const char* name = (header->name_len > 0)
        ? (const char*)(header->buf + header->name_offset)
        : NULL;
    const char* value = (header->val_len > 0)
        ? (const char*)(header->buf + header->val_offset)
        : "";
    size_t name_len = header->name_len;
    size_t value_len = header->val_len;

    if (!name || name_len == 0) {
        wtf_context* log_ctx = get_log_context(ctx);
        if (log_ctx) {
            WTF_LOG_ERROR(log_ctx, "qpack", "Header missing name");
        }
        return -1;
    }

    if (!validate_header_size(ctx, name_len, value_len)) {
        return -1;
    }

    wtf_context* log_ctx = get_log_context(ctx);
    if (log_ctx) {
        WTF_LOG_TRACE(log_ctx, "qpack", "Processing header: %.*s = %.*s", (int)name_len, name,
                      (int)value_len, value);
    }

    if (name[0] == ':') {
        process_pseudo_header(request, name, name_len, value, value_len, log_ctx);
    } else {
        process_regular_header(request, name, name_len, value, value_len, log_ctx);
    }

    ctx->header_count++;
    return 0;
}


static const struct lsqpack_dec_hset_if wtf_qpack_decoder_interface = {
    .dhi_unblocked = wtf_qpack_unblocked,
    .dhi_prepare_decode = wtf_qpack_prepare_decode,
    .dhi_process_header = wtf_qpack_process_header,
};

bool wtf_qpack_preinit(wtf_qpack_context* qpack, uint32_t max_table_capacity,
                       uint32_t max_blocked_streams)
{
    if (!qpack)
        return false;
    memset(qpack, 0, sizeof(*qpack));
    qpack->max_table_capacity = max_table_capacity;
    qpack->max_blocked_streams = max_blocked_streams;
    qpack->peer_max_table_capacity = 0;
    qpack->peer_blocked_streams = 0;
    qpack->initialized = false;
    qpack->tsu_buf_sz = sizeof(qpack->tsu_buf);
    lsqpack_enc_preinit(&qpack->encoder, NULL);
    lsqpack_dec_init(&qpack->decoder, NULL, 0, 0, &wtf_qpack_decoder_interface, 0);
    return true;
}

void wtf_qpack_cleanup(wtf_qpack_context* qpack)
{
    if (!qpack)
        return;

    mtx_lock(&qpack->mutex);

    if (qpack->initialized) {
        lsqpack_enc_cleanup(&qpack->encoder);
        lsqpack_dec_cleanup(&qpack->decoder);
        qpack->initialized = false;
    }

    mtx_unlock(&qpack->mutex);
}

bool wtf_qpack_init_encoder(wtf_context* ctx, wtf_qpack_context* qpack)
{
    bool success = false;

    if (!qpack) {
        return false;
    }

    mtx_lock(&qpack->mutex);

    if (qpack->initialized) {
        WTF_LOG_ERROR(ctx, "qpack", "QPACK encoder already initialized");
        goto cleanup_unlock;
    }

    uint32_t table_capacity = min(qpack->max_table_capacity, qpack->peer_max_table_capacity);
    uint32_t blocked_streams = (uint32_t)min(qpack->max_blocked_streams,
                                             qpack->peer_blocked_streams);

    if (table_capacity == 0) {
        table_capacity = WTF_QPACK_DYNAMIC_TABLE_SIZE;
    }
    if (blocked_streams == 0) {
        blocked_streams = WTF_QPACK_MAX_BLOCKED_STREAMS;
    }

    qpack->tsu_buf_sz = sizeof(qpack->tsu_buf);
    int result = lsqpack_enc_init(&qpack->encoder, NULL, table_capacity, table_capacity,
                                  blocked_streams, LSQPACK_ENC_OPT_STAGE_2, qpack->tsu_buf,
                                  &qpack->tsu_buf_sz);

    if (result != 0) {
        WTF_LOG_ERROR(ctx, "qpack", "Failed to initialize QPACK encoder: %d", result);
        goto cleanup_unlock;
    }

    lsqpack_dec_cleanup(&qpack->decoder);
    lsqpack_dec_init(&qpack->decoder, NULL, table_capacity, blocked_streams,
                     &wtf_qpack_decoder_interface, (enum lsqpack_dec_opts)0);

    qpack->initialized = true;
    success = true;

cleanup_unlock:
    mtx_unlock(&qpack->mutex);
    return success;
}

bool wtf_qpack_process_decoder(wtf_context* ctx, wtf_qpack_context* qpack, const uint8_t* data,
                               size_t length)
{
    bool success = false;

    if (!qpack || !data || length == 0) {
        goto cleanup;
    }

    mtx_lock(&qpack->mutex);

    if (!qpack->initialized) {
        WTF_LOG_ERROR(ctx, "qpack", "QPACK not initialized");
        goto cleanup_unlock;
    }

    int result = lsqpack_enc_decoder_in(&qpack->encoder, data, length);
    if (result != 0) {
        WTF_LOG_ERROR(ctx, "qpack", "Failed to process QPACK decoder input: %d", result);
        goto cleanup_unlock;
    }

    success = true;

cleanup_unlock:
    mtx_unlock(&qpack->mutex);
cleanup:
    return success;
}

bool wtf_qpack_process_encoder(wtf_context* ctx, wtf_qpack_context* qpack, const uint8_t* data,
                               size_t length)
{
    bool success = false;

    if (!qpack || !data || length == 0) {
        goto cleanup;
    }

    mtx_lock(&qpack->mutex);

    if (!qpack->initialized) {
        WTF_LOG_ERROR(ctx, "qpack", "QPACK not initialized");
        goto cleanup_unlock;
    }

    int result = lsqpack_dec_enc_in(&qpack->decoder, data, length);
    if (result != 0) {
        WTF_LOG_ERROR(ctx, "qpack", "Failed to process QPACK encoder input: %d", result);
        goto cleanup_unlock;
    }

    success = true;

cleanup_unlock:
    mtx_unlock(&qpack->mutex);
cleanup:
    return success;
}

wtf_result_t wtf_qpack_parse_connect_headers(wtf_context* ctx, wtf_http3_stream* stream,
                                             const uint8_t* data, size_t data_len,
                                             wtf_connect_request* request)
{
    if (!ctx || !stream || !data || !request || !stream->connection) {
        return WTF_ERROR_INVALID_PARAMETER;
    }

    wtf_connection* conn = stream->connection;

    WTF_LOG_DEBUG(ctx, "qpack", "Parsing CONNECT headers: %zu bytes", data_len);

    // Initialize request structure
    memset(request, 0, sizeof(*request));
    request->valid = false;

    // Setup decode context
    wtf_header_decode_context decode_ctx = {0};
    decode_ctx.request = request;
    decode_ctx.connection = conn;
    decode_ctx.headers_complete = false;
    decode_ctx.header_count = 0;

    // Lock the QPACK context for thread safety
    mtx_lock(&conn->qpack.mutex);

    if (!conn->qpack.initialized) {
        mtx_unlock(&conn->qpack.mutex);
        WTF_LOG_ERROR(ctx, "qpack", "QPACK not initialized");
        return WTF_ERROR_INVALID_STATE;
    }

    struct lsqpack_dec* decoder = &conn->qpack.decoder;
    const uint8_t* header_data = data;
    uint64_t stream_id = stream->id;

    // Decode headers using QPACK decoder
    enum lsqpack_read_header_status decode_result = lsqpack_dec_header_in(
        decoder, &decode_ctx, stream_id, data_len, &header_data, data_len, NULL, NULL);

    mtx_unlock(&conn->qpack.mutex);

    wtf_result_t result = WTF_SUCCESS;

    // Process decode result
    switch (decode_result) {
        case LQRHS_DONE:
            WTF_LOG_DEBUG(ctx, "qpack", "Headers decoded successfully, %zu headers processed",
                          decode_ctx.header_count);

            // Validate required CONNECT headers
            if (!request->method || strncmp(request->method, "CONNECT", 8) != 0) {
                WTF_LOG_ERROR(ctx, "qpack", "Invalid or missing :method header");
                result = WTF_ERROR_PROTOCOL_VIOLATION;
            } else if (!request->protocol) {
                WTF_LOG_ERROR(ctx, "qpack", "Missing :protocol header");
                result = WTF_ERROR_PROTOCOL_VIOLATION;
            } else if (!request->scheme) {
                WTF_LOG_ERROR(ctx, "qpack", "Missing :scheme header");
                result = WTF_ERROR_PROTOCOL_VIOLATION;
            } else if (!request->authority) {
                WTF_LOG_ERROR(ctx, "qpack", "Missing :authority header");
                result = WTF_ERROR_PROTOCOL_VIOLATION;
            } else {
                request->valid = true;
                WTF_LOG_DEBUG(ctx, "qpack", "CONNECT headers validated successfully");
                WTF_LOG_TRACE(ctx, "qpack",
                              "CONNECT request - method:%s, protocol:%s, scheme:%s, authority:%s",
                              request->method ? request->method : "NULL",
                              request->protocol ? request->protocol : "NULL",
                              request->scheme ? request->scheme : "NULL",
                              request->authority ? request->authority : "NULL");
            }
            break;

        case LQRHS_BLOCKED:
            WTF_LOG_DEBUG(ctx, "qpack", "Header block blocked - waiting for encoder stream data");
            result = WTF_ERROR_PROTOCOL_VIOLATION;
            break;

        case LQRHS_NEED:
            WTF_LOG_ERROR(ctx, "qpack", "Incomplete header block - need more data");
            result = WTF_ERROR_PROTOCOL_VIOLATION;
            break;

        case LQRHS_ERROR:
        default:
            WTF_LOG_ERROR(ctx, "qpack", "Header decoding error: %d", decode_result);
            result = WTF_ERROR_PROTOCOL_VIOLATION;
            break;
    }

    // Cleanup on error
    if (result != WTF_SUCCESS) {
        if (request->method) {
            free(request->method);
            request->method = NULL;
        }
        if (request->protocol) {
            free(request->protocol);
            request->protocol = NULL;
        }
        if (request->scheme) {
            free(request->scheme);
            request->scheme = NULL;
        }
        if (request->authority) {
            free(request->authority);
            request->authority = NULL;
        }
        if (request->path) {
            free(request->path);
            request->path = NULL;
        }
        if (request->origin) {
            free(request->origin);
            request->origin = NULL;
        }
        request->valid = false;
    }

    return result;
}

wtf_result_t wtf_qpack_send_encoder_data(wtf_connection* conn)
{
    if (!conn || !conn->qpack.initialized || !conn->peer_encoder_stream) {
        return WTF_ERROR_INVALID_PARAMETER;
    }
    wtf_context* ctx = conn->server->context;

    mtx_lock(&conn->qpack.mutex);
    if (!conn->qpack.initialized || conn->qpack.tsu_buf_sz == 0) {
        mtx_unlock(&conn->qpack.mutex);
        return WTF_ERROR_INVALID_STATE;
    }

    size_t total_size = sizeof(QUIC_BUFFER) + conn->qpack.tsu_buf_sz;
    void* send_buffer_raw = malloc(total_size);
    if (!send_buffer_raw) {
        WTF_LOG_ERROR(ctx, "qpack", "Failed to allocate encoder send buffer");
        mtx_unlock(&conn->qpack.mutex);
        return WTF_ERROR_OUT_OF_MEMORY;
    }

    QUIC_BUFFER* send_buffer = (QUIC_BUFFER*)send_buffer_raw;
    uint8_t* data = (uint8_t*)send_buffer_raw + sizeof(QUIC_BUFFER);

    memcpy(data, conn->qpack.tsu_buf, conn->qpack.tsu_buf_sz);
    send_buffer->Buffer = data;
    send_buffer->Length = (uint32_t)conn->qpack.tsu_buf_sz;

    conn->qpack.tsu_buf_sz = 0;

    mtx_unlock(&conn->qpack.mutex);

    size_t bytes_to_send = send_buffer->Length;

    QUIC_STATUS status = ctx->quic_api->StreamSend(
        conn->peer_encoder_stream->quic_stream, send_buffer, 1, QUIC_SEND_FLAG_NONE,
        send_buffer_raw);

    if (QUIC_SUCCEEDED(status)) {
        WTF_LOG_TRACE(ctx, "qpack", "Sent %zu bytes of encoder data", bytes_to_send);
    } else {
        WTF_LOG_ERROR(ctx, "qpack", "Failed to send encoder data: 0x%x", status);
        free(send_buffer_raw);
    }
    return wtf_quic_status_to_result(status);
}
